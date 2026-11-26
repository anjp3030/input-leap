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

#include "platform/MSWindowsClipboardPNGConverter.h"

#include <cstring>

namespace inputleap {

MSWindowsClipboardPNGConverter::MSWindowsClipboardPNGConverter() :
    m_format(RegisterClipboardFormat("PNG"))
{
}

MSWindowsClipboardPNGConverter::~MSWindowsClipboardPNGConverter() = default;

IClipboard::EFormat MSWindowsClipboardPNGConverter::getFormat() const
{
    return IClipboard::kPNG;
}

UINT MSWindowsClipboardPNGConverter::getWin32Format() const
{
    return m_format;
}

HANDLE MSWindowsClipboardPNGConverter::fromIClipboard(const std::string& data) const
{
    if (data.empty()) {
        return nullptr;
    }
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, data.size());
    if (handle == nullptr) {
        return nullptr;
    }
    void* dst = GlobalLock(handle);
    if (dst == nullptr) {
        GlobalFree(handle);
        return nullptr;
    }
    std::memcpy(dst, data.data(), data.size());
    GlobalUnlock(handle);
    return handle;
}

std::string MSWindowsClipboardPNGConverter::toIClipboard(HANDLE data) const
{
    const void* src = GlobalLock(data);
    if (src == nullptr) {
        return {};
    }
    std::string result(static_cast<const char*>(src), GlobalSize(data));
    GlobalUnlock(data);
    return result;
}

} // namespace inputleap
