// SPDX-License-Identifier: GPL-3.0-or-later
#include "isds/device.hpp"

#include <libusb.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <format>
#include <mutex>
#include <optional>
#include <random>
#include <thread>

namespace isds {
namespace {

using namespace std::chrono_literals;

// vdso reports GetMemoryLength() = 1024 KB on the ISDS205X; not found in the descriptor yet.
constexpr std::size_t kMemoryDepthSamples = 1024 * 1024;
constexpr auto kReplyTimeout = 1000ms;
constexpr unsigned kWriteTimeoutMs = 1000;
constexpr unsigned kPollTimeoutMs = 100;
constexpr std::size_t kCtrlBufferSize = 1024;
constexpr std::size_t kDataBufferSize = 64 * 1024;
constexpr std::size_t kMaxStaleBytes = 2 * 2 * kMemoryDepthSamples;  // a full capture on both channels, twice
constexpr std::size_t kPrimeSamples = 2 * kCaptureGranularity;       // the smallest priming capture tried, 2 ms at 1 MSa/s
// After a coarse gain change the input jumps: captures within about 2 ms of it read wrong, up to full scale (bench check
// 2026-11-08, docs/measurements.md §13). The coarse gain and the coupling are relays.
constexpr auto kRelaySettle = 5ms;

std::string usb_error(const char* what, int rc)
{
    return std::format("{}: {}", what, libusb_error_name(rc));
}

}  // namespace

struct Device::Impl {
    libusb_context* ctx = nullptr;
    libusb_device_handle* handle = nullptr;
    bool claimed = false;

    std::thread reader;
    std::atomic<bool> running{false};
    std::atomic<bool> disconnected{false};

    std::mutex write_mutex;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::deque<Bytes> queue;

    std::array<std::uint8_t, kCtrlBufferSize> ctrl_buffer{};
    std::atomic<bool> ctrl_pending{false};
    std::atomic<int> heartbeats_due{0};

    std::unique_ptr<Calibration> cal;
    std::uint32_t rate_hz = 0;
    std::array<Stage, 2> stage{};
    std::array<Coupling, 2> coupling{Coupling::DC, Coupling::DC};
    std::chrono::steady_clock::time_point settled{};  // when the inputs have settled after the last relay change

    void relay_switched() { settled = std::chrono::steady_clock::now() + kRelaySettle; }
    void wait_settled() const { std::this_thread::sleep_until(settled); }

    GeneratorSettings generator;
    bool generator_on = false;

    std::optional<ReadStrategy> read_strategy;  // set by the user of the driver; otherwise read_strategy_for()
    bool raw_io = false;

    // Measured on one machine (docs/measurements.md §10): over WinUSB in RAW_IO mode, 8 KB reads lost samples at 4 and
    // 8 MSa/s, 4 KB ones did not; at 16 MSa/s 8 KB did best, and every size left a rare loss there.
    ReadStrategy read_strategy_for(std::uint32_t hz) const
    {
        if (read_strategy)
            return *read_strategy;
        if (raw_io && hz < 16'000'000)
            return {4096, 64};
        return {};
    }

    ~Impl()
    {
        if (handle && running && !disconnected) {
            try {
                if (generator_on)
                    send(dds_value(Target::Device, DdsCommand::OutputOff));
                send(osc_value(Target::Ch2, OscCommand::ChannelOff));
                send(osc_value(Target::Ch1, OscCommand::ChannelOff));
            } catch (const Error&) {
                // Best effort: the device may already be gone.
            }
        }
        running = false;
        if (reader.joinable())
            reader.join();
        if (claimed)
            libusb_release_interface(handle, kInterface);
        if (handle)
            libusb_close(handle);
        if (ctx)
            libusb_exit(ctx);
    }

    void open_usb()
    {
#if defined(LIBUSB_API_VERSION) && LIBUSB_API_VERSION >= 0x0100010A
        int rc = libusb_init_context(&ctx, nullptr, 0);
#else
        int rc = libusb_init(&ctx);
#endif
        if (rc != 0)
            throw Error(usb_error("libusb init failed", rc));
        handle = open_device();
        libusb_set_auto_detach_kernel_driver(handle, 1);
        rc = libusb_claim_interface(handle, kInterface);
        if (rc != 0)
            throw Error(usb_error("cannot claim interface (is another program using the device?)", rc));
        claimed = true;
#ifdef ISDS_HAVE_RAW_IO
        // WinUSB hands queued reads to the controller one after the other unless the pipe is in RAW_IO mode. Between
        // them the device's FIFO of 1024 samples ran over at 4 MSa/s and faster: every capture lost samples
        // (docs/measurements.md §10). Reads are then whole packets, as captures are. Other systems do not have this.
        if (libusb_endpoint_supports_raw_io(handle, kEpDataIn) == 1)
            raw_io = libusb_endpoint_set_raw_io(handle, kEpDataIn, 1) == 0;
#endif
    }

