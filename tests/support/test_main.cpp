// Speculation Fabric — test framework entry point.
#include "sf_test.hpp"

int main() {
    const int rc = sf_test::run_all();
    return rc;
}
