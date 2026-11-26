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

#include "platform/MSWindowsClipboardImageUtils.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

namespace inputleap {

namespace {

// 2x2 bottom-up DIB, rows (bottom to top): [red, green], [blue, white]
std::string make_dib(WORD bit_count, DWORD compression)
{
    BITMAPINFOHEADER info = {};
    info.biSize = sizeof(info);
    info.biWidth = 2;
    info.biHeight = 2;
    info.biPlanes = 1;
    info.biBitCount = bit_count;
    info.biCompression = compression;

    std::string dib(reinterpret_cast<const char*>(&info), sizeof(info));
    if (compression == BI_BITFIELDS) {
        const DWORD masks[3] = { 0x00ff0000, 0x0000ff00, 0x000000ff };
        dib.append(reinterpret_cast<const char*>(masks), sizeof(masks));
    }
    const unsigned char bgr[4][3] = {
        { 0, 0, 255 }, { 0, 255, 0 },       // bottom row: red, green
        { 255, 0, 0 }, { 255, 255, 255 },   // top row: blue, white
    };
    for (int row = 0; row < 2; ++row) {
        std::string line;
        for (int x = 0; x < 2; ++x) {
            const unsigned char* p = bgr[row * 2 + x];
            line.append(reinterpret_cast<const char*>(p), 3);
            if (bit_count == 32) {
                line += '\0';   // alpha is usually 0 in clipboard bitmaps
            }
        }
        while (line.size() % 4 != 0) {
            line += '\0';
        }
        dib += line;
    }
    return dib;
}

// returns BGR of pixel (x, y) counted from the bottom of a 32bpp bottom-up DIB
std::uint32_t pixel(const std::string& dib, int x, int y)
{
    const auto* info = reinterpret_cast<const BITMAPINFOHEADER*>(dib.data());
    const unsigned char* p = reinterpret_cast<const unsigned char*>(dib.data()) +
                             info->biSize + (y * info->biWidth + x) * 4;
    return (p[0] << 16) | (p[1] << 8) | p[2];
}

void expect_roundtrip(const std::string& dib)
{
    std::string png = mswindows_image::dib_to_png(dib.data(), dib.size());
    ASSERT_GT(png.size(), 8u);
    EXPECT_EQ(0, std::memcmp(png.data(), "\x89PNG\r\n\x1a\n", 8));

    std::string decoded = mswindows_image::decode_to_dib(png);
    ASSERT_EQ(sizeof(BITMAPINFOHEADER) + 2 * 2 * 4, decoded.size());
    const auto* info = reinterpret_cast<const BITMAPINFOHEADER*>(decoded.data());
    EXPECT_EQ(2, info->biWidth);
    EXPECT_EQ(2, info->biHeight);
    EXPECT_EQ(32, info->biBitCount);
    EXPECT_EQ(static_cast<DWORD>(BI_RGB), info->biCompression);

    // (blue, green, red) packed as 0xBBGGRR
    EXPECT_EQ(0x0000ffu, pixel(decoded, 0, 0));    // red
    EXPECT_EQ(0x00ff00u, pixel(decoded, 1, 0));    // green
    EXPECT_EQ(0xff0000u, pixel(decoded, 0, 1));    // blue
    EXPECT_EQ(0xffffffu, pixel(decoded, 1, 1));    // white
}

} // namespace

TEST(MSWindowsClipboardImageUtilsTests, roundtrip_24bpp)
{
    expect_roundtrip(make_dib(24, BI_RGB));
}

TEST(MSWindowsClipboardImageUtilsTests, roundtrip_32bpp_zero_alpha_is_opaque)
{
    expect_roundtrip(make_dib(32, BI_RGB));
}

TEST(MSWindowsClipboardImageUtilsTests, roundtrip_32bpp_bitfields)
{
    expect_roundtrip(make_dib(32, BI_BITFIELDS));
}

TEST(MSWindowsClipboardImageUtilsTests, invalid_input_returns_empty)
{
    EXPECT_TRUE(mswindows_image::decode_to_dib("").empty());
    EXPECT_TRUE(mswindows_image::decode_to_dib("not an image").empty());
    std::string tiny(10, '\0');
    EXPECT_TRUE(mswindows_image::dib_to_png(tiny.data(), tiny.size()).empty());
}

} // namespace inputleap
