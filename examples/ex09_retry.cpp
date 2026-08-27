// Example 9: retry uses a NEW attempt identity and preserves committed tokens.
#include "examples_support.hpp"
using namespace speculation_fabric;
int main() {
    auto f = sfex::engine(5, 1, 0);   // aligned=0 => reject-all, then retry
    auto r = sfex::request(9, 5, 1);
    r.max_attempts = 2;
    (void)f.submit(r);
    auto c1 = f.run_cycle(r.id);
    // After a reject-all retry is permitted with a new attempt.
    auto tr = f.retry(r.id);
    std::printf("example9: first=%d retry_attempt=%llu\n", (int)c1.value().outcome,
                (unsigned long long)(tr.has_value() ? tr.value().get() : 0));
    return tr.has_value() && tr.value().get() == 2 ? 0 : 1;
}