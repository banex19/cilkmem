#include "SeriesParallelDAG.h"
#include <assert.h>
#include <fstream>
#include "SPEdgeProducer.h"

// We have spawned a new task. Create the spawn node.
void FullSPDAG::Spawn(SPEdgeData & currentEdge, size_t regionId) {
    SPNode* spawnNode = AddNode();


    out << "Adding spawn node (id: " << spawnNode->id << ")\n";

    if (currentStack.size() == 0 || afterSpawn)
    {
        SPLevel* parentLevel = GetParentLevel();

        SPLevel* newLevel = new SPLevel(currentLevel, regionId, spawnNode);
        currentStack.push_back(newLevel);

        SPNode* syncNode = AddNode();
        newLevel->syncNodes.push_back(syncNode);
        spawnNode->associatedSyncNode = syncNode;

        if (parentLevel != nullptr)
        {
            SPNode* parent = parentLevel->currentNode;
            AddEdge(parent, spawnNode, currentEdge, true);
        }
        else
        { // Beginning of program.
            SPNode* startNode = AddNode();


            out << "Adding start node (id: " << startNode->id << ")\n";

            AddEdge(startNode, spawnNode, currentEdge);

            firstNode = spawnNode;
        }

        out << "Adding sync node (id: " << syncNode->id << ")\n";
    }
    else if (!afterSpawn)
    {
        SPLevel* parentLevel = GetParentLevel();
        DEBUG_ASSERT(parentLevel != nullptr);
        DEBUG_ASSERT(parentLevel->functionLevels.size() == 0 ||
            parentLevel->functionLevels.back() <= currentLevel);

        // Check if we are still at the same level in the
        // function stack. If we're not, we will need an 
        // additional sync node.
        if (parentLevel->functionLevels.size() == 0 ||
            parentLevel->functionLevels.back() < currentLevel ||
            parentLevel->regionIds.back() != regionId)
        {
            SPNode* syncNode = AddNode();
            parentLevel->PushFunctionLevel(syncNode, currentLevel, regionId);

            spawnNode->associatedSyncNode = syncNode;

            out << "Adding sync node (id: " << syncNode->id << ")\n";
        }
        else
        {
            DEBUG_ASSERT(parentLevel->functionLevels.size() > 0 &&
                parentLevel->functionLevels.back() == currentLevel);
            DEBUG_ASSERT(regionId == parentLevel->regionIds.back());
            parentLevel->syncNodes.back()->numStrandsLeft = 2;
            spawnNode->associatedSyncNode = parentLevel->syncNodes.back();
        }

        SPNode* pred = parentLevel->currentNode;
        parentLevel->currentNode = spawnNode;
        AddEdge(pred, spawnNode, currentEdge);
    }

    lastNode = spawnNode;
    afterSpawn = true;
}

void FullSPDAG::Sync(SPEdgeData & currentEdge, size_t regionId) {
    if (nodes.size() == 0)
        return;

    SPLevel* parentLevel = GetParentLevel();
    DEBUG_ASSERT(parentLevel != nullptr);

    // regionId is provided only by sync events, not by task exit events.
    if (regionId != 0)
        DEBUG_ASSERT(regionId == parentLevel->regionIds.back());

    out << "DAG sync: level " << currentStack.size() - 1 << "\n";

    if (parentLevel->syncNodes.size() > 0)
    {
        out << "Left to sync for node " << parentLevel->syncNodes.back()->id << ": " <<
            parentLevel->syncNodes.back()->numStrandsLeft << "\n";
    }

    SPNode* pred = lastNode;

    if (parentLevel->syncNodes.size() == 0) // Is this sync for the upper level?
    {
        delete parentLevel;
        currentStack.pop_back();

        out << "Finished level " << currentStack.size() << "\n";

        if (currentStack.size() == 0) // The program is exiting.
        {
            out << "Adding exit node\n";

            SPNode* exitNode = AddNode();
            AddEdge(pred, exitNode, currentEdge);

            isComplete = true;
            return;
        }

        parentLevel = GetParentLevel();
        DEBUG_ASSERT(parentLevel != nullptr);
        DEBUG_ASSERT(parentLevel->syncNodes.size() > 0);
        DEBUG_ASSERT(parentLevel->syncNodes.back()->numStrandsLeft == 2);

        out << "DAG sync (continued): level " << currentStack.size() - 1 << "\n";
    }

    DEBUG_ASSERT(parentLevel->syncNodes.size() > 0);
    SPNode* syncNode = parentLevel->syncNodes.back();

    if (syncNode->numStrandsLeft == 1) // Horizontal sync.
    {
        pred = parentLevel->currentNode;
    }

    bool spawn = syncNode->numStrandsLeft == 2 && afterSpawn;
    syncNode->numStrandsLeft--;

    if (syncNode->numStrandsLeft == 0) // Everybody synced.
    {
        parentLevel->currentNode = syncNode;
        parentLevel->PopFunctionLevel();
    }

    AddEdge(pred, syncNode, currentEdge, spawn);

    lastNode = syncNode;
    afterSpawn = false;
}

SPComponent FullSPDAG::AggregateComponents(SPEdgeProducer* edgeProducer, int64_t threshold) {
    if (IsComplete() && firstNode == nullptr)
        return SPComponent();


    DEBUG_ASSERT(firstNode != nullptr);

    SPComponent start{ edgeProducer->NextData() };

    start.CombineSeries(AggregateComponentsFromNode(edgeProducer, firstNode, threshold));

    SPComponent end{ edgeProducer->NextData() };

    start.CombineSeries(end);

    // Make sure there are no more edges to consume.
    DEBUG_ASSERT(edgeProducer->Next() == nullptr);
    DEBUG_ASSERT(IsComplete());

    return start;
}

