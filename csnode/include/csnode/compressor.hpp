#ifndef COMPRESSOR_HPP
#define COMPRESSOR_HPP

#include <limits>
#include <mutex>

#include <lz4.h>

#include <datastream.hpp>

#include <lib/system/allocators.hpp>

namespace cs {
// represents universal entity's compressor
// based on lz4
class Compressor {
public:
    enum Compression : cs::Byte {
        None = 0,
        Compressed = 1
    };

    static Compression checkCompression(const cs::Byte* data, size_t size) {
        return size < 2 ? Compression::None : Compression(*(data));
    }

    static Compression checkCompression(const CompressedRegion& region) {
        return checkCompression(region.data(), region.size());
    }

    // returns compressed bytes
    // compression may failed of errors or no possibility to compress
    // bytes representation will be copied to region ptr anyway
    template<typename T>
    CompressedRegion compress(const T& entity) {
        cs::Bytes bytes;
        cs::ODataStream stream(bytes);

        stream << entity;

        auto data = reinterpret_cast<char*>(bytes.data());
        const int binSize = cs::numeric_cast<int>(bytes.size());

        const auto maxSize = LZ4_compressBound(binSize);

        cs::Bytes region(static_cast<size_t>(byteSizeof_ + maxSize));
        const int compressedSize = LZ4_compress_default(data, reinterpret_cast<char*>(region.data()) + byteSizeof_, binSize,
                                                        cs::numeric_cast<int>(region.size()) - byteSizeof_);

        CompressedRegion::SizeType size = 0;

        if (!compressedSize) {
            std::copy(data, data + binSize, reinterpret_cast<char*>(region.data()) + byteSizeof_);
            size = static_cast<uint32_t>(binSize + byteSizeof_);
        }
        else {
            size = static_cast<uint32_t>(compressedSize + byteSizeof_);
        }

        *(static_cast<cs::Byte*>(region.data())) = compressedSize ? Compression::Compressed : Compression::None;
        region.resize(size);

        return CompressedRegion { std::move(region), static_cast<size_t>(binSize) };
    }

    // try to decompress data, returns object if serializable
    template<typename T>
    T decompress(const CompressedRegion& region) {
        // region and its binary size may come from the network: reject what no compress() could produce
        if (region.size() < static_cast<size_t>(byteSizeof_)) {
            cserror() << "Decompress error of " << cstype(T) << ": empty region";
            return T{};
        }

        const auto compression = checkCompression(region.data(), region.size());

        cs::Bytes bytes;
        cs::Byte* data = nullptr;
        size_t size = 0;

        if (compression == Compression::Compressed) {
            const size_t compressedSize = region.size() - static_cast<size_t>(byteSizeof_);

            if (region.binarySize() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
                region.binarySize() > compressedSize * kLz4MaxExpansion) {
                cserror() << "Decompress error of " << cstype(T) << ": invalid binary size " << region.binarySize();
                return T{};
            }

            bytes.resize(region.binarySize());

            const int uncompressedSize = LZ4_decompress_safe(reinterpret_cast<char*>(region.data()) + byteSizeof_, reinterpret_cast<char*>(bytes.data()),
                                                             cs::numeric_cast<int>(region.size()) - byteSizeof_, cs::numeric_cast<int>(region.binarySize()));
            if (uncompressedSize < 0) {
                cserror() << "Decompress error of " << cstype(T);
                return T{};
            }

            data = bytes.data();
            size = bytes.size();
        }
        else {
            data = region.data() + byteSizeof_;
            size = region.size() - static_cast<size_t>(byteSizeof_);
        }

        cs::IDataStream stream(data, size);

        T result;
        stream >> result;

        return result;
    }

private:
    static inline int byteSizeof_ = sizeof(cs::Byte);

    // LZ4 can not expand compressed data more than 255 times
    static constexpr size_t kLz4MaxExpansion = 256;
};

// multi-threaded compressor
class SynchronizedCompressor : public Compressor {
public:
    template<typename T>
    CompressedRegion compress(const T& entity) {
        cs::Lock lock(mutex_);
        return Compressor::compress(entity);
    }

    template<typename T>
    T decompress(CompressedRegion region) {
        cs::Lock lock(mutex_);
        return Compressor::decompress<T>(region);
    }

protected:
    std::mutex mutex_;
};
}

#endif  // COMPRESSOR_HPP
