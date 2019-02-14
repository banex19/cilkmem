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
    {
        isComplete = true;
        return;
    }

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

SPComponent FullSPDAG::AggregateComponents(SPEdgeProducer* edgeProducer, SPEventBareboneOnlineProducer* eventProducer, int64_t threshold) {
    if (IsComplete() && firstNode == nullptr)
        return SPComponent();

    DEBUG_ASSERT(firstNode != nullptr);

    SPComponent start{ edgeProducer->NextData() };

    start.CombineSeries(AggregateComponentsFromNode(edgeProducer, firstNode, threshold));

    SPEdge* next = edgeProducer->Next();
    while (next->to->associatedSyncNode != nullptr)
    {
        start.CombineSeries(SPComponent(next->data));
        start.CombineSeries(AggregateComponentsFromNode(edgeProducer, next->to, threshold));
        next = edgeProducer->Next();
    }

    start.CombineSeries(SPComponent(next->data));

    // Make sure there are no more edges to consume.
    next = edgeProducer->Next();
    DEBUG_ASSERT(next == nullptr);
    DEBUG_ASSERT(IsComplete());

    return start;
}

SPComponent FullSPDAG::AggregateComponentsEfficient(SPEdgeProducer * edgeProducer, SPEventBareboneOnlineProducer* eventProducer, int64_t threshold) {
    if (IsComplete() && firstNode == nullptr)
        return SPComponent();

    DEBUG_ASSERT(firstNode != nullptr);

    SPEdge* start = edgeProducer->Next();

    SPComponent final = AggregateMultispawn(edgeProducer, start, firstNode, threshold);

    SPEdge* next = edgeProducer->Next();
    while (next->to->associatedSyncNode != nullptr)
    {
        final.CombineSeries(AggregateMultispawn(edgeProducer, next, next->to, threshold));
        next = edgeProducer->Next();
    }

    final.CombineSeries(SPComponent(next->data));

    // Make sure there are no more edges to consume.
    next = edgeProducer->Next();
    DEBUG_ASSERT(next == nullptr);
    DEBUG_ASSERT(IsComplete());

    return final;
}

SPNaiveComponent FullSPDAG::AggregateComponentsNaive(SPEdgeProducer* edgeProducer, SPEventBareboneOnlineProducer* eventProducer, int64_t threshold, size_t p) {
    if (IsComplete() && firstNode == nullptr)
        return SPNaiveComponent(SPEdgeData(), 8);

    DEBUG_ASSERT(firstNode != nullptr);

    SPNaiveComponent start{ edgeProducer->NextData(), p };

    start.CombineSeries(AggregateComponentsFromNodeNaive(edgeProducer, firstNode, threshold, p));

    SPEdge* next = edgeProducer->Next();
    while (next->to->associatedSyncNode != nullptr)
    {
        start.CombineSeries(SPNaiveComponent(next->data, p));
        start.CombineSeries(AggregateComponentsFromNodeNaive(edgeProducer, next->to, threshold, p));
        next = edgeProducer->Next();
    }

    start.CombineSeries(SPNaiveComponent(next->data, p));

    // Make sure there are no more edges to consume.
    next = edgeProducer->Next();
    DEBUG_ASSERT(next == nullptr);
    DEBUG_ASSERT(IsComplete());

    return start;
}

SPNaiveComponent FullSPDAG::AggregateComponentsNaiveEfficient(SPEdgeProducer * edgeProducer, SPEventBareboneOnlineProducer * eventProducer, int64_t threshold, size_t p) {
    if (IsComplete() && firstNode == nullptr)
        return SPNaiveComponent(SPEdgeData(), 8);

    DEBUG_ASSERT(firstNode != nullptr);

    SPEdge* start = edgeProducer->Next();

    SPNaiveComponent final = AggregateMultispawnNaive(edgeProducer, start, firstNode, threshold, p);

    SPEdge* next = edgeProducer->Next();
    while (next->to->associatedSyncNode != nullptr)
    {
        final.CombineSeries(AggregateMultispawnNaive(edgeProducer, next, next->to, threshold, p));
        next = edgeProducer->Next();
    }

    final.CombineSeries(SPNaiveComponent(next->data, p));

    // Make sure there are no more edges to consume.
    next = edgeProducer->Next();
    DEBUG_ASSERT(next == nullptr);
    DEBUG_ASSERT(IsComplete());

    return final;
}


