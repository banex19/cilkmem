#pragma once
#include <vector>
#include <iostream>
#include <unordered_map>
#include <cstdint>
#include <cassert>

struct SPNode;

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
    friend std::ostream& operator<< (std::ostream & os,  const Nullable<U>  & obj);

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

    void AddSuccessor(SPEdge* newEdgeForward, SPNode* succ, const SPEdgeData &data, bool spawn = false) {
        newEdgeForward->from = this;
        newEdgeForward->to = succ;
        newEdgeForward->data = data;
        newEdgeForward->forward = true;
        newEdgeForward->spawn = spawn;
        successors.push_back(newEdgeForward);

        std::cout << "Adding edge " << this->id << " --> " << succ->id << "\n";
    }
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
        for (auto & edge : edges)
            delete edge;

        nodes.clear();
        edges.clear();
    }

    void Spawn(SPEdgeData &currentEdge, size_t regionId);
    void Sync(SPEdgeData &currentEdge, size_t regionId);

    SPComponent AggregateComponents(int64_t threshold);

    void IncrementLevel() { currentLevel++; }
    void DecrementLevel() { currentLevel--; }

    void Print();
    void WriteDotFile(const std::string& filename);

private:

    SPComponent AggregateComponentsFromNode(SPNode* pivot, int64_t threshold);
    SPComponent AggregateUntilSync(SPEdge* start, SPNode* syncNode, int64_t threshold);

    SPNode* AddNode() { SPNode* newNode = new SPNode(); newNode->id = nodes.size(); nodes.push_back(newNode); return newNode; }
    SPEdge* AddEdge() { SPEdge* newEdge = new SPEdge(); newEdge->id = edges.size(); edges.push_back(newEdge); return newEdge; }

    SPLevel* GetParentLevel() { if (currentStack.size() > 0) return currentStack[currentStack.size() - 1]; else return nullptr; }

    std::vector<SPNode*> nodes;
    std::vector<SPEdge*> edges;

    std::vector<SPLevel*> currentStack;
    SPNode* lastNode;

    bool afterSpawn = false;
    size_t currentLevel = 0;
};