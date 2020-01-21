#ifndef PMRFACTORY_HPP
#define PMRFACTORY_HPP

#include <lib/system/common.hpp>
#include <lib/system/allocators.hpp>
#include <lib/system/reflection.hpp>

namespace cs {
class PmrFactory {
public:
    template<size_t size>
    static cs::PmrBytes bytes(const cs::PmrAllocator<size>& allocator) {
#ifdef __cpp_lib_memory_resource
        return cs::PmrBytes(allocator.resource());
#else
        csunused(allocator);
        return cs::Bytes{};
#endif
    }
};
}   // namespace cs

#endif  // PMRFACTORY_HPP
