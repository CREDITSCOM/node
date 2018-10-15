#include "csdb/csdb.h"

#include <clocale>

#include "leveldb/env.h"

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
#if defined(__MINGW32__)
    std::setlocale(LC_ALL, "Russian");
#elif defined(_WIN32)
    std::setlocale(LC_ALL, "ru-RU");
#else
    std::setlocale(LC_ALL, "ru_RU.UTF-8");
#endif

    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}

TEST(Init, Done)
{
  std::string path_to_tests;
  leveldb::Env *env = leveldb::Env::Default();
  EXPECT_TRUE(env->GetTestDirectory(&path_to_tests).ok());
  path_to_tests += "/csdb_unittests";
  if (!env->FileExists(path_to_tests)) {
    EXPECT_TRUE(env->CreateDir(path_to_tests).ok());
  }

  EXPECT_FALSE(::csdb::isInitialized());
  EXPECT_EQ(::csdb::lastError(), ::csdb::Storage::NoError);
  EXPECT_EQ(::csdb::dbLastError(), ::csdb::Database::NotOpen);
  EXPECT_FALSE(::csdb::lastErrorMessage().empty());
  EXPECT_FALSE(::csdb::dbLastErrorMessage().empty());

  EXPECT_FALSE(::csdb::init("/dev/null"));
  EXPECT_FALSE(::csdb::isInitialized());
  EXPECT_EQ(::csdb::lastError(), ::csdb::Storage::DatabaseError);
  EXPECT_EQ(::csdb::dbLastError(), ::csdb::Database::IOError);

  EXPECT_TRUE(::csdb::init(path_to_tests.c_str()));
  EXPECT_TRUE(::csdb::isInitialized());
  EXPECT_EQ(::csdb::lastError(), ::csdb::Storage::NoError);
  EXPECT_EQ(::csdb::dbLastError(), ::csdb::Database::NoError);

  EXPECT_FALSE(::csdb::init(path_to_tests.c_str()));
  EXPECT_TRUE(::csdb::isInitialized());
  EXPECT_EQ(::csdb::lastError(), ::csdb::Storage::NoError);
  EXPECT_EQ(::csdb::dbLastError(), ::csdb::Database::NoError);

  csdb::done();
  EXPECT_FALSE(::csdb::isInitialized());
  EXPECT_EQ(::csdb::lastError(), ::csdb::Storage::NoError);
  EXPECT_EQ(::csdb::dbLastError(), ::csdb::Database::NotOpen);
}

