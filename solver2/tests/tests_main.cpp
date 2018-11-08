#include <Consensus.h>

#include <clocale>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

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
    ::testing::InitGoogleMock(&argc, argv);

    return RUN_ALL_TESTS();
}

TEST(Init, Done)
{
  EXPECT_FALSE(false);
  EXPECT_TRUE(true);

  char array[] = "0123456789ABCDEF";
  std::string s(array, sizeof(array));
  array[10] = 'a';
  const char *p = s.c_str();
}
