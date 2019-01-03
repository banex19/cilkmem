#pragma once
#include <vector>
#include <iostream>
#include <unordered_map>
#include <cstdint>

struct SPNode;

struct SPComponent {
    int64_t memTotal = 0;
    int64_t maxSingle = 0;
    int64_t multiRobust = 0;

    void CombineSeries(const SPComponent& other)
    {

    }

    void CombineParallel(const SPComponent& other)
    {

    }
};

struct SPEdgeData {
    int64_t memAllocated = 0;
    int64_t maxMemAllocated = 0;

    bool operator==(const SPEdgeData& other) const {
        return memAllocated == other.memAllocated;
    }
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

    SPLevel(size_t level, SPNode* currentNode) : currentNode(currentNode)
    {
        functionLevels.push_back(level);
    }

    void PopFunctionLevel() {
        syncNodes.pop_back();
        functionLevels.pop_back();
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

    void Spawn(SPEdgeData &currentEdge);
    void Sync(SPEdgeData &currentEdge, bool taskExit);

    void IncrementLevel() { currentLevel++; }
    void DecrementLevel() { currentLevel--; }

    void Print();
    void WriteDotFile(const std::string& filename);

private:

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