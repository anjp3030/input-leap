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

#include "inputleap/ClipboardFiles.h"

#include "base/Log.h"

#if SYSAPI_WIN32
#include "common/win32/encoding_utilities.h"
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <set>
#include <stdexcept>

namespace inputleap {
namespace clipboard_files {

namespace {

const char kMagic[4] = { 'I', 'L', 'F', '1' };
const std::uint8_t kTypeFile = 0;
const std::uint8_t kTypeDirectory = 1;
const std::uint32_t kMaxPathLength = 64 * 1024;
const std::size_t kKeptTempDirs = 2;

struct Entry {
    bool directory;
    std::string rel_path;   // UTF-8, '/' separated
    fs::path source;
    std::uint64_t size;
};

void write_u8(std::string& out, std::uint8_t v)
{
    out += static_cast<char>(v);
}

void write_u32(std::string& out, std::uint32_t v)
{
    for (int shift = 24; shift >= 0; shift -= 8) {
        out += static_cast<char>((v >> shift) & 0xff);
    }
}

void write_u64(std::string& out, std::uint64_t v)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        out += static_cast<char>((v >> shift) & 0xff);
    }
}

class Reader {
public:
    explicit Reader(const std::string& data) : m_data(data), m_pos(0) { }

    bool at_end() const { return m_pos == m_data.size(); }

    std::uint64_t read_uint(std::size_t bytes)
    {
        need(bytes);
        std::uint64_t v = 0;
        for (std::size_t i = 0; i < bytes; ++i) {
            v = (v << 8) | static_cast<unsigned char>(m_data[m_pos++]);
        }
        return v;
    }

    // returns a pointer to the next n bytes and advances past them
    const char* read_bytes(std::uint64_t n)
    {
        need(n);
        const char* p = m_data.data() + m_pos;
        m_pos += static_cast<std::size_t>(n);
        return p;
    }

private:
    void need(std::uint64_t n) const
    {
        if (n > m_data.size() - m_pos) {
            throw std::runtime_error("truncated clipboard file archive");
        }
    }

    const std::string& m_data;
    std::size_t m_pos;
};

// relative path below a copied directory as a '/' separated UTF-8 string
std::string relative_utf8(const fs::path& path, const fs::path& base)
{
    std::string result;
    for (const auto& component : path.lexically_relative(base)) {
        if (!result.empty()) {
            result += '/';
        }
        result += path_to_utf8(component);
    }
    return result;
}

bool add_file(std::vector<Entry>& entries, const fs::path& path, const std::string& rel,
              std::uint64_t& total, std::uint64_t max_size)
{
    std::error_code ec;
    std::uint64_t size = fs::file_size(path, ec);
    if (ec) {
        LOG_WARN("clipboard files: can't get size of %s: %s",
                 path_to_utf8(path).c_str(), ec.message().c_str());
        return true;    // skip just this file
    }
    total += size;
    if (total > max_size) {
        LOG_WARN("clipboard files: copied files exceed %llu MiB limit, not sharing them "
                 "(set INPUTLEAP_CLIPBOARD_FILES_MAX_MB to change the limit)",
                 static_cast<unsigned long long>(max_size / (1024 * 1024)));
        return false;
    }
    entries.push_back(Entry{false, rel, path, size});
    return true;
}

bool collect(std::vector<Entry>& entries, const fs::path& input,
             std::uint64_t& total, std::uint64_t max_size)
{
    fs::path path = input.lexically_normal();
    if (path.filename().empty()) {
        path = path.parent_path();      // path had a trailing separator
    }
    const std::string name = path_to_utf8(path.filename());
    if (name.empty() || name == "." || name == "..") {
        LOG_WARN("clipboard files: ignoring %s", path_to_utf8(input).c_str());
        return true;
    }

    std::error_code ec;
    fs::file_status status = fs::status(path, ec);
    if (ec) {
        LOG_WARN("clipboard files: can't access %s: %s",
                 path_to_utf8(path).c_str(), ec.message().c_str());
        return true;
    }

    if (fs::is_regular_file(status)) {
        return add_file(entries, path, name, total, max_size);
    }
    if (!fs::is_directory(status)) {
        LOG_WARN("clipboard files: ignoring special file %s", path_to_utf8(path).c_str());
        return true;
    }

    entries.push_back(Entry{true, name, path, 0});

    // symlinks to directories are not followed which also avoids loops
    fs::recursive_directory_iterator it(path, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        const fs::path& child = it->path();
        std::error_code ec2;
        fs::file_status link_status = it->symlink_status(ec2);
        fs::file_status child_status = it->status(ec2);
        if (ec2) {
            continue;   // e.g. dangling symlink
        }

        std::string rel = name + "/" + relative_utf8(child, path);
        if (fs::is_directory(child_status)) {
            if (fs::is_symlink(link_status)) {
                LOG_DEBUG("clipboard files: skipping symlinked directory %s",
                          path_to_utf8(child).c_str());
                continue;
            }
            entries.push_back(Entry{true, rel, child, 0});
        } else if (fs::is_regular_file(child_status)) {
            if (!add_file(entries, child, rel, total, max_size)) {
                return false;
            }
        }
    }
    if (ec) {
        LOG_WARN("clipboard files: error reading directory %s: %s",
                 path_to_utf8(path).c_str(), ec.message().c_str());
    }
    return true;
}

bool read_file(const fs::path& path, std::uint64_t size, std::string& out)
{
    std::ifstream file;
    open_utf8_path(file, path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    std::size_t offset = out.size();
    out.resize(offset + static_cast<std::size_t>(size));
    file.read(&out[offset], static_cast<std::streamsize>(size));
    if (static_cast<std::uint64_t>(file.gcount()) != size) {
        out.resize(offset);
        return false;
    }
    return true;
}

// Replaces characters that the local file system can't store.
std::string sanitize_component(const std::string& component)
{
#if SYSAPI_WIN32
    std::string result = component;
    for (char& c : result) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 32 || std::string("<>:\"/\\|?*").find(c) != std::string::npos) {
            c = '_';
        }
    }
    // Windows silently strips trailing dots and spaces
    while (!result.empty() && (result.back() == '.' || result.back() == ' ')) {
        result.back() = '_';
    }
    return result;
#else
    return component;
#endif
}

