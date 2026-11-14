// SPDX-License-Identifier: GPL-3.0-or-later
// Writes the application icon (app/src/appicon.cpp) as files: a PNG of each size for the Linux installers, and the
// Windows .ico that goes into the executable.
//
//   star-icon <folder>     e.g. star-icon app/resources
//
// Run it after changing the drawing and commit the files; a GUI test checks that they still match it.
#include <cstdint>
#include <cstdio>

#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QImage>

#include "appicon.hpp"

namespace {

QByteArray png(int size)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    app_icon::image(size).save(&buffer, "PNG");
    return bytes;
}

void put16(QByteArray& out, std::uint16_t v)
{
    out.append(static_cast<char>(v & 0xff));
    out.append(static_cast<char>(v >> 8));
}

void put32(QByteArray& out, std::uint32_t v)
{
    put16(out, static_cast<std::uint16_t>(v & 0xffff));
    put16(out, static_cast<std::uint16_t>(v >> 16));
}

// An .ico of PNG images, which Windows reads since Vista: a header, one 16-byte entry per image, then the images.
QByteArray ico()
{
    constexpr std::size_t count = std::size(app_icon::kIcoSizes);
    QByteArray out;
    put16(out, 0);  // reserved
    put16(out, 1);  // icon
    put16(out, count);
    std::uint32_t offset = 6 + 16 * count;
    QByteArray images;
    for (int size : app_icon::kIcoSizes) {
        const QByteArray image = png(size);
        out.append(static_cast<char>(size >= 256 ? 0 : size));  // 0 means 256
        out.append(static_cast<char>(size >= 256 ? 0 : size));
        out.append('\0');  // no palette
        out.append('\0');  // reserved
        put16(out, 1);     // colour planes
        put16(out, 32);    // bits per pixel
        put32(out, static_cast<std::uint32_t>(image.size()));
        put32(out, offset);
        offset += static_cast<std::uint32_t>(image.size());
        images += image;
    }
    return out + images;
}

bool write(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        std::fprintf(stderr, "cannot write %s\n", qPrintable(path));
        return false;
    }
    std::printf("%s (%lld bytes)\n", qPrintable(QDir::toNativeSeparators(path)), static_cast<long long>(bytes.size()));
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: star-icon <folder>\n");
        return 2;
    }
    const QDir folder(QString::fromLocal8Bit(argv[1]));
    if (!folder.mkpath("icons")) {
        std::fprintf(stderr, "cannot make %s/icons\n", argv[1]);
        return 1;
    }
    bool ok = true;
    for (int size : app_icon::kPngSizes)
        ok = write(folder.filePath(QString("icons/star-oscilloscope-%1.png").arg(size)), png(size)) && ok;
    ok = write(folder.filePath("star-oscilloscope.ico"), ico()) && ok;
    return ok ? 0 : 1;
}
