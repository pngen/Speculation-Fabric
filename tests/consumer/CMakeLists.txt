# External downstream consumer proof for Speculation Fabric.
cmake_minimum_required(VERSION 3.24)
project(SFConsumer LANGUAGES CXX)

# This project does NOT add the SpeculationFabric source tree. It finds an
# installed package and links the exported target.
find_package(SpeculationFabric CONFIG REQUIRED)

add_executable(sf_consumer main.cpp)
target_link_libraries(sf_consumer PRIVATE SpeculationFabric::SpeculationFabric)

set_target_properties(sf_consumer PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON)
