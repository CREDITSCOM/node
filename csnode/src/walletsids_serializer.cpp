#include <algorithm>
#include <fstream>
#include <sstream>

//#include <boost/archive/text_oarchive.hpp>
//#include <boost/archive/text_iarchive.hpp>

#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>

#include <csnode/walletsids.hpp>
#include <csnode/walletsids_serializer.hpp>
#include <csnode/serializers_helper.hpp>

#include "logger.hpp"

namespace {
const std::string kDataFileName = "walletsids.dat";
} // namespace

namespace cs {
void WalletsIds_Serializer::bind(WalletsIds& ids) {
    data_ = reinterpret_cast<decltype(data_)>(&ids.data_);
    nextId_ = &ids.nextId_;
    csdebug() << "WalletsIds bindings made";
}

void WalletsIds_Serializer::clear(const std::filesystem::path& rootDir) {
    data_->clear();
    *nextId_ = 0;
    save(rootDir);
}

void WalletsIds_Serializer::save(const std::filesystem::path& rootDir) {
    std::ofstream ofs(rootDir / kDataFileName, std::ios::binary);
    boost::archive::binary_oarchive oa(ofs);
    oa << *data_;
    oa << *nextId_;
}

::cscrypto::Hash WalletsIds_Serializer::hash() {
    {
        std::ofstream ofs(kDataFileName, std::ios::binary);
        boost::archive::binary_oarchive oa(
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
    auto result = SerializersHelper::getHashFromFile(kDataFileName);
    std::filesystem::remove(kDataFileName);
    return result;
}

void WalletsIds_Serializer::load(const std::filesystem::path& rootDir) {
    std::ifstream ifs(rootDir / kDataFileName, std::ios::binary);
    boost::archive::binary_iarchive ia(ifs);
    ia >> *data_;
    ia >> *nextId_;
}
}  // namespace cs
