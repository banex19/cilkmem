#pragma once
#include "common.h"
#include <vector>
#include <mutex>

template<typename T>
struct PooledNode {
    T data;
    volatile PooledNode* next;
};

template <typename T>
class MemPoolVector {



public:
    MemPoolVector() : MemPoolVector(1000) {

    }

    MemPoolVector(size_t poolSize) : poolSize(poolSize)
    {
    }

    ~MemPoolVector() {
        clear();
    }

    void push_back(const T& data) {
        PooledNode<T>* next = GetNextAvailable();
        next->data = data;
        next->next = nullptr;

        if (head == nullptr)
        {
            head = tail = next;
        }
        else {
            DEBUG_ASSERT(tail != nullptr);
            tail->next = next;
            tail = next;
        }

        currentSize++;
    }

    PooledNode<T>* GetHeadNode() {
        return head;
    }

    T& back() {
        DEBUG_ASSERT(currentSize > 0);
        return tail->data;
    }

    T& front() {
        DEBUG_ASSERT(currentSize > 0);
        return head->data;
    }

    T& operator[](size_t index) {
        DEBUG_ASSERT(currentSize > index);

        return  (pools[index / poolSize])[index % poolSize].data;
    }

    volatile size_t size() {
        return currentSize;
    }

    void clear() {
        for (auto& pool : pools)
            delete[] pool;
        pools.clear();
    }

private:

    void AddPool() {
        pools.push_back(new PooledNode<T>[poolSize]);
    }

    PooledNode<T>* GetNextAvailable() {
        if (tail == nullptr || tail == GetBoundNode())
        {
            AddPool();
       //     std::cout << "Allocating new pool " << pools.back() << "\n";
            return pools.back();
        }

     
  //      std::cout << "Allocating node " << (tail + 1) << "\n";
        return tail + 1;
    }

    PooledNode<T>* GetBoundNode()
    {
        return pools.back() + (poolSize - 1);
    }

    volatile size_t currentSize = 0;

    std::vector<PooledNode<T>*> pools;

    size_t poolSize;
    PooledNode<T>* head = nullptr;
    PooledNode<T>* tail = nullptr;
};