// Example 5: model-pair incompatibility is a correctness rejection.
#include "examples_support.hpp"
using namespace speculation_fabric;
int main() {
    auto f = sfex::engine(5, 1, 5);
    auto r = sfex::request(5, 5, 1);
    r.target_model = sfex::model(TokenizerId{10});
    (void)f.submit(r);
    auto c = f.run_cycle(r.id);
    std::printf("example5: outcome=%d committed=%llu\n", (int)c.value().outcome,
                (unsigned long long)f.authoritative_length(r.id).value());
    return c.value().outcome == CycleOutcomeKind::ProposalNonRetryableFailure ? 0 : 1;
}