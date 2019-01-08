#include "SeriesParallelDAG.h"
#include <algorithm>

// Null is 0
static constexpr int64_t NULL_VALUE = 0;

int64_t NullAdd(int64_t a, int64_t b)
{
    if (a == NULL_VALUE || b == NULL_VALUE)
        return NULL_VALUE;
    else return a + b;
}

int64_t NullMax(int64_t a, int64_t b)
{
    if (a == NULL_VALUE)
        return b;
    if (b == NULL_VALUE)
        return a;
    else return std::max(a, b);
}

int64_t NullMax(int64_t a, int64_t b, int64_t c)
{
    return NullMax(a, NullMax(b, c));
}

int64_t NullMax(int64_t a, int64_t b, int64_t c, int64_t d)
{
    return NullMax(a, NullMax(b, c, d));
}

void SPComponent::CombineSeries(const SPComponent & other)
{
    memTotal = memTotal + other.memTotal;
    maxSingle = std::max(maxSingle, memTotal + other.maxSingle);
    multiRobust = NullMax(multiRobust, NullAdd(memTotal, other.multiRobust));
}

void SPComponent::CombineParallel(const SPComponent & other, int64_t threshold)
{
    memTotal = memTotal + other.memTotal;
    maxSingle = std::max(maxSingle + std::max(0L, other.memTotal), other.maxSingle + std::max(0L, memTotal));

    int64_t c1MaxSingleBar = maxSingle > threshold ? maxSingle : NULL_VALUE;
    int64_t c2MaxSingleBar = other.maxSingle > threshold ? other.maxSingle : NULL_VALUE;

    multiRobust = NullMax(
        NullAdd(c1MaxSingleBar, c2MaxSingleBar),
        NullAdd(NullMax(c1MaxSingleBar, multiRobust, memTotal, 0L), other.multiRobust),
        NullAdd(NullMax(c2MaxSingleBar, other.multiRobust, other.memTotal, 0L), multiRobust));
}

int64_t SPComponent::GetWatermark(int64_t threshold)
{
    return NullMax(maxSingle > threshold ? maxSingle : NULL_VALUE, multiRobust, 0L);
}

void SPComponent::Print()
{
    std::cout << "Component - memTotal: " << memTotal << ", maxSingle: " << maxSingle << ", multiRobust: " << multiRobust << "\n";
}