    // The first oscilloscope on the bus. Told apart from libusb_open_device_with_vid_pid, which says nothing more than
    // "not opened": a missing device, one the user may not open (no udev rule, the usual case with the AppImage) and
    // one without a driver on Windows each need their own remedy.
    libusb_device_handle* open_device()
    {
        libusb_device** list = nullptr;
        const auto count = libusb_get_device_list(ctx, &list);
        if (count < 0)
            throw Error(usb_error("cannot list the USB devices", static_cast<int>(count)));
        bool found = false;
        int rc = 0;
        libusb_device_handle* opened = nullptr;
        for (decltype(libusb_get_device_list(ctx, &list)) i = 0; i < count && !opened; ++i) {
            libusb_device_descriptor descriptor{};
            if (libusb_get_device_descriptor(list[i], &descriptor) != 0 || descriptor.idVendor != kVendorId ||
                descriptor.idProduct != kProductId)
                continue;
            found = true;
            rc = libusb_open(list[i], &opened);
        }
        libusb_free_device_list(list, 1);
        if (opened)
            return opened;
        if (!found)
            throw Error(std::format("no ISDS205 series oscilloscope found ({:04x}:{:04x}); check the USB cable and the "
                                    "rear switch",
                                    kVendorId, kProductId));
        if (rc == LIBUSB_ERROR_ACCESS)
            throw Error("the oscilloscope is there, but this user may not open it: install the udev rule "
                        "60-star-oscilloscope.rules (see the README), then plug the oscilloscope in again");
#ifdef _WIN32
        if (rc == LIBUSB_ERROR_NOT_SUPPORTED)
            throw Error("the oscilloscope is there, but has no WinUSB driver: install the driver that comes with the "
                        "manufacturer's software");
#endif
        throw Error(usb_error("cannot open the oscilloscope", rc));
    }

    void write(std::span<const std::uint8_t> data)
    {
        std::lock_guard lock(write_mutex);
        int sent = 0;
        const int rc = libusb_bulk_transfer(handle, kEpOut, const_cast<std::uint8_t*>(data.data()),
                                            static_cast<int>(data.size()), &sent, kWriteTimeoutMs);
        if (rc != 0 || sent != static_cast<int>(data.size()))
            throw Error(usb_error("write failed", rc));
    }

    void mark_disconnected()
    {
        disconnected = true;
        queue_cv.notify_all();
    }

    // May run on any thread that handles libusb events, so it must not wait for a transfer: heartbeats are answered
    // by the reader thread.
    static void LIBUSB_CALL on_ctrl(libusb_transfer* t)
    {
        auto* self = static_cast<Impl*>(t->user_data);
        switch (t->status) {
        case LIBUSB_TRANSFER_COMPLETED: {
            const std::span<const std::uint8_t> message(t->buffer, static_cast<std::size_t>(t->actual_length));
            if (is_heartbeat(message)) {
                ++self->heartbeats_due;
            } else if (!message.empty()) {
                {
                    std::lock_guard lock(self->queue_mutex);
                    self->queue.emplace_back(message.begin(), message.end());
                }
                self->queue_cv.notify_all();
            }
            break;
        }
        case LIBUSB_TRANSFER_CANCELLED:
            self->ctrl_pending = false;
            return;
        case LIBUSB_TRANSFER_TIMED_OUT:
        case LIBUSB_TRANSFER_OVERFLOW:
            break;
        default:  // the device is gone
            self->ctrl_pending = false;
            self->mark_disconnected();
            return;
        }
        if (!self->running || libusb_submit_transfer(t) != 0) {
            self->ctrl_pending = false;
            if (self->running)
                self->mark_disconnected();
        }
    }

