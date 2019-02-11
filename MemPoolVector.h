#pragma once
#include "common.h"
#include <vector>
#include <mutex>
#include <cstring>

template<typename T>
struct PooledNode {
    T data;
    volatile PooledNode* next;
};

template <typename T>
class MemPoolVector {
public:
    MemPoolVector() : MemPoolVector(4000) {

    }

    MemPoolVector(size_t poolSize) : poolSize(poolSize) {
        AddPool();
    }

    ~MemPoolVector() {
        clear();
    }

    void push_back(const T& data) {
        bool fromFreeList = false;

        PooledNode<T>* next = GetNextAvailable(fromFreeList);
        next->data = data;
        next->next = nullptr;

        if (head == nullptr)
        {
            head = tail = next;
        }
        else
        {
            DEBUG_ASSERT(tail != nullptr);
            tail->next = next;
            tail = next;
        }

        if (!fromFreeList)
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

    // Unsafe if nodes move around.
    T& operator[](size_t index) {
        DEBUG_ASSERT(currentSize > index);
        DEBUG_ASSERT(pools.size() > (index / poolSize));

        return  (pools[index / poolSize])[index % poolSize].data;
    }

    volatile size_t size() {
        return currentSize;
    }

    void clear() {
        //if (pools.size() > 0)
        //    std::cout << "Allocated " << pools.size() << " pools\n";

        for (auto& pool : pools)
            delete[] pool;
        pools.clear();
    }

    // Can only return the head.
    void ReturnToPool(PooledNode<T>* node) {
        DEBUG_ASSERT(node == head);
        head = (PooledNode<T>*)node->next;
        node->next = nullptr;

        if (freeHead == nullptr)
        {
            DEBUG_ASSERT(freeTail == nullptr);
            freeHead = freeTail = node;
        }
        else
        {
            DEBUG_ASSERT(freeTail != nullptr);
            freeTail = node;
            freeTail->next = node;     
        }

    }

private:

    void AddPool() {
        lastPoolUtilized = 0;
        pools.push_back(new PooledNode<T>[poolSize]);
    }

    PooledNode<T>* GetNextAvailable(bool& out_fromFreeList) {
        out_fromFreeList = false;

        // Check if there are still free nodes from the last allocated pool.
        if (pools.size() > 0 && lastPoolUtilized < poolSize)
        {
            PooledNode<T>*  node = &(pools.back()[lastPoolUtilized]);
            lastPoolUtilized++;
            return node;
        }

        // Try to get a node from the free list.
        if (freeHead != nullptr && freeHead->next != nullptr)
        {
            out_fromFreeList = true;
            DEBUG_ASSERT_EX(freeHead != freeTail, "freeHead: %p, freeTail: %p, freeHead->next: %p", freeHead, freeTail, freeHead->next);
            PooledNode<T>*  node = freeHead;
            freeHead = (PooledNode<T>*)freeHead->next;
            return node;
        }

        // No nodes available from the free list, add a new pool.
        AddPool();
        return GetNextAvailable(out_fromFreeList);
    }

    PooledNode<T>* GetBoundNode() {
        return pools.back() + (poolSize - 1);
    }

    volatile size_t currentSize = 0;

    std::vector<PooledNode<T>*> pools;

    size_t lastPoolUtilized;
    size_t poolSize;
    PooledNode<T>* head = nullptr;
    PooledNode<T>* tail = nullptr;

    PooledNode<T>* freeHead = nullptr;
    PooledNode<T>* freeTail = nullptr;
};