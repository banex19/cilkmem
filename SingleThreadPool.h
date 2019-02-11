#pragma once
#include "common.h"
#include <vector>

class SingleThreadPool {
public:
    struct Node {
        Node* next;

        char mem[0];
    };

    SingleThreadPool() {
    }


    SingleThreadPool(size_t elementSize, size_t poolSize) {
        Initialize(elementSize, poolSize);
    }

    void Initialize(size_t elementSize, size_t poolSize) {
        this->poolSize = poolSize;
        this->elementSize = elementSize;
        AllocatePool();
    }

    ~SingleThreadPool() {
        for (auto& pool : pools)
            delete[] pool;
    }

    bool IsInitialized() {
        return elementSize > 0;
    }

    void* Allocate() {
        if (usedFromPool < poolSize)
        {
            void* mem = ((Node*)(pools.back() + (sizeof(Node) + elementSize) * usedFromPool))->mem;
            usedFromPool++;
            return mem;
        }

        if (firstFree != nullptr)
        {
            void* mem = firstFree->mem;
            if (lastFree == firstFree)
            {
                firstFree = lastFree = nullptr;
            }
            else
                firstFree = firstFree->next;

            return mem;
        }

        AllocatePool();
        return Allocate();
    }

    void Free(void* mem) {
        Node* node = (Node*)((uint8_t*)mem - sizeof(Node));

        if (lastFree == nullptr)
        {
            DEBUG_ASSERT(firstFree == nullptr);
            lastFree = firstFree = node;
        }
        else
        {
            lastFree->next = node;
            lastFree = node;
        }
    }
private:

    void AllocatePool() {
        pools.push_back(new unsigned char[(sizeof(Node) + elementSize) * poolSize]);
        usedFromPool = 0;
        memset(pools.back(), 0, (sizeof(Node) + elementSize) * poolSize);
    }

    std::vector<uint8_t*> pools;
    size_t usedFromPool;


    size_t poolSize = 0;
    size_t elementSize = 0;

    Node* firstFree = nullptr;
    Node* lastFree = nullptr;

};