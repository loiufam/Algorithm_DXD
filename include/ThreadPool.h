#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <thread>
#include <future>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>
#include <memory>
#include <atomic>
#include <random>
#include <chrono>
#include <type_traits>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <climits>
#include <execution>

// ========================
// 无锁环形队列实现
// ========================
template<typename T>
class LockFreeQueue {
private:
    struct Node {
        std::atomic<T*> data{nullptr};
        std::atomic<Node*> next{nullptr};
    };
    
    std::atomic<Node*> head;
    std::atomic<Node*> tail;
    std::atomic<size_t> size_{0};
    const size_t capacity_;
    
public:
    explicit LockFreeQueue(size_t capacity = 1024) : capacity_(capacity) {
        Node* dummy = new Node;
        head.store(dummy);
        tail.store(dummy);
    }
    
    ~LockFreeQueue() {
        while (Node* old_head = head.load()) {
            head.store(old_head->next.load());
            if (old_head->data.load()) {
                delete old_head->data.load();
            }
            delete old_head;
        }
    }
    
    bool push(T item) {
        if (size_.load() >= capacity_) {
            return false; // 队列满
        }
        
        Node* new_node = new Node;
        T* data = new T(std::move(item));
        new_node->data.store(data);
        
        Node* prev_tail = tail.exchange(new_node);
        prev_tail->next.store(new_node);
        size_.fetch_add(1);
        return true;
    }
    
    bool pop(T& result) {
        Node* head_node = head.load();
        Node* next = head_node->next.load();
        
        if (next == nullptr) {
            return false; // 队列空
        }
        
        T* data = next->data.exchange(nullptr);
        if (data == nullptr) {
            return false;
        }
        
        result = std::move(*data);
        delete data;
        
        if (head.compare_exchange_weak(head_node, next)) {
            delete head_node;
            size_.fetch_sub(1);
            return true;
        }
        
        // CAS失败，回滚
        next->data.store(new T(std::move(result)));
        return false;
    }
    
    size_t size() const { return size_.load(); }
    bool empty() const { return size() == 0; }
};

// ========================
// 线程池实现
// ========================
class ThreadPool {
public:
    explicit ThreadPool(size_t max_threads = std::thread::hardware_concurrency(), 
                       size_t queue_capacity = 1024)
        : stop(false), queue(queue_capacity) {
        
        for (size_t i = 0; i < max_threads; ++i) {
            workers.emplace_back([this, i] {
                while (!stop.load()) {
                    std::function<void()> task;
                    
                    if (queue.pop(task)) {
                        if (task) {
                            try {
                                task();
                            } catch (const std::exception& e) {
                                std::cerr << "[ThreadPool Worker " << i << "] exception: " 
                                         << e.what() << std::endl;
                            } catch (...) {
                                std::cerr << "[ThreadPool Worker " << i << "] unknown exception" 
                                         << std::endl;
                            }
                        }
                    } else {
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                    }
                }
                
                // 处理剩余任务
                std::function<void()> task;
                while (queue.pop(task)) {
                    if (task) {
                        try {
                            task();
                        } catch (...) {}
                    }
                }
            });
        }
    }
    
    ~ThreadPool() {
        shutdown();
    }
    
    // 禁用拷贝和移动
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;
    
    template <class F, class... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using return_type = std::invoke_result_t<F, Args...>;
        
        if (stop.load()) {
            throw std::runtime_error("ThreadPool is shutting down");
        }
        
        auto task_ptr = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> res = task_ptr->get_future();
        
        auto wrapper = std::make_shared<std::function<void()>>([task_ptr]() {
            try {
                (*task_ptr)();
            } catch (const std::future_error& e) {
                std::cerr << "[ThreadPool] future_error: " << e.what() << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[ThreadPool] exception: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[ThreadPool] unknown exception caught" << std::endl;
            }
        });
        
        while (!queue.push([wrapper]() { (*wrapper)(); })) {
            if (stop.load()) {
                throw std::runtime_error("ThreadPool is shutting down");
            }
            std::this_thread::yield();
        }
        
        return res;
    }
    
    void shutdown() {
        stop.store(true);
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
    
    size_t queue_size() const { return queue.size(); }
    bool is_stopping() const { return stop.load(); }

private:
    std::vector<std::thread> workers;
    LockFreeQueue<std::function<void()>> queue;
    std::atomic<bool> stop;
};

#endif