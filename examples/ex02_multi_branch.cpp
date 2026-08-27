// Example 2: multi-branch speculation, deterministic single winner.
#include "examples_support.hpp"
using namespace speculation_fabric;
int main() {
    auto f = sfex::engine(5, 2, 2);
    auto r = sfex::request(2, 5, 2);
    (void)f.submit(r);
    auto c = f.run_cycle(r.id);
    auto brs = f.branches(r.id);
    std::printf("example2: outcome=%d accepted=%u branches=%zu\n",
                (int)c.value().outcome, c.value().accepted_tokens, brs.value().size());
    return (brs.value().size() >= 2 && c.value().accepted_tokens == 2) ? 0 : 1;
}