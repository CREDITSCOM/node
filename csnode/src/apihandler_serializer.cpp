#include <csnode/apihandler_serializer.hpp>

#include <fstream>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include <csconnector/csconnector.hpp>
#include <csnode/serializers_helper.hpp>

namespace {
const std::string kDataFileName = "apihandler.dat";
} // namespace

namespace cs {

void APIHandler_Serializer::bind(api::APIHandler& apih) {
    smart_operations = reinterpret_cast<decltype(smart_operations)>(&apih.smart_operations);
    smarts_pending = reinterpret_cast<decltype(smarts_pending)>(&apih.smarts_pending);
    smart_origin = reinterpret_cast<decltype(smart_origin)>(&apih.smart_origin);
    deployedByCreator_ = reinterpret_cast<decltype(deployedByCreator_)>(&apih.deployedByCreator_);
    mExecuteCount_ = reinterpret_cast<decltype(mExecuteCount_)>(&apih.mExecuteCount_);
}

void APIHandler_Serializer::save(const std::filesystem::path& rootDir) {
  std::ofstream ofs(rootDir / kDataFileName);
  boost::archive::text_oarchive oa(ofs);

  auto saveHelper = [&](auto& entity) {
    auto ref = lockedReference(entity);
    oa << *ref;
  };

  saveHelper(*smart_operations);
  saveHelper(*smarts_pending);
  saveHelper(*smart_origin);
  saveHelper(*deployedByCreator_);

  oa << *mExecuteCount_;
}

void APIHandler_Serializer::load(const std::filesystem::path& rootDir) {
  std::ifstream ifs(rootDir / kDataFileName);
  boost::archive::text_iarchive ia(ifs);

  auto loadHelper = [&](auto& entity) {
    auto ref = lockedReference(entity);
    ia >> *ref;
  };

  loadHelper(*smart_operations);
  loadHelper(*smarts_pending);
  loadHelper(*smart_origin);
  loadHelper(*deployedByCreator_);

  ia >> *mExecuteCount_;
}

void APIHandler_Serializer::clear(const std::filesystem::path& rootDir) {
  auto clearHelper = [this](auto& entity) {
    auto ref = lockedReference(entity);
    ref->clear();
  };

  clearHelper(*smart_operations);
  clearHelper(*smarts_pending);
  clearHelper(*smart_origin);
  clearHelper(*deployedByCreator_);

  mExecuteCount_->clear();

  save(rootDir);
}

::cscrypto::Hash APIHandler_Serializer::hash() {
  save(".");
  auto result = SerializersHelper::getHashFromFile(kDataFileName);
  std::filesystem::remove(kDataFileName);
  return result;
}

} // namespace cs
