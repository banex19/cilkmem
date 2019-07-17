#include "SeriesParallelDAG.h"
#include <algorithm>

SingleThreadPool SPArrayBasedComponent::memPool{};

template <typename T>
Nullable<T> operator+(T a, const Nullable<T>& b) {
    return Nullable<T>(a).operator+(b);
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const Nullable<T>& obj) {
    if (obj.HasValue())
        os << obj.GetValue();
    else
        os << "null";
    return os;
}

template <typename T>
Nullable<T> NullMin(const Nullable<T>& a, const Nullable<T>& b) {
    return a.Min(b);
}

template <typename T>
Nullable<T> NullMax(const Nullable<T>& a, const Nullable<T>& b) {
    return a.Max(b);
}

template <typename T>
Nullable<T> NullMax(const Nullable<T>& a, const  Nullable<T>& b, const Nullable<T>& c) {
    return NullMax(a, b).Max(c);
}

template <typename T>
Nullable<T> NullMax(const Nullable<T>& a, const Nullable<T>& b, const Nullable<T>& c, const Nullable<T>& d) {
    return NullMax(a, b, c).Max(d);
}

void SourceMapPurge(SourceMap& target) {
    auto iter = target.begin();
    for (; iter != target.end(); ) {
        if (iter->second == 0) {
            iter = target.erase(iter);
        }
        else {
            ++iter;
        }
    }
}

SourceMap SourceMapCombine(SourceMap& target, const SourceMap& other) {
    SourceMap result = target;
    for (auto& keyVal : other) {
        result[keyVal.first] += keyVal.second;
        // int64_t val = keyVal.second;
        //auto it = result.find(keyVal.first);
        //if (it != result.end())
        //    it->second = it->second + val;
        //else result.insert(std::make_pair(keyVal.first, val));
    }

    DEBUG_ASSERT(result.size() >= target.size());

    SourceMapPurge(result);
    return result;
}



using NullableT = Nullable<int64_t>;

/* SP component functions */
void SPComponent::CombineSeries(const SPComponent & other) {
    if (trivial && other.trivial)
        return;

    SPComponent old = *this;
    memTotal = old.memTotal + other.memTotal;
    maxSingle = std::max(old.maxSingle, old.memTotal + other.maxSingle);

    multiRobust = NullMax(old.multiRobust, other.multiRobust + old.memTotal);

    trivial = false;
}

void SPComponent::CombineParallel(const SPComponent & other, int64_t threshold) {
    if (trivial && other.trivial)
        return;

    SPComponent old = *this;
    memTotal = old.memTotal + other.memTotal;
    maxSingle = std::max(old.maxSingle + std::max((int64_t)0, other.memTotal), other.maxSingle + std::max((int64_t)0, old.memTotal));

    NullableT c1MaxSingleBar = old.maxSingle > threshold ? old.maxSingle : NullableT();
    NullableT c2MaxSingleBar = other.maxSingle > threshold ? other.maxSingle : NullableT();

    multiRobust = NullMax(
        c1MaxSingleBar + c2MaxSingleBar,
        NullMax(c1MaxSingleBar, old.multiRobust, NullableT(old.memTotal), NullableT(0)) + other.multiRobust,
        NullMax(c2MaxSingleBar, other.multiRobust, NullableT(other.memTotal), NullableT(0)) + old.multiRobust);

    trivial = false;
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
    if (continuation.trivial)
        return;

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
    if (spawn.trivial)
        return;

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
    component.maxSingle = NullMax(singleIgnoreEnd, singleSuspendEnd).GetValue();

    component.trivial = false;

    return component;
}


void SPMultispawnComponent::Print() {
    std::cout << "Multispawn - runningMemTotal: " << runningMemTotal <<
        ", singleSuspendEnd: " << singleSuspendEnd << ", singleIgnoreEnd: " <<
        singleIgnoreEnd << ", multiRobustSuspendEnd: " << multiRobustSuspendEnd <<
        ", multiRobustIgnoreEnd: " << multiRobustIgnoreEnd << "\n";
}

// SPNaiveComponent::SPNaiveComponent(const SPEdgeData& edge, size_t p)

