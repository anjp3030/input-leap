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

#include "io/filesystem.h"

#include <cstdint>
#include <string>
#include <vector>

namespace inputleap {

/*
Helpers for the IClipboard::kFiles format.

The kFiles clipboard data is an archive holding copied files and
directories together with their contents.  All integers are big endian.

    4 bytes  magic "ILF1"
    4 bytes  number of entries
    for each entry:
        1 byte   type (0 = regular file, 1 = directory)
        4 bytes  path length n
        n bytes  relative path, UTF-8, components separated by '/'.  The
                 first component is the name of the item that was copied.
                 Directories always appear before their contents.
        (regular files only)
        8 bytes  file size m
        m bytes  file contents
*/
namespace clipboard_files {

// Maximum total size of file contents put on the clipboard.  Defaults to
// 90 MiB and can be overridden with the INPUTLEAP_CLIPBOARD_FILES_MAX_MB
// environment variable (0 disables copying files).
std::uint64_t max_total_size();

// Reads the given files and directories (recursively) and returns the
// archive.  Returns an empty string if nothing could be read or if the
// total size exceeds max_total_size().
std::string pack(const std::vector<fs::path>& paths);
std::string pack(const std::vector<fs::path>& paths, std::uint64_t max_size);

// Extracts the archive into dest_dir, which must exist.  Returns the paths
// of the top level items in dest_dir.  Throws std::runtime_error if the
// archive is malformed or contains unsafe paths.
std::vector<fs::path> unpack(const std::string& archive, const fs::path& dest_dir);

// Extracts the archive into a new private temporary directory and returns
// the top level items.  Extracting the same archive again returns the
// previous result as long as the files still exist.  Only the last few
// temporary directories are kept.  Throws std::runtime_error on failure.
std::vector<fs::path> unpack_to_temp(const std::string& archive);

// Conversions between local paths and file:// URIs (RFC 8089).
std::string path_to_file_uri(const fs::path& path);
bool file_uri_to_path(const std::string& uri, fs::path* path);

// Conversions between paths and UTF-8 strings.
std::string path_to_utf8(const fs::path& path);
fs::path utf8_to_path(const std::string& str);

} // namespace clipboard_files
} // namespace inputleap
