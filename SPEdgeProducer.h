#pragma once
#include "SeriesParallelDAG.h"


class SPEdgeProducer {
public:
    virtual SPEdge* Next(size_t sleep_ns = 1000000) = 0;

    virtual SPEdgeData& NextData() {
        SPEdge* next = Next();
        DEBUG_ASSERT(next != nullptr);
        return next->data;
    }

    virtual ~SPEdgeProducer() {}
};

class SPEdgeFullOnlineProducer : public SPEdgeProducer {
public:
    SPEdgeFullOnlineProducer(FullSPDAG* dag) : dag(dag) {
        DEBUG_ASSERT(dag != nullptr);
    }

    SPEdge* Next(size_t sleep_ns = 0) {
        // No more edges, stop.
        if (dag->IsComplete() && (dag->edges.size() == 0 || (current != nullptr && current->next == nullptr)))
        {
            if (current != nullptr)
            {
                ReturnNodeToPool(current);
                current = nullptr;
            }

            return nullptr;
        }

        auto previous = current;

        if (current == nullptr) // Get the first edge.
        {
            DEBUG_ASSERT(currentEdge == 0);
            current = dag->edges.GetHeadNode();
        }
        else
        { // Get the next edge from the current one.
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
        DEBUG_ASSERT(next != nullptr);

        currentEdge++;

        if (previous) // Return the previous edge.
        {
            ReturnNodeToPool(previous);
        }

        return next;
    }

private:
    void ReturnNodeToPool(volatile PooledNode<SPEdge*>* node) {
        DEBUG_ASSERT(node != nullptr);

        // Delete the edge.
        delete node->data;
        node->data = nullptr;

        // Return the node.
        dag->edges.ReturnToPool((PooledNode<SPEdge*>*)node);
    }

    FullSPDAG* dag;
    size_t currentEdge = 0;

    volatile PooledNode<SPEdge*>* current = nullptr;
};