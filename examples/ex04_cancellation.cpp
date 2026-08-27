// Example 4: speculative cancellation releases reservations and blocks work.
#include "examples_support.hpp"
using namespace speculation_fabric;
int main() {
    auto f = sfex::engine(5, 1, 5);
    auto r = sfex::request(4, 5, 1);
    (void)f.submit(r);
    (void)f.cancel(r.id, "operator cancel");
    auto c = f.run_cycle(r.id);
    std::printf("example4: outcome=%d committed=%llu\n", (int)c.value().outcome,
                (unsigned long long)f.authoritative_length(r.id).value());
    return c.value().outcome == CycleOutcomeKind::Cancelled ? 0 : 1;
}