// SPDX-License-Identifier: GPL-3.0-or-later
// The application's icon: an oscilloscope screen in a grey bevelled frame, with a yellow trace and a blue sparkle at
// its corner. Drawn in code at whatever size is asked for; below about 40 pixels the graticule is left out and the
// trace drawn bolder, so the small sizes stay clear; the 16-pixel icon is drawn for its pixels. The frame keeps to
// whole pixels.
#pragma once

#include <QIcon>
#include <QImage>

namespace app_icon {

QIcon icon();

// The icon at one size in pixels, for the files of the executable and the installers (tools/make_icon.cpp).
QImage image(int size);

// The sizes of the icon files: a PNG of each of the first in app/resources/icons, the second in the Windows .ico.
inline constexpr int kPngSizes[] = {16, 24, 32, 48, 64, 128, 256};
inline constexpr int kIcoSizes[] = {16, 20, 24, 32, 40, 48, 64, 256};

}  // namespace app_icon
