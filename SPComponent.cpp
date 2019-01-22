#include "SeriesParallelDAG.h"
#include <algorithm>

template <typename T>
Nullable<T> operator+(T a, const Nullable<T>& b) {
    return Nullable<T>(a).operator+(b);
}

template <typename T>
std::ostream & operator<<(std::ostream & os, const Nullable<T>  & obj) {
    if (obj.HasValue())
        os << obj.GetValue();
    else
        os << "null";
    return os;
}

template <typename T>
Nullable<T> NullMax(const Nullable<T>& a, const Nullable<T> &b) {
    return a.Max(b);
}

template <typename T>
Nullable<T> NullMax(const Nullable<T>& a, const  Nullable<T>& b, const Nullable<T>& c) {
    return NullMax(a, b).Max(c);
}

template <typename T>
Nullable<T> NullMax(const Nullable<T>& a, const Nullable<T> &b, const Nullable<T>& c, const Nullable<T> &d) {
    return NullMax(a, b, c).Max(d);
}

using NullableT = Nullable<int64_t>;

void SPComponent::CombineSeries(const SPComponent & other) {
    memTotal = memTotal + other.memTotal;
    maxSingle = std::max(maxSingle, memTotal + other.maxSingle);
    multiRobust = NullMax(multiRobust, memTotal + other.multiRobust);
}

void SPComponent::CombineParallel(const SPComponent & other, int64_t threshold) {
    memTotal = memTotal + other.memTotal;
    maxSingle = std::max(maxSingle + std::max((int64_t)0, other.memTotal), other.maxSingle + std::max((int64_t)0, memTotal));

    NullableT c1MaxSingleBar = maxSingle > threshold ? maxSingle : NullableT();
    NullableT c2MaxSingleBar = other.maxSingle > threshold ? other.maxSingle : NullableT();

    multiRobust = NullMax(
        c1MaxSingleBar + c2MaxSingleBar,
        NullMax(c1MaxSingleBar, multiRobust, NullableT(memTotal), NullableT(0)) + other.multiRobust,
        NullMax(c2MaxSingleBar, other.multiRobust, NullableT(other.memTotal), NullableT(0)) + multiRobust);
}

int64_t SPComponent::GetWatermark(int64_t threshold) {
    auto nullableWatermark = NullMax(maxSingle > threshold ? NullableT(maxSingle) : NullableT(), multiRobust, NullableT(0));
    DEBUG_ASSERT(nullableWatermark.HasValue());

    return nullableWatermark.GetValue();
}

void SPComponent::Print() {
    std::cout << "Component - memTotal: " << memTotal << ", maxSingle: " << maxSingle << ", multiRobust: " << multiRobust << "\n";
}

void SPMultispawnComponent::IncrementOnContinuation(const SPComponent & continuation, int64_t threshold) {
    SPMultispawnComponent old = *this; // Make a copy of the current state.

    multiRobustSuspendEnd = old.multiRobustSuspendEnd + continuation.memTotal;
    singleSuspendEnd = old.singleSuspendEnd + continuation.memTotal;
    singleIgnoreEnd = NullMax(old.singleIgnoreEnd, NullableT(continuation.maxSingle + old.emptyTail));

    robustUnfinishedTail = old.robustUnfinishedTail + continuation.memTotal;
    runningMemTotal = old.runningMemTotal + continuation.memTotal;
    emptyTail = old.emptyTail + continuation.memTotal;

    if (old.robustUnfinishedTail + continuation.maxSingle > threshold && old.robustUnfinished.HasValue())
    {
        multiRobustIgnoreEnd = NullMax(multiRobustIgnoreEnd, old.robustUnfinished + old.robustUnfinishedTail + continuation.maxSingle);
    }

    if (continuation.multiRobust.HasValue())
    {
        if (old.robustUnfinished.HasValue())
        {
            multiRobustIgnoreEnd = NullMax(multiRobustIgnoreEnd, old.robustUnfinished + old.robustUnfinishedTail + continuation.multiRobust);
        }
        else
        {
            multiRobustIgnoreEnd = NullMax(multiRobustIgnoreEnd, old.robustUnfinishedTail + continuation.multiRobust);
        }
    }
}

void SPMultispawnComponent::IncrementOnSpawn(const SPComponent & spawn, int64_t threshold) {
    SPMultispawnComponent old = *this; // Make a copy of the current state.

    singleSuspendEnd = NullMax(singleSuspendEnd + spawn.memTotal, NullableT(spawn.maxSingle + old.emptyTail));
    singleIgnoreEnd = NullMax(singleIgnoreEnd, NullableT(spawn.maxSingle + old.emptyTail));
    runningMemTotal = old.runningMemTotal + spawn.memTotal;
    emptyTail = old.emptyTail + std::max(spawn.memTotal, int64_t(0));

    NullableT temp;
    if (spawn.maxSingle + old.robustUnfinishedTail > threshold && old.robustUnfinished.HasValue())
    {
        temp = old.robustUnfinished + spawn.maxSingle + old.robustUnfinishedTail;
    }

    if (spawn.multiRobust.HasValue())
    {
        if (old.robustUnfinished.HasValue())
        {
            temp = NullMax(temp, old.robustUnfinished + old.robustUnfinishedTail + spawn.multiRobust);
        }
        else
        {
            temp = NullMax(temp, old.robustUnfinishedTail + spawn.multiRobust);
        }
    }

    multiRobustSuspendEnd = NullMax(temp, old.multiRobustSuspendEnd);
    multiRobustIgnoreEnd = NullMax(temp, old.multiRobustIgnoreEnd);

    NullableT m = spawn.multiRobust;
    if (spawn.maxSingle > threshold)
    {
        temp = NullMax(temp, NullableT(spawn.maxSingle));
    }

    int64_t t = spawn.memTotal;
    if (t > 0 && m <= (t + threshold))
    {
        robustUnfinishedTail = old.robustUnfinishedTail + t;
    }

    if (m >= (std::max(int64_t(0), t) + threshold))
    {
        robustUnfinished = NullMax(NullableT(0), old.robustUnfinished) + old.robustUnfinishedTail + m;
        robustUnfinishedTail = 0;
    }
}


SPComponent SPMultispawnComponent::ToComponent() {
    SPComponent component;

    component.memTotal = runningMemTotal;
    component.multiRobust = NullMax(multiRobustSuspendEnd, multiRobustIgnoreEnd);
    component.maxSingle = NullMax(singleIgnoreEnd, singleSuspendEnd).GetValue(); // TODO.

    return component;
}


void SPMultispawnComponent::Print() {
    std::cout << "Multispawn - runningMemTotal: " << runningMemTotal << ", singleSuspendEnd: " << singleSuspendEnd << ", singleIgnoreEnd: " << singleIgnoreEnd << ", multiRobustSuspendEnd: " << multiRobustSuspendEnd <<
        ", multiRobustIgnoreEnd: " << multiRobustIgnoreEnd << "\n";
}
