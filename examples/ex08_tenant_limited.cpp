// Example 8: per-tenant speculative limits (fairness budget).
#include "examples_support.hpp"
using namespace speculation_fabric;
int main() {
    auto f = sfex::engine(5, 1, 5);
    auto r = sfex::request(8, 5, 1);
    r.tenant = TenantId{42};
    r.policy.tenant_max_concurrent_branches = 1;
    r.policy.max_branches = 2;   // policy allows 2 but tenant cap limits to 1
    (void)f.submit(r);
    auto c = f.run_cycle(r.id);
    std::printf("example8: outcome=%d accepted=%u tenant=%llu\n",
                (int)c.value().outcome, c.value().accepted_tokens,
                (unsigned long long)r.tenant.get());
    return c.value().accepted_tokens == 5 ? 0 : 1;
}