#pragma once
#include "SeriesParallelDAG.h"


constexpr size_t DEFAULT_SLEEP_NS = 1000*1000;

class SPEdgeProducer {
public:
    virtual SPBareboneEdge* NextBarebone(size_t sleep_ns = DEFAULT_SLEEP_NS) = 0;
    virtual SPEdge* Next(size_t sleep_ns = DEFAULT_SLEEP_NS) = 0;

    virtual SPEdgeData& NextData() {
        SPBareboneEdge* next = NextBarebone();
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

    SPBareboneEdge* NextBarebone(size_t sleep_ns) { return static_cast<SPBareboneEdge*>(Next()); }

    SPEdge* Next(size_t sleep_ns = DEFAULT_SLEEP_NS) {
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
        dag->memPool.Free(node->data);
        node->data = nullptr;

        // Return the node.
        dag->edges.ReturnToPool((PooledNode<SPEdge*>*)node);
    }

    FullSPDAG* dag;
    size_t currentEdge = 0;

    volatile PooledNode<SPEdge*>* current = nullptr;
};

class SPEdgeBareboneOnlineProducer : public SPEdgeProducer {
public:
    SPEdgeBareboneOnlineProducer(BareboneSPDAG* dag) : dag(dag) {
        DEBUG_ASSERT(dag != nullptr);
    }

    SPBareboneEdge* NextBarebone(size_t sleep_ns = DEFAULT_SLEEP_NS) {
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

        SPBareboneEdge* next = current->data;
        DEBUG_ASSERT(next != nullptr);

        currentEdge++;

        if (previous) // Return the previous edge.
        {
            ReturnNodeToPool(previous);
        }

        return next;
    }

    // Does not support full SPEdge structures.
    SPEdge* Next(size_t sleep_ns) {
        return nullptr;
    }

private:
    void ReturnNodeToPool(volatile PooledNode<SPBareboneEdge*>* node) {
        DEBUG_ASSERT(node != nullptr);

        // Delete the edge.
        dag->memPool.Free(node->data);
        node->data = nullptr;

        // Return the node.
        dag->edges.ReturnToPool((PooledNode<SPBareboneEdge*>*)node);
    }

    BareboneSPDAG* dag;
    size_t currentEdge = 0;

    volatile PooledNode<SPBareboneEdge*>* current = nullptr;
};


class SPEventBareboneOnlineProducer {
public:
    SPEventBareboneOnlineProducer(BareboneSPDAG* dag) : dag(dag) {
        DEBUG_ASSERT(dag != nullptr);
    }

    void FreeLast() {
        if (current != nullptr)
        {
            ReturnNodeToPool(current);
            current = nullptr;
        }
    }

    bool HasNext() {
        if (dag->IsComplete() && (dag->events.size() == 0 || (current != nullptr && current->next == nullptr)))
            return false;

        return true;
    }

    SPEvent Next(size_t sleep_ns = DEFAULT_SLEEP_NS) {
        auto previous = current;

        if (current == nullptr) // Get the first event.
        {
            current = dag->events.GetHeadNode();
        }
        else
        { // Get the next event from the current one.
            while (current->next == nullptr)
            {
                // Wait for the event.
                if (sleep_ns)
                    std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_ns));
            }
            current = current->next;
        }

        DEBUG_ASSERT(current != nullptr);

        if (previous) // Return the previous node.
        {
            ReturnNodeToPool(previous);
        }

        SPEvent next;
        memcpy(&next, const_cast<SPEvent*>(&current->data), sizeof(SPEvent));

        return next;
    }

private:
    void ReturnNodeToPool(volatile PooledNode<SPEvent>* node) {
        DEBUG_ASSERT(node != nullptr);

        dag->events.ReturnToPool((PooledNode<SPEvent>*)node);
    }

    BareboneSPDAG* dag;

    volatile PooledNode<SPEvent>* current = nullptr;
};