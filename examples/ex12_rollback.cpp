// Example 12: explicit rollback releases leaves no authoritative residue.
#include "examples_support.hpp"
using namespace speculation_fabric;
int main() {
    auto f = sfex::engine(6, 1, 3);   // aligned=3 of depth 6 => partial
    auto r = sfex::request(12, 6, 1);
    (void)f.submit(r);
    auto c = f.run_cycle(r.id);
    auto rb = f.rollback(r.id, "operator rollback");
    std::printf("example12: committed=%llu rollback_ok=%d released=%u\n",
                (unsigned long long)f.authoritative_length(r.id).value(),
                (int)(rb.has_value() && rb.value().success),
                rb.has_value() ? rb.value().released_reservations : 0);
    // Authoritative length never decreases: it reflects the committed prefix.
    return (rb.has_value() && rb.value().success) ? 0 : 1;
}