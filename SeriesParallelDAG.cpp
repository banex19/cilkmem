#include "SeriesParallelDAG.h"
#include <assert.h>

// We have spawned a new task. Create the spawn node.
void SPDAG::Spawn(SPEdgeData & currentEdge)
{
    SPNode* spawnNode = AddNode();

    std::cout << "Adding spawn node (id: " << spawnNode->id << ")\n";

    if (currentStack.size() == 0 || afterSpawn)
    {
        SPLevel* parentLevel = GetParentLevel();

        SPLevel* newLevel = new SPLevel(spawnNode);
        currentStack.push_back(newLevel);

        if (parentLevel != nullptr)
        {
            SPNode* parent = parentLevel->currentNode;
            parent->AddSuccessor(AddEdge(), spawnNode, currentEdge);
            spawnNode->AddPredecessor(AddEdge(), parent, currentEdge);
        }
    }
    else if (!afterSpawn)
    {
        SPLevel* parentLevel = GetParentLevel();
        assert(parentLevel != nullptr);

        assert(parentLevel->numStrandsLeft == 1);
        parentLevel->numStrandsLeft = 2;

        SPNode* pred = parentLevel->currentNode;
        parentLevel->currentNode = spawnNode;

        pred->AddSuccessor(AddEdge(), spawnNode, currentEdge);
        spawnNode->AddPredecessor(AddEdge(), pred, currentEdge);
    }

    lastNode = spawnNode;
    afterSpawn = true;
}

void SPDAG::Sync(SPEdgeData & currentEdge, bool taskExit)
{
    SPLevel* parentLevel = GetParentLevel();
    assert(parentLevel != nullptr);

    std::cout << "DAG sync: level " << currentStack.size() - 1 << ", left " << parentLevel->numStrandsLeft << "\n";

    SPNode* pred = lastNode;

    if (parentLevel->numStrandsLeft == 0) // Is this sync for the upper level?
    {
        delete parentLevel;
        currentStack.pop_back();

        std::cout << "Finished level " << currentStack.size() << "\n";

        if (currentStack.size() == 0) // The program is exiting.
            return;

        parentLevel = GetParentLevel();
        assert(parentLevel != nullptr);
        assert(parentLevel->numStrandsLeft == 2);

        std::cout << "DAG sync (continued): level " << currentStack.size() - 1 << ", left " << parentLevel->numStrandsLeft << "\n";
    }

    if (parentLevel->numStrandsLeft == 1) // Horizontal sync.
    {
        pred = parentLevel->currentNode;
    }

    SPNode* syncNode = parentLevel->syncNode;

    if (syncNode == nullptr)
    {
        syncNode = AddNode();
        parentLevel->syncNode = syncNode;
        std::cout << "Adding sync node (id: " << syncNode->id << ")\n";
    }

    parentLevel->numStrandsLeft--;

    pred->AddSuccessor(AddEdge(), syncNode, currentEdge);
    syncNode->AddPredecessor(AddEdge(), pred, currentEdge);

    lastNode = syncNode;
    afterSpawn = false;
}

void SPDAG::Print()
{
    std::cout << "Series Parallel DAG - Node count: " << nodes.size() << " - Edge count: " << (edges.size() / 2) << "\n";
    for (auto& edge : edges)
    {
        assert(edge->from);
        assert(edge->to);
        if (edge->forward)
            std::cout << "(" << edge->id << ") " << edge->from->id << " --> " << edge->to->id <<
            " (weight: " << edge->data.memAllocated << ")\n";
    }
}
