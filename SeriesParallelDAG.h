#pragma once
#include <vector>
#include <iostream>
#include <deque>
#include <unordered_map>
#include <cstdint>
#include "common.h"
#include "MemPoolVector.h"
#include "SingleThreadPool.h"
#include "Nullable.h"

struct SPNode;
class SPEdgeProducer;
class SPEventBareboneOnlineProducer;



struct SPEdgeData {
    int64_t memAllocated = 0;
    int64_t maxMemAllocated = 0;

    bool operator==(const SPEdgeData& other) const {
        return memAllocated == other.memAllocated;
    }

    bool IsTrivial() {
        return memAllocated == 0 && maxMemAllocated == 0;
    }
};

struct SPComponent {
    int64_t memTotal = 0;
    int64_t maxSingle = 0;
    Nullable<int64_t> multiRobust;

    SPComponent() {}

    SPComponent(const SPEdgeData& edge) {
        memTotal = edge.memAllocated;
        maxSingle = edge.maxMemAllocated;
    }

    void CombineSeries(const SPComponent& other);

    void CombineParallel(const SPComponent& other, int64_t threshold);

    int64_t GetWatermark(int64_t threshold);

    void Print();
};

struct SPMultispawnComponent {
    Nullable<int64_t> multiRobustSuspendEnd;
    Nullable<int64_t> multiRobustIgnoreEnd;
    Nullable<int64_t> singleSuspendEnd;
    Nullable<int64_t> singleIgnoreEnd;
    Nullable<int64_t> robustUnfinished;
    int64_t robustUnfinishedTail = 0;
    int64_t runningMemTotal = 0;
    int64_t emptyTail = 0;

    void IncrementOnContinuation(const SPComponent& continuation, int64_t threshold);
    void IncrementOnSpawn(const SPComponent& spawn, int64_t threshold);

    void Print();

    SPComponent ToComponent();
};

class SPArrayBasedComponent {

protected:
    Nullable<int64_t>* AllocateArray(size_t size) {
        if (!memPool.IsInitialized())
            memPool.Initialize(sizeof(Nullable<int64_t>) * size, 5000);

        auto arr =  (Nullable<int64_t>*)  memPool.Allocate();
        for (size_t i = 0; i < size; ++i)
            arr[i].SetNull();

        return arr;
    }

    void FreeArray(Nullable<int64_t>* arr) { if (arr != nullptr) memPool.Free(arr); }
    static SingleThreadPool memPool;
};

struct SPNaiveComponent  : public SPArrayBasedComponent {
    SPNaiveComponent(const SPNaiveComponent& other) = delete;

    SPNaiveComponent(size_t p) :p(p) {
        r = AllocateArray(p + 1);
        r[0] = 0;
        maxPos = 0;
    }

    void MoveOther(SPNaiveComponent&& other) {
        FreeArray(r);

        p = other.p;
        memTotal = other.memTotal;
        maxPos = other.maxPos;
        r = other.r;

        other.r = nullptr;
    }

    SPNaiveComponent(SPNaiveComponent&& other) {
        MoveOther(std::move(other));
    }

    SPNaiveComponent& operator=(const SPNaiveComponent& other) = delete;
    SPNaiveComponent& operator=(SPNaiveComponent&& other) {
        MoveOther(std::move(other));
        return *this;
    }

    SPNaiveComponent(const SPEdgeData& edge, size_t p) {
        trivial = edge.IsTrivial();

        this->p = p;
        r = AllocateArray(p + 1);

        memTotal = edge.memAllocated;

        r[0] = std::max((int64_t)0, edge.memAllocated);
        r[1] = edge.maxMemAllocated;

        for (size_t i = 2; i < p + 1; ++i)
        {
            DEBUG_ASSERT(r[i] == Nullable<int64_t>());
        }

        maxPos = 1;
    }

    ~SPNaiveComponent() {
        FreeArray(r);
    }

    void CombineParallel(const SPNaiveComponent& other);
    void CombineSeries(const SPNaiveComponent& other);

    int64_t GetWatermark(size_t watermarkP);

    size_t maxPos = 1;
    size_t p = 0;
    int64_t memTotal = 0;
    Nullable<int64_t>* r = nullptr;
    bool trivial = false;

};

struct SPNaiveMultispawnComponent : public SPArrayBasedComponent{
    SPNaiveMultispawnComponent(const SPNaiveMultispawnComponent& other) = delete;

    SPNaiveMultispawnComponent(SPNaiveMultispawnComponent&& other) {
        p = other.p;
        memTotal = other.memTotal;

        suspendEnd = other.suspendEnd;
        ignoreEnd = other.ignoreEnd;
        partial = other.partial;

        other.suspendEnd = nullptr;
        other.ignoreEnd = nullptr;
        other.partial = nullptr;
    }

    SPNaiveMultispawnComponent& operator=(const SPNaiveMultispawnComponent& other) = delete;
    SPNaiveMultispawnComponent& operator=(const SPNaiveMultispawnComponent&& other) = delete;

