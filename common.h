#pragma once
#include <cassert>


#ifndef NDEBUG
#define DEBUG_ASSERT(x) do { assert(x); } while(0)
#define DEBUG_ASSERT_EX(x, format, ...) do { if (!(x)) { printf(format, __VA_ARGS__); printf(" - ASSERTING \n"); assert(x); exit(-1); }} while (0)
#else
#define DEBUG_ASSERT(x) 
#define DEBUG_ASSERT_EX(x, format, ...) 
#endif