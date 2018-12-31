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
    std::vector<SPEdge*> predecessors;

    void AddSuccessor(SPEdge* newEdge, SPNode* succ, const SPEdgeData &data) {
        newEdge->from = this;
        newEdge->to = succ;
        newEdge->data = data;
        newEdge->forward = true;
        successors.push_back(newEdge);

        std::cout << "Adding edge " << this->id << " --> " << succ->id << "\n";
    }

    void AddPredecessor(SPEdge* newEdge, SPNode* pred, const SPEdgeData &data) {
        newEdge->from = pred;
        newEdge->to = this;
        newEdge->data = data;
        newEdge->forward = false;
        predecessors.push_back(newEdge);
    }
};

struct SPLevel {
    SPNode* currentNode = nullptr;
    size_t numStrandsLeft = 0;
    SPNode* syncNode = nullptr;

    SPLevel(SPNode* currentNode) : currentNode(currentNode), numStrandsLeft(2), syncNode(nullptr) {}
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

    void Print();

private:

    SPNode* AddNode() { SPNode* newNode = new SPNode(); newNode->id = nodes.size(); nodes.push_back(newNode); return newNode; }
    SPEdge* AddEdge() { SPEdge* newEdge = new SPEdge(); newEdge->id = edges.size(); edges.push_back(newEdge); return newEdge; }

    SPLevel* GetParentLevel() { if (currentStack.size() > 0) return currentStack[currentStack.size() - 1]; else return nullptr; }

    std::vector<SPNode*> nodes;
    std::vector<SPEdge*> edges;

    std::vector<SPLevel*> currentStack;
    SPNode* lastNode;

    bool afterSpawn = false;
};