SPComponent FullSPDAG::AggregateMultispawn(SPEdgeProducer * edgeProducer, SPEdge * incomingEdge, SPNode * pivot, int64_t threshold) {
    SPNode* sync = pivot->associatedSyncNode;
    DEBUG_ASSERT_EX(sync != nullptr, "[AggregateMultispawn] Node %zu has no sync node", pivot->id);

    out << "Aggregating multispawn from node " << pivot->id << "\n";

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

                out << "Found multispawn starting from node " << next->to->id << "\n";
                spawn.CombineSeries(AggregateMultispawn(edgeProducer, next, next->to, threshold));
                next = edgeProducer->Next();
            }

            spawn.CombineSeries(SPComponent(next->data));

            multispawn.IncrementOnSpawn(spawn, threshold);
            isSpawn = false;
        }
        else // We are following a continuation sub-component.
        {
            SPComponent continuation;

            while (next->to != sync && next->to->associatedSyncNode != sync)
            {
                out << "Found multispawn starting from node " << next->to->id << "\n";
                continuation.CombineSeries(AggregateMultispawn(edgeProducer, next, next->to, threshold));
                next = edgeProducer->Next();
            }

            if (next->to == sync) // Are we at the last continuation strand?
            {
                out << "Edge from " << next->from->id << " to " << next->to->id << " is the last continuation\n";
                stop = true;
            }

            continuation.CombineSeries(SPComponent(next->data));

            multispawn.IncrementOnContinuation(continuation, threshold);
            isSpawn = true;
        }
    }

    DEBUG_ASSERT(isSpawn);

    out << "Multispawn from node " << pivot->id << ": ";
    //multispawn.ToComponent().Print();


    return multispawn.ToComponent();
}

