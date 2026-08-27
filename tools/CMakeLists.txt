# Speculation Fabric command-line tools.
add_executable(sf sf.cpp)
target_link_libraries(sf PRIVATE SpeculationFabric::SpeculationFabric)
target_compile_features(sf PRIVATE cxx_std_20)
if(SF_CUDA_ENABLED)
    target_link_libraries(sf PRIVATE CUDA::cudart)
endif()
