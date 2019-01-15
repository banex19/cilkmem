#include "SeriesParallelDAG.h"
#include <assert.h>
#include <fstream>

// We have spawned a new task. Create the spawn node.
void SPDAG::Spawn(SPEdgeData & currentEdge, size_t regionId)
{
    SPNode* spawnNode = AddNode();

    if (debugVerbose)
        std::cout << "Adding spawn node (id: " << spawnNode->id << ")\n";

    if (currentStack.size() == 0 || afterSpawn)
    {
        SPLevel* parentLevel = GetParentLevel();

        SPLevel* newLevel = new SPLevel(currentLevel, regionId, spawnNode);
        currentStack.push_back(newLevel);

        if (parentLevel != nullptr)
        {
            SPNode* parent = parentLevel->currentNode;
            parent->AddSuccessor(AddEdge(), spawnNode, currentEdge, true);
        }
        else { // Beginning of program.
            SPNode* startNode = AddNode();

            if (debugVerbose)
                std::cout << "Adding start node (id: " << startNode->id << ")\n";

            startNode->AddSuccessor(AddEdge(), spawnNode, currentEdge);
        }

        SPNode* syncNode = AddNode();
        newLevel->syncNodes.push_back(syncNode);
        spawnNode->associatedSyncNode = syncNode;

        if (debugVerbose)
            std::cout << "Adding sync node (id: " << syncNode->id << ")\n";
    }
    else if (!afterSpawn)
    {
        SPLevel* parentLevel = GetParentLevel();
        assert(parentLevel != nullptr);
        assert(parentLevel->functionLevels.size() == 0 ||
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

            if (debugVerbose)
                std::cout << "Adding sync node (id: " << syncNode->id << ")\n";
        }
        else {
            assert(parentLevel->functionLevels.size() > 0 &&
                parentLevel->functionLevels.back() == currentLevel);
            assert(regionId == parentLevel->regionIds.back());
            parentLevel->syncNodes.back()->numStrandsLeft = 2;
            spawnNode->associatedSyncNode = parentLevel->syncNodes.back();
        }

        SPNode* pred = parentLevel->currentNode;
        parentLevel->currentNode = spawnNode;
        pred->AddSuccessor(AddEdge(), spawnNode, currentEdge);
    }

    lastNode = spawnNode;
    afterSpawn = true;
}

void SPDAG::Sync(SPEdgeData & currentEdge, size_t regionId)
{
    if (nodes.size() == 0)
        return;

    SPLevel* parentLevel = GetParentLevel();
    assert(parentLevel != nullptr);

    // regionId is provided only by sync events, not by task exit events.
    if (regionId != 0)
        assert(regionId == parentLevel->regionIds.back());

    if (debugVerbose)
        std::cout << "DAG sync: level " << currentStack.size() - 1 << "\n";

    if (parentLevel->syncNodes.size() > 0)
    {
        if (debugVerbose)
            std::cout << "Left to sync for node " << parentLevel->syncNodes.back()->id << ": " <<
            parentLevel->syncNodes.back()->numStrandsLeft << "\n";
    }


    SPNode* pred = lastNode;

    if (parentLevel->syncNodes.size() == 0) // Is this sync for the upper level?
    {
        delete parentLevel;
        currentStack.pop_back();

        if (debugVerbose)
            std::cout << "Finished level " << currentStack.size() << "\n";

        if (currentStack.size() == 0) // The program is exiting.
        {
            if (debugVerbose)
                std::cout << "Adding exit node\n";

            SPNode* exitNode = AddNode();
            pred->AddSuccessor(AddEdge(), exitNode, currentEdge);
            return;
        }

        parentLevel = GetParentLevel();
        assert(parentLevel != nullptr);
        assert(parentLevel->syncNodes.size() > 0);
        assert(parentLevel->syncNodes.back()->numStrandsLeft == 2);

        if (debugVerbose)
            std::cout << "DAG sync (continued): level " << currentStack.size() - 1 << "\n";
    }

    assert(parentLevel->syncNodes.size() > 0);
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

    pred->AddSuccessor(AddEdge(), syncNode, currentEdge, spawn);

    lastNode = syncNode;
    afterSpawn = false;
}

SPComponent SPDAG::AggregateComponents(int64_t threshold)
{
    if (nodes.size() == 0)
        return SPComponent();

    assert(edges.size() >= 2);

    SPComponent start{ edges.front()->data };
    SPComponent end{ edges.back()->data };

    start.CombineSeries(AggregateComponentsFromNode(nodes[0], threshold));
    start.CombineSeries(end);

    return start;
}


SPComponent SPDAG::AggregateComponentsFromNode(SPNode * pivot, int64_t threshold)
{
    SPNode* sync = pivot->associatedSyncNode;
    assert(sync != nullptr);
    assert(pivot->successors.size() == 2);

    SPComponent left = AggregateUntilSync(pivot->successors[0], sync, threshold);
    SPComponent right = AggregateUntilSync(pivot->successors[1], sync, threshold);

    left.CombineParallel(right, threshold);

    return left;

}

SPComponent SPDAG::AggregateUntilSync(SPEdge * start, SPNode * syncNode, int64_t threshold)
{
    SPComponent subComponent{ start->data };

    SPEdge* currentEdge = start;

    while (currentEdge->to != syncNode)
    {
        subComponent.CombineSeries(AggregateComponentsFromNode(currentEdge->to, threshold));

        SPNode* nextSync = currentEdge->to->associatedSyncNode;
        if (nextSync != nullptr)
        {
            assert(nextSync->successors.size() == 1);
            currentEdge = nextSync->successors[0];
            subComponent.CombineSeries(SPComponent(currentEdge->data));
        }
    }


    return subComponent;
}


void SPDAG::Print()
{
    std::cout << "Series Parallel DAG - Node count: " << nodes.size() << " - Edge count: " << edges.size() << "\n";
    for (auto& edge : edges)
    {
        assert(edge->from);
        assert(edge->to);
        if (edge->forward)
        {
            std::cout << "(" << edge->id << ") " << edge->from->id << " --> " << edge->to->id <<
                " (max: " << edge->data.maxMemAllocated << " - total: " << edge->data.memAllocated << ")";

            if (edge->spawn)
                std::cout << " [spawn] [sync node: " << edge->from->associatedSyncNode->id << "]";

            std::cout << "\n";
        }
    }
}

void SPDAG::WriteDotFile(const std::string& filename)
{
    std::ofstream file{ filename };

    assert(file);

    file << "digraph {\nrankdir=LR\n";

    for (auto& edge : edges)
    {
        assert(edge->from);
        assert(edge->to);
        if (edge->forward)
        {
            file << edge->from->id << " -> " << edge->to->id << " [label=" << edge->data.memAllocated;
            if (edge->spawn)
            {
                file << ", penwidth=2, color=\"red\"";
            }
            else {
                file << ", color=\"blue\"";
            }
            file << "];\n";
        }
    }

    file << "}";

    file.close();
}
