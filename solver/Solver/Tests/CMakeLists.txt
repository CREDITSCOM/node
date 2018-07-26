add_executable(Tests
        main.cpp
        testCase.cpp)

target_compile_features(Tests PRIVATE cxx_std_14)
