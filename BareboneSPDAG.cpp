#include "SeriesParallelDAG.h"
#include "SPEdgeProducer.h"

void BareboneSPDAG::Spawn(SPEdgeData & currentEdge, size_t regionId) {
    SPEvent event;
    event.spawn = 1;

    if (stack.size() == 0 || afterSpawn ||
        stack.back().regionId != regionId || stack.back().level != currentLevel)
    {
        event.newSync = 1;
        stack.push_back(SPBareboneLevel(regionId, currentLevel, 2));
    }
    else
    {
        event.newSync = 0;
        stack.back().remaining = 2;
    }

    out << "Spawn region: " << regionId << " - level: " << currentLevel << "\n";

    edges.push_back(AddEdge(currentEdge));
    events.push_back(event);
    afterSpawn = true;
    spawnedAtLeastOnce = true;
}

void BareboneSPDAG::Sync(SPEdgeData & currentEdge, size_t regionId) {
    if (!spawnedAtLeastOnce) // If there wasn't a spawn before, this is the final simulated sync.
    {
        isComplete = true;
        return;
    }

    SPEvent event;
    event.spawn = 0;

    DEBUG_ASSERT(!IsComplete());

    if (stack.size() == 0)
    {
        // Exiting program.
        DEBUG_ASSERT(regionId == 0);

        isComplete = true;
    }
    else
    {
        DEBUG_ASSERT(regionId == 0 || stack.back().regionId == regionId);
        DEBUG_ASSERT(stack.back().level == currentLevel);

        SPBareboneLevel& current = stack.back();

        if (current.remaining == 1)
            stack.pop_back();
        else
            current.remaining--;

        out << "Sync at level " << currentLevel << "\n";
    }

    edges.push_back(AddEdge(currentEdge));
    events.push_back(event);
    afterSpawn = false;
}

SPComponent BareboneSPDAG::AggregateComponents(SPEdgeProducer * edgeProducer, SPEventBareboneOnlineProducer* eventProducer, int64_t threshold) {
    if (IsComplete() && !spawnedAtLeastOnce)
        return SPComponent();

    SPComponent start{ edgeProducer->NextData() };

    SPEvent event = eventProducer->Next();
    DEBUG_ASSERT(event.spawn);

    start.CombineSeries(AggregateComponentsSpawn(edgeProducer, eventProducer, threshold));

    SPComponent end{ edgeProducer->NextData() };

    start.CombineSeries(end);

    event = eventProducer->Next();
    DEBUG_ASSERT(!event.spawn);

    // Make sure there are no more edges to consume.
    SPBareboneEdge* next = edgeProducer->NextBarebone();
    DEBUG_ASSERT_EX(next == nullptr, "There are still edges left with value %zu", next->data.memAllocated);
    DEBUG_ASSERT(!eventProducer->HasNext());
    DEBUG_ASSERT(IsComplete());

    eventProducer->FreeLast();

    return start;
}

SPComponent BareboneSPDAG::AggregateComponentsSpawn(SPEdgeProducer * edgeProducer, SPEventBareboneOnlineProducer* eventProducer, int64_t threshold) {

    out << "Aggregating from spawn\n";

    SPComponent spawnPath = AggregateUntilSync(edgeProducer, eventProducer, false, threshold);

    SPComponent continuation = AggregateUntilSync(edgeProducer, eventProducer, true, threshold);

    spawnPath.CombineParallel(continuation, threshold);

    return spawnPath;
}

SPComponent BareboneSPDAG::AggregateUntilSync(SPEdgeProducer * edgeProducer, SPEventBareboneOnlineProducer * eventProducer, bool continuation, int64_t threshold) {
    out << "Aggregating until sync (continuation: " << continuation << ") - ";

    SPComponent path;
    SPEvent event = eventProducer->Next();

    if (!event.spawn) // Single-edge sub-component.
    {
        path = SPComponent(edgeProducer->NextData());
        out << "Only one component\n";
        return path;
    }

    out << "With subcomponents\n";

    bool delegatedContinuation = false;
    while (!delegatedContinuation && event.spawn)
    {
        path.CombineSeries(SPComponent(edgeProducer->NextData())); // Combine in series with the edge going to the spawn.

        path.CombineSeries(AggregateComponentsSpawn(edgeProducer, eventProducer, threshold)); // Combine in series with the spawn.

        if (!event.newSync) // This is a multispawn sub-component, therefore it will sync for us.
        {
            DEBUG_ASSERT(continuation);
            delegatedContinuation = true;
        }

        if (!delegatedContinuation)
            event = eventProducer->Next();
    }

    // If we haven't delegated the sync to a sub-component,
    // we must acknowledge the sync here.
    if (!delegatedContinuation)
    {
        DEBUG_ASSERT(!event.spawn);
        path.CombineSeries(edgeProducer->NextData());
    }

    out << "Finished aggregating (continuation: " << continuation << ")\n";

    return path;
}
