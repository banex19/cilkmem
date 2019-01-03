#include "SeriesParallelDAG.h"
#include <assert.h>
#include <fstream>

// We have spawned a new task. Create the spawn node.
void SPDAG::Spawn(SPEdgeData & currentEdge)
{
    SPNode* spawnNode = AddNode();

    std::cout << "Adding spawn node (id: " << spawnNode->id << ")\n";



    if (currentStack.size() == 0 || afterSpawn)
    {
        SPLevel* parentLevel = GetParentLevel();

        SPLevel* newLevel = new SPLevel(currentLevel, spawnNode);
        currentStack.push_back(newLevel);

        if (parentLevel != nullptr)
        {
            SPNode* parent = parentLevel->currentNode;
            parent->AddSuccessor(AddEdge(), spawnNode, currentEdge, true);
        }

        SPNode* syncNode = AddNode();
        newLevel->syncNodes.push_back(syncNode);

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
            parentLevel->functionLevels.back() < currentLevel)
        {
            SPNode* syncNode = AddNode();
            parentLevel->syncNodes.push_back(syncNode);
            parentLevel->functionLevels.push_back(currentLevel);

            std::cout << "Adding sync node (id: " << syncNode->id << ")\n";
        }
        else {
            assert(parentLevel->functionLevels.size() > 0 &&
                parentLevel->functionLevels.back() == currentLevel);
            parentLevel->syncNodes.back()->numStrandsLeft = 2;
        }

        SPNode* pred = parentLevel->currentNode;
        parentLevel->currentNode = spawnNode;
        pred->AddSuccessor(AddEdge(), spawnNode, currentEdge);
    }

    lastNode = spawnNode;
    afterSpawn = true;
}

void SPDAG::Sync(SPEdgeData & currentEdge, bool taskExit)
{
    if (nodes.size() == 0)
        return;

    SPLevel* parentLevel = GetParentLevel();
    assert(parentLevel != nullptr);

    std::cout << "DAG sync: level " << currentStack.size() - 1 << "\n";
    if (parentLevel->syncNodes.size() > 0)
    {
        std::cout << "Left to sync for node " << parentLevel->syncNodes.back()->id << ": " <<
            parentLevel->syncNodes.back()->numStrandsLeft << "\n";
    }
    

    SPNode* pred = lastNode;

    if (parentLevel->syncNodes.size() == 0) // Is this sync for the upper level?
    {
        delete parentLevel;
        currentStack.pop_back();

        std::cout << "Finished level " << currentStack.size() << "\n";

        if (currentStack.size() == 0) // The program is exiting.
        {
            std::cout << "Adding exit node\n";
            SPNode* exitNode = AddNode();
            pred->AddSuccessor(AddEdge(), exitNode, currentEdge);
            return;
        }

        parentLevel = GetParentLevel();
        assert(parentLevel != nullptr);
        assert(parentLevel->syncNodes.size() > 0);
        assert(parentLevel->syncNodes.back()->numStrandsLeft == 2);

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
                std::cout << " [spawn]";

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