    // Runs on its own thread: keeps one read of the control endpoint waiting and answers heartbeats; everything else
    // goes to the queue. The read has no time-out and stays queued until the device closes. A read given up on after a
    // time-out can take a message with it: over usbip, reads of 100 ms lost about one command reply in a thousand, and
    // acquisition then stopped with "timed out waiting for the device" (docs/measurements.md §10).
    void reader_loop()
    {
        libusb_transfer* transfer = libusb_alloc_transfer(0);
        if (!transfer) {
            mark_disconnected();
            return;
        }
        libusb_fill_bulk_transfer(transfer, handle, kEpCtrlIn, ctrl_buffer.data(), static_cast<int>(ctrl_buffer.size()),
                                  on_ctrl, this, 0);
        ctrl_pending = true;
        if (libusb_submit_transfer(transfer) != 0) {
            ctrl_pending = false;
            libusb_free_transfer(transfer);
            mark_disconnected();
            return;
        }
        while (running && !disconnected) {
            timeval tv{0, kPollTimeoutMs * 1000};
            libusb_handle_events_timeout_completed(ctx, &tv, nullptr);
            for (; heartbeats_due > 0; --heartbeats_due) {
                try {
                    write(kHeartbeatReply);
                } catch (const Error&) {
                    // Reported by the next command that fails.
                }
            }
        }
        if (ctrl_pending)
            libusb_cancel_transfer(transfer);
        for (int i = 0; ctrl_pending && i < 20; ++i) {
            timeval tv{0, kPollTimeoutMs * 1000};
            libusb_handle_events_timeout_completed(ctx, &tv, nullptr);
        }
        if (!ctrl_pending)  // otherwise libusb still holds it, and leaking it is the safe choice
            libusb_free_transfer(transfer);
    }

    Bytes read_message(std::chrono::milliseconds timeout = kReplyTimeout)
    {
        std::unique_lock lock(queue_mutex);
        if (!queue_cv.wait_for(lock, timeout, [this] { return !queue.empty() || disconnected; }))
            throw Error("timed out waiting for the device");
        if (queue.empty())
            throw Error("device disconnected");
        Bytes message = std::move(queue.front());
        queue.pop_front();
        return message;
    }

    void send(const Bytes& frame, bool expect_ack = true)
    {
        write(frame);
        if (!expect_ack)
            return;
        const Bytes reply = read_message();
        if (!is_ack_for(reply, frame))
            throw Error(std::format("unexpected reply {} to {}", to_hex(reply), to_hex(frame)));
    }

    // Sends the capture `command` and reads `raw` from the data endpoint with `strategy`; records when each read
    // completed, after the command.
    void read_data(const Bytes& command, Bytes& raw, const ReadStrategy& strategy, unsigned timeout_ms,
                   std::vector<float>& arrival_ms);

    // A host that went away mid-capture leaves the rest of that capture queued on the data endpoint,
    // and the device does not answer the handshake until it has been read.
    void drain_data()
    {
        std::vector<std::uint8_t> buffer(kDataBufferSize);
        for (std::size_t total = 0; total < kMaxStaleBytes;) {
            int got = 0;
            const int rc = libusb_bulk_transfer(handle, kEpDataIn, buffer.data(), static_cast<int>(buffer.size()),
                                                &got, kPollTimeoutMs);
            if (got <= 0 || (rc != 0 && rc != LIBUSB_ERROR_TIMEOUT))
                return;
            total += static_cast<std::size_t>(got);
        }
    }

    void handshake()
    {
        drain_data();
        running = true;
        reader = std::thread([this] { reader_loop(); });

        std::this_thread::sleep_for(150ms);  // let stale messages arrive, then drop them
        {
            std::lock_guard lock(queue_mutex);
            queue.clear();
        }

        write(kSync);
        if (const Bytes echo = read_message(); !std::ranges::equal(echo, kSync))
            throw Error("bad sync echo: " + to_hex(echo));

        std::random_device rd;
        Bytes nonce(kNonceSize);
        std::ranges::generate(nonce, [&rd] { return static_cast<std::uint8_t>(rd()); });
        write(nonce);
        const Bytes device_nonce = read_message();
        if (device_nonce.size() != kNonceSize)
            throw Error("unexpected handshake reply: " + to_hex(device_nonce));
        Bytes reply = nonce;
        reply.insert(reply.end(), device_nonce.begin(), device_nonce.end());
        write(reply);

        Bytes descriptor = read_message();
        while (descriptor.size() < 0xa0) {
            const Bytes more = read_message();
            descriptor.insert(descriptor.end(), more.begin(), more.end());
        }
        const std::array<std::uint8_t, 1> confirm{kConfirm};
        write(confirm);
        read_message();

        cal = std::make_unique<Calibration>(Descriptor::parse(descriptor));
        // Nothing has been set yet: another family is left as it was found.
        if (const std::string& model = cal->descriptor().model; !supported_model(model))
            throw Error(std::format("{} is not supported: this program drives the ISDS205 series", model));
    }

