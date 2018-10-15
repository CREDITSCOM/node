#include "csdb/internal/utils.h"
#include <fstream>
#include <gtest/gtest.h>

namespace
{

bool make_file(const std::string &path, size_t size = 0)
{
  std::ofstream os(path);
  if (!os.is_open()) {
    return false;
  }

  if (0 != size) {
    os << ::std::string(size, '\0');
  }

  return true;
}

}

class UtilsTest : public ::testing::Test
{
};

using namespace csdb;

TEST_F(UtilsTest, FromHex)
{
  const internal::byte_array a{0x45, 0xAB};
  EXPECT_EQ(internal::from_hex("45AB"), a);
  EXPECT_EQ(internal::from_hex("45ABXX12"), a);
}

TEST_F(UtilsTest, ToHex)
{
  const internal::byte_array a{0x45, 0xAB};
  EXPECT_EQ( internal::to_hex(a), "45AB");

  const internal::byte_array b{0x45, 0xAB, 0xDE, 0x3A};
  EXPECT_EQ(internal::to_hex(b), "45ABDE3A");
}

TEST_F(UtilsTest, PathAddSeparator)
{
#ifdef _MSC_VER
# define SEP "\\"
#else
# define SEP "/"
#endif
  EXPECT_EQ(internal::path_add_separator(""), "");
  EXPECT_EQ(internal::path_add_separator("a"), "a" SEP);
  EXPECT_EQ(internal::path_add_separator("a/"), "a/");
  EXPECT_EQ(internal::path_add_separator("a\\"), "a\\");
  EXPECT_EQ(internal::path_add_separator("a\\a/a"), "a\\a/a" SEP);
  EXPECT_EQ(internal::path_add_separator("a\\a/a/"), "a\\a/a/");
  EXPECT_EQ(internal::path_add_separator("a\\a/a\\"), "a\\a/a\\");
}

TEST_F(UtilsTest, DataDir)
{
  EXPECT_FALSE(internal::app_data_path().empty());
}

TEST_F(UtilsTest, Exists)
{
  EXPECT_TRUE(internal::dir_exists(internal::app_data_path()));
  EXPECT_FALSE(internal::dir_exists(internal::app_data_path() + "dir_not_found/"));

  const std::string filename = internal::app_data_path() + "file";
  EXPECT_TRUE(make_file(filename));
  EXPECT_FALSE(internal::dir_exists(filename));
  EXPECT_TRUE(internal::file_exists(filename));
  EXPECT_TRUE(internal::file_remove(filename));
  EXPECT_FALSE(internal::file_exists(filename));
}

TEST_F(UtilsTest, OperationDir)
{
  EXPECT_TRUE(internal::dir_make(internal::app_data_path() + "dir"));
  EXPECT_TRUE(internal::dir_exists(internal::app_data_path() + "dir"));

  EXPECT_TRUE(internal::dir_remove(internal::app_data_path() + "dir"));
  EXPECT_FALSE(internal::dir_exists(internal::app_data_path() + "dir"));

  EXPECT_FALSE(internal::dir_make(internal::app_data_path() + "dir/dir"));
  EXPECT_FALSE(internal::dir_exists(internal::app_data_path() + "dir/dir"));
}

TEST_F(UtilsTest, OperationPath)
{
  EXPECT_TRUE(internal::path_make(internal::app_data_path() + "a/b/c/"));
  EXPECT_TRUE(internal::dir_exists(internal::app_data_path() + "a/b/c/"));

  EXPECT_TRUE(internal::path_remove(internal::app_data_path() + "a/b/c/"));
  EXPECT_TRUE(internal::dir_exists(internal::app_data_path() + "a/b/"));
  EXPECT_FALSE(internal::dir_exists(internal::app_data_path() + "a/b/c/"));

  EXPECT_TRUE(make_file(internal::app_data_path() + "a/b/file"));
  EXPECT_TRUE(internal::file_exists(internal::app_data_path() + "a/b/file"));

  EXPECT_TRUE(internal::path_remove(internal::app_data_path() + "a/"));
  EXPECT_FALSE(internal::dir_exists(internal::app_data_path() + "a/"));

  EXPECT_FALSE(internal::file_exists(internal::app_data_path() + "a/b/file"));

  EXPECT_TRUE(internal::path_make(internal::app_data_path() + "a/b/c"));
  EXPECT_TRUE(internal::dir_exists(internal::app_data_path() + "a/b/c"));

  EXPECT_TRUE(internal::path_remove(internal::app_data_path() + "a/b/c"));
  EXPECT_TRUE(internal::dir_exists(internal::app_data_path() + "a/b"));
  EXPECT_FALSE(internal::dir_exists(internal::app_data_path() + "a/b/c"));

  EXPECT_TRUE(make_file(internal::app_data_path() + "a/b/file"));
  EXPECT_TRUE(internal::file_exists(internal::app_data_path() + "a/b/file"));

  EXPECT_TRUE(internal::path_remove(internal::app_data_path() + "a"));
  EXPECT_FALSE(internal::dir_exists(internal::app_data_path() + "a"));

  EXPECT_FALSE(internal::file_exists(internal::app_data_path() + "a/b/file"));
}