void SPNaiveComponent::CombineParallel(const SPNaiveComponent & other) {
    if (trivial && other.trivial)
        return;

    NullableT* temp = AllocateArray(p + 1);
    memcpy(temp, r, sizeof(NullableT) * (p + 1));

#ifdef USE_BACKTRACE
    SourceMap * tempMaps = new SourceMap[p + 1];
    for (size_t i = 0; i < p + 1; ++i) {
        tempMaps[i] = rSourceMaps[i];
    }
#endif

    for (size_t i = 0; i <= maxPos; ++i)
    {
        DEBUG_ASSERT_EX(temp[i].HasValue(), "Element %zu is null but maxPos is %zu", i, maxPos);
    }
    for (size_t i = maxPos + 1; i < p + 1; ++i)
    {
        DEBUG_ASSERT_EX(!temp[i].HasValue(), "Element %zu is not null but maxPos is %zu", i, maxPos);
    }

    int64_t oldMemTotal = memTotal;

    memTotal = memTotal + other.memTotal;
    r[0] = std::max((int64_t)0, memTotal);

#ifdef USE_BACKTRACE
    memTotalSourceMap = SourceMapCombine(memTotalSourceMap, other.memTotalSourceMap);


    if (r[0].GetValue() != 0)
        rSourceMaps[0] = memTotalSourceMap;
    else rSourceMaps[0] = SourceMap();
#endif

    for (size_t i = 1; i < p + 1; ++i)
    {
        NullableT max;
        bool anyNonNull = false;
        size_t bestJ = 0;

        size_t j = std::max((int64_t)0, (int64_t)i - (int64_t)other.maxPos);
        size_t jMax = std::min(i, maxPos);
        for (; j <= jMax; ++j)
            // for (size_t j = 0; j <= i; ++j)
        {
            NullableT term = temp[j] + other.r[i - j];
            if (term.HasValue())
            {
                anyNonNull = true;

                if (!max.HasValue() || max.GetValue() < NullMax(max, term).GetValue()) {
                    bestJ = j;
                }

                max = NullMax(max, term);
            }
        }


        if (anyNonNull)
        {
            r[i] = max;
#ifdef USE_BACKTRACE
            rSourceMaps[i] = SourceMapCombine(tempMaps[bestJ], other.rSourceMaps[i - bestJ]);
#endif
        }
        else
        {
            r[i] = NullableT();
#ifdef USE_BACKTRACE
            rSourceMaps[i] = SourceMap();
#endif
        }
    }

    /*  std::cout << "Combining parallel - G_1 (" << oldMemTotal << ") - maxPos: " << maxPos << ":\n";
      for (size_t i = 0; i < p + 1; ++i)
      {
          std::cout << "R[" << i << "]: " << temp[i] << ",  ";
      }

      std::cout << "\n";

      std::cout << "Combining parallel - G_2 (" << other.memTotal << ") - maxPos: " << other.maxPos << ":\n";
      for (size_t i = 0; i < p + 1; ++i)
      {
          std::cout << "R[" << i << "]: " << other.r[i] << ",  ";
      }

      std::cout << "\n";

      std::cout << "Combining parallel - result (" << memTotal << ") - maxPos: " << maxPos << ":\n";
      for (size_t i = 0; i < p + 1; ++i)
      {
          std::cout << "R[" << i << "]: " << r[i] << ",  ";
      }

      std::cout << "\n";  */

    maxPos = std::min(p, maxPos + other.maxPos);

    FreeArray(temp);

#ifdef USE_BACKTRACE
    delete[] tempMaps;
#endif

    trivial = false;
}

void SPNaiveComponent::CombineSeries(const SPNaiveComponent & other) {
    if (trivial && other.trivial)
        return;

    NullableT* temp = AllocateArray(p + 1);
    memcpy(temp, r, sizeof(NullableT) * (p + 1));

    int64_t oldMemTotal = memTotal;

    if (maxPos > 0)
    {
        /*    std::cout << "Combining series - G_1 (" << oldMemTotal << ") - maxPos: " << maxPos << ":\n";
            for (size_t i = 0; i < p + 1; ++i)
            {
                std::cout << "R[" << i << "]: " << temp[i] << ",  ";
            }

            std::cout << "\n";

            std::cout << "Combining series -  G_2 (" << other.memTotal << ") - maxPos: " << other.maxPos << ":\n";
            for (size_t i = 0; i < p + 1; ++i)
            {
                std::cout << "R[" << i << "]: " << other.r[i] << ",  ";
            }

            std::cout << "\n"; */
    }

    for (size_t i = 0; i <= maxPos; ++i)
    {
        DEBUG_ASSERT_EX(temp[i].HasValue(), "Element %zu is null but maxPos is %zu", i, maxPos);
    }
    for (size_t i = maxPos + 1; i < p + 1; ++i)
    {
        DEBUG_ASSERT_EX(!temp[i].HasValue(), "Element %zu is not null but maxPos is %zu", i, maxPos);
    }

    memTotal = oldMemTotal + other.memTotal;
    r[0] = std::max((int64_t)0, memTotal);
    
    
#ifdef USE_BACKTRACE
    SourceMap oldMemTotalSourceMap = memTotalSourceMap;
    memTotalSourceMap = SourceMapCombine(memTotalSourceMap, other.memTotalSourceMap);
 
    if (r[0].GetValue() != 0)
        rSourceMaps[0] = memTotalSourceMap;
    else rSourceMaps[0] = SourceMap();
#endif

    for (size_t i = 1; i < p + 1; ++i)
    {
        NullableT term = NullMax(temp[i], other.r[i] + oldMemTotal);

        r[i] = term;

#ifdef USE_BACKTRACE
        if (term != temp[i])
            rSourceMaps[i] = SourceMapCombine(other.rSourceMaps[i], oldMemTotalSourceMap);
#endif

    }


    maxPos = std::max(maxPos, other.maxPos);

    /*   std::cout << "Combining series - result (" << memTotal << ") - maxPos: " << maxPos << ":\n";
       for (size_t i = 0; i < p + 1; ++i)
       {
           std::cout << "R[" << i << "]: " << r[i] << ",  ";
       }

       std::cout << "\n";   */

    FreeArray(temp);

    trivial = false;
}

