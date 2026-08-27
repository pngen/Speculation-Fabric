// Example 1: basic single-draft speculative decoding (full acceptance).
#include "examples_support.hpp"
using namespace speculation_fabric;
int main() {
    auto f = sfex::engine(5, 1, 5);
    auto r = sfex::request(1, 5, 1);
    (void)f.submit(r);
    auto c = f.run_cycle(r.id);
    std::printf("example1: outcome=%d accepted=%u committed=%llu\n",
                (int)c.value().outcome, c.value().accepted_tokens,
                (unsigned long long)f.authoritative_length(r.id).value());
    return c.value().accepted_tokens == 5 ? 0 : 1;
}