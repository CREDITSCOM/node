#pragma once
#include <csnode/nodecore.hpp>
#include <lib/system/common.hpp>

namespace csdb
{
  class Pool;
}

namespace cs
{

  class NodeUtils
  {
  public:

    static bool checkGroupSignature(const cs::ConfidantsKeys& confidants, const cs::Bytes& mask, const cs::Signatures& signatures, const cs::Hash& hash);

    static size_t NodeUtils::realTrustedValue(const cs::Bytes& mask);

    static cs::Bytes NodeUtils::getTrustedMask(const csdb::Pool& block);
  };

} // cs