    // The sequence vdso sends right after the handshake (docs/protocol.md §3).
    void startup()
    {
        send(osc_value(Target::Device, OscCommand::Start));
        for (Channel ch : {Channel::Ch1, Channel::Ch2})
            send(osc_value(target(ch), OscCommand::ChannelOn));
        set_rate(cal->descriptor().sample_rates.back());
        const Stage s = cal->startup_stage();
        for (Channel ch : {Channel::Ch1, Channel::Ch2})
            send(osc_u16(target(ch), OscCommand::Gain, cal->gain_code(s)));
        for (Channel ch : {Channel::Ch1, Channel::Ch2}) {
            send(osc_u16(target(ch), OscCommand::Level, cal->level_code(s, ch)));
            stage[index(ch)] = s;
        }
        for (Channel ch : {Channel::Ch1, Channel::Ch2}) {
            send(osc_value(target(ch), OscCommand::Coupling, static_cast<std::uint32_t>(Coupling::DC)));
            coupling[index(ch)] = Coupling::DC;
        }
        relay_switched();
        prime();
    }

    // One capture below 16 MSa/s after the start makes every later capture at the top rate begin with
    // kContiguousSamplesAtTopRate contiguous samples; without it only the first block is. It lasts until the device
    // is opened again. Found by replaying the vendor software's commands, which happens to do this because it opens
    // at a slow time base (docs/measurements.md §10, 2026-11-08).
    void prime()
    {
        const auto& rates = cal->descriptor().sample_rates;
        const std::uint32_t top = rate_hz;
        set_rate(rates.front());
        Bytes raw(2 * kPrimeSamples);
        std::vector<float> arrival_ms;
        wait_settled();
        read_data(osc_value(Target::Device, OscCommand::Capture, static_cast<std::uint32_t>(kPrimeSamples)), raw,
                  read_strategy_for(rate_hz), 1000, arrival_ms);
        set_rate(top);
    }

    void set_rate(std::uint32_t hz)
    {
        const auto& rates = cal->descriptor().sample_rates;
        if (std::ranges::find(rates, hz) == rates.end())
            throw Error(std::format("unsupported sample rate {} Hz", hz));
        send(osc_u32(Target::Device, OscCommand::SampleRate, hz));
        rate_hz = hz;
    }

    // Sends the generator settings that differ from `old` (all of them when `old` is null),
    // in the order vdso uses.
    void send_generator(const GeneratorSettings& s, const GeneratorSettings* old)
    {
        const bool levels = has_level_control(cal->descriptor().model);
        if (!old || s.waveform != old->waveform)
            send(dds_value(Target::Ch1, DdsCommand::Waveform, waveform_code(s.waveform)));
        if (!old || s.frequency_hz != old->frequency_hz)
            send(dds_value(Target::Ch1, DdsCommand::Frequency, s.frequency_hz));
        if (levels && (!old || s.offset != old->offset))
            send(dds_value(Target::Ch1, DdsCommand::Offset, s.offset));
        if (levels && (!old || s.amplitude != old->amplitude))
            send(dds_value(Target::Ch1, DdsCommand::Amplitude, s.amplitude));
        if (!old || s.duty_percent != old->duty_percent)
            send(dds_value(Target::Ch1, DdsCommand::Duty, duty_code(s.duty_percent)));
    }
};

namespace {

// Callbacks run on whichever thread handles libusb events, also the control reader's, while the capture thread
// may still be queueing the first reads. A read takes its part of the buffer and is queued under `mutex`, so the
// parts follow the order in which the reads complete.
struct ReadState {
    std::mutex mutex;
    std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t chunk = 0;
    std::size_t next = 0;  // first byte not yet requested
    std::atomic<int> pending{0};
    std::atomic<int> failure{0};  // a libusb_transfer_status other than COMPLETED, or -1 for a short read
    std::chrono::steady_clock::time_point start;
    std::vector<float>* arrival_ms = nullptr;
};

void LIBUSB_CALL on_data(libusb_transfer* t)
{
    auto* s = static_cast<ReadState*>(t->user_data);
    std::lock_guard lock(s->mutex);
    s->arrival_ms->push_back(
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - s->start).count());
    if (t->status != LIBUSB_TRANSFER_COMPLETED || t->actual_length != t->length) {
        if (!s->failure)
            s->failure = t->status != LIBUSB_TRANSFER_COMPLETED ? static_cast<int>(t->status) : -1;
    } else if (!s->failure && s->next < s->size) {
        // Reads complete in the order they were queued; this one takes the next part of the buffer.
        const std::size_t length = std::min(s->chunk, s->size - s->next);
        t->buffer = s->data + s->next;
        t->length = static_cast<int>(length);
        s->next += length;
        if (libusb_submit_transfer(t) == 0)
            return;
        s->failure = LIBUSB_TRANSFER_ERROR;
    }
    --s->pending;
}

}  // namespace

