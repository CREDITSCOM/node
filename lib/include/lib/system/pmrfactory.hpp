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
        using Allocator = cs::PmrBytes::allocator_type;
        return cs::PmrBytes(Allocator(allocator.resource()));
#else
        csunused(allocator);
        return cs::PmrBytes{};
#endif
    }
};
}   // namespace cs

#endif  // PMRFACTORY_HPP
