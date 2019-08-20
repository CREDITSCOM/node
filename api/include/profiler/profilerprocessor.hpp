#ifndef PROFILERPROCESSOR_HPP
#define PROFILERPROCESSOR_HPP

#include <API.h>

namespace cs {
class ProfilerProcessor : public ::api::APIProcessor {
public:
    explicit ProfilerProcessor(::apache::thrift::stdcxx::shared_ptr<api::APIIf> iface);
    virtual bool dispatchCall(::apache::thrift::protocol::TProtocol* iprot,
                              ::apache::thrift::protocol::TProtocol* oprot,
                              const std::string& fname, int32_t seqid, void* callContext) override;
};
}

#endif // PROFILERPROCESSOR_HPP
