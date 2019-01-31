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
Nullable<T> NullMin(const Nullable<T>& a, const Nullable<T> &b) {
    return a.Min(b);
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

/* SP component functions */
void SPComponent::CombineSeries(const SPComponent & other) {
    SPComponent old = *this;
    memTotal = old.memTotal + other.memTotal;
    maxSingle = std::max(old.maxSingle, old.memTotal + other.maxSingle);

    multiRobust = NullMax(old.multiRobust, other.multiRobust + old.memTotal);
}

void SPComponent::CombineParallel(const SPComponent & other, int64_t threshold) {
    SPComponent old = *this;
    memTotal = old.memTotal + other.memTotal;
    maxSingle = std::max(old.maxSingle + std::max((int64_t)0, other.memTotal), other.maxSingle + std::max((int64_t)0, old.memTotal));

    NullableT c1MaxSingleBar = old.maxSingle > threshold ? old.maxSingle : NullableT();
    NullableT c2MaxSingleBar = other.maxSingle > threshold ? other.maxSingle : NullableT();

    multiRobust = NullMax(
        c1MaxSingleBar + c2MaxSingleBar,
        NullMax(c1MaxSingleBar, old.multiRobust, NullableT(old.memTotal), NullableT(0)) + other.multiRobust,
        NullMax(c2MaxSingleBar, other.multiRobust, NullableT(other.memTotal), NullableT(0)) + old.multiRobust);
}

int64_t SPComponent::GetWatermark(int64_t threshold) {
    auto nullableWatermark = NullMax(
        maxSingle > threshold ? NullableT(maxSingle) : NullableT(),
        multiRobust,
        NullableT(0));
    DEBUG_ASSERT(nullableWatermark.HasValue());

    return nullableWatermark.GetValue();
}

void SPComponent::Print() {
    std::cout << "Component - memTotal: " << memTotal << ", maxSingle: " << maxSingle << ", multiRobust: " << multiRobust << "\n";
}

/* Multispawn component functions */
void SPMultispawnComponent::IncrementOnContinuation(const SPComponent & continuation, int64_t threshold) {
    SPMultispawnComponent old = *this; // Make a copy of the current state.

    multiRobustSuspendEnd = old.multiRobustSuspendEnd + continuation.memTotal;
    singleSuspendEnd = old.singleSuspendEnd + continuation.memTotal;
    singleIgnoreEnd = NullMax(old.singleIgnoreEnd, NullableT(continuation.maxSingle + old.emptyTail));

    robustUnfinishedTail = old.robustUnfinishedTail + continuation.memTotal;
    runningMemTotal = old.runningMemTotal + continuation.memTotal;
    emptyTail = old.emptyTail + continuation.memTotal;

    if ((old.robustUnfinishedTail + continuation.maxSingle) > threshold && old.robustUnfinished.HasValue())
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

    singleSuspendEnd = NullMax(old.singleSuspendEnd + spawn.memTotal, NullableT(spawn.maxSingle + old.emptyTail));
    singleIgnoreEnd = NullMax(old.singleIgnoreEnd, NullableT(spawn.maxSingle + old.emptyTail));
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

    NullableT nullableM = spawn.multiRobust;
    if (spawn.maxSingle > threshold)
    {
        nullableM = NullMax(nullableM, NullableT(spawn.maxSingle));
    }

    int64_t m = 0;
    if (nullableM.HasValue())
        m = nullableM.GetValue();

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
    std::cout << "Multispawn - runningMemTotal: " << runningMemTotal <<
        ", singleSuspendEnd: " << singleSuspendEnd << ", singleIgnoreEnd: " <<
        singleIgnoreEnd << ", multiRobustSuspendEnd: " << multiRobustSuspendEnd <<
        ", multiRobustIgnoreEnd: " << multiRobustIgnoreEnd << "\n";
}


void SPNaiveComponent::CombineParallel(const SPNaiveComponent& other) {
    NullableT* temp = new NullableT[p + 1];
    memcpy(temp, r, sizeof(NullableT) * (p + 1));

    for (size_t i = 0; i <= maxPos; ++i)
    {
        DEBUG_ASSERT(temp[i].HasValue());
    }
    for (size_t i = maxPos + 1; i < p + 1; ++i)
    {
        DEBUG_ASSERT(!temp[i].HasValue());
    }

    int64_t oldMemTotal = memTotal;

    memTotal = memTotal + other.memTotal;

    r[0] = std::min((int64_t)0, memTotal);

    for (size_t i = 1; i < p + 1; ++i)
    {
        NullableT sum;
        bool anyNonNull = false;

        size_t j = std::max((int64_t)0, (int64_t)i - (int64_t)other.maxPos);
        size_t jMax = std::min(i, maxPos);
        for (; j <= jMax; ++j)
            // for (size_t j = 0; j <= i; ++j)
        {
            NullableT term = temp[j] + other.r[i - j];
            if (term.HasValue())
            {
                anyNonNull = true;
                sum = NullMax(sum, term);
            }
        }


        if (anyNonNull)
            r[i] = sum;
        else
            r[i] = NullableT();
    }

   /* std::cout << "Combining parallel - G_1 (" << oldMemTotal << "):\n";
    for (size_t i = 0; i < p + 1; ++i)
    {
        std::cout << "R[" << i << "]: " << temp[i] << ",  ";
    }

    std::cout << "\n";

    std::cout << "Combining parallel - G_2 ( " << other.memTotal << "):\n";
    for (size_t i = 0; i < p + 1; ++i)
    {
        std::cout << "R[" << i << "]: " << other.r[i] << ",  ";
    }

    std::cout << "\n";

    std::cout << "Combining parallel - result (" << memTotal << "):\n";
    for (size_t i = 0; i < p + 1; ++i)
    {
        std::cout << "R[" << i << "]: " << r[i] << ",  ";
    }

    std::cout << "\n"; */

    maxPos = maxPos + other.maxPos;

    delete[] temp;
}

void SPNaiveComponent::CombineSeries(const SPNaiveComponent & other) {
    NullableT* temp = new NullableT[p + 1];
    memcpy(temp, r, sizeof(NullableT) * (p + 1));


    for (size_t i = 0; i <= maxPos; ++i)
    {
        DEBUG_ASSERT(temp[i].HasValue());
    }
    for (size_t i = maxPos + 1; i < p + 1; ++i)
    {
        DEBUG_ASSERT(!temp[i].HasValue());
    }

    int64_t oldMemTotal = memTotal;

    memTotal = oldMemTotal + other.memTotal;

    r[0] = std::min((int64_t)0, memTotal);

    for (size_t i = 1; i < p + 1; ++i)
    {
        NullableT term = NullMax(temp[i], other.r[i] + oldMemTotal);
        r[i] = term;
    }

/*    std::cout << "Combining series - G_1 (" << oldMemTotal << "):\n";
    for (size_t i = 0; i < p + 1; ++i)
    {
        std::cout << "R[" << i << "]: " << temp[i] << ",  ";
    }

    std::cout << "\n";

    std::cout << "Combining series -  G_2 (" << other.memTotal << "):\n";
    for (size_t i = 0; i < p + 1; ++i)
    {
        std::cout << "R[" << i << "]: " << other.r[i] << ",  ";
    }

    std::cout << "\n";

    std::cout << "Combining series - result (" << memTotal << "):\n";
    for (size_t i = 0; i < p + 1; ++i)
    {
        std::cout << "R[" << i << "]: " << r[i] << ",  ";
    }

    std::cout << "\n";*/

    maxPos = std::max(maxPos, other.maxPos); 

    delete[] temp;
}

int64_t SPNaiveComponent::GetWatermark() {
    NullableT watermark = r[0];

    for (size_t i = 1; i < p + 1; ++i)
    {
        watermark = NullMax(watermark, r[i]);
    }

    DEBUG_ASSERT(watermark.HasValue());

    return watermark.GetValue();

}
