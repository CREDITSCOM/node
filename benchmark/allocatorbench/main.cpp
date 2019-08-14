#include <framework.hpp>
#include <vector>

#ifdef __cpp_lib_memory_resource
#include <memory_resource>
#endif

class A {
public:
    char buffer[100] = {};
};

static constexpr size_t allocationsCount = 1000000;
static std::vector<A*> storage;

// default allocation
static void defaultAllocation() {
    for (size_t i = 0; i < allocationsCount; ++i) {
        storage.push_back(new A{});
    }

    for (A* a : storage) {
        delete a;
    }
}

static void testDefaultAllocation() {
    cs::Framework::execute(&defaultAllocation);
    storage.clear();
}

#ifdef __cpp_lib_memory_resource
static char resourceBuffer[sizeof(A) * allocationsCount];
static std::pmr::monotonic_buffer_resource resource{ resourceBuffer, sizeof(resourceBuffer), std::pmr::new_delete_resource() };

// memory source
static void memorySourceAllocation() {
    for (size_t i = 0; i < allocationsCount; ++i) {
        storage.push_back(reinterpret_cast<A*>(resource.allocate(sizeof(A))));
    }

    for (A* a : storage) {
        resource.deallocate(a, sizeof(A));
    }
}

static void testMemorySourceAllocation() {
    cs::Framework::execute(&memorySourceAllocation);
    storage.clear();
}
#endif

int main() {
    storage.resize(allocationsCount);

    testDefaultAllocation();
#ifdef __cpp_lib_memory_resource
    testMemorySourceAllocation();
#endif

    return 0;
}
