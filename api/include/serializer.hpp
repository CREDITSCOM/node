#ifndef SERIALIZER_HPP
#define SERIALIZER_HPP

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TBufferTransports.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <string>

namespace cs {
class Serializer {
public:
    template <typename T>
    static T deserialize(std::string&& s) {
        using namespace ::apache;

        // https://stackoverflow.com/a/16261758/2016154
        static_assert(CHAR_BIT == 8 && std::is_same<std::uint8_t, unsigned char>::value, "This code requires std::uint8_t to be implemented as unsigned char.");

        const auto buffer = thrift::stdcxx::make_shared<thrift::transport::TMemoryBuffer>(reinterpret_cast<uint8_t*>(&(s[0])), static_cast<uint32_t>(s.size()));
        thrift::protocol::TBinaryProtocol proto(buffer);

        T sc;
        sc.read(&proto);

        return sc;
    }

    template <typename T>
    static std::string serialize(const T& sc) {
        using namespace ::apache;

        auto buffer = thrift::stdcxx::make_shared<thrift::transport::TMemoryBuffer>();
        thrift::protocol::TBinaryProtocol proto(buffer);
        sc.write(&proto);

        return buffer->getBufferAsString();
    }
};
}  // namespace cs::api

#endif // SERIALIZER_HPP
