// include/ingestion/memory_pool.hpp

#pragma once

#include <memory>
#include <atomic>

// Lock-Free Memory Pool Implementation
template <typename T>
class LockFreeMemoryPool
{
public:
    LockFreeMemoryPool(size_t pool_size = 10000)
    {
        expand_pool(pool_size);
    }

    std::shared_ptr<T> acquire()
    {
        Node* old_head = head_.load(std::memory_order_acquire);
        while (old_head != nullptr)
        {
            Node* next = old_head->next.load(std::memory_order_acquire);
            if (head_.compare_exchange_weak(old_head, next, std::memory_order_release, std::memory_order_relaxed))
            {
                return old_head->data;
            }
        }
        // Pool exhausted, expand
        expand_pool(pool_size_);
        return acquire();
    }

    void release(std::shared_ptr<T> ptr)
    {
        Node* new_node = new Node(ptr);
        new_node->next.store(head_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        Node* expected = new_node->next.load(std::memory_order_relaxed);
        while (!head_.compare_exchange_weak(expected, new_node, std::memory_order_release, std::memory_order_relaxed))
        {
            new_node->next.store(expected, std::memory_order_relaxed);
        }
    }

    ~LockFreeMemoryPool()
    {
        Node* current = head_.load(std::memory_order_relaxed);
        while (current != nullptr)
        {
            Node* next = current->next.load(std::memory_order_relaxed);
            delete current;
            current = next;
        }
    }

private:
    struct Node
    {
        std::shared_ptr<T> data;
        std::atomic<Node*> next;

        Node(std::shared_ptr<T> ptr)
            : data(ptr), next(nullptr)
        {
        }
    };

    void expand_pool(size_t count)
    {
        for (size_t i = 0; i < count; ++i)
        {
            auto ptr = std::make_shared<T>();
            release(ptr);
        }
    }

    std::atomic<Node*> head_{nullptr};
    size_t pool_size_ = 10000;
};
