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

#include "platform/MSWindowsClipboardFilesConverter.h"

#include "inputleap/ClipboardFiles.h"
#include "base/Log.h"

#include <shlobj.h>

#include <cstring>
#include <exception>
#include <vector>

namespace inputleap {

MSWindowsClipboardFilesConverter::MSWindowsClipboardFilesConverter() = default;

MSWindowsClipboardFilesConverter::~MSWindowsClipboardFilesConverter() = default;

IClipboard::EFormat MSWindowsClipboardFilesConverter::getFormat() const
{
    return IClipboard::kFiles;
}

UINT MSWindowsClipboardFilesConverter::getWin32Format() const
{
    return CF_HDROP;
}

HANDLE MSWindowsClipboardFilesConverter::fromIClipboard(const std::string& archive) const
{
    std::vector<fs::path> paths;
    try {
        paths = clipboard_files::unpack_to_temp(archive);
    } catch (const std::exception& e) {
        LOG_WARN("can't paste files from clipboard: %s", e.what());
        return nullptr;
    }
    if (paths.empty()) {
        return nullptr;
    }

    // DROPFILES followed by a double null terminated list of wide strings
    std::size_t chars = 1;
    for (const auto& path : paths) {
        chars += path.native().size() + 1;
    }
    std::size_t size = sizeof(DROPFILES) + chars * sizeof(wchar_t);

    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE | GMEM_ZEROINIT, size);
    if (handle == nullptr) {
        return nullptr;
    }
    auto* drop = static_cast<DROPFILES*>(GlobalLock(handle));
    if (drop == nullptr) {
        GlobalFree(handle);
        return nullptr;
    }
    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;
    auto* dst = reinterpret_cast<wchar_t*>(reinterpret_cast<char*>(drop) + sizeof(DROPFILES));
    for (const auto& path : paths) {
        const std::wstring& name = path.native();
        std::memcpy(dst, name.c_str(), name.size() * sizeof(wchar_t));
        dst += name.size() + 1;     // terminators come from GMEM_ZEROINIT
    }
    GlobalUnlock(handle);
    return handle;
}

std::string MSWindowsClipboardFilesConverter::toIClipboard(HANDLE data) const
{
    std::vector<fs::path> paths;

    const auto* drop = static_cast<const DROPFILES*>(GlobalLock(data));
    if (drop == nullptr) {
        return {};
    }
    SIZE_T total = GlobalSize(data);
    if (total >= sizeof(DROPFILES) && drop->pFiles < total) {
        const char* begin = reinterpret_cast<const char*>(drop) + drop->pFiles;
        const char* end = reinterpret_cast<const char*>(drop) + total;
        if (drop->fWide) {
            const auto* p = reinterpret_cast<const wchar_t*>(begin);
            const auto* e = reinterpret_cast<const wchar_t*>(end);
            while (p < e && *p != L'\0') {
                const wchar_t* s = p;
                while (p < e && *p != L'\0') {
                    ++p;
                }
                paths.emplace_back(std::wstring(s, p));
                ++p;
            }
        } else {
            const char* p = begin;
            while (p < end && *p != '\0') {
                const char* s = p;
                while (p < end && *p != '\0') {
                    ++p;
                }
                std::string ansi(s, p);
                int len = MultiByteToWideChar(CP_ACP, 0, ansi.data(),
                                              static_cast<int>(ansi.size()), nullptr, 0);
                std::wstring wide(len, L'\0');
                MultiByteToWideChar(CP_ACP, 0, ansi.data(), static_cast<int>(ansi.size()),
                                    &wide[0], len);
                paths.emplace_back(wide);
                ++p;
            }
        }
    }
    GlobalUnlock(data);

    if (paths.empty()) {
        return {};
    }
    return clipboard_files::pack(paths);
}

} // namespace inputleap
