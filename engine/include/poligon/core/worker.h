#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace poligon {

// One background thread that runs jobs one after another, in the order they were given, and
// hands what they made back to the main thread. Built on SDL3's threads, mutexes and
// condition variables (SDL_CreateThread, SDL_Mutex, SDL_Condition).
//
//     BackgroundWorker worker("loader");
//     worker.Push(tag, [] {
//         auto data = std::make_shared<Data>(ReadFile());           // on the worker thread
//         return [data] { Use(*data); };                             // later, on the main thread
//     });
//     worker.RunFinished();   // once per frame: runs the returned functions, oldest first
//
// A job must only read what the main thread may change meanwhile, or nothing shared at all.
// What a job returns runs on the main thread, in RunFinished, in the order the jobs finished
// (which is the order they were pushed, as there is one thread).
class BackgroundWorker {
public:
    // What runs on the main thread with a job's result; may be empty.
    using Finish = std::function<void()>;
    // What runs on the worker thread.
    using Job = std::function<Finish()>;

    explicit BackgroundWorker(const char* name);
    // Waits for the running job, drops the jobs not started and the finished ones not yet run.
    ~BackgroundWorker();
    BackgroundWorker(const BackgroundWorker&) = delete;
    BackgroundWorker& operator=(const BackgroundWorker&) = delete;

    // Adds a job. `tag` names it for Cancel and IsPending.
    void Push(std::uint64_t tag, Job job);
    // Drops jobs with this tag that have not started. Returns how many.
    int Cancel(std::uint64_t tag);
    // Drops every job that has not started; returns their tags, oldest first.
    std::vector<std::uint64_t> CancelAll();
    // Whether a job with this tag waits, runs, or has finished but not been run by RunFinished.
    bool IsPending(std::uint64_t tag) const;
    // Runs up to `max_count` finished jobs' Finish functions on the calling (main) thread.
    // Returns how many ran.
    int RunFinished(int max_count = 1 << 30);
    // Blocks until no job waits or runs (finished ones stay for RunFinished).
    void WaitUntilIdle();
    // Jobs waiting, running or finished and not yet run.
    int GetPendingCount() const;
    // For tests: every job sleeps this long before it starts, to imitate a slow disk.
    void SetDelayMilliseconds(int milliseconds);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// A mutex for data shared with a BackgroundWorker's jobs (SDL_Mutex). Lock it with MutexLock.
class Mutex {
public:
    Mutex();
    ~Mutex();
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    void Lock();
    void Unlock();

private:
    void* mutex_;  // SDL_Mutex
};

// Holds a Mutex for one scope.
class MutexLock {
public:
    explicit MutexLock(Mutex& mutex) : mutex_(mutex) { mutex_.Lock(); }
    ~MutexLock() { mutex_.Unlock(); }
    MutexLock(const MutexLock&) = delete;
    MutexLock& operator=(const MutexLock&) = delete;

private:
    Mutex& mutex_;
};

} // namespace poligon
