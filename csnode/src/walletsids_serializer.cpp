#include <algorithm>
#include <fstream>
#include <sstream>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include <csnode/walletsids.hpp>
#include <csnode/walletsids_serializer.hpp>

namespace cs {
void WalletsIds_Serializer::bind(WalletsIds& ids) {
    data_ = reinterpret_cast<decltype(data_)>(&ids.data_);
    nextId_ = &ids.nextId_;
}

void WalletsIds_Serializer::clear() {
    data_->clear();
    *nextId_ = 0;
    save();
}

void WalletsIds_Serializer::save() {
    std::ofstream ofs("walletsids.dat");
    boost::archive::text_oarchive oa(ofs);
    oa << *data_;
    oa << *nextId_;
}

::cscrypto::Hash WalletsIds_Serializer::hash() {
    std::ostringstream ofs;
    {
      boost::archive::text_oarchive oa(
        ofs,
        boost::archive::no_header | boost::archive::no_codecvt
      );
      auto& data_ref = data_->get<0>();
      std::vector<Wallet> data(
        data_ref.begin(),
        data_ref.end()
      );
      std::sort(
        data.begin(),
        data.end(),
        [](const Wallet& l, const Wallet& r) { return l.address < r.address; }
      );
      oa << data;
      oa << *nextId_;
    }
    auto data = ofs.str();
    return ::cscrypto::calculateHash(
        (const ::cscrypto::Byte*)data.data(),
        data.size()
    );
}

void WalletsIds_Serializer::load() {
    std::ifstream ifs("walletsids.dat");
    boost::archive::text_iarchive ia(ifs);
    ia >> *data_;
    ia >> *nextId_;
}
}  // namespace cs
