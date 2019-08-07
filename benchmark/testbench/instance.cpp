#include "instance.hpp"

void voidTest() {
}

bool boolTest() {
    return true;
}

std::vector<int> vectorTest() {
    return std::vector<int>{};
}

cstests::TestBenchmarkInstance::TestBenchmarkInstance() {
    cs::Framework::execute(&voidTest);
    cs::Framework::execute(&boolTest);
    cs::Framework::execute(&vectorTest);
}

static cstests::TestBenchmarkInstance value{};
