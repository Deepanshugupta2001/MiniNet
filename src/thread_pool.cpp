#include "thread_pool.hpp"

#include <stdexcept>

ThreadPool::ThreadPool(std::size_t worker_count) {
    if (worker_count == 0) {
        throw std::invalid_argument("worker_count must be positive");
    }
    workers_.reserve(worker_count);
    for (std::size_t i = 0; i < worker_count; ++i) {
        workers_.emplace_back(&ThreadPool::worker_loop, this);
    }
}

ThreadPool::~ThreadPool() {
    shutdown(true);
}

void ThreadPool::shutdown(bool drain) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
        if (!drain) {
            std::queue<std::function<void()>> empty;
            tasks_.swap(empty);
        }
    }
    ready_.notify_all();
    for (auto& worker : workers_) {
        worker.join();
    }
}

bool ThreadPool::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            return false;
        }
        tasks_.push(std::move(task));
    }
    ready_.notify_one();
    return true;
}

void ThreadPool::worker_loop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
            if (stopping_ && tasks_.empty()) {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        task();
    }
}
