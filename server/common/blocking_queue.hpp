#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <optional>

namespace dniip::server {

// Thread-safe MPMC queue used as the telemetry processing pipeline between
// the epoll I/O thread(s) (producers) and the correlation/incident worker
// pool (consumers).
template <typename T>
class BlockingQueue {
public:
    void push(T item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(item));
        }
        cv_.notify_one();
    }

    // Blocks until an item is available or shutdown() is called.
    // Returns std::nullopt only after shutdown with an empty queue.
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !queue_.empty() || shutting_down_; });
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutting_down_ = true;
        }
        cv_.notify_all();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<T> queue_;
    bool shutting_down_ = false;
};

} // namespace dniip::server
