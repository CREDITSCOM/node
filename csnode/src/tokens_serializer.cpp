#include <fstream>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include <tokens.hpp>
#include <csnode/tokens_serializer.hpp>

namespace cs {
void TokensMaster_Serializer::bind(TokensMaster& tokens) {
    tokens_ = reinterpret_cast<decltype(tokens_)>(&tokens.tokens_);
    holders_ = reinterpret_cast<decltype(holders_)>(&tokens.holders_);
}

void TokensMaster_Serializer::clear() {
    tokens_->clear();
    holders_->clear();
}

void TokensMaster_Serializer::save() {
    std::ofstream ofs("tokens.dat");
    boost::archive::text_oarchive oa(ofs);
    oa << *tokens_;
    oa << *holders_;
}

void TokensMaster_Serializer::load() {
    std::ifstream ifs("tokens.dat");
    boost::archive::text_iarchive ia(ifs);
    ia >> *tokens_;
    ia >> *holders_;
}
}  // namespace cs
