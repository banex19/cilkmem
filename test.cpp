#include <cilk/cilk.h>
#include <iostream>
#include <cstdio>
#include <cstdint>

// <>

void* mem = nullptr;

int test(int x) {
    return printf("Value: %d\n", x);
}

void* __attribute__((noinline)) testAlloc(size_t size) {
    return malloc(size);
}

void testFree(void* mem) {
    return free(mem);
}

__attribute__((noinline)) int testSpawn(int x) {
    if (x == 0)
        return 1;

    void* y = cilk_spawn testAlloc((size_t)x);

    // int k = printf("Testing subspawn\n");
    int k = 2;

    mem = malloc(200);

    cilk_sync;

    free(mem);

    cilk_spawn testFree(y);

    return k;

}



__attribute__((noinline)) uint64_t fibAlloc(uint64_t n) {
    // std::cout << "fib(" << n << ")\n";
    if (n < 2)
    {
        return n;
    }

    size_t x, y;
    x = cilk_spawn fibAlloc(n - 1);
    // test((int)n);
    mem = malloc(n);
    y = fibAlloc(n - 2);
    cilk_sync;

    mem = malloc(100);

    return x + y;
}

__attribute__((noinline)) uint64_t fib(uint64_t n) {
    if (n < 2)
        return n;

    size_t x, y;
    x = cilk_spawn fib(n - 1);
    y = fib(n - 2);
    cilk_sync;

    return x + y;
}

__attribute__((noinline)) uint64_t rec(uint64_t n) {
    //  std::cout << "rec(" << n << ")\n";
    if (n < 2)
    {
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

    //   std::cout << "Finished rec(" << n << ")\n";
    return x + y;
}

__attribute__((noinline))  uint64_t testInterleave(uint64_t n) {

    mem = malloc(10000);

    free(mem);

    mem = malloc(10000);

    free(mem);

    mem = malloc(50000);

    free(mem);

    return 0;
}

__attribute__((noinline))  uint64_t testFunction(uint64_t n) {
    mem = malloc(n * 100);

    if (mem == nullptr)
    {
        mem = malloc(500);
    }

    free(mem);

    mem = malloc(n * 10);

    return 0;
}

__attribute__((noinline)) uint64_t stress(uint64_t n) {
    if (n == 0)
    {
        mem = malloc(50);
        free(mem);
        return 1;
    }
    void* nowmem = nullptr;

        nowmem = malloc(n * 100);


    mem = nowmem;


    cilk_spawn stress(n - 1);
    auto x = stress(n - 1);

    free(nowmem);

    return x;
}

uint64_t x = 0;

int main(int argc, char** argv) {
    uint64_t n = 10;
    uint64_t k = 5;
    uint64_t o = 1;

    if (argc > 1)
    {
        n = std::atoi(argv[1]);
    }
    if (argc > 2)
    {
        k = std::atoi(argv[2]);
    }
    if (argc > 3)
    {
        o = std::atoi(argv[3]);
    }
    //  uint64_t x = fib(3);



      //if (x == 1)
      //    cilk_spawn test(100);
     //  uint64_t x = cilk_spawn test(10);

   // mem = aligned_alloc(64,200);
    // uint64_t x = cilk_spawn fib(2);
   //  uint64_t y =  testSpawn(100);

    /* for(size_t i = 0; i < 2; ++i)
     {
         mem = cilk_spawn testAlloc(10);
     } */

    for (size_t j = 0; j < o; ++j)
    {
        for (size_t i = 0; i < k; ++i)
        {
            x = cilk_spawn stress(n);
          //  x = fib(n);
              
	}
        //#pragma cilk grainsize 1
             //   cilk_for(size_t i = 0; i < k; ++i) {
             //        x = testFunction(n); }


    }

    cilk_sync;
    /*
        for (size_t i = 0; i < k; ++i)
        {
            uint64_t x = cilk_spawn testFunction(n);

        }

        cilk_sync;
    */
    // mem = malloc(123);

     //  cilk_sync;

     //  uint64_t x = cilk_spawn fib(3);

   //  mem = malloc(500);


     //  printf("Returned %lu and %lu\n", x, y);


    return 0;
}

