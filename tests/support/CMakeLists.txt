# Speculation Fabric — test support (header-only framework + shared main).
add_library(sf_test_support STATIC test_main.cpp)
target_include_directories(sf_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(sf_test_support PUBLIC cxx_std_20)
