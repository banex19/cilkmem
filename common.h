#pragma once
#include <cassert>
#include "OutputPrinter.h"
#include <thread>      
#include <chrono>    
#include <iomanip>
#include <sstream>
#include <locale>

#ifndef NDEBUG
#define DEBUG_ASSERT_EXIT(x) do { if (!(x)) {printf("Assertion failed\n"); exit(-1); }} while(0)
#define DEBUG_ASSERT(x) do { if (!(x)) {assert(x); exit(-1); }} while(0)
#define DEBUG_ASSERT_EX(x, format, ...) do { if (!(x)) { printf(format, __VA_ARGS__); printf("\n"); assert(x); exit(-1); }} while (0)
#else
#define DEBUG_ASSERT(x) 
#define DEBUG_ASSERT_EX(x, format, ...) 
#endif

#define GUARD_REENTRANT(stmt) do { if (!reentrant) { reentrant = true; stmt; reentrant = false; } } while (0)

#ifndef DISABLE_OUTPUT_COMPILE
#define OUTPUT(x) do {x;} while (0)
#else
#define OUTPUT(x) 
#endif



template<class T>
std::string FormatWithCommas(T value) {
    std::locale systemLocale{ "" };
    std::stringstream ss;
    ss.imbue(systemLocale);
    ss << std::fixed << value;
    return ss.str();
}