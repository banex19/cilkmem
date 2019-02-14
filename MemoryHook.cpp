#include <stddef.h>
#include <stdlib.h>
#include <iostream>
#include <cstring>
#include "hooks.h"


bool reentrant = false;

extern std::string programName;

#ifdef USE_BACKTRACE
#include "backtrace.h"
#include "backtrace-supported.h"



struct bt_ctx {
    struct backtrace_state *state;
    std::string function, filename;
    int line;
    int error;
};

static void error_callback(void *data, const char *msg, int errnum) {
    struct bt_ctx *ctx = (bt_ctx*)data;
    fprintf(stderr, "ERROR: %s (%d)", msg, errnum);
    ctx->error = 1;
}

static void syminfo_callback(void *data, uintptr_t pc, const char *symname, uintptr_t symval, uintptr_t symsize) {
    //struct bt_ctx *ctx = data;
    if (symname)
    {
        printf("%lx %s ??:0\n", (unsigned long)pc, symname);
    }
    else
    {
        printf("%lx ?? ??:0\n", (unsigned long)pc);
    }
}

static int full_callback(void *data, uintptr_t pc, const char *filename, int lineno, const char *function) {
    struct bt_ctx *ctx = (bt_ctx*)data;
    if (function)
    {
        //  printf("%lx %s %s:%d\n", (unsigned long)pc, function, filename ? filename : "??", lineno);
    }
    else
    {
        //  backtrace_syminfo(ctx->state, pc, syminfo_callback, error_callback, data);
    }


    if (filename != nullptr && ((programName.size() > 0 && strstr(filename, programName.c_str()) != NULL) || strstr(filename, "./") != NULL) && strstr(filename, "MemoryHook") == NULL)
    {
        if (function != nullptr)
            ctx->function = function;
        ctx->filename = filename;
        ctx->line = lineno;

        return 1;
    }

    return 0;
}

static int simple_callback(void *data, uintptr_t pc) {

    struct bt_ctx *ctx = (bt_ctx*)data;
    backtrace_pcinfo(ctx->state, pc, full_callback, error_callback, data);

    return 0;
}

struct backtrace_state *state = nullptr;


static inline void bt(SPEdgeData& data) {
    reentrant = true;


    if (state == nullptr)
        state = backtrace_create_state(nullptr, 0, error_callback, nullptr);
    struct bt_ctx ctx = { state };
    backtrace_full(state, 0, full_callback, error_callback, &ctx);

    if (ctx.function != "")
    {
     //   printf("--> %s %s:%d\n", ctx.function.c_str(), ctx.filename != "" ? ctx.filename.c_str() : "??", ctx.line);
        if (data.filename)
            *(data.filename) = ctx.filename;
        else
            data.filename = new std::string(ctx.filename);
        if (data.function)
            *(data.function) = ctx.function;
        else
            data.function = new std::string(ctx.function);
        data.line = (size_t)ctx.line;
    }

    reentrant = false;
}
#endif

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

    extern size_t minSizeBacktrace;

    static constexpr size_t PAYLOAD_BYTES = 64;
    static constexpr bool debug = false;

    size_t numAllocs = 0;
    size_t numFrees = 0;

    static size_t magicValue = 0xAABBCCDDEEFFAABB;

    void* malloc(size_t size) {

        // numAllocs++;

        uint8_t* mem = (uint8_t*)__libc_malloc(PAYLOAD_BYTES + size);

        if (size == 0) // Treat zero-allocations as non-zero for sake of testing.
            size = 1;

        bool isMainThread = mainThread == 0 || (GetThreadId() == mainThread);

#ifdef USE_BACKTRACE
        if (isMainThread && !reentrant && size > minSizeBacktrace)
        {
            if (size > currentEdge.biggestAllocation)
            {
                bt(currentEdge);
                currentEdge.biggestAllocation = size;
    }
}
#endif

        if (started && !inInstrumentation&& isMainThread)

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



        // DEBUG_ASSERT(numAllocs > numFrees);

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