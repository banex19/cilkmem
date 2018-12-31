#include "/home/daniele/llvm/build/lib/clang/6.0.0/include/cilk/cilk.h"
#include <iostream>
#include <cstdio>
#include <cstdint>

// <>

void* mem = nullptr;

int test(int x)
{
    return printf("Value: %d\n", x);
}

void* testAlloc(size_t size)
{
    return malloc(size);
}

void testFree(void* mem)
{
    return free(mem);
}

int testSpawn(int x)
{
    void* y = cilk_spawn testAlloc((size_t)x);

    int k = printf("Testing subspawn\n");

    mem = malloc(200);

    cilk_sync;

    free(mem);

    cilk_spawn testFree(y);

    return k;
}



__attribute__((noinline)) uint64_t fib(uint64_t n) {
    if (n < 2) return n;
    size_t x, y;
    x = cilk_spawn fib(n - 1);
    // test((int)n);
    y = fib(n - 2);
    cilk_sync;
    return x + y;
}



int main()
{

    // uint64_t x = cilk_spawn fib(10);
   // uint64_t x = cilk_spawn test(10);
    uint64_t y = //cilk_spawn
        testSpawn(100);


    cilk_sync;


  //  printf("Returned %lu and %lu\n", x, y);


    return 0;
}

