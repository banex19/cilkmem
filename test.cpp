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

void* __attribute__((noinline)) testAlloc(size_t size)
{
    return malloc(size);
}

void testFree(void* mem)
{
    return free(mem);
}

__attribute__((noinline)) int testSpawn(int x)
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
   // std::cout << "fib(" << n << ")\n";
    if (n < 2) {
        return n;
    }

    size_t x, y;
    x = cilk_spawn fib(n - 1);
    // test((int)n);
    mem = malloc(n);
    y = fib(n - 2);
    cilk_sync;

    mem = malloc(100);

    return x + y;
}

__attribute__((noinline)) uint64_t rec(uint64_t n) {
    std::cout << "rec(" << n << ")\n";
    if (n < 2) {
        return n;
    }

    size_t x, y;
    if (n == 2)
        x = 0;
    else
        x = rec(0);
    // test((int)n);
    mem = malloc(n);
    y = cilk_spawn rec(n - 1);
    cilk_sync;

    mem = malloc(100);

    std::cout << "Finished rec(" << n << ")\n";
    return x + y;
}


int main(int argc, char** argv)
{
    uint64_t n = 30;
    if (argc > 1)
    {
        n = std::atoi(argv[1]);
    }
    //  uint64_t x = fib(3);



      //if (x == 1)
      //    cilk_spawn test(100);
     //  uint64_t x = cilk_spawn test(10);

    mem = malloc(100);
   // uint64_t x = cilk_spawn fib(2);
  //  uint64_t y =  testSpawn(100);

   /* for(size_t i = 0; i < 2; ++i)
    {
        mem = cilk_spawn testAlloc(10);
    } */

    cilk_for(size_t i = 0; i < 1; ++i)
    {
        uint64_t x = fib(n);
    }


    mem = malloc(123);

  //  cilk_sync;

  //  uint64_t x = cilk_spawn fib(3);

    mem = malloc(500);


  //  printf("Returned %lu and %lu\n", x, y);


    return 0;
}

