# Speculation Fabric — core type tests.
sf_add_test(test_error test_error.cpp)
sf_add_test(test_ids test_ids.cpp)
sf_add_test(test_compat test_compat.cpp)
sf_add_test(test_state test_state.cpp)
sf_add_test(test_lifecycle test_lifecycle.cpp)
sf_add_test(test_cpu_executors test_cpu_executors.cpp)
sf_add_test(test_fabric test_fabric.cpp)
if(SF_CUDA_ENABLED)
    sf_add_test(test_cuda test_cuda.cpp)
endif()
sf_add_test(test_persistence test_persistence.cpp)
sf_add_test(test_wire test_wire.cpp)
sf_add_test(test_property test_property.cpp)
sf_add_test(test_adversarial test_adversarial.cpp)

sf_add_test(test_concurrency test_concurrency.cpp)

sf_add_test(test_recovery test_recovery.cpp)
