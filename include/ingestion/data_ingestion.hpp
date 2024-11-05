// include/ingestion/data_ingestion.hpp

#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <cstddef>

// Include memory pool
#include "memory_pool.hpp"

// Lock-Free Queue Implementation using std::shared_ptr
template <typename T>
class LockFreeQueue
{
public:
    LockFreeQueue()
        : head_(new Node()), tail_(head_.load(std::memory_order_relaxed))
    {
    }

    ~LockFreeQueue()
    {
        while (Node* old_head = head_.load(std::memory_order_relaxed))
        {
            head_.store(old_head->next, std::memory_order_relaxed);
            delete old_head;
        }
    }

    void enqueue(std::shared_ptr<T> value)
    {
        Node* new_node = new Node(value);
        Node* prev_tail = tail_.exchange(new_node, std::memory_order_acq_rel);
        prev_tail->next.store(new_node, std::memory_order_release);
    }

    bool dequeue(std::shared_ptr<T>& result)
    {
        Node* old_head = head_.load(std::memory_order_acquire);
        Node* next = old_head->next.load(std::memory_order_acquire);
        if (next == nullptr)
        {
            return false;
        }
        result = next->data;
        head_.store(next, std::memory_order_release);
        delete old_head;
        return true;
    }

private:
    struct Node
    {
        std::shared_ptr<T> data;
        std::atomic<Node*> next;

        Node()
            : data(nullptr), next(nullptr)
        {
        }

        Node(std::shared_ptr<T> val)
            : data(val), next(nullptr)
        {
        }
    };

    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;
};

// DataRecord Structure
struct alignas(64) DataRecord
{
    uint64_t timestamp;
    std::string message;
};

// DataIngestion Class
class DataIngestion
{
public:
    // Modified constructor to accept multiple ingestion_thread_cores
    DataIngestion(const std::string& ip, int port, const std::vector<int>& ingestion_thread_cores = {2});
    ~DataIngestion();

    void start();
    void stop();

    bool get_data(std::shared_ptr<DataRecord>& record);

private:
    void ingest(int cpu_core);

    std::string ip_;
    int port_;
    std::vector<int> ingestion_thread_cores_; // Store multiple cores
    std::atomic<bool> running_;
    std::vector<std::thread> ingest_threads_;

    // Lock-Free Queue for storing data
    LockFreeQueue<DataRecord> data_queue_;

    // Lock-Free Memory Pool for DataRecord objects
    LockFreeMemoryPool<DataRecord> memory_pool_;

    // epoll file descriptor
    int epoll_fd_;

    // Socket file descriptor
    int sock_fd_;

    // Buffer size
    static const int BUFFER_SIZE = 4096;

    // Preallocated buffer
    char buffer_[BUFFER_SIZE] __attribute__((aligned(64)));
};
