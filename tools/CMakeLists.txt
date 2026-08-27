# Speculation Fabric command-line tools + distributed control plane.
add_executable(sf sf.cpp)
target_link_libraries(sf PRIVATE SpeculationFabric::SpeculationFabric)
target_compile_features(sf PRIVATE cxx_std_20)
if(SF_CUDA_ENABLED)
    target_link_libraries(sf PRIVATE CUDA::cudart)
endif()

# Distributed control plane: real worker OS processes.
add_executable(sf_worker sf_worker.cpp)
target_link_libraries(sf_worker PRIVATE SpeculationFabric::SpeculationFabric ws2_32)
target_compile_features(sf_worker PRIVATE cxx_std_20)

add_executable(sf_coordinator sf_coordinator.cpp)
target_link_libraries(sf_coordinator PRIVATE SpeculationFabric::SpeculationFabric ws2_32)
target_compile_features(sf_coordinator PRIVATE cxx_std_20)

add_executable(sf_driver sf_driver.cpp)
target_link_libraries(sf_driver PRIVATE SpeculationFabric::SpeculationFabric ws2_32)
target_compile_features(sf_driver PRIVATE cxx_std_20)
