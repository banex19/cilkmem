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

int testSubSpawn(int x)
{
    int y = cilk_spawn test(x);

    printf("Testing subspawn\n");

    cilk_sync;

    mem = malloc(200);

    return y;
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
    uint64_t x = cilk_spawn test(10);
    uint64_t y = cilk_spawn testSubSpawn(100);


    cilk_sync;


    printf("Returned %lu and %lu\n", x, y);


    return 0;
}

