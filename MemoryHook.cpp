#include <stddef.h>
#include <stdlib.h>
#include <iostream>
#include <cstring>
#include "hooks.h"

extern "C" {


    extern void *__libc_malloc(size_t);
    extern void *__libc_free(void*);
    extern void *__libc_realloc(void*, size_t);
    extern void *__libc_memalign(size_t alignment, size_t size, const void *caller);
}

#define MAX_DEBUG_PTRS 5000
void* ptrs[MAX_DEBUG_PTRS];
size_t currentPtr = 0;

extern uint32_t mainThread;

uint32_t GetThreadId() {
    auto id = std::this_thread::get_id();

    uint32_t uid;
    memcpy(&uid, &id, std::min(sizeof(id), sizeof(uid)));

    return uid;
}

extern "C" {
    extern bool started;
    extern bool inInstrumentation;

    extern SPEdgeData currentEdge;

    static constexpr size_t PAYLOAD_BYTES = 64;
    static constexpr bool debug = false;

    size_t numAllocs = 0;
    size_t numFrees = 0;

    static size_t magicValue = 0xAABBCCDDEEFFAABB;

    void* malloc(size_t size) {
        numAllocs++;

        uint8_t* mem = (uint8_t*)__libc_malloc(PAYLOAD_BYTES + size);

        if (size == 0) // Treat zero-allocations as non-zero for sake of testing.
            size = 1;

        if (started && !inInstrumentation
            && (mainThread == 0 || GetThreadId() == mainThread)
            )
        {
            currentEdge.memAllocated += size;

            if (currentEdge.memAllocated > currentEdge.maxMemAllocated)
                currentEdge.maxMemAllocated = currentEdge.memAllocated;
        }

        // Store the size of the allocation.
        if (PAYLOAD_BYTES > 2 * sizeof(size_t))
        {
            memcpy(mem, &magicValue, sizeof(size_t));
            memcpy(mem + sizeof(size_t), &size, sizeof(size_t));
        }

        if (debug && currentPtr < MAX_DEBUG_PTRS)
        {
            if (currentPtr == 0)
                memset(ptrs, 0, sizeof(void*) * MAX_DEBUG_PTRS);

            ptrs[currentPtr] = mem;
            currentPtr++;
        }

        // printf("Allocating %p\n", mem);

        return mem + PAYLOAD_BYTES;
    }


    void free(void* mem) {
        if (mem == nullptr)
            return;



        DEBUG_ASSERT(numAllocs > numFrees);

        uint8_t* addr = (uint8_t*)mem - PAYLOAD_BYTES;



        if (debug)
        {
            bool found = false;
            for (size_t i = 0; i < currentPtr; ++i)
            {
                if (ptrs[i] == addr)
                {
                    //ptrs[i] = nullptr;
                    found = true;
                    break;
                }

            }
            if (currentPtr < MAX_DEBUG_PTRS)
            {
                // DEBUG_ASSERT_EX(found, "Didn't find %p - Allocs: %zu, frees: %zu", mem, numAllocs, numFrees);
            }

        }

        size_t size = 0;

        if (PAYLOAD_BYTES > 2 * sizeof(size_t))
        {
            size_t magic = 0;
            memcpy(&magic, addr, sizeof(size_t));

            if (magic == magicValue)
                memcpy(&size, addr + sizeof(size_t), sizeof(size_t));
            
        }

        if (started && !inInstrumentation 
            && (mainThread == 0 || GetThreadId() == mainThread)
            )
        {
            currentEdge.memAllocated -= size;
        }

        numFrees++;

        // printf("Freeing %p\n", addr);

        if (size > 0 && started)
         __libc_free((void*)addr);
    }

    void* calloc(size_t num, size_t size) {
        void* mem = malloc(num * size);
        memset(mem, 0, num*size);
        return mem;
    }

    void *realloc(void *ptr, size_t new_size) {

        if (ptr == nullptr)
            return malloc(new_size);

        uint8_t* oldptr = (uint8_t*)ptr - PAYLOAD_BYTES;
        uint8_t* mem = (uint8_t*)__libc_realloc(oldptr, new_size + PAYLOAD_BYTES);

        if (oldptr != mem)
        {
            numAllocs++;
            if (debug &&  currentPtr < MAX_DEBUG_PTRS)
            {
                if (currentPtr == 0)
                    memset(ptrs, 0, sizeof(void*) * MAX_DEBUG_PTRS);

                ptrs[currentPtr] = mem;
                currentPtr++;
            }

            //  printf("Allocating %p\n", mem);
        }

        // Store the size of the allocation.
        if (PAYLOAD_BYTES > 2 * sizeof(size_t))
        {
            memcpy(mem, &magicValue, sizeof(size_t));
            memcpy(mem + sizeof(size_t), &new_size, sizeof(size_t));
        }


        return mem + PAYLOAD_BYTES;
    }

    void* memalign(size_t alignment, size_t size, const void *caller) {
        DEBUG_ASSERT(alignment <= PAYLOAD_BYTES);
        return malloc(size);
        //  return __libc_memalign(alignment, size, caller);

       //  return mem + sizeof(size_t);
    }

}