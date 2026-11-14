// SPDX-License-Identifier: GPL-3.0-or-later
// LD_PRELOAD shim that logs the libusb bulk traffic of a closed-source driver.
//
//   USBSNIFF_LOG=trace.log USBSNIFF_DIR=dumps LD_PRELOAD=./libusbsniff.so <program>
//
// Every bulk transfer is written to the log as one line (time, thread, endpoint, result, hex).
// IN payloads larger than USBSNIFF_HEX bytes (default 64) are also saved whole under USBSNIFF_DIR.
// Empty IN transfers that just timed out (polling threads) are only counted.
// A program can insert markers into the log by calling usbsniff_mark("text").
#define _GNU_SOURCE
#include <dlfcn.h>
#include <libusb-1.0/libusb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

typedef int (*bulk_fn)(libusb_device_handle *, unsigned char, unsigned char *, int, int *, unsigned int);

static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static bulk_fn real_bulk;
static FILE *out;
static const char *dump_dir;
static int hex_limit = 64;
static unsigned dump_seq;
static unsigned long idle_in;
static struct timespec t0;

// libusb may be loaded later and only into a dlopen()ed library's local scope (e.g. via ctypes),
// where RTLD_NEXT cannot see it, so resolve on first use and fall back to opening it by name.
static bulk_fn resolve_bulk(void)
{
    bulk_fn fn = (bulk_fn)dlsym(RTLD_NEXT, "libusb_bulk_transfer");
    if (!fn) {
        void *lib = dlopen("libusb-1.0.so.0", RTLD_LAZY | RTLD_NOLOAD);
        if (!lib)
            lib = dlopen("libusb-1.0.so.0", RTLD_LAZY);
        if (lib)
            fn = (bulk_fn)dlsym(lib, "libusb_bulk_transfer");
    }
    return fn;
}

static void setup(void)
{
    const char *path = getenv("USBSNIFF_LOG");
    out = path ? fopen(path, "w") : NULL;
    if (!out)
        out = stderr;
    dump_dir = getenv("USBSNIFF_DIR");
    if (getenv("USBSNIFF_HEX"))
        hex_limit = atoi(getenv("USBSNIFF_HEX"));
    clock_gettime(CLOCK_MONOTONIC, &t0);
}

static double elapsed(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (t.tv_sec - t0.tv_sec) + (t.tv_nsec - t0.tv_nsec) / 1e9;
}

static void flush_idle(void)
{
    if (idle_in) {
        fprintf(out, "%11.6f       (%lu empty IN timeouts)\n", elapsed(), idle_in);
        idle_in = 0;
    }
}

__attribute__((visibility("default"))) void usbsniff_mark(const char *text)
{
    pthread_once(&once, setup);
    pthread_mutex_lock(&lock);
    flush_idle();
    fprintf(out, "%11.6f ---- %s\n", elapsed(), text);
    fflush(out);
    pthread_mutex_unlock(&lock);
}

int libusb_bulk_transfer(libusb_device_handle *handle, unsigned char endpoint, unsigned char *data,
                         int length, int *transferred, unsigned int timeout)
{
    pthread_once(&once, setup);
    if (!real_bulk)
        real_bulk = resolve_bulk();
    if (!real_bulk)
        return LIBUSB_ERROR_NOT_SUPPORTED;
    int rc = real_bulk(handle, endpoint, data, length, transferred, timeout);
    int in = endpoint & LIBUSB_ENDPOINT_IN;
    int got = transferred ? *transferred : 0;

    pthread_mutex_lock(&lock);
    if (in && got == 0 && rc == LIBUSB_ERROR_TIMEOUT) {
        idle_in++;
        pthread_mutex_unlock(&lock);
        return rc;
    }
    flush_idle();
    fprintf(out, "%11.6f T%-5ld %s %02x len=%-6d rc=%-3d got=%-6d ", elapsed(), (long)syscall(SYS_gettid),
            in ? "IN " : "OUT", endpoint, length, rc, got);
    if (in && dump_dir && got > hex_limit) {
        char name[512];
        snprintf(name, sizeof name, "%s/%06u_ep%02x.bin", dump_dir, dump_seq++, endpoint);
        FILE *f = fopen(name, "wb");
        if (f) {
            fwrite(data, 1, got, f);
            fclose(f);
            fprintf(out, "[%s] ", name);
        }
    }
    int shown = got < hex_limit ? got : hex_limit;
    for (int i = 0; i < shown; i++)
        fprintf(out, "%02x%s", data[i], (i % 4 == 3 && i + 1 < shown) ? " " : "");
    if (shown < got)
        fputs(" ...", out);
    fputc('\n', out);
    fflush(out);
    pthread_mutex_unlock(&lock);
    return rc;
}
