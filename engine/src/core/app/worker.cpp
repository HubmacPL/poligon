#include "poligon/core/worker.h"

#include <deque>
#include <string>
#include <utility>

#include <SDL3/SDL.h>

#include "poligon/core/fatal_error.h"

namespace poligon {

namespace {

struct TaggedJob {
    std::uint64_t tag = 0;
    BackgroundWorker::Job job;
};

struct TaggedFinish {
    std::uint64_t tag = 0;
    BackgroundWorker::Finish finish;
};

// Holds the mutex for one scope.
class Lock {
public:
    explicit Lock(SDL_Mutex* mutex) : mutex_(mutex) { SDL_LockMutex(mutex_); }
    ~Lock() { SDL_UnlockMutex(mutex_); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

private:
    SDL_Mutex* mutex_;
};

} // namespace

struct BackgroundWorker::Impl {
    SDL_Thread* thread = nullptr;
    SDL_Mutex* mutex = nullptr;
    SDL_Condition* work_ready = nullptr;  // a job was pushed, or the worker must stop
    SDL_Condition* idle = nullptr;        // the worker has nothing to do
    std::deque<TaggedJob> waiting;
    std::deque<TaggedFinish> finished;
    bool running_job = false;
    std::uint64_t running_tag = 0;
    bool stopping = false;
    int delay_milliseconds = 0;

    static int Run(void* data) {
        Impl& impl = *static_cast<Impl*>(data);
        for (;;) {
            TaggedJob next;
            int delay = 0;
            {
                Lock lock(impl.mutex);
                while (impl.waiting.empty() && !impl.stopping) {
                    SDL_SignalCondition(impl.idle);
                    SDL_WaitCondition(impl.work_ready, impl.mutex);
                }
                if (impl.stopping) {
                    return 0;
                }
                next = std::move(impl.waiting.front());
                impl.waiting.pop_front();
                impl.running_job = true;
                impl.running_tag = next.tag;
                delay = impl.delay_milliseconds;
            }
            if (delay > 0) {
                SDL_Delay(static_cast<Uint32>(delay));
            }
            Finish finish = next.job();
            Lock lock(impl.mutex);
            impl.finished.push_back({next.tag, std::move(finish)});
            impl.running_job = false;
            if (impl.waiting.empty()) {
                SDL_SignalCondition(impl.idle);
            }
        }
    }
};

BackgroundWorker::BackgroundWorker(const char* name) : impl_(std::make_unique<Impl>()) {
    impl_->mutex = SDL_CreateMutex();
    impl_->work_ready = SDL_CreateCondition();
    impl_->idle = SDL_CreateCondition();
    if (impl_->mutex == nullptr || impl_->work_ready == nullptr || impl_->idle == nullptr) {
        FatalError((std::string("BackgroundWorker: cannot create a mutex or condition: ") + SDL_GetError()).c_str());
    }
    impl_->thread = SDL_CreateThread(&Impl::Run, name, impl_.get());
    if (impl_->thread == nullptr) {
        FatalError((std::string("BackgroundWorker: cannot start a thread: ") + SDL_GetError()).c_str());
    }
}

BackgroundWorker::~BackgroundWorker() {
    {
        Lock lock(impl_->mutex);
        impl_->stopping = true;
        impl_->waiting.clear();
        SDL_SignalCondition(impl_->work_ready);
    }
    SDL_WaitThread(impl_->thread, nullptr);
    SDL_DestroyCondition(impl_->idle);
    SDL_DestroyCondition(impl_->work_ready);
    SDL_DestroyMutex(impl_->mutex);
}

void BackgroundWorker::Push(std::uint64_t tag, Job job) {
    Lock lock(impl_->mutex);
    impl_->waiting.push_back({tag, std::move(job)});
    SDL_SignalCondition(impl_->work_ready);
}

int BackgroundWorker::Cancel(std::uint64_t tag) {
    Lock lock(impl_->mutex);
    int count = 0;
    for (auto it = impl_->waiting.begin(); it != impl_->waiting.end();) {
        if (it->tag == tag) {
            it = impl_->waiting.erase(it);
            ++count;
        } else {
            ++it;
        }
    }
    return count;
}

std::vector<std::uint64_t> BackgroundWorker::CancelAll() {
    Lock lock(impl_->mutex);
    std::vector<std::uint64_t> tags;
    tags.reserve(impl_->waiting.size());
    for (const TaggedJob& job : impl_->waiting) {
        tags.push_back(job.tag);
    }
    impl_->waiting.clear();
    return tags;
}

bool BackgroundWorker::IsPending(std::uint64_t tag) const {
    Lock lock(impl_->mutex);
    if (impl_->running_job && impl_->running_tag == tag) {
        return true;
    }
    for (const TaggedJob& job : impl_->waiting) {
        if (job.tag == tag) {
            return true;
        }
    }
    for (const TaggedFinish& done : impl_->finished) {
        if (done.tag == tag) {
            return true;
        }
    }
    return false;
}

int BackgroundWorker::RunFinished(int max_count) {
    int count = 0;
    while (count < max_count) {
        TaggedFinish done;
        {
            Lock lock(impl_->mutex);
            if (impl_->finished.empty()) {
                break;
            }
            done = std::move(impl_->finished.front());
            impl_->finished.pop_front();
        }
        if (done.finish) {
            done.finish();
        }
        ++count;
    }
    return count;
}

void BackgroundWorker::WaitUntilIdle() {
    Lock lock(impl_->mutex);
    while (!impl_->waiting.empty() || impl_->running_job) {
        SDL_WaitCondition(impl_->idle, impl_->mutex);
    }
}

int BackgroundWorker::GetPendingCount() const {
    Lock lock(impl_->mutex);
    return static_cast<int>(impl_->waiting.size() + impl_->finished.size()) + (impl_->running_job ? 1 : 0);
}

void BackgroundWorker::SetDelayMilliseconds(int milliseconds) {
    Lock lock(impl_->mutex);
    impl_->delay_milliseconds = milliseconds;
}

Mutex::Mutex() : mutex_(SDL_CreateMutex()) {
    if (mutex_ == nullptr) {
        FatalError((std::string("Mutex: cannot create a mutex: ") + SDL_GetError()).c_str());
    }
}

Mutex::~Mutex() {
    SDL_DestroyMutex(static_cast<SDL_Mutex*>(mutex_));
}

void Mutex::Lock() {
    SDL_LockMutex(static_cast<SDL_Mutex*>(mutex_));
}

void Mutex::Unlock() {
    SDL_UnlockMutex(static_cast<SDL_Mutex*>(mutex_));
}

} // namespace poligon
