#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <utility>

template <typename T>
class ThreadSafeQueue
{
private:
    mutable std::mutex mtx;
    std::queue<T> data_queue;
    std::condition_variable cv;
    std::atomic<bool> shutdown_flag{false};

public:
    void push(T new_value)
    {
        std::lock_guard<std::mutex> lock(mtx);
        data_queue.push(std::move(new_value));
        cv.notify_one();
    }

    bool wait_and_pop(T &value)
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this] { return !data_queue.empty() || shutdown_flag.load(); });
        if (data_queue.empty())
            return false;
        value = std::move(data_queue.front());
        data_queue.pop();
        return true;
    }

    bool pop(T &value)
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (data_queue.empty())
            return false;
        value = std::move(data_queue.front());
        data_queue.pop();
        return true;
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mtx);
        return data_queue.empty();
    }

    void notify_all()
    {
        cv.notify_all();
    }

    void shutdown()
    {
        shutdown_flag.store(true);
        cv.notify_all();
    }
};