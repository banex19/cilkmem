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

    uint32_t uid = *((uint32_t*)(&id));


    return uid;
}

extern "C" {
    extern bool started;

    extern SPEdgeData currentEdge;

    static constexpr size_t PAYLOAD_BYTES = 16;

    void* malloc(size_t size) {
        uint8_t* mem = (uint8_t*)__libc_malloc(PAYLOAD_BYTES + size);

        if (size == 0) // Treat zero-allocations as non-zero for sake of testing.
            size = 1;

        if (started 
           &&  (mainThread == 0 || GetThreadId() == mainThread)
            )
        {
            currentEdge.memAllocated += size;

            if (currentEdge.memAllocated > currentEdge.maxMemAllocated)
                currentEdge.maxMemAllocated = currentEdge.memAllocated;
        }

        // Store the size of the allocation.
        memcpy(mem, &size, sizeof(size_t));

        if (false && currentPtr < MAX_DEBUG_PTRS)
        {
            if (currentPtr == 0)
                memset(ptrs, 0, sizeof(void*) * MAX_DEBUG_PTRS);

            ptrs[currentPtr] = mem;
            currentPtr++;
        }

        return mem + PAYLOAD_BYTES;
    }


    void free(void* mem) {
        if (mem == nullptr)
            return;

        uint8_t* addr = (uint8_t*)mem - PAYLOAD_BYTES;

        if (false)
        {
            bool found = false;
            for (size_t i = 0; i < currentPtr; ++i)
            {
                if (ptrs[i] == addr)
                {
                    ptrs[i] = nullptr;
                    found = true;
                    break;
                }

            }
            if (currentPtr < MAX_DEBUG_PTRS)
            {
                DEBUG_ASSERT(found);
            }

        }

        size_t size = 0;
        memcpy(&size, addr, sizeof(size_t));

        if (started 
           &&  (mainThread == 0 || GetThreadId() == mainThread)
            )
        {
         //   currentEdge.memAllocated -= size;
        }

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

        return mem + PAYLOAD_BYTES;
    }

    void* memalign(size_t alignment, size_t size, const void *caller) {
        return malloc(size);
        //uint8_t* mem = (uint8_t*)__libc_memalign(alignment, size + sizeof(size_t), caller);

      //  return mem + sizeof(size_t);
    }

}