#ifndef TOKENS_SERIALIZER_HPP
#define TOKENS_SERIALIZER_HPP
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/set.hpp>

#include "address_serializer.hpp"

class TokensMaster;

namespace cs {
class TokensMaster_Serializer {
public:
    void bind(TokensMaster&);
    void save();
    void load();
    void clear();

private:
    using HolderKey = csdb::Address;

    class Token {
        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive &ar, [[maybe_unused]] const unsigned int version) {
            ar & tokenStandard;
            ar & owner;
            ar & transactionsCount;
            ar & transfersCount;
            ar & realHoldersCount;
            ar & name;
            ar & symbol;
            ar & totalSupply;
            ar & holders;
        }

        int64_t tokenStandard;
        csdb::Address owner;
        uint64_t transactionsCount;
        uint64_t transfersCount;
        uint64_t realHoldersCount;
        std::string name;
        std::string symbol;
        std::string totalSupply;

        class HolderInfo {
            friend class boost::serialization::access;
            template<class Archive>
            void serialize(Archive &ar, [[maybe_unused]] const unsigned int version) {
                ar & balance;
                ar & transfersCount;
            }

            std::string balance;
            uint64_t transfersCount;
        };

        std::map<HolderKey, HolderInfo> holders;
    };

    using TokenId = csdb::Address;

    using TokensMap = std::unordered_map<TokenId, Token>;
    using HoldersMap = std::unordered_map<HolderKey, std::set<TokenId>>;

    TokensMap *tokens_;
    HoldersMap *holders_;
};
}  // namespace cs
#endif // TOKENS_SERIALIZER_HPP
