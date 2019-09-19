#define TESTING

#include <gtest/gtest.h>

#include <compressor.hpp>

#include <lib/system/utils.hpp>
#include <lib/system/console.hpp>

#include <cscrypto/cscrypto.hpp>

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

TEST(Compressor, TestCompressBase58) {
    cs::Compressor compressor;
    cs::PublicKey key{};
    std::initializer_list<char> list{ '4', 't', 'E', 'Q', 'b', 'Q', 'P', 'Y', 'Z', 'q', '1', 'b', 'Z', '8', 'T', 'n', '9', 'D', 'p', 'C', 'X', 'Y',
                                      'U', 'g', 'P', 'g', 'E', 'g', 'c', 'q', 's', 'B', 'P', 'X', 'X', '4', 'f', 'X', 'e', 'f', '7', 'F', 'u', 'L' };

    std::string base58 { list };
    std::vector<unsigned char> decoded;

    if (!DecodeBase58(base58, decoded)) {
        cs::Console::writeLine("Can not decode base58 str");
    }

    std::copy(std::begin(decoded), std::end(decoded), std::begin(key));

    auto ptr = compressor.compress(key);
    auto entity = compressor.decompress<cs::PublicKey>(ptr);

    ASSERT_EQ(key, entity);
}
