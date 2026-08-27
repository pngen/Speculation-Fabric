// Example 6: persistence archive round-trip preserves committed tokens.
#include "examples_support.hpp"
using namespace speculation_fabric;
#include "speculation_fabric/core/persistence.hpp"
int main() {
    auto f = sfex::engine(5, 1, 5);
    auto r = sfex::request(6, 5, 1);
    (void)f.submit(r);
    (void)f.run_cycle(r.id);
    auto tokens = std::vector<std::uint32_t>(f.authoritative_length(r.id).value());
    StateArchive a; a.epoch = CoordinatorEpoch{1};
    PersistedRequest pr; pr.id = r.id; pr.sequence = r.sequence;
    pr.attempt = AttemptId{1}; pr.epoch = CoordinatorEpoch{1};
    pr.auth_gen = AuthGeneration{5};
    pr.auth_state.id = StateId{100}; pr.auth_state.generation = StateGeneration{5};
    pr.committed = tokens;
    a.requests.push_back(pr);
    auto blob = serialize_archive(a);
    auto b = deserialize_archive(blob.value());
    std::printf("example6: roundtrip=%d tokens=%zu\n", (int)b.has_value(),
                b.value().requests[0].committed.size());
    return b.has_value() && b.value().requests[0].committed.size() == 5 ? 0 : 1;
}