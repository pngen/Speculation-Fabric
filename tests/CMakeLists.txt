# Speculation Fabric — test suite.

function(sf_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE SpeculationFabric::SpeculationFabric sf_test_support)
    target_compile_features(${name} PRIVATE cxx_std_20)
    if(SF_CUDA_ENABLED)
        target_link_libraries(${name} PRIVATE CUDA::cudart)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 1200 ENVIRONMENT "SF_TEST=1")
endfunction()

add_subdirectory(support)
add_subdirectory(unit)
add_subdirectory(core)
