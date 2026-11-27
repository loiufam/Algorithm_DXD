#ifndef PARALLEL_TASK_QUEUE_H
#define PARALLEL_TASK_QUEUE_H

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

// Shared bounded MPMC queue used by HybridDXD and DXD.  The submitting thread
// also participates in execution, so nested search groups cannot deadlock:
// every waiting caller can help drain tasks from any active group.
template <typename Task>
class ParallelTaskQueue {
public:
    using Executor = std::function<void(Task&)>;

    ParallelTaskQueue(int threadCount, Executor executor)
        : executor_(std::move(executor)),
          capacity_(nextPowerOfTwo(size_t{1} << 14)),
          mask_(capacity_ - 1),
          buffer_(std::make_unique<Cell[]>(capacity_)) {
        for (size_t i = 0; i < capacity_; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
        const int workers = std::max(0, threadCount - 1);
        workers_.reserve(static_cast<size_t>(workers));
        for (int i = 0; i < workers; ++i) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    }

    ~ParallelTaskQueue() {
        stop_.store(true, std::memory_order_release);
        workCondition_.notify_all();
        for (std::thread& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
    }

    ParallelTaskQueue(const ParallelTaskQueue&) = delete;
    ParallelTaskQueue& operator=(const ParallelTaskQueue&) = delete;

    void enqueue(Task task) {
        size_t position = tail_.load(std::memory_order_relaxed);
        for (;;) {
            Cell& cell = buffer_[position & mask_];
            const size_t sequence =
                cell.sequence.load(std::memory_order_acquire);
            const intptr_t difference =
                static_cast<intptr_t>(sequence) -
                static_cast<intptr_t>(position);
            if (difference == 0) {
                if (tail_.compare_exchange_weak(
                        position, position + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    cell.task = std::move(task);
                    cell.sequence.store(
                        position + 1, std::memory_order_release);
                    workCondition_.notify_one();
                    return;
                }
            } else if (difference < 0) {
                std::this_thread::yield();
                position = tail_.load(std::memory_order_relaxed);
            } else {
                position = tail_.load(std::memory_order_relaxed);
            }
        }
    }

    bool tryPop(Task& task) {
        size_t position = head_.load(std::memory_order_relaxed);
        for (;;) {
            Cell& cell = buffer_[position & mask_];
            const size_t sequence =
                cell.sequence.load(std::memory_order_acquire);
            const intptr_t difference =
                static_cast<intptr_t>(sequence) -
                static_cast<intptr_t>(position + 1);
            if (difference == 0) {
                if (head_.compare_exchange_weak(
                        position, position + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    task = std::move(cell.task);
                    cell.sequence.store(
                        position + capacity_,
                        std::memory_order_release);
                    return true;
                }
            } else if (difference < 0) {
                return false;
            } else {
                position = head_.load(std::memory_order_relaxed);
            }
        }
    }

private:
    struct Cell {
        std::atomic<size_t> sequence{0};
        Task task;
    };

    static size_t nextPowerOfTwo(size_t value) {
        size_t result = 1;
        while (result < value) result <<= 1;
        return result;
    }

    void workerLoop() {
        while (!stop_.load(std::memory_order_acquire)) {
            Task task;
            if (tryPop(task)) {
                executor_(task);
                continue;
            }
            std::unique_lock<std::mutex> lock(waitMutex_);
            workCondition_.wait_for(
                lock, std::chrono::milliseconds(2), [this] {
                    return stop_.load(std::memory_order_acquire);
                });
        }
    }

    Executor executor_;
    const size_t capacity_;
    const size_t mask_;
    std::unique_ptr<Cell[]> buffer_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    std::atomic<bool> stop_{false};
    std::mutex waitMutex_;
    std::condition_variable workCondition_;
    std::vector<std::thread> workers_;
};

#endif