// Splits a relative archive path into components and checks that it can't
// escape the destination directory.
std::vector<std::string> split_safe_path(const std::string& rel)
{
    std::vector<std::string> components;
    if (rel.find('\0') != std::string::npos) {
        throw std::runtime_error("invalid path in clipboard file archive");
    }
    std::size_t start = 0;
    while (true) {
        std::size_t slash = rel.find('/', start);
        std::string component = rel.substr(start, slash == std::string::npos ?
                                                      std::string::npos : slash - start);
        if (component.empty() || component == "." || component == "..") {
            throw std::runtime_error("unsafe path in clipboard file archive: " + rel);
        }
        components.push_back(sanitize_component(component));
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    return components;
}

std::uint64_t parse_max_size()
{
    // stays below the 100 MB maximum of the "clipboard sharing size" server
    // setting so that copied files are not silently dropped by the server
    const std::uint64_t default_mb = 90;
    std::uint64_t mb = default_mb;
    if (const char* env = std::getenv("INPUTLEAP_CLIPBOARD_FILES_MAX_MB")) {
        char* end = nullptr;
        unsigned long long v = std::strtoull(env, &end, 10);
        if (end != env && *end == '\0') {
            mb = v;
        } else {
            LOG_WARN("invalid INPUTLEAP_CLIPBOARD_FILES_MAX_MB value: %s", env);
        }
    }
    return mb * 1024 * 1024;
}

// Returns (and creates if needed) the directory under which archives are
// extracted.  The directory is private to the current user.
fs::path temp_base_dir()
{
#if SYSAPI_WIN32
    fs::path base = fs::temp_directory_path() / "InputLeap-clipboard";
    fs::create_directories(base);
#else
    fs::path base = fs::temp_directory_path() /
                    ("inputleap-clipboard-" + std::to_string(getuid()));
    if (mkdir(base.c_str(), 0700) != 0 && errno != EEXIST) {
        throw std::runtime_error("can't create " + base.string());
    }
    // the temp directory is shared with other users, make sure that nobody
    // planted something else there
    struct stat st;
    if (lstat(base.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) ||
        st.st_uid != getuid() || (st.st_mode & 077) != 0) {
        throw std::runtime_error(base.string() + " is not a private directory");
    }
#endif
    return base;
}

} // namespace

std::uint64_t max_total_size()
{
    static const std::uint64_t value = parse_max_size();
    return value;
}

