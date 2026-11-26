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

#include "platform/XWindowsClipboardFilesConverter.h"

#include "inputleap/ClipboardFiles.h"
#include "base/Log.h"

#include <exception>

namespace inputleap {

XWindowsClipboardFilesConverter::XWindowsClipboardFilesConverter(
                Display* display, const char* name, Style style) :
    m_atom(XInternAtom(display, name, False)),
    m_style(style)
{
}

XWindowsClipboardFilesConverter::~XWindowsClipboardFilesConverter() = default;

IClipboard::EFormat XWindowsClipboardFilesConverter::getFormat() const
{
    return IClipboard::kFiles;
}

Atom XWindowsClipboardFilesConverter::getAtom() const
{
    return m_atom;
}

int XWindowsClipboardFilesConverter::getDataSize() const
{
    return 8;
}

std::string XWindowsClipboardFilesConverter::fromIClipboard(const std::string& archive) const
{
    std::vector<fs::path> paths;
    try {
        paths = clipboard_files::unpack_to_temp(archive);
    } catch (const std::exception& e) {
        LOG_WARN("can't paste files from clipboard: %s", e.what());
        return {};
    }

    std::string result;
    if (m_style == Style::GnomeCopiedFiles) {
        result = "copy";
        for (const auto& path : paths) {
            result += "\n";
            result += clipboard_files::path_to_file_uri(path);
        }
    } else {
        for (const auto& path : paths) {
            result += clipboard_files::path_to_file_uri(path);
            result += "\r\n";
        }
    }
    return result;
}

std::string XWindowsClipboardFilesConverter::toIClipboard(const std::string& data) const
{
    std::vector<fs::path> paths;
    bool first = true;
    std::size_t start = 0;
    while (start < data.size()) {
        std::size_t end = data.find('\n', start);
        if (end == std::string::npos) {
            end = data.size();
        }
        std::string line = data.substr(start, end - start);
        start = end + 1;

        while (!line.empty() && (line.back() == '\r' || line.back() == '\0')) {
            line.pop_back();
        }

        if (m_style == Style::GnomeCopiedFiles && first) {
            // first line is the operation: "copy" or "cut".  cut is treated
            // as copy, the source files are never removed.
            first = false;
            if (line == "copy" || line == "cut") {
                continue;
            }
        }
        first = false;

        if (line.empty() || line[0] == '#') {
            continue;
        }

        fs::path path;
        if (clipboard_files::file_uri_to_path(line, &path)) {
            paths.push_back(path);
        } else {
            LOG_DEBUG("clipboard files: ignoring non-local uri %s", line.c_str());
        }
    }

    if (paths.empty()) {
        return {};
    }
    return clipboard_files::pack(paths);
}

} // namespace inputleap
