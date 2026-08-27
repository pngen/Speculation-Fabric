# Speculation Fabric benchmarks (measured throughput).
add_executable(sf_bench bench.cpp)
target_link_libraries(sf_bench PRIVATE SpeculationFabric::SpeculationFabric)
target_compile_features(sf_bench PRIVATE cxx_std_20)
if(SF_CUDA_ENABLED)
    target_link_libraries(sf_bench PRIVATE CUDA::cudart)
endif()
