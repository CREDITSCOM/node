#define TESTING

#include <gtest/gtest.h>

#include <compressor.hpp>

TEST(Compressor, BaseUsage) {
    cs::Compressor compressor;

    constexpr size_t count = 100;
    std::vector<size_t> data;

    for (size_t i = 0; i < count; ++i) {
        data.push_back(i);
    }

    auto ptr = compressor.compress(data);
    auto entity = compressor.decompress<std::vector<size_t>>(ptr);

    ASSERT_EQ(data, entity);
}
