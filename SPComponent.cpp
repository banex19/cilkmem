#include "SeriesParallelDAG.h"
#include <algorithm>

template <typename T>
Nullable<T> operator+(T a, const Nullable<T>& b)
{
    return Nullable<T>(a).operator+(b);
}

template <typename T>
std::ostream & operator<<(std::ostream & os,  const Nullable<T>  & obj)
{
    if (obj.HasValue())
        os << obj.GetValue();
    else
        os << "null";
    return os;
}

template <typename T>
T Nullable<T>::NULL_VALUE = std::numeric_limits<T>::max();

Nullable<int64_t> test;

template <typename T>
Nullable<T> NullMax(const Nullable<T>& a, const Nullable<T> &b)
{
    return a.Max(b);
}

template <typename T>
Nullable<T> NullMax(const Nullable<T>& a, const  Nullable<T>& b, const Nullable<T>& c)
{
    return NullMax(a, b).Max(c);
}

template <typename T>
Nullable<T> NullMax(const Nullable<T>& a, const Nullable<T> &b, const Nullable<T>& c, const Nullable<T> &d)
{
    return NullMax(a, b, c).Max(d);
}

void SPComponent::CombineSeries(const SPComponent & other)
{
    memTotal = memTotal + other.memTotal;
    maxSingle = std::max(maxSingle, memTotal + other.maxSingle);
    multiRobust = NullMax(multiRobust, memTotal + other.multiRobust);
}

void SPComponent::CombineParallel(const SPComponent & other, int64_t threshold)
{
    memTotal = memTotal + other.memTotal;
    maxSingle = std::max(maxSingle + std::max((int64_t)0, other.memTotal), other.maxSingle + std::max((int64_t)0, memTotal));

    Nullable<int64_t> c1MaxSingleBar = maxSingle > threshold ? maxSingle : Nullable<int64_t>::NULL_VALUE;
    Nullable<int64_t> c2MaxSingleBar = other.maxSingle > threshold ? other.maxSingle : Nullable<int64_t>::NULL_VALUE;

    multiRobust = NullMax(
        c1MaxSingleBar + c2MaxSingleBar,
        NullMax(c1MaxSingleBar, multiRobust, Nullable<int64_t>(memTotal), Nullable<int64_t>(0)) + other.multiRobust,
        NullMax(c2MaxSingleBar, other.multiRobust, Nullable<int64_t>(other.memTotal), Nullable<int64_t>(0)) + multiRobust);
}

int64_t SPComponent::GetWatermark(int64_t threshold)
{
    auto nullableWatermark = NullMax(maxSingle > threshold ? Nullable<int64_t>(maxSingle) : Nullable<int64_t>::NULL_VALUE, multiRobust, Nullable<int64_t>(0));
    assert(nullableWatermark.HasValue());

    return nullableWatermark.GetValue();
}

void SPComponent::Print()
{
    std::cout << "Component - memTotal: " << memTotal << ", maxSingle: " << maxSingle << ", multiRobust: " << multiRobust << "\n";
}