std::string pack(const std::vector<fs::path>& paths)
{
    return pack(paths, max_total_size());
}

std::string pack(const std::vector<fs::path>& paths, std::uint64_t max_size)
{
    std::vector<Entry> entries;
    std::uint64_t total = 0;
    for (const auto& path : paths) {
        if (!collect(entries, path, total, max_size)) {
            return {};
        }
    }
    if (entries.empty()) {
        return {};
    }

    std::string out;
    out.reserve(static_cast<std::size_t>(total) + entries.size() * 64 + 8);
    out.append(kMagic, sizeof(kMagic));
    write_u32(out, 0);  // number of entries, patched below

    std::uint32_t count = 0;
    for (const auto& entry : entries) {
        if (entry.directory) {
            write_u8(out, kTypeDirectory);
            write_u32(out, static_cast<std::uint32_t>(entry.rel_path.size()));
            out += entry.rel_path;
            ++count;
            continue;
        }

        std::size_t entry_start = out.size();
        write_u8(out, kTypeFile);
        write_u32(out, static_cast<std::uint32_t>(entry.rel_path.size()));
        out += entry.rel_path;
        write_u64(out, entry.size);
        if (!read_file(entry.source, entry.size, out)) {
            LOG_WARN("clipboard files: can't read %s", path_to_utf8(entry.source).c_str());
            out.resize(entry_start);
            continue;
        }
        ++count;
    }

    std::string count_bytes;
    write_u32(count_bytes, count);
    out.replace(sizeof(kMagic), 4, count_bytes);

    LOG_DEBUG("clipboard files: packed %u entries, %llu bytes", count,
              static_cast<unsigned long long>(out.size()));
    return out;
}

std::vector<fs::path> unpack(const std::string& archive, const fs::path& dest_dir)
{
    Reader reader(archive);
    const char* magic = reader.read_bytes(sizeof(kMagic));
    if (!std::equal(magic, magic + sizeof(kMagic), kMagic)) {
        throw std::runtime_error("not a clipboard file archive");
    }

    std::vector<fs::path> top_level;
    std::set<fs::path> seen_top_level;

    std::uint64_t count = reader.read_uint(4);
    for (std::uint64_t i = 0; i < count; ++i) {
        std::uint8_t type = static_cast<std::uint8_t>(reader.read_uint(1));
        std::uint64_t path_len = reader.read_uint(4);
        if (path_len > kMaxPathLength) {
            throw std::runtime_error("path too long in clipboard file archive");
        }
        std::string rel(reader.read_bytes(path_len), static_cast<std::size_t>(path_len));

        std::vector<std::string> components = split_safe_path(rel);
        fs::path target = dest_dir;
        for (const auto& component : components) {
            target /= utf8_to_path(component);
        }

        fs::path top = dest_dir / utf8_to_path(components.front());
        if (seen_top_level.insert(top).second) {
            top_level.push_back(top);
        }

        if (type == kTypeDirectory) {
            fs::create_directories(target);
        } else if (type == kTypeFile) {
            std::uint64_t size = reader.read_uint(8);
            const char* data = reader.read_bytes(size);
            fs::create_directories(target.parent_path());
            std::ofstream file;
            open_utf8_path(file, target, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!file.is_open()) {
                throw std::runtime_error("can't create " + path_to_utf8(target));
            }
            file.write(data, static_cast<std::streamsize>(size));
            if (!file) {
                throw std::runtime_error("can't write " + path_to_utf8(target));
            }
        } else {
            throw std::runtime_error("unknown entry type in clipboard file archive");
        }
    }
    if (!reader.at_end()) {
        throw std::runtime_error("trailing data in clipboard file archive");
    }
    return top_level;
}

std::vector<fs::path> unpack_to_temp(const std::string& archive)
{
    static std::mutex mutex;
    static bool cleaned_stale = false;
    static std::size_t last_hash = 0;
    static std::size_t last_size = 0;
    static std::vector<fs::path> last_result;
    static std::deque<fs::path> dirs;

    std::lock_guard<std::mutex> lock(mutex);

    std::size_t hash = std::hash<std::string>()(archive);
    if (!last_result.empty() && hash == last_hash && archive.size() == last_size) {
        bool all_exist = std::all_of(last_result.begin(), last_result.end(),
                                     [](const fs::path& p) {
                                         std::error_code ec;
                                         return fs::exists(p, ec);
                                     });
        if (all_exist) {
            return last_result;
        }
    }

    fs::path base = temp_base_dir();
    if (!cleaned_stale) {
        // remove leftovers from previous runs
        std::error_code ec;
        for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code ec2;
            fs::remove_all(it->path(), ec2);
        }
        cleaned_stale = true;
    }

    // keep a couple of previous directories around in case a paste from
    // them is still in progress
    while (dirs.size() >= kKeptTempDirs) {
        std::error_code ec;
        fs::remove_all(dirs.front(), ec);
        dirs.pop_front();
    }

    auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
    fs::path dir;
    for (int attempt = 0;; ++attempt) {
        dir = base / ("clip-" + std::to_string(stamp) + "-" + std::to_string(attempt));
        if (fs::create_directory(dir)) {
            break;
        }
    }
    dirs.push_back(dir);

    std::vector<fs::path> result;
    try {
        result = unpack(archive, dir);
    } catch (...) {
        std::error_code ec;
        fs::remove_all(dir, ec);
        dirs.pop_back();
        throw;
    }

    LOG_DEBUG("clipboard files: extracted %zu items to %s", result.size(),
              path_to_utf8(dir).c_str());
    last_hash = hash;
    last_size = archive.size();
    last_result = result;
    return result;
}

std::string path_to_file_uri(const fs::path& path)
{
    std::string p = path_to_utf8(path);
#if SYSAPI_WIN32
    std::replace(p.begin(), p.end(), '\\', '/');
    if (!p.empty() && p[0] != '/') {
        p.insert(p.begin(), '/');   // file:///C:/...
    }
#endif
    static const char hex[] = "0123456789ABCDEF";
    std::string uri = "file://";
    for (char c : p) {
        unsigned char uc = static_cast<unsigned char>(c);
        if ((uc >= 'A' && uc <= 'Z') || (uc >= 'a' && uc <= 'z') ||
            (uc >= '0' && uc <= '9') || std::string("-._~/").find(c) != std::string::npos) {
            uri += c;
        } else {
            uri += '%';
            uri += hex[uc >> 4];
            uri += hex[uc & 0xf];
        }
    }
    return uri;
}

bool file_uri_to_path(const std::string& uri, fs::path* path)
{
    const std::string scheme = "file:";
    if (uri.size() < scheme.size() ||
        !std::equal(scheme.begin(), scheme.end(), uri.begin(),
                    [](char a, char b) { return a == std::tolower(static_cast<unsigned char>(b)); })) {
        return false;
    }
    std::string rest = uri.substr(scheme.size());
    if (rest.compare(0, 2, "//") == 0) {
        // authority: only local files are supported
        std::size_t slash = rest.find('/', 2);
        if (slash == std::string::npos) {
            return false;
        }
        std::string host = rest.substr(2, slash - 2);
        if (!host.empty() && host != "localhost") {
            return false;
        }
        rest = rest.substr(slash);
    }
    if (rest.empty() || rest[0] != '/') {
        return false;
    }

    std::string decoded;
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] == '%' && i + 2 < rest.size()) {
            auto hexval = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hexval(rest[i + 1]);
            int lo = hexval(rest[i + 2]);
            if (hi < 0 || lo < 0) {
                return false;
            }
            decoded += static_cast<char>((hi << 4) | lo);
            i += 2;
        } else if (rest[i] == '%') {
            return false;
        } else {
            decoded += rest[i];
        }
    }
    if (decoded.find('\0') != std::string::npos) {
        return false;
    }
#if SYSAPI_WIN32
    if (decoded.size() >= 3 && decoded[0] == '/' && decoded[2] == ':') {
        decoded.erase(0, 1);    // /C:/foo -> C:/foo
    }
#endif
    *path = utf8_to_path(decoded);
    return true;
}

std::string path_to_utf8(const fs::path& path)
{
#if SYSAPI_WIN32
    return win_wchar_to_utf8(path.native().c_str());
#else
    return path.native();
#endif
}

fs::path utf8_to_path(const std::string& str)
{
#if SYSAPI_WIN32
    std::vector<WCHAR> wide = utf8_to_win_char(str);
    return fs::path(std::wstring(wide.data()));
#else
    return fs::path(str);
#endif
}

} // namespace clipboard_files
} // namespace inputleap
