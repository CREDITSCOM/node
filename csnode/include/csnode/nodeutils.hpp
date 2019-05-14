#ifndef NODEUTILS_HPP
#define NODEUTILS_HPP

#include <csnode/nodecore.hpp>

namespace csdb {
class Pool;
}

namespace cs {
class NodeUtils {
public:
    static bool checkGroupSignature(const cs::ConfidantsKeys& confidants, const cs::Bytes& mask, const cs::Signatures& signatures, const cs::Hash& hash);
    static size_t realTrustedValue(const cs::Bytes& mask);
    static cs::Bytes getTrustedMask(const csdb::Pool& block);
    static std::string roundsToString(const std::vector<cs::RoundNumber>& rounds);
};
}  // namespace cs

#endif  // NODEUTILS_HPP
