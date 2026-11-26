/*
 * InputLeap -- mouse and keyboard sharing utility
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstddef>
#include <string>

namespace inputleap {
namespace mswindows_image {

// Decodes an encoded image (PNG, JPEG, TIFF, WEBP if a codec is installed,
// ...) with WIC and returns it as a 32bpp bottom-up BI_RGB DIB (a
// BITMAPINFOHEADER followed by the pixels), the CF_DIB clipboard format.
// Transparent pixels are composited onto white because most applications
// ignore the alpha channel of CF_DIB.  Returns an empty string on failure.
std::string decode_to_dib(const std::string& encoded);

// Encodes a CF_DIB (BITMAPINFO followed by the pixels) as PNG with WIC.
// Returns an empty string on failure.
std::string dib_to_png(const void* dib, std::size_t size);

} // namespace mswindows_image
} // namespace inputleap