SPComponent FullSPDAG::AggregateComponentsEfficient(SPEdgeProducer * edgeProducer, int64_t threshold) {
    if (IsComplete() && firstNode == nullptr)
        return SPComponent();

    DEBUG_ASSERT(firstNode != nullptr);

    SPEdge* start = edgeProducer->Next();

    SPComponent final = AggregateMultispawn(edgeProducer, start, firstNode, threshold);

    SPComponent end{ edgeProducer->NextData() };

    final.CombineSeries(end);

    // Make sure there are no more edges to consume.
    DEBUG_ASSERT(edgeProducer->Next() == nullptr);
    DEBUG_ASSERT(IsComplete());

    return final;
}


SPComponent FullSPDAG::AggregateMultispawn(SPEdgeProducer * edgeProducer, SPEdge * incomingEdge, SPNode * pivot, int64_t threshold) {
    SPNode* sync = pivot->associatedSyncNode;
    DEBUG_ASSERT_EX(sync != nullptr, "[AggregateMultispawn] Node %zu has no sync node", pivot->id);

    SPMultispawnComponent multispawn;

    SPComponent start{ incomingEdge->data };
    multispawn.IncrementOnContinuation(start, threshold);

    bool isSpawn = true;
    bool stop = false;
    while (!stop)
    {
        SPEdge* next = edgeProducer->Next();

        if (isSpawn) // We are going down a spawn sub-component of this multi-spawn component.
        {
            SPComponent spawn;

            while (next->to != sync)
            {
                DEBUG_ASSERT(next->to->associatedSyncNode != sync);

                spawn.CombineSeries(AggregateMultispawn(edgeProducer, next, next->to, threshold));
                next = edgeProducer->Next();
            }

            spawn.CombineSeries(next->data);

            multispawn.IncrementOnSpawn(spawn, threshold);
            isSpawn = false;
        }
        else // We are following a continuation sub-component.
        {
            SPComponent continuation;

            while (next->to != sync && next->to->associatedSyncNode != sync)
            {
                continuation.CombineSeries(AggregateMultispawn(edgeProducer, next, next->to, threshold));
                next = edgeProducer->Next();
            }

            if (next->to == sync) // Are we at the last continuation strand?
                stop = true;

            continuation.CombineSeries(SPComponent(next->data));

            multispawn.IncrementOnContinuation(continuation, threshold);
            isSpawn = true;
        }
    }

    DEBUG_ASSERT(isSpawn);

    return multispawn.ToComponent();
}

SPComponent FullSPDAG::AggregateComponentsFromNode(SPEdgeProducer* edgeProducer, SPNode * pivot, int64_t threshold) {
    SPNode* sync = pivot->associatedSyncNode;
    DEBUG_ASSERT_EX(sync != nullptr, "[AggregateComponentsFromNode] Node %zu has no sync node", pivot->id);

    out << "Aggregating spawn from id " << pivot->id << " to id " << sync->id << "\n";

    SPEdge* next = edgeProducer->Next();
    SPComponent spawnPath = AggregateUntilSync(edgeProducer, next, sync, threshold);

    next = edgeProducer->Next();
    SPComponent continuation = AggregateUntilSync(edgeProducer, next, sync, threshold);

    spawnPath.CombineParallel(continuation, threshold);

    return spawnPath;
}

SPComponent FullSPDAG::AggregateUntilSync(SPEdgeProducer* edgeProducer, SPEdge * start, SPNode * syncNode, int64_t threshold) {
    SPComponent subComponent{ start->data };

    SPEdge* currentEdge = start;

    while (currentEdge->to != syncNode)
    {
        SPNode* toNode = currentEdge->to;

        DEBUG_ASSERT_EX(currentEdge->to->associatedSyncNode != nullptr, "[AggregateUntilSync] Node %zu has no sync node", toNode->id);

        // There's another spawn in this path. Resolve that sub-component first.
        subComponent.CombineSeries(AggregateComponentsFromNode(edgeProducer, toNode, threshold));

        // The spawn has returned, continue from the only edge coming out of that 
        // spawn's associated sync node.
        currentEdge = edgeProducer->Next();
        subComponent.CombineSeries(SPComponent(currentEdge->data));
    }

    return subComponent;
}


void FullSPDAG::Print() {
    out << "Series Parallel DAG - Node count: " << nodes.size() << " - Edge count: " << edges.size() << "\n";
    for (size_t i = 0; i < edges.size(); ++i)
    {
        SPEdge* edge = edges[i];
        DEBUG_ASSERT(edge->from);
        DEBUG_ASSERT(edge->to);
        if (edge->forward)
        {
            out << "(" << edge->id << ") " << edge->from->id << " --> " << edge->to->id <<
                " (max: " << edge->data.maxMemAllocated << " - total: " << edge->data.memAllocated << ")";

            if (edge->spawn)
                out << " [spawn] [sync node: " << edge->from->associatedSyncNode->id << "]";

            out << "\n";
        }
    }
}

void FullSPDAG::WriteDotFile(const std::string& filename) {
    std::ofstream file{ filename };

    DEBUG_ASSERT(file);

    file << "digraph {\nrankdir=LR\n";

    for (size_t i = 0; i < edges.size(); ++i)
    {
        SPEdge* edge = edges[i];
        DEBUG_ASSERT(edge->from);
        DEBUG_ASSERT(edge->to);
        if (edge->forward)
        {
            file << edge->from->id << " -> " << edge->to->id << " [label=" << edge->data.memAllocated;
            if (edge->spawn)
            {
                file << ", penwidth=2, color=\"red\"";
            }
            else
            {
                file << ", color=\"blue\"";
            }
            file << "];\n";
        }
    }

    file << "}";

    file.close();
}