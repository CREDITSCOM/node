#include <profiler/profilerprocessor.hpp>
#include <profiler/profiler.hpp>

cs::ProfilerProcessor::ProfilerProcessor(::apache::thrift::stdcxx::shared_ptr<api::APIIf> iface)
: ::api::APIProcessor(iface) {
}

bool cs::ProfilerProcessor::dispatchCall(apache::thrift::protocol::TProtocol* iprot,
                                         apache::thrift::protocol::TProtocol* oprot,
                                         const std::string& fname, int32_t seqid, void* callContext) {
    cs::Profiler profiler("Method " + fname);
    return ::api::APIProcessor::dispatchCall(iprot, oprot, fname, seqid, callContext);
}
