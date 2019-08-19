#include <profiler/profilerprocessor.hpp>
#include <profiler/profiler.hpp>

bool cs::ProfilerProcessor::dispatchCall(apache::thrift::protocol::TProtocol* iprot,
                                         apache::thrift::protocol::TProtocol* oprot,
                                         const std::string& fname, int32_t seqid, void* callContext) {
    cs::Profiler profiler("Method " + fname);
    return ::api::APIProcessor::dispatchCall(iprot, oprot, fname, seqid, callContext);
}
