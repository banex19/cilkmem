#include <stddef.h>
#include <stdlib.h>
#include <iostream>

extern "C" {
    extern void *__libc_malloc(size_t);
    extern bool started;

    void *malloc(size_t size) {
        if (started)
        {
            return nullptr;
        }
        return __libc_malloc(size);
    }
}