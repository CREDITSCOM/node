#include <csnode/apihandler_serializer.hpp>

#include <csconnector/csconnector.hpp>

namespace cs {

void APIHandler_Serializer::bind(api::APIHandler& apih) {
    smart_operations = reinterpret_cast<decltype(smart_operations)>(&apih.smart_operations);
    smarts_pending = reinterpret_cast<decltype(smarts_pending)>(&apih.smarts_pending);
    smart_origin = reinterpret_cast<decltype(smart_origin)>(&apih.smart_origin);
    deployedByCreator_ = reinterpret_cast<decltype(deployedByCreator_)>(&apih.deployedByCreator_);
    mExecuteCount_ = reinterpret_cast<decltype(mExecuteCount_)>(&apih.mExecuteCount_);
}

void APIHandler_Serializer::save(const std::filesystem::path& rootDir) {

}

void APIHandler_Serializer::load(const std::filesystem::path& rootDir) {

}

void APIHandler_Serializer::clear(const std::filesystem::path& rootDir) {

}

::cscrypto::Hash APIHandler_Serializer::hash() {

}

} // namespace cs