    SPNaiveMultispawnComponent(size_t p) : p(p) {
        suspendEnd = AllocateArray(p + 1);
        ignoreEnd = AllocateArray(p + 1);
        partial = AllocateArray(p + 1);

        partial[0] = 0;
    }

    ~SPNaiveMultispawnComponent() {
        FreeArray(suspendEnd);
        FreeArray(ignoreEnd);
        FreeArray(partial);
    }


    void IncrementOnContinuation(const SPNaiveComponent& continuation);
    void IncrementOnSpawn(const SPNaiveComponent& spawn);

    SPNaiveComponent ToComponent();

    size_t p;
    int64_t memTotal = 0;
    size_t maxPos = 0;

    Nullable<int64_t>* suspendEnd;
    Nullable<int64_t>* ignoreEnd;
    Nullable<int64_t>* partial;
};

struct SPBareboneEdge {
    SPEdgeData data;
};

struct SPEdge : public SPBareboneEdge {
    size_t id;
    SPNode* from;
    SPNode* to;
    bool forward;
    bool spawn;

    bool operator==(const SPEdge& other) const {
        return from == other.from && to == other.to
            && data == other.data;
        ;
    }
};



struct SPNode {
    size_t id;
    std::vector<SPEdge*> successors;
    size_t numStrandsLeft = 2;
    SPNode* associatedSyncNode;

    char* locationName;
    int32_t locationLine;
};

struct SPLevel {
    SPNode* currentNode;
    std::vector<SPNode*> syncNodes;
    std::vector<size_t> functionLevels;
    std::vector<size_t> regionIds;

    SPLevel(size_t level, size_t regionId, SPNode* currentNode) : currentNode(currentNode) {
        functionLevels.push_back(level);
        regionIds.push_back(regionId);
    }

    void PushFunctionLevel(SPNode* syncNode, size_t functionLevel, size_t regionId) {
        syncNodes.push_back(syncNode);
        functionLevels.push_back(functionLevel);
        regionIds.push_back(regionId);
    }

    void PopFunctionLevel() {
        syncNodes.pop_back();
        functionLevels.pop_back();
        regionIds.pop_back();
    }
};

struct SPBareboneLevel {
    size_t regionId;
    size_t level;
    size_t remaining;

    SPBareboneLevel(size_t region, size_t level, size_t remaining) : regionId(region), level(level), remaining(remaining) {}
};

struct SPEvent {
    uint8_t spawn : 1;
    uint8_t newSync : 1;
};

class SPDAG {
public:
    SPDAG(OutputPrinter& outputPrinter) : out(outputPrinter) {}

    virtual ~SPDAG() {}

    virtual void Spawn(SPEdgeData &currentEdge, size_t regionId) = 0;
    virtual void Sync(SPEdgeData &currentEdge, size_t regionId) = 0;

    virtual SPComponent AggregateComponents(SPEdgeProducer* edgeProducer, SPEventBareboneOnlineProducer* eventProducer, int64_t threshold) = 0;
    virtual SPComponent AggregateComponentsEfficient(SPEdgeProducer* edgeProducer, SPEventBareboneOnlineProducer* eventProducer, int64_t threshold) = 0;

    virtual SPNaiveComponent AggregateComponentsNaive(SPEdgeProducer* edgeProducer, SPEventBareboneOnlineProducer* eventProducer, int64_t threshold, size_t p) = 0;
    virtual   SPNaiveComponent AggregateComponentsNaiveEfficient(SPEdgeProducer* edgeProducer, SPEventBareboneOnlineProducer* eventProducer, int64_t threshold, size_t p) = 0;


    virtual void Print() {}
    virtual void WriteDotFile(const std::string& filename) {}

    void IncrementLevel() { currentLevel++; }
    void DecrementLevel() { currentLevel--; }

    bool IsComplete() { return isComplete; }

    virtual void SetLastNodeLocation(char* name, int32_t line) {}

protected:
    size_t currentLevel = 0;

    volatile bool isComplete = false;

    OutputPrinter& out;
};

class FullSPDAG : public SPDAG {
public:
    FullSPDAG(OutputPrinter& outputPrinter) : SPDAG(outputPrinter) {}

    ~FullSPDAG() {
        for (auto& node : nodes)
            delete node;

        nodes.clear();
    }

    void Print();
    void WriteDotFile(const std::string& filename);

    void Spawn(SPEdgeData &currentEdge, size_t regionId);
    void Sync(SPEdgeData &currentEdge, size_t regionId);

    SPComponent AggregateComponents(SPEdgeProducer* edgeProducer, SPEventBareboneOnlineProducer* eventProducer, int64_t threshold);
    SPComponent AggregateComponentsEfficient(SPEdgeProducer* edgeProducer, SPEventBareboneOnlineProducer* eventProducer, int64_t threshold);

    SPNaiveComponent AggregateComponentsNaive(SPEdgeProducer* edgeProducer, SPEventBareboneOnlineProducer* eventProducer, int64_t threshold, size_t p);
    SPNaiveComponent AggregateComponentsNaiveEfficient(SPEdgeProducer* edgeProducer, SPEventBareboneOnlineProducer* eventProducer, int64_t threshold, size_t p);

