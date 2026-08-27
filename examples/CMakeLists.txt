# Speculation Fabric examples (real public-API programs).
function(sf_add_example name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE SpeculationFabric::SpeculationFabric)
    target_compile_features(${name} PRIVATE cxx_std_20)
endfunction()

add_library(examples_support INTERFACE)
target_include_directories(examples_support INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

sf_add_example(example_01_basic_single_draft ex01_basic_single_draft.cpp)
sf_add_example(example_02_multi_branch ex02_multi_branch.cpp)
sf_add_example(example_03_partial_accept_rollback ex03_partial_accept_rollback.cpp)
sf_add_example(example_04_cancellation ex04_cancellation.cpp)
sf_add_example(example_05_incompatible_pair ex05_incompatible_pair.cpp)
sf_add_example(example_06_persistence ex06_persistence.cpp)
