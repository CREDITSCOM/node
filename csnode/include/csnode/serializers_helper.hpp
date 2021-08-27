#pragma once

#include <fstream>

#include <cscrypto/cscrypto.hpp>

namespace cs {

class SerializersHelper {
public:
  static ::cscrypto::Hash getHashFromFile(const std::string& fileName) {
      static const size_t kBufSize = 1 << 27;
      std::vector<uint8_t> buf(::cscrypto::kHashSize + kBufSize, 0);
      {
        std::ifstream ifs(fileName);
        while (!ifs.eof()) {
            ifs.read(
              reinterpret_cast<char*>(buf.data() + ::cscrypto::kHashSize),
              kBufSize
            );
            auto hash = ::cscrypto::calculateHash(buf.data(), buf.size());
            std::copy(hash.begin(), hash.end(), buf.begin());
        }
      }

      ::cscrypto::Hash result;
      std::copy(
        buf.begin(),
        std::next(buf.begin(), ::cscrypto::kHashSize),
        result.begin()
      );
      return result;
  }
};

} // namespace cs