TEST_F(UtilsTest, FileSize)
{
  EXPECT_TRUE(internal::dir_exists(internal::app_data_path()));
  EXPECT_TRUE(internal::path_make(internal::app_data_path() + "a/b"));
  EXPECT_TRUE(internal::dir_exists(internal::app_data_path() + "a/b"));
  EXPECT_TRUE(make_file(internal::app_data_path() + "a/b/file", 100));
  EXPECT_TRUE(internal::file_exists(internal::app_data_path() + "a/b/file"));
  EXPECT_EQ(internal::file_size(internal::app_data_path() + "a/b/file"), static_cast<size_t>(100));
  EXPECT_FALSE(internal::file_exists(internal::app_data_path() + "a/b/file/"));
  EXPECT_EQ(internal::file_size(internal::app_data_path() + "a/b/file/"), static_cast<size_t>(-1));
  EXPECT_FALSE(internal::file_exists(internal::app_data_path() + "a/b"));
  EXPECT_EQ(internal::file_size(internal::app_data_path() + "a/b"), static_cast<size_t>(-1));

  EXPECT_TRUE(internal::path_remove(internal::app_data_path() + "a"));
  EXPECT_FALSE(internal::dir_exists(internal::app_data_path() + "a"));
}

TEST_F(UtilsTest, PathSize)
{
  EXPECT_TRUE(internal::dir_exists(internal::app_data_path()));
  EXPECT_TRUE(internal::path_make(internal::app_data_path() + "a/b/c"));
  EXPECT_TRUE(internal::dir_exists(internal::app_data_path() + "a/b/c"));

  EXPECT_TRUE(make_file(internal::app_data_path() + "a/b/file1", 1));
  EXPECT_TRUE(internal::file_exists(internal::app_data_path() + "a/b/file1"));
  EXPECT_EQ(internal::file_size(internal::app_data_path() + "a/b/file1"), static_cast<size_t>(1));

  EXPECT_TRUE(make_file(internal::app_data_path() + "a/b/file2", 2));
  EXPECT_TRUE(internal::file_exists(internal::app_data_path() + "a/b/file2"));
  EXPECT_EQ(internal::file_size(internal::app_data_path() + "a/b/file2"), static_cast<size_t>(2));

  EXPECT_TRUE(make_file(internal::app_data_path() + "a/b/c/file1", 4));
  EXPECT_TRUE(internal::file_exists(internal::app_data_path() + "a/b/c/file1"));
  EXPECT_EQ(internal::file_size(internal::app_data_path() + "a/b/c/file1"), static_cast<size_t>(4));

  EXPECT_TRUE(make_file(internal::app_data_path() + "a/b/c/file2", 8));
  EXPECT_TRUE(internal::file_exists(internal::app_data_path() + "a/b/c/file2"));
  EXPECT_EQ(internal::file_size(internal::app_data_path() + "a/b/c/file2"), static_cast<size_t>(8));

  EXPECT_EQ(internal::path_size(internal::app_data_path() + "a/b/c/file1"), static_cast<size_t>(4));
  EXPECT_EQ(internal::path_size(internal::app_data_path() + "a/b/c/file2"), static_cast<size_t>(8));
  EXPECT_EQ(internal::path_size(internal::app_data_path() + "a/b/c/"), static_cast<size_t>(12));
  EXPECT_EQ(internal::path_size(internal::app_data_path() + "a/b/c"), static_cast<size_t>(12));

  EXPECT_EQ(internal::path_size(internal::app_data_path() + "a/b/file1"), static_cast<size_t>(1));
  EXPECT_EQ(internal::path_size(internal::app_data_path() + "a/b/file2"), static_cast<size_t>(2));
  EXPECT_EQ(internal::path_size(internal::app_data_path() + "a/b/"), static_cast<size_t>(15));
  EXPECT_EQ(internal::path_size(internal::app_data_path() + "a/b"), static_cast<size_t>(15));

  EXPECT_TRUE(internal::path_remove(internal::app_data_path() + "a"));
  EXPECT_FALSE(internal::dir_exists(internal::app_data_path() + "a"));
}
