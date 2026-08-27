// Example 10: observability - snapshot, stats, events, explain.
#include "examples_support.hpp"
using namespace speculation_fabric;
int main() {
    auto f = sfex::engine(5, 1, 5);
    auto r = sfex::request(10, 5, 1);
    (void)f.submit(r);
    (void)f.run_cycle(r.id);
    auto snap = f.snapshot();
    auto ev = f.events(r.id);
    auto ex = f.explain(r.id, "why did this commit?");
    std::printf("example10: active_requests=%zu events=%zu\nexpl=%s\n",
                snap.active_requests, ev.size(), ex.answer_text.c_str());
    return (ev.size() > 0 && snap.active_requests >= 1) ? 0 : 1;
}