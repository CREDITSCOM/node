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

TEST(Compressor, TestCompressByte) {
    cs::Compressor compressor;
    cs::Byte byte = { 0xFF };

    auto ptr = compressor.compress(byte);
    auto entity = compressor.decompress<cs::Byte>(ptr);

    ASSERT_EQ(byte, entity);
}

TEST(SynchronizedCompressor, BaseSynchronizedCompressorUsage) {
    cs::SynchronizedCompressor compressor;

    constexpr size_t count = 100;
    const size_t threadsCount = std::thread::hardware_concurrency();
    std::mutex mutex;

    std::vector<std::size_t> data;
    data.reserve(count);

    std::map<std::thread::id, std::vector<size_t>> threadData;

    for (size_t i = 0; i < count; ++i) {
        data.push_back(i);
    }

    std::vector<std::thread> threads;

    for (size_t i = 0; i < threadsCount; ++i) {
        std::thread thread([&] {
            CompressedRegion region;
            std::vector<size_t> raw;

            {
                cs::Lock lock(mutex);
                raw = data;
            }

            region = compressor.compress(raw);
            raw = compressor.decompress<std::vector<size_t>>(region);

            {
                cs::Lock lock(mutex);
                threadData[std::this_thread::get_id()] = raw;
            }
        });

        threads.push_back(std::move(thread));
    }

    for (auto& thread : threads) {
        thread.join();
    }

    for (const auto& element : threadData) {
        ASSERT_EQ(element.second, data);
    }
}
