#include <fstream>
#include <sstream>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include <tokens.hpp>
#include <csnode/tokens_serializer.hpp>

namespace cs {
void TokensMaster_Serializer::bind(TokensMaster& tokens) {
    tokens_ = reinterpret_cast<decltype(tokens_)>(&tokens.tokens_);
    holders_ = reinterpret_cast<decltype(holders_)>(&tokens.holders_);
}

void TokensMaster_Serializer::clear(const std::filesystem::path& rootDir) {
    tokens_->clear();
    holders_->clear();
    save(rootDir);
}

void TokensMaster_Serializer::save(const std::filesystem::path& rootDir) {
    std::ofstream ofs(rootDir / "tokens.dat");
    boost::archive::text_oarchive oa(ofs);
    oa << *tokens_;
    oa << *holders_;
}

::cscrypto::Hash TokensMaster_Serializer::hash() {
    std::ostringstream ofs;
    {
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
    auto data = ofs.str();
    return ::cscrypto::calculateHash(
        (const ::cscrypto::Byte*)data.data(),
        data.size()
    );
}

void TokensMaster_Serializer::load(const std::filesystem::path& rootDir) {
    std::ifstream ifs(rootDir / "tokens.dat");
    boost::archive::text_iarchive ia(ifs);
    ia >> *tokens_;
    ia >> *holders_;
}
}  // namespace cs