void Device::Impl::read_data(const Bytes& command, Bytes& raw, const ReadStrategy& strategy, unsigned timeout_ms,
                             std::vector<float>& arrival_ms)
{
    if (strategy.in_flight <= 0 || strategy.chunk_bytes == 0) {
        const auto start = std::chrono::steady_clock::now();
        send(command, false);
        int got = 0;
        const int rc = libusb_bulk_transfer(handle, kEpDataIn, raw.data(), static_cast<int>(raw.size()), &got, timeout_ms);
        arrival_ms.push_back(std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count());
        if (rc != 0 || got != static_cast<int>(raw.size()))
            throw Error(std::format("capture failed ({}, {} of {} bytes)", libusb_error_name(rc), got, raw.size()));
        return;
    }

    // The reads are queued right after the command, not before it: the device streams whenever the host reads
    // and only stops when its FIFO is full (which is why a capture starts with 1024 samples of the previous one).
    // Reads waiting before the command took that stream, and the device then did not accept the command.
    const auto start = std::chrono::steady_clock::now();
    send(command, false);
    ReadState s;
    s.data = raw.data();
    s.size = raw.size();
    s.chunk = strategy.chunk_bytes;
    s.start = start;
    s.arrival_ms = &arrival_ms;
    std::vector<libusb_transfer*> transfers;
    for (int i = 0; i < strategy.in_flight; ++i) {
        libusb_transfer* t = libusb_alloc_transfer(0);
        if (!t)
            break;
        std::lock_guard lock(s.mutex);
        if (s.next >= s.size || s.failure) {
            libusb_free_transfer(t);
            break;
        }
        const std::size_t length = std::min(s.chunk, s.size - s.next);
        libusb_fill_bulk_transfer(t, handle, kEpDataIn, s.data + s.next, static_cast<int>(length), on_data, &s, 0);
        s.next += length;
        ++s.pending;
        if (libusb_submit_transfer(t) != 0) {
            --s.pending;
            s.failure = LIBUSB_TRANSFER_ERROR;
            libusb_free_transfer(t);
            break;
        }
        transfers.push_back(t);
    }

    const auto deadline = start + std::chrono::milliseconds(timeout_ms);
    bool cancelled = false;
    while (s.pending > 0) {
        timeval tv{0, 50'000};
        libusb_handle_events_timeout_completed(ctx, &tv, nullptr);
        if (!cancelled && (s.failure || std::chrono::steady_clock::now() > deadline)) {
            {
                std::lock_guard lock(s.mutex);  // no read is queued again after this
                if (!s.failure)
                    s.failure = LIBUSB_TRANSFER_TIMED_OUT;
            }
            for (libusb_transfer* t : transfers)
                libusb_cancel_transfer(t);
            cancelled = true;
        }
    }
    for (libusb_transfer* t : transfers)
        libusb_free_transfer(t);
    if (s.failure || cancelled)
        throw Error(std::format("capture failed ({} of {} bytes requested, status {})", s.next, s.size, s.failure.load()));
}

Device::Device(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Device::~Device() = default;

std::unique_ptr<Device> Device::open()
{
    auto impl = std::make_unique<Impl>();
    impl->open_usb();
    impl->handshake();
    impl->startup();
    return std::unique_ptr<Device>(new Device(std::move(impl)));
}

const Calibration& Device::calibration() const { return *impl_->cal; }

void Device::set_correction(Stage stage, Channel ch, Correction correction)
{
    impl_->cal->set_correction(stage, ch, correction);
}

void Device::clear_corrections() { impl_->cal->clear_corrections(); }

void Device::set_sample_rate(std::uint32_t hz) { impl_->set_rate(hz); }

std::uint32_t Device::sample_rate() const { return impl_->rate_hz; }

void Device::set_stage(Channel ch, Stage stage)
{
    const Calibration& cal = *impl_->cal;
    const bool relay = cal.gain_code(stage) != cal.gain_code(impl_->stage[index(ch)]);
    impl_->send(osc_u16(target(ch), OscCommand::Gain, cal.gain_code(stage)));
    impl_->send(osc_u16(target(ch), OscCommand::Level, cal.level_code(stage, ch)));
    impl_->stage[index(ch)] = stage;
    if (relay)
        impl_->relay_switched();
}

Stage Device::stage(Channel ch) const { return impl_->stage[index(ch)]; }

void Device::set_coupling(Channel ch, Coupling coupling)
{
    impl_->send(osc_value(target(ch), OscCommand::Coupling, static_cast<std::uint32_t>(coupling)));
    if (impl_->coupling[index(ch)] != coupling) {
        impl_->coupling[index(ch)] = coupling;
        impl_->relay_switched();
    }
}

void Device::set_trigger(Channel source, double level_mv, double min_mv, double max_mv)
{
    const TriggerCodes codes = impl_->cal->trigger_codes(stage(source), source, level_mv, min_mv, max_mv);
    impl_->send(trigger_frame(codes.level, codes.hysteresis));
}

std::size_t Device::max_capture_samples() const { return kMemoryDepthSamples - kSettleSamples; }

std::size_t Device::max_contiguous_samples(std::uint32_t hz) const
{
    const auto& rates = descriptor().sample_rates;
    if (!rates.empty() && hz >= rates.back())
        return kContiguousSamplesAtTopRate;
    return max_capture_samples();
}

std::size_t Device::reliable_samples(std::uint32_t hz) const
{
    const auto& rates = descriptor().sample_rates;
    if (!rates.empty() && hz >= kLossyRateHz && hz < rates.back())
        return std::min(kReliableSamplesAtLossyRate, max_capture_samples());
    return max_capture_samples();
}

std::size_t Device::block_samples(std::uint32_t hz) const
{
    const auto& rates = descriptor().sample_rates;
    return !rates.empty() && hz >= rates.back() ? kTopRateBlockSamples : 0;
}

CaptureData Device::capture(std::size_t samples)
{
    if (samples == 0 || samples > max_capture_samples())
        throw Error(std::format("capture length must be 1..{} samples", max_capture_samples()));
    const std::size_t request =
        (samples + kSettleSamples + kCaptureGranularity - 1) / kCaptureGranularity * kCaptureGranularity;

    Bytes raw(request * 2);
    const auto acquisition_ms = static_cast<unsigned>(request * 1000 / impl_->rate_hz);
    CaptureData data;
    impl_->wait_settled();
    impl_->read_data(osc_value(Target::Device, OscCommand::Capture, static_cast<std::uint32_t>(request)), raw,
                     impl_->read_strategy_for(impl_->rate_hz), 2000 + 2 * acquisition_ms, data.arrival_ms);
    data.sample_rate_hz = impl_->rate_hz;
    data.stage = impl_->stage;
    for (std::size_t ch = 0; ch < 2; ++ch) {
        data.raw[ch].resize(samples);
        for (std::size_t i = 0; i < samples; ++i)
            data.raw[ch][i] = raw[2 * (kSettleSamples + i) + ch];
    }
    return data;
}

void Device::set_read_strategy(const ReadStrategy& strategy) { impl_->read_strategy = strategy; }

bool Device::has_generator() const { return isds::has_generator(descriptor().model); }

bool Device::has_generator_level_control() const { return has_level_control(descriptor().model); }

void Device::set_generator(const GeneratorSettings& settings)
{
    if (!has_generator())
        throw Error("this model has no signal generator");
    if (impl_->generator_on)
        impl_->send_generator(settings, &impl_->generator);
    impl_->generator = settings;
}

const GeneratorSettings& Device::generator() const { return impl_->generator; }

void Device::set_generator_output(bool on)
{
    if (!has_generator())
        throw Error("this model has no signal generator");
    if (on) {
        impl_->send(dds_value(Target::Device, DdsCommand::Start));
        impl_->send_generator(impl_->generator, nullptr);
        impl_->send(dds_value(Target::Device, DdsCommand::OutputOn));
    } else {
        impl_->send(dds_value(Target::Device, DdsCommand::OutputOff));
    }
    impl_->generator_on = on;
}

bool Device::generator_output() const { return impl_->generator_on; }

bool Device::connected() const { return !impl_->disconnected; }

}  // namespace isds
