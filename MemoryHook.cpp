#include <stddef.h>
#include <stdlib.h>
#include <iostream>
#include <cstring>
#include "hooks.h"
#include <malloc.h>


bool reentrant = false;

extern std::string programName;

#ifdef USE_BACKTRACE
#include "backtrace.h"
#include "backtrace-supported.h"



struct bt_ctx {
    struct backtrace_state* state;
    std::string function, filename;
    int line;
    int error;
    size_t allocSize;
};

static void error_callback(void* data, const char* msg, int errnum) {
    struct bt_ctx* ctx = (bt_ctx*)data;
    fprintf(stderr, "ERROR: %s (%d)", msg, errnum);
    ctx->error = 1;
}

static void syminfo_callback(void* data, uintptr_t pc, const char* symname, uintptr_t symval, uintptr_t symsize) {
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

static int full_callback(void* data, uintptr_t pc, const char* filename, int lineno, const char* function) {
    struct bt_ctx* ctx = (bt_ctx*)data;
    if (function)
    {
        //    printf("[%zu] %lx %s %s:%d\n", ctx->allocSize, (unsigned long)pc, function, filename ? filename : "??", lineno);
    }
    else
    {
        //  backtrace_syminfo(ctx->state, pc, syminfo_callback, error_callback, data);
    }


    if (true && filename != nullptr && ((programName.size() > 0 && strstr(filename, programName.c_str()) != NULL) || strstr(filename, "./") == filename) && strstr(filename, "MemoryHook") == NULL)
    {
        //  printf("HERE\n");
        if (function != nullptr)
            ctx->function = function;
        ctx->filename = filename;
        ctx->line = lineno;

        return 1;
    }

    return 0;
}

static int simple_callback(void* data, uintptr_t pc) {

    struct bt_ctx* ctx = (bt_ctx*)data;
    backtrace_pcinfo(ctx->state, pc, full_callback, error_callback, data);

    return 0;
}

struct backtrace_state* state = nullptr;

std::unordered_map<void*, std::string> addrToSource;

static inline void bt_inner(SPEdgeData & data, size_t size, bool newMax = false, void* addr = nullptr) {

    if (state == nullptr)
        state = backtrace_create_state(nullptr, 0, error_callback, nullptr);
    struct bt_ctx ctx = { state };
    ctx.allocSize = size;
    backtrace_full(state, 0, full_callback, error_callback, &ctx);

    if (ctx.function != "")
    {
        //printf("[%zu] --> %s %s:%d\n", ctx.allocSize, ctx.function.c_str(), ctx.filename != "" ? ctx.filename.c_str() : "??", ctx.line);
        std::string sourceLoc = std::string(ctx.filename) + ":" + std::to_string(ctx.line);
        DEBUG_ASSERT(data.allocMap);
        DEBUG_ASSERT(data.maxAllocMap);
        (*(data.allocMap))[sourceLoc] += size;
        //std::cout << "Current memory for " << sourceLoc << ": " << (*(data.allocMap))[sourceLoc] << "\n";
        if (newMax) {
            *(data.maxAllocMap) = *(data.allocMap);
            data.maxAllocMapSize = data.maxMemAllocated;
        }

        addrToSource[addr] = sourceLoc; 
        /* if (data.filename)
             *(data.filename) = ctx.filename;
         else
             data.filename = new std::string(ctx.filename);
         if (data.function)
             *(data.function) = ctx.function;
         else
             data.function = new std::string(ctx.function);
         data.line = (size_t)ctx.line; */
    }

}

static inline void bt(SPEdgeData & data, size_t size, bool newMax = false, void* addr = nullptr) {
    bool wasAlreadyReentrant = reentrant;

    reentrant = true;

    bt_inner(data, size, newMax, addr);

    if (!wasAlreadyReentrant)
        reentrant = false;
}

#endif

extern "C" {
    extern void* __libc_malloc(size_t);
    extern void* __libc_free(void*);
    extern void* __libc_realloc(void*, size_t);
    extern void* __libc_memalign(size_t alignment, size_t size, const void* caller);
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

        if (size == 0) // Treat zero-allocations as non-zero for sake of testing.
            size = 1;

#ifdef USE_PAYLOAD
        uint8_t * mem = (uint8_t*)__libc_malloc(PAYLOAD_BYTES + size);
#else
        uint8_t * mem = (uint8_t*)__libc_malloc(size);
        size = malloc_usable_size(mem);
#endif
        bool isMainThread = mainThread == 0 || (GetThreadId() == mainThread);

        bool newMax = false;

        if (!reentrant && started && !inInstrumentation && isMainThread)
        {
            currentEdge.memAllocated += size;
            //  GUARD_REENTRANT(printf("[malloc] addr: %p - size: %d - currentEdge.memAllocated: %d - currentEdge.maxMemAllocated: %d\n", mem, (int)size, (int)(currentEdge.memAllocated), (int)(currentEdge.maxMemAllocated)));

            if (currentEdge.memAllocated > currentEdge.maxMemAllocated)
            {

#ifdef USE_BACKTRACE
                if (currentEdge.memAllocated > 2 * currentEdge.maxAllocMapSize)
                    newMax = true;
#endif

                currentEdge.maxMemAllocated = currentEdge.memAllocated;
            }


            // GUARD_REENTRANT(printf("currentEdge.memAllocated: %d\n", (int)(currentEdge.memAllocated)));

#ifdef USE_BACKTRACE
            if (!reentrant && size > minSizeBacktrace)
            {
                //  if (size > currentEdge.biggestAllocation)
                {
                    bt(currentEdge, size, newMax, mem);
                    currentEdge.biggestAllocation = size;

                }
            }
#endif
        }






#ifdef USE_PAYLOAD
        // Store the size of the allocation.
        if (PAYLOAD_BYTES > 2 * sizeof(size_t))
        {
            memcpy(mem, &magicValue, sizeof(size_t));
            memcpy(mem + sizeof(size_t), &size, sizeof(size_t));
        }
        return mem + PAYLOAD_BYTES;
#else
        return mem;
#endif
    }


    void free(void* mem) {
        if (mem == nullptr)
            return;

        void* originalAddr = mem;

#ifdef USE_PAYLOAD
        uint8_t * addr = (uint8_t*)mem - PAYLOAD_BYTES;
        size_t size = 0;
        if (PAYLOAD_BYTES > 2 * sizeof(size_t))
        {
            size_t magic = 0;
            memcpy(&magic, addr, sizeof(size_t));

            if (magic == magicValue)
                memcpy(&size, addr + sizeof(size_t), sizeof(size_t));
        }
#else
        uint8_t* addr = (uint8_t*)mem;
        size_t size = malloc_usable_size(mem);
#endif


        if (!reentrant && started && !inInstrumentation
            && (mainThread == 0 || GetThreadId() == mainThread)
            )
        {

            //GUARD_REENTRANT(printf("[free] addr: %p - size: %d - currentEdge.memAllocated: %d - currentEdge.maxMemAllocated: %d\n", addr, (int)size, (int)(currentEdge.memAllocated), (int)(currentEdge.maxMemAllocated)));


#ifdef USE_BACKTRACE
            DEBUG_ASSERT_EXIT(currentEdge.allocMap);
            auto& map = *currentEdge.allocMap;

            if (addrToSource.find(addr) != addrToSource.end()) {
                // GUARD_REENTRANT(printf("addrToSource hit\n"));
                GUARD_REENTRANT(map[addrToSource[addr]] -= size);
            }
            else {
                //   GUARD_REENTRANT(printf("addrToSource MISS\n"));
            }
#endif

            currentEdge.memAllocated -= size;

        }

        numFrees++;



        if (size > 0 && started)
            __libc_free((void*)addr);
    }

    void* calloc(size_t num, size_t size) {
        void* mem = malloc(num * size);
        memset(mem, 0, num * size);
        return mem;
    }

    void* realloc(void* ptr, size_t new_size) {

        if (ptr == nullptr)
            return malloc(new_size);

#ifdef USE_PAYLOAD
        uint8_t * oldptr = (uint8_t*)ptr - PAYLOAD_BYTES;

        size_t size = 0;
        int64_t diff = 0;

        if (PAYLOAD_BYTES > 2 * sizeof(size_t))
        {
            size_t magic = 0;
            memcpy(&magic, oldptr, sizeof(size_t));

            if (magic == magicValue)
                memcpy(&size, oldptr + sizeof(size_t), sizeof(size_t));

            if (size > 0)
                diff = (int64_t)new_size - (int64_t)size;

        }


        uint8_t* mem = (uint8_t*)__libc_realloc(oldptr, new_size + PAYLOAD_BYTES);
#else
        size_t oldSize = malloc_usable_size(ptr);
        uint8_t * mem = (uint8_t*)__libc_realloc(ptr, new_size);
        int64_t diff = (int64_t)malloc_usable_size(mem) - (int64_t)oldSize;
#endif

        bool isMainThread = mainThread == 0 || (GetThreadId() == mainThread);

        bool newMax = false;

        if (!reentrant && started && !inInstrumentation && isMainThread)
        {
            if (diff > 0)
                currentEdge.memAllocated += diff;
            else currentEdge.memAllocated -= diff;

            if (currentEdge.memAllocated > currentEdge.maxMemAllocated) {
#ifdef USE_BACKTRACE
                if (currentEdge.memAllocated > 2 * currentEdge.maxAllocMapSize)
                    newMax = true;
#endif

                currentEdge.maxMemAllocated = currentEdge.memAllocated;
            }

#ifdef USE_BACKTRACE
            if (!reentrant && new_size > minSizeBacktrace)
            {
                //   if (new_size > currentEdge.biggestAllocation)
                {
                    bt(currentEdge, size, newMax, mem);
                    currentEdge.biggestAllocation = size;
                }

            }
#endif
        }





        // Store the size of the allocation.
#ifdef USE_PAYLOAD
        if (PAYLOAD_BYTES > 2 * sizeof(size_t))
        {
            memcpy(mem, &magicValue, sizeof(size_t));
            memcpy(mem + sizeof(size_t), &new_size, sizeof(size_t));
        }


        return mem + PAYLOAD_BYTES;
#else
        return mem;
#endif
    }

    void* memalign(size_t alignment, size_t size /*, const void *caller */) {
        DEBUG_ASSERT(alignment <= PAYLOAD_BYTES);
#ifdef USE_PAYLOAD
        return malloc(size);
#else 
        return malloc(PAYLOAD_BYTES + size);
#endif
        //  return __libc_memalign(alignment, size, caller);

       //  return mem + sizeof(size_t);
    }

}