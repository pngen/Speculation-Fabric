// Example 3: partial-prefix acceptance with rejected-suffix rollback.
#include "examples_support.hpp"
using namespace speculation_fabric;
int main() {
    auto f = sfex::engine(6, 1, 3);
    auto r = sfex::request(3, 6, 1);
    (void)f.submit(r);
    auto c = f.run_cycle(r.id);
    std::printf("example3: accepted=%u rejected=%u committed=%llu rolled_back=%d\n",
                c.value().accepted_tokens, c.value().rejected_tokens,
                (unsigned long long)f.authoritative_length(r.id).value(),
                (int)c.value().rollback.success);
    return (c.value().accepted_tokens == 3 && c.value().rejected_tokens == 3) ? 0 : 1;
}