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

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>

namespace inputleap {

namespace {

class TempDir {
public:
    TempDir()
    {
        std::random_device rd;
        path_ = fs::temp_directory_path() /
                ("inputleap-clipboard-test-" + std::to_string(rd()) + std::to_string(rd()));
        fs::create_directories(path_);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

void write_file(const fs::path& path, const std::string& contents)
{
    fs::create_directories(path.parent_path());
    std::ofstream file;
    open_utf8_path(file, path, std::ios::out | std::ios::binary);
    file << contents;
}

std::string read_file(const fs::path& path)
{
    std::ifstream file;
    open_utf8_path(file, path, std::ios::in | std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void append_u32(std::string& s, std::uint32_t v)
{
    for (int shift = 24; shift >= 0; shift -= 8) {
        s += static_cast<char>((v >> shift) & 0xff);
    }
}

std::string archive_with_file(const std::string& rel, const std::string& data)
{
    std::string s = "ILF1";
    append_u32(s, 1);
    s += '\0';
    append_u32(s, static_cast<std::uint32_t>(rel.size()));
    s += rel;
    append_u32(s, 0);
    append_u32(s, static_cast<std::uint32_t>(data.size()));
    s += data;
    return s;
}

} // namespace

TEST(ClipboardFilesTests, pack_unpack_roundtrip)
{
    TempDir src;
    TempDir dst;

    std::string binary("a\0b\xff", 4);
    write_file(src.path() / "file.txt", "hello");
    write_file(src.path() / "folder" / "nested" / "data.bin", binary);
    write_file(src.path() / "folder" / "top.txt", "top");
    fs::create_directories(src.path() / "folder" / "empty");

    std::string archive = clipboard_files::pack({ src.path() / "file.txt",
                                                  src.path() / "folder" });
    ASSERT_FALSE(archive.empty());

    auto result = clipboard_files::unpack(archive, dst.path());
    ASSERT_EQ(2u, result.size());
    EXPECT_EQ(dst.path() / "file.txt", result[0]);
    EXPECT_EQ(dst.path() / "folder", result[1]);

    EXPECT_EQ("hello", read_file(dst.path() / "file.txt"));
    EXPECT_EQ(binary, read_file(dst.path() / "folder" / "nested" / "data.bin"));
    EXPECT_EQ("top", read_file(dst.path() / "folder" / "top.txt"));
    EXPECT_TRUE(fs::is_directory(dst.path() / "folder" / "empty"));
}

TEST(ClipboardFilesTests, pack_utf8_names)
{
    TempDir src;
    TempDir dst;
    fs::path name = clipboard_files::utf8_to_path("\xED\x95\x9C\xEA\xB8\x80 \xED\x8C\x8C\xEC\x9D\xBC.txt");
    write_file(src.path() / name, "x");

    auto result = clipboard_files::unpack(clipboard_files::pack({ src.path() / name }),
                                          dst.path());
    ASSERT_EQ(1u, result.size());
    EXPECT_EQ(dst.path() / name, result[0]);
    EXPECT_EQ("x", read_file(result[0]));
}

TEST(ClipboardFilesTests, pack_over_limit_returns_empty)
{
    TempDir src;
    write_file(src.path() / "big.bin", std::string(2000, 'x'));
    EXPECT_TRUE(clipboard_files::pack({ src.path() / "big.bin" }, 1000).empty());
    EXPECT_FALSE(clipboard_files::pack({ src.path() / "big.bin" }, 4000).empty());
}

TEST(ClipboardFilesTests, pack_missing_file_returns_empty)
{
    TempDir src;
    EXPECT_TRUE(clipboard_files::pack({ src.path() / "does-not-exist" }).empty());
}

TEST(ClipboardFilesTests, unpack_rejects_path_traversal)
{
    TempDir dst;
    EXPECT_THROW(clipboard_files::unpack(archive_with_file("../evil", "x"), dst.path()),
                 std::runtime_error);
    EXPECT_THROW(clipboard_files::unpack(archive_with_file("a/../../evil", "x"), dst.path()),
                 std::runtime_error);
    EXPECT_THROW(clipboard_files::unpack(archive_with_file("/etc/evil", "x"), dst.path()),
                 std::runtime_error);
    EXPECT_THROW(clipboard_files::unpack(archive_with_file("a//b", "x"), dst.path()),
                 std::runtime_error);
    EXPECT_FALSE(fs::exists(dst.path().parent_path() / "evil"));
}

TEST(ClipboardFilesTests, unpack_rejects_truncated_archive)
{
    TempDir dst;
    std::string archive = archive_with_file("file", "contents");
    archive.resize(archive.size() - 1);
    EXPECT_THROW(clipboard_files::unpack(archive, dst.path()), std::runtime_error);
    EXPECT_THROW(clipboard_files::unpack("XXXX", dst.path()), std::runtime_error);
    EXPECT_THROW(clipboard_files::unpack("", dst.path()), std::runtime_error);
}

TEST(ClipboardFilesTests, unpack_to_temp_reuses_extraction)
{
    TempDir src;
    write_file(src.path() / "a.txt", "a");
    std::string archive = clipboard_files::pack({ src.path() / "a.txt" });

    auto first = clipboard_files::unpack_to_temp(archive);
    auto second = clipboard_files::unpack_to_temp(archive);
    ASSERT_EQ(1u, first.size());
    EXPECT_EQ(first, second);
    EXPECT_EQ("a", read_file(first[0]));
}

TEST(ClipboardFilesTests, file_uri_roundtrip)
{
#if !SYSAPI_WIN32
    fs::path path("/home/user/My Files/\xED\x95\x9C 100%.txt");
    std::string uri = clipboard_files::path_to_file_uri(path);
    EXPECT_EQ("file:///home/user/My%20Files/%ED%95%9C%20100%25.txt", uri);

    fs::path decoded;
    ASSERT_TRUE(clipboard_files::file_uri_to_path(uri, &decoded));
    EXPECT_EQ(path, decoded);

    ASSERT_TRUE(clipboard_files::file_uri_to_path("file://localhost/tmp/x", &decoded));
    EXPECT_EQ(fs::path("/tmp/x"), decoded);
    ASSERT_TRUE(clipboard_files::file_uri_to_path("file:/tmp/y", &decoded));
    EXPECT_EQ(fs::path("/tmp/y"), decoded);
#endif
    fs::path unused;
    EXPECT_FALSE(clipboard_files::file_uri_to_path("http://example.com/x", &unused));
    EXPECT_FALSE(clipboard_files::file_uri_to_path("file://otherhost/x", &unused));
    EXPECT_FALSE(clipboard_files::file_uri_to_path("file:///bad%zz", &unused));
    EXPECT_FALSE(clipboard_files::file_uri_to_path("file:///nul%00", &unused));
}

} // namespace inputleap