    void SetLastNodeLocation(char* name, int32_t line) {
        lastNode->locationName = name;
        lastNode->locationLine = line;
    }

private:
    SPComponent AggregateMultispawn(SPEdgeProducer* edgeProducer, SPEdge* incomingEdge, SPNode* pivot, int64_t threshold);
    SPNaiveComponent AggregateMultispawnNaive(SPEdgeProducer* edgeProducer, SPEdge* incomingEdge, SPNode* pivot, int64_t threshold, size_t p);

    SPComponent AggregateComponentsFromNode(SPEdgeProducer* edgeProducer, SPNode* pivot, int64_t threshold);
    SPComponent AggregateUntilSync(SPEdgeProducer* edgeProducer, SPEdge* start, SPNode* syncNode, int64_t threshold);

    SPNaiveComponent AggregateComponentsFromNodeNaive(SPEdgeProducer* edgeProducer, SPNode* pivot, int64_t threshold, size_t p);
    SPNaiveComponent AggregateUntilSyncNaive(SPEdgeProducer* edgeProducer, SPEdge* start, SPNode* syncNode, int64_t threshold, size_t p);

    SPNode* AddNode() { SPNode* newNode = new SPNode(); newNode->id = nodes.size(); nodes.push_back(newNode); return newNode; }

    SPEdge* AddEdge(SPNode* from, SPNode* succ, const SPEdgeData &data, bool spawn = false) {
        SPEdge* newEdge = (SPEdge*) memPool.Allocate();
        newEdge->id = edges.size();

        newEdge->from = from;
        newEdge->to = succ;
        newEdge->data = data;
        newEdge->forward = true;
        newEdge->spawn = spawn;
        from->successors.push_back(newEdge);

        out << "Adding edge " << from->id << " --> " << succ->id << "\n";

        edges.push_back(newEdge);
        return newEdge;
    }


    SPLevel* GetParentLevel() { if (currentStack.size() > 0) return currentStack[currentStack.size() - 1]; else return nullptr; }

    std::vector<SPNode*> nodes;
    MemPoolVector<SPEdge*> edges;

    std::vector<SPLevel*> currentStack;
    SPNode* lastNode;
    SPNode* firstNode;

    bool afterSpawn = false;

    friend class SPEdgeFullOnlineProducer;
    friend class SPNode;

    SingleThreadPool memPool{ sizeof(SPEdge), 5000 };
};

class BareboneSPDAG : public SPDAG {
public:
    BareboneSPDAG(OutputPrinter& outputPrinter) : SPDAG(outputPrinter) {
    }

    void Spawn(SPEdgeData &currentEdge, size_t regionId);
    void Sync(SPEdgeData &currentEdge, size_t regionId);

    SPComponent AggregateComponents(SPEdgeProducer* edgeProducer, SPEventBareboneOnlineProducer* eventProducer, int64_t threshold);
    SPComponent AggregateComponentsEfficient(SPEdgeProducer* edgeProducer, SPEventBareboneOnlineProducer* eventProducer, int64_t threshold);

    SPNaiveComponent AggregateComponentsNaive(SPEdgeProducer* edgeProducer, SPEventBareboneOnlineProducer* eventProducer, int64_t threshold, size_t p);
    SPNaiveComponent AggregateComponentsNaiveEfficient(SPEdgeProducer* edgeProducer, SPEventBareboneOnlineProducer* eventProducer, int64_t threshold, size_t p);

private:
    SPComponent AggregateComponentsSpawn(SPEdgeProducer* edgeProducer, SPEventBareboneOnlineProducer* eventProducer, int64_t threshold);
    SPComponent AggregateUntilSync(SPEdgeProducer* edgeProducer, SPEventBareboneOnlineProducer* eventProducer, bool continuation, int64_t threshold);

    SPNaiveComponent AggregateComponentsSpawnNaive(SPEdgeProducer* edgeProducer, SPEventBareboneOnlineProducer* eventProducer, int64_t threshold, size_t p);
    SPNaiveComponent AggregateUntilSyncNaive(SPEdgeProducer* edgeProducer, SPEventBareboneOnlineProducer* eventProducer, bool continuation, int64_t threshold, size_t p);

    SPComponent AggregateComponentsMultispawn(SPEdgeProducer* edgeProducer, SPEventBareboneOnlineProducer* eventProducer, int64_t threshold);

    SPNaiveComponent AggregateComponentsMultispawnNaive(SPEdgeProducer* edgeProducer, SPEventBareboneOnlineProducer* eventProducer, int64_t threshold, size_t p);

    SPBareboneEdge* AddEdge(const SPEdgeData& data) { SPBareboneEdge* edge = (SPBareboneEdge*) memPool.Allocate(); edge->data = data; return edge; }

    std::deque<SPBareboneLevel> stack;

    MemPoolVector<SPEvent> events;
    MemPoolVector<SPBareboneEdge*> edges;

    bool afterSpawn = false;
    bool spawnedAtLeastOnce = false;


    friend class SPEdgeBareboneOnlineProducer;
    friend class SPEventBareboneOnlineProducer;

    SingleThreadPool memPool{ sizeof(SPBareboneEdge), 5000 };

};