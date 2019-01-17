#pragma once
#include "SeriesParallelDAG.h"
#include <thread>      
#include <chrono>         

class SPEdgeProducer {
public:
    virtual SPEdge* Next(size_t sleep_ns = 1000000) = 0;

    virtual SPEdgeData& NextData() {
        SPEdge* next = Next();
        DEBUG_ASSERT(next != nullptr);
        return next->data;
    }
};

class SPEdgeOfflineProducer : public SPEdgeProducer {
public:
    SPEdgeOfflineProducer(SPDAG* dag) : dag(dag) {
        DEBUG_ASSERT(dag != nullptr);
        DEBUG_ASSERT(dag->IsComplete());
    }

    SPEdge* Next(size_t sleep_ns = 1000000) {
        SPEdge* next = nullptr;
        if (dag->edges.size() > currentEdge)
        {
            next = dag->edges[currentEdge];
            currentEdge++;
        }

        return next;
    }

private:
    SPDAG* dag;
    size_t currentEdge = 0;
};

class SPEdgeOnlineProducer : public SPEdgeProducer {
public:
    SPEdgeOnlineProducer(SPDAG* dag) : dag(dag) {
        DEBUG_ASSERT(dag != nullptr);
    }

    SPEdge* Next(size_t sleep_ns = 100000) {
        if (dag->IsComplete() && dag->edges.size() <= currentEdge)
            return nullptr;

        if (current == nullptr)
        {
            DEBUG_ASSERT(currentEdge == 0);
            current = dag->edges.GetHeadNode();
        }
        else {
            while (current->next == nullptr)
            {
                // Wait for the edge.
                if (sleep_ns)
                    std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_ns));
            }
            current = current->next;
        }

        DEBUG_ASSERT(current != nullptr);

        SPEdge* next = current->data;

        currentEdge++;

        return next;
    }

private:
    SPDAG* dag;
    size_t currentEdge = 0;

    volatile PooledNode<SPEdge*>* current = nullptr;
};