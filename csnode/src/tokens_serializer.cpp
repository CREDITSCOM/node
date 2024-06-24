#include <fstream>
#include <sstream>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include <tokens.hpp>
#include <csnode/tokens_serializer.hpp>
#include <csnode/serializers_helper.hpp>


#include "logger.hpp"

namespace {
const std::string kDataFileName = "tokens.dat";
const std::string kLogPrefix = "TokensMaster_Serializer: ";
} // namespace

namespace cs {
void TokensMaster_Serializer::bind(TokensMaster& tokens) {
    tokens_ = reinterpret_cast<decltype(tokens_)>(&tokens.tokens_);
    holders_ = reinterpret_cast<decltype(holders_)>(&tokens.holders_);
    csdebug() << "TokensMaster bindings made";
}

void TokensMaster_Serializer::clear(const std::filesystem::path& rootDir) {
    tokens_->clear();
    holders_->clear();
    save(rootDir);
}

void TokensMaster_Serializer::save(const std::filesystem::path& rootDir) {
    std::ofstream ofs(rootDir / kDataFileName);
    boost::archive::text_oarchive oa(ofs);
    oa << *tokens_;
    oa << *holders_;
}

::cscrypto::Hash TokensMaster_Serializer::hash() {
    {
        std::ofstream ofs(kDataFileName);
        boost::archive::text_oarchive oa(
          ofs,
          boost::archive::no_header | boost::archive::no_codecvt
        );
        std::map<TokenId, Token> tmp_tokens(
          tokens_->begin(),
          tokens_->end()
        );
        std::map<HolderKey, std::set<TokenId>> tmp_holders(
          holders_->begin(),
          holders_->end()
        );
        oa << tmp_tokens;
        oa << tmp_holders;
    }
    auto result = SerializersHelper::getHashFromFile(kDataFileName);
    std::filesystem::remove(kDataFileName);
    return result;
}

void TokensMaster_Serializer::load(const std::filesystem::path& rootDir) {
    std::ifstream ifs(rootDir / kDataFileName);
    boost::archive::text_iarchive ia(ifs);
    ia >> *tokens_;
    ia >> *holders_;
}
}  // namespace cs