SPNaiveComponent FullSPDAG::AggregateMultispawnNaive(SPEdgeProducer * edgeProducer, SPEdge * incomingEdge, SPNode * pivot, int64_t threshold, size_t p) {
    SPNode* sync = pivot->associatedSyncNode;
    DEBUG_ASSERT_EX(sync != nullptr, "[AggregateMultispawn] Node %zu has no sync node", pivot->id);

    out << "Aggregating multispawn from node " << pivot->id << "\n";

    SPNaiveMultispawnComponent multispawn{ p };

    SPNaiveComponent start{ incomingEdge->data, p };
    multispawn.IncrementOnContinuation(start);

    bool isSpawn = true;
    bool stop = false;
    while (!stop)
    {
        SPEdge* next = edgeProducer->Next();

        if (isSpawn) // We are going down a spawn sub-component of this multi-spawn component.
        {
            SPNaiveComponent spawn{ p };

            while (next->to != sync)
            {
                DEBUG_ASSERT(next->to->associatedSyncNode != sync);

                out << "Found multispawn starting from node " << next->to->id << "\n";
                spawn.CombineSeries(AggregateMultispawnNaive(edgeProducer, next, next->to, threshold, p));
                next = edgeProducer->Next();
            }

            spawn.CombineSeries(SPNaiveComponent(next->data, p));

            multispawn.IncrementOnSpawn(spawn);
            isSpawn = false;
        }
        else // We are following a continuation sub-component.
        {
            SPNaiveComponent continuation{ p };

            while (next->to != sync && next->to->associatedSyncNode != sync)
            {
                out << "Found multispawn starting from node " << next->to->id << "\n";
                continuation.CombineSeries(AggregateMultispawnNaive(edgeProducer, next, next->to, threshold, p));
                next = edgeProducer->Next();
            }

            if (next->to == sync) // Are we at the last continuation strand?
            {
                out << "Edge from " << next->from->id << " to " << next->to->id << " is the last continuation\n";
                stop = true;
            }

            continuation.CombineSeries(SPNaiveComponent(next->data, p));

            multispawn.IncrementOnContinuation(continuation);
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

    out << "Finished subcomponent (spawn) from id " << pivot->id << " to id " << sync->id << "\n";

    next = edgeProducer->Next();
    SPComponent continuation = AggregateUntilSync(edgeProducer, next, sync, threshold);

    spawnPath.CombineParallel(continuation, threshold);

    out << "Finished subcomponent (continuation) from id " << pivot->id << " to id " << sync->id << "\n";

    return spawnPath;
}

SPComponent FullSPDAG::AggregateUntilSync(SPEdgeProducer* edgeProducer, SPEdge * start, SPNode * syncNode, int64_t threshold) {
    size_t startId = start->from->id;

    SPComponent subComponent{ start->data };

    SPEdge* currentEdge = start;

    while (currentEdge->to != syncNode)
    {
        SPNode* toNode = currentEdge->to;
        SPNode* associatedSyncNode = toNode->associatedSyncNode;

        DEBUG_ASSERT_EX(currentEdge->to->associatedSyncNode != nullptr, "[AggregateUntilSync] Node %zu has no sync node", toNode->id);

        // There's another spawn in this path. Resolve that sub-component first.
        out << "Found spawn from node " << toNode->id << "\n";
        subComponent.CombineSeries(AggregateComponentsFromNode(edgeProducer, toNode, threshold));

        // The spawn has returned, continue from the only edge coming out of that 
        // spawn's associated sync node. Don't do anything if the sub-spawn shared the same sync.
        // This can happen in a multispawn component.
        if (associatedSyncNode != syncNode)
        {
            currentEdge = edgeProducer->Next();
            subComponent.CombineSeries(SPComponent(currentEdge->data));
        }
        else
        {
            break;
        }
    }

    out << "Aggregated from " << startId << " to " << syncNode->id << "\n";

    return subComponent;
}

SPNaiveComponent FullSPDAG::AggregateComponentsFromNodeNaive(SPEdgeProducer * edgeProducer, SPNode * pivot, int64_t threshold, size_t p) {
    SPNode* sync = pivot->associatedSyncNode;
    DEBUG_ASSERT_EX(sync != nullptr, "[AggregateComponentsFromNode] Node %zu has no sync node", pivot->id);

    out << "Aggregating spawn from id " << pivot->id << " to id " << sync->id << "\n";

    SPEdge* next = edgeProducer->Next();
    SPNaiveComponent spawnPath = AggregateUntilSyncNaive(edgeProducer, next, sync, threshold, p);

    out << "Finished subcomponent (spawn) from id " << pivot->id << " to id " << sync->id << "\n";

    next = edgeProducer->Next();
    SPNaiveComponent continuation = AggregateUntilSyncNaive(edgeProducer, next, sync, threshold, p);

    spawnPath.CombineParallel(continuation);

    out << "Finished subcomponent (continuation) from id " << pivot->id << " to id " << sync->id << "\n";

    return spawnPath;
}

SPNaiveComponent FullSPDAG::AggregateUntilSyncNaive(SPEdgeProducer * edgeProducer, SPEdge * start, SPNode * syncNode, int64_t threshold, size_t p) {
    size_t startId = start->from->id;
    SPNaiveComponent subComponent{ start->data, p };

    SPEdge* currentEdge = start;

    while (currentEdge->to != syncNode)
    {
        SPNode* toNode = currentEdge->to;
        SPNode* associatedSyncNode = toNode->associatedSyncNode;

        DEBUG_ASSERT_EX(currentEdge->to->associatedSyncNode != nullptr, "[AggregateUntilSync] Node %zu has no sync node", toNode->id);

        // There's another spawn in this path. Resolve that sub-component first.
        out << "Found spawn from node " << toNode->id << "\n";
        subComponent.CombineSeries(AggregateComponentsFromNodeNaive(edgeProducer, toNode, threshold, p));

        // The spawn has returned, continue from the only edge coming out of that 
        // spawn's associated sync node. Don't do anything if the sub-spawn shared the same sync.
        // This can happen in a multispawn component.
        if (associatedSyncNode != syncNode)
        {
            currentEdge = edgeProducer->Next();
            subComponent.CombineSeries(SPNaiveComponent(currentEdge->data, p));
        }
        else
        {
            break;
        }
    }

    out << "Aggregated from " << startId << " to " << syncNode->id << "\n";

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

std::string GetDotNameForNode(const SPNode* node) {
    if (node->locationName == nullptr)
        return std::to_string(node->id);
    else
        return std::string(node->locationName) + "_" + std::to_string(node->locationLine);
}

void FullSPDAG::WriteDotFile(const std::string& filename) {
    std::ofstream file{ filename };

    DEBUG_ASSERT(file);

    file << "digraph {\nrankdir=LR\n";

    for (size_t i = 0; i < nodes.size(); ++i)
    {
        SPNode* node = nodes[i];
        file << node->id << "[label=\"" << GetDotNameForNode(node) << "\"]\n";
    }

    size_t allocIndex = 0;
    for (size_t i = 0; i < edges.size(); ++i)
    {
        SPEdge* edge = edges[i];
        DEBUG_ASSERT(edge->from);
        DEBUG_ASSERT(edge->to);
        if (edge->forward)
        {

            file << edge->from->id << " -> " << edge->to->id
                << " [label=\"" << FormatWithCommas(edge->data.memAllocated) << " (" << FormatWithCommas(edge->data.maxMemAllocated) << ")";
            
            if (edge->data.biggestAllocation > 0)
                file << " !" << allocIndex++;

            file << "\"";
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

    std::ofstream allocFile{ filename + ".txt" };

    DEBUG_ASSERT(allocFile);

    allocIndex = 0;
    for (size_t i = 0; i < edges.size(); ++i)
    {
        SPEdge* edge = edges[i];

        if (edge->forward && edge->data.biggestAllocation > 0)
        {
            allocFile << allocIndex << ": " << edge->data.GetSource() << "\n";
        }
    }



    allocFile.close();
}