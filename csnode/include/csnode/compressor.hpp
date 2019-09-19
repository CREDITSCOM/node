#ifndef COMPRESSOR_HPP
#define COMPRESSOR_HPP

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
        if (size < 2) {
            return Compression::None;
        }

        return Compression(*(data));
    }

    // returns compressed bytes
    // compression may failed of errors or no possibility to compress
    // bytes representation will be copied to region ptr anyway
    template<typename T>
    CompressedRegion compress(const T& entity) {
        cs::Bytes bytes;
        cs::DataStream stream(bytes);

        stream << entity;

        auto data = reinterpret_cast<char*>(bytes.data());
        const int binSize = cs::numeric_cast<int>(bytes.size());

        const auto maxSize = LZ4_compressBound(binSize);

        auto region = allocator_.allocateNext(static_cast<uint32_t>(byteSizeof_ + maxSize));
        const int compressedSize = LZ4_compress_default(data, static_cast<char*>(region->data()) + byteSizeof_, binSize, cs::numeric_cast<int>(region->size()) - byteSizeof_);

        if (!compressedSize) {
            std::copy(data, data + binSize, static_cast<char*>(region->data()) + byteSizeof_);
            region->setSize(static_cast<uint32_t>(binSize + byteSizeof_));
        }
        else {
            region->setSize(static_cast<uint32_t>(compressedSize + byteSizeof_));
        }

        *(static_cast<cs::Byte*>(region->data())) = compressedSize ? Compression::Compressed : Compression::None;

        return CompressedRegion { region, static_cast<size_t>(binSize) };
    }

    // try to decompress data, returns object if serializable
    template<typename T>
    T decompress(CompressedRegion region) {
        const auto compression = checkCompression(region.data(), region.size());

        cs::Bytes bytes;
        cs::Byte* data = nullptr;
        size_t size = 0;

        if (compression == Compression::Compressed) {
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

        cs::DataStream stream(data, size);

        T result;
        stream >> result;

        return result;
    }

private:
    RegionAllocator allocator_;
    static inline int byteSizeof_ = sizeof(cs::Byte);
};
}

#endif  // COMPRESSOR_HPP
