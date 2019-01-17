#pragma once
#include <vector>
#include <iostream>
#include <unordered_map>
#include <cstdint>
#include "common.h"
#include "MemPoolVector.h"


constexpr bool debugVerbose = false;

struct SPNode;
class SPEdgeProducer;

template <typename T>
class Nullable {
public:
    Nullable(T val) : value(val) {}
    Nullable() : Nullable(NULL_VALUE) {}

    Nullable<T> operator+(const Nullable<T>& other) const {
        if (!HasValue() || !other.HasValue())
            return NULL_VALUE;
        else return Nullable<T>(value + other.value);
    }

    template <typename U>
    friend std::ostream& operator<< (std::ostream & os, const Nullable<U>  & obj);

    Nullable<T> Max(const Nullable<T>& other) const {
        if (!HasValue())
            return other;
        else if (!other.HasValue())
            return *this;
        else return Nullable<T>(std::max(value, other.value));
    }

    bool HasValue() const { return value != NULL_VALUE; }
    T GetValue() const { return value; }

    static T NULL_VALUE;

private:
    T value;
};


struct SPEdgeData {
    int64_t memAllocated = 0;
    int64_t maxMemAllocated = 0;

    bool operator==(const SPEdgeData& other) const {
        return memAllocated == other.memAllocated;
    }
};

struct SPComponent {
    int64_t memTotal = 0;
    int64_t maxSingle = 0;
    Nullable<int64_t> multiRobust = 0;

    SPComponent() {}

    SPComponent(const SPEdgeData& edge) {
        memTotal = edge.memAllocated;
        maxSingle = edge.maxMemAllocated;
        multiRobust = 0;
    }

    void CombineSeries(const SPComponent& other);

    void CombineParallel(const SPComponent& other, int64_t threshold);

    int64_t GetWatermark(int64_t threshold);

    void Print();
};

struct SPEdge {
    size_t id;
    SPNode* from;
    SPNode* to;
    SPEdgeData data;
    bool forward;
    bool spawn;

    bool operator==(const SPEdge& other) const
    {
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
};

struct SPLevel {
    SPNode* currentNode;
    std::vector<SPNode*> syncNodes;
    std::vector<size_t> functionLevels;
    std::vector<size_t> regionIds;

    SPLevel(size_t level, size_t regionId, SPNode* currentNode) : currentNode(currentNode)
    {
        functionLevels.push_back(level);
        regionIds.push_back(regionId);
    }

    void PushFunctionLevel(SPNode* syncNode, size_t functionLevel, size_t regionId)
    {
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

class SPDAG {
public:
    SPDAG() {}

    ~SPDAG() {
        for (auto& node : nodes)
            delete node;

        for (size_t i = 0; i < edges.size(); ++i)
        {
            SPEdge* edge = edges[i];
            delete edge;
        }

        nodes.clear();
        edges.clear();
    }

    void Spawn(SPEdgeData &currentEdge, size_t regionId);
    void Sync(SPEdgeData &currentEdge, size_t regionId);

    SPComponent AggregateComponents(SPEdgeProducer* edgeProducer, int64_t threshold);

    bool IsComplete() { return isComplete; }

    void IncrementLevel() { currentLevel++; }
    void DecrementLevel() { currentLevel--; }

    void Print();
    void WriteDotFile(const std::string& filename);

private:
    SPComponent AggregateComponentsFromNode(SPEdgeProducer* edgeProducer, SPNode* pivot, int64_t threshold);
    SPComponent AggregateUntilSync(SPEdgeProducer* edgeProducer, SPEdge* start, SPNode* syncNode, int64_t threshold);

    SPNode* AddNode() { SPNode* newNode = new SPNode(); newNode->id = nodes.size(); nodes.push_back(newNode); return newNode; }

    SPEdge* AddEdge(SPNode* from, SPNode* succ, const SPEdgeData &data, bool spawn = false) {
        SPEdge* newEdge = new SPEdge();
        newEdge->id = edges.size();

        newEdge->from = from;
        newEdge->to = succ;
        newEdge->data = data;
        newEdge->forward = true;
        newEdge->spawn = spawn;
        from->successors.push_back(newEdge);

        if (debugVerbose)
            std::cout << "Adding edge " << from->id << " --> " << succ->id << "\n";

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
    size_t currentLevel = 0;

    bool isComplete = false;

    friend class SPEdgeOfflineProducer;
    friend class SPEdgeOnlineProducer;
    friend class SPNode;
};
