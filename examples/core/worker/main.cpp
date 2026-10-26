// Example: run jobs on a background thread and use their results on the main thread.
// World streaming loads patches this way.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include "poligon/core/worker.h"

int main() {
    poligon::BackgroundWorker worker("example");
    std::vector<long long> sums;  // touched only on the main thread

    for (std::uint64_t tag = 1; tag <= 3; ++tag) {
        worker.Push(tag, [tag, &sums]() -> poligon::BackgroundWorker::Finish {
            // On the worker thread: compute without touching shared data.
            auto sum = std::make_shared<long long>(0);
            for (long long i = 1; i <= static_cast<long long>(tag) * 1000; ++i) {
                *sum += i;
            }
            // Later, on the main thread, in RunFinished.
            return [sum, &sums] { sums.push_back(*sum); };
        });
    }
    std::printf("pending after pushing: %d\n", worker.GetPendingCount());

    worker.WaitUntilIdle();
    std::printf("finished jobs run: %d\n", worker.RunFinished());
    for (const long long sum : sums) {
        std::printf("sum %lld\n", sum);
    }
    std::printf("pending at the end: %d\n", worker.GetPendingCount());
    return (sums.size() == 3 && sums[0] == 500500 && worker.GetPendingCount() == 0) ? 0 : 1;
}
