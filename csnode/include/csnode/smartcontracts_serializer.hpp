#ifndef SMARTCONTRACTS_SERIALIZER_HPP
#define SMARTCONTRACTS_SERIALIZER_HPP
#include <filesystem>

#include <boost/serialization/serialization.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/list.hpp>
#include <boost/serialization/unordered_set.hpp>
#include <boost/serialization/split_member.hpp>

#include <csnode/transactionspacket.hpp>
#include <cscrypto/cscrypto.hpp>
#include <csdb/transaction.hpp>
#include <lib/system/common.hpp>

#include "address_serializer.hpp"

class SmartContracts;
class SmartConsensus;
//class csdb::Transaction;

namespace cs {


class SmartContracts_Serializer {
public:
    void bind(SmartContracts&);
    void save(const std::filesystem::path& rootDir);
    void load(const std::filesystem::path& rootDir);
    void clear(const std::filesystem::path& rootDir);
    ::cscrypto::Hash hash();



private:
    SmartContracts* contracts_;
};
}  // namespace cs
#endif // SMARTCONTRACTS_SERIALIZER_HPP