int64_t SPNaiveComponent::GetWatermark(size_t watermarkP) {
    DEBUG_ASSERT_EX(watermarkP <= p, "Requested watermark for p = %zu but the algorithm ran on p = %zu", watermarkP, p);

    NullableT watermark = r[0];

    for (size_t i = 1; i < watermarkP + 1; ++i)
    {
        watermark = NullMax(watermark, r[i]);
    }

    DEBUG_ASSERT(watermark.HasValue());

    return watermark.GetValue();
}


void SPNaiveMultispawnComponent::IncrementOnContinuation(const SPNaiveComponent & continuation) {
    if (continuation.trivial)
        return;


    for (size_t i = 0; i <= maxPos; ++i)
    {
        DEBUG_ASSERT(partial[i].HasValue());
    }
    for (size_t i = maxPos + 1; i < p + 1; ++i)
    {
        DEBUG_ASSERT(!partial[i].HasValue());
    }

    for (size_t i = 1; i < p + 1; ++i)
    {
        suspendEnd[i] = suspendEnd[i] + continuation.memTotal;

        NullableT maxPartial;
        size_t j = std::max((int64_t)1, (int64_t)(i - maxPos));
        size_t jMax = std::min((int64_t)continuation.maxPos, (int64_t)i);
        for (; j <= jMax; ++j)
        {
            maxPartial = NullMax(maxPartial, partial[i - j] + continuation.r[j]);
        }

        ignoreEnd[i] = NullMax(ignoreEnd[i], maxPartial);
    }

    for (size_t i = 0; i < p + 1; ++i)
    {
        partial[i] = partial[i] + continuation.memTotal;
    }

    /* std::cout << "Incrementing on continuation - result (" << memTotal << "):\n";
    for (size_t i = 0; i < p + 1; ++i)
    {
        std::cout << "Partial[" << i << "]: " << partial[i] << ",  ";
    }

    std::cout << "\n"; */

    memTotal += continuation.memTotal;
}

void SPNaiveMultispawnComponent::IncrementOnSpawn(const SPNaiveComponent & spawn) {
    if (spawn.trivial)
        return;

    NullableT* oldPartial = AllocateArray(p + 1);
    memcpy(oldPartial, partial, sizeof(NullableT) * (p + 1));

    for (size_t i = 0; i <= maxPos; ++i)
    {
        DEBUG_ASSERT(partial[i].HasValue());
    }
    for (size_t i = maxPos + 1; i < p + 1; ++i)
    {
        DEBUG_ASSERT(!partial[i].HasValue());
    }

    size_t oldMaxPos = maxPos;
    maxPos = 0;

    for (size_t i = 1; i < p + 1; ++i)
    {
        NullableT maxPartial;
        size_t j = std::max((int64_t)1, (int64_t)(i - oldMaxPos));
        size_t jMax = std::min((int64_t)spawn.maxPos, (int64_t)i);
        for (; j <= jMax; ++j)
        {
            maxPartial = NullMax(maxPartial, oldPartial[i - j] + spawn.r[j]);
        }


        suspendEnd[i] = NullMax(suspendEnd[i] + spawn.memTotal, maxPartial);
        ignoreEnd[i] = NullMax(ignoreEnd[i], maxPartial);
        partial[i] = NullMax(oldPartial[i] + spawn.r[0], maxPartial);

        if (partial[i].HasValue())
            maxPos = i;
    }

    partial[0] = partial[0] + spawn.r[0];

    /*  std::cout << "Incrementing on spawn - result (" << memTotal << "):\n";
      for (size_t i = 0; i < p + 1; ++i)
      {
          std::cout << "Partial[" << i << "]: " << partial[i] << ",  ";
      }

      std::cout << "\n"; */

    memTotal += spawn.memTotal;

    FreeArray(oldPartial);
}

SPNaiveComponent SPNaiveMultispawnComponent::ToComponent() {
    SPNaiveComponent component(SPEdgeData(), p);

    component.memTotal = memTotal;

    component.r[0] = std::max(memTotal, (int64_t)0);

    component.maxPos = p;
    for (size_t i = 1; i < p + 1; ++i)
    {
        component.r[i] = NullMax(suspendEnd[i], ignoreEnd[i]);
        if (!component.r[i].HasValue())
        {
            component.maxPos = i - 1;
            break;
        }
    }

    component.trivial = false;

    return component;
}
