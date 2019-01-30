
#include "hooks.h"
#include <thread>
#include <stdlib.h>
#include <cstring>


bool fullSPDAG = true;
bool runOnline = false;
bool runEfficient = false;

int64_t memLimit = 10000;
int64_t p = 2;


OutputPrinter out{ std::cout };
OutputPrinter alwaysOut{ std::cout };
SPDAG* dag = nullptr;
SPEdgeData currentEdge;

extern size_t currentLevel;

std::thread* aggregatingThread = nullptr;

void SetOption(int64_t* option, const char* envVarName) {
    char* string = getenv(envVarName);

    if (string == nullptr)
        return;

    int64_t val = std::atoll(string);
    if (val != 0)
        *option = val;
}

void SetOption(bool* option, const char* envVarName, const char* trueString, const char* falseString) {
    char* string = getenv(envVarName);

    if (string == nullptr)
        return;

    if (strcmp(string, trueString) == 0)
        *option = true;
    else if (strcmp(string, falseString) == 0)
        *option = false;
}

void GetOptionsFromEnvironment() {
    char* cilkWorkers = getenv("CILK_NWORKERS");
    if (cilkWorkers == nullptr || strcmp(cilkWorkers, "1") != 0)
    {
        alwaysOut << "ERROR: To run the tool you must set CILK_NWORKERS=1\n";
        exit(-1);
    }

    SetOption(&fullSPDAG, "MHWM_FullSPDAG", "1", "0");
    SetOption(&runOnline, "MHWM_Online", "1", "0");
    SetOption(&runEfficient, "MHWM_Efficient", "1", "0");
    SetOption(&memLimit, "MHWM_MemLimit");
    SetOption(&p, "MHWM_NumProcessors");
}

extern "C" {

    void AggregateComponentsOnline() {
        int64_t threshold = memLimit / (2 * p);

        SPEdgeProducer* producer = nullptr;
        SPEventBareboneOnlineProducer* eventProducer = nullptr;


        if (fullSPDAG)
            producer = new SPEdgeFullOnlineProducer{ static_cast<FullSPDAG*>(dag) };
        else
        {
            producer = new SPEdgeBareboneOnlineProducer{ static_cast<BareboneSPDAG*>(dag) };
            eventProducer = new SPEventBareboneOnlineProducer{ static_cast<BareboneSPDAG*>(dag) };
        }

        SPComponent aggregated;
        if (runEfficient)
            aggregated = dag->AggregateComponentsEfficient(producer, eventProducer, threshold);
        else
            aggregated = dag->AggregateComponents(producer, eventProducer, threshold);

        int64_t watermark = aggregated.GetWatermark(threshold);

        aggregated.Print();

        alwaysOut << "Memory high-water mark: " << watermark << "\n";
        if (watermark <= (memLimit / 2))
        {
            alwaysOut << "Program will use LESS than " << memLimit << " bytes\n";
        }
        else
        {
            alwaysOut << "Program will use AT LEAST " << (memLimit / 2) << " bytes\n";
        }


        delete producer;
        delete eventProducer;
    }

    void program_start() {
        GetOptionsFromEnvironment();

        if (!dag)
        {
            if (fullSPDAG)
                dag = new FullSPDAG(out);
            else
                dag = new BareboneSPDAG(out);
        }

        out.SetActive(debugVerbose);
    }

    void program_exit() {
        out << "Exiting program\n";

        // Simulate a final sync.
        dag->Sync(currentEdge, 0);

        DEBUG_ASSERT(dag->IsComplete());

        // Print out the Series Parallel dag.
        // dag->Print();

        if (!runOnline)
            dag->WriteDotFile("sp.dot");

        if (!aggregatingThread) // Start aggregation if it wasn't being done online.
            aggregatingThread = new std::thread{ AggregateComponentsOnline };

        aggregatingThread->join();

        delete dag;
    }

    // Prepend the size to each allocated block so it can be retrieved
    // when calling free().
    void* __csi_interpose_malloc(size_t size) {
        uint8_t* mem = (uint8_t*)malloc(sizeof(size_t) + size);

        if (size == 0) // Treat zero-allocations as non-zero for sake of testing.
            size = 1;

        currentEdge.memAllocated += size;

        if (currentEdge.memAllocated > currentEdge.maxMemAllocated)
            currentEdge.maxMemAllocated = currentEdge.memAllocated;

        // Store the size of the allocation.
        memcpy(mem, &size, sizeof(size_t));

        return mem + sizeof(size_t);
    }

    void  __csi_interpose_free(void* mem) {
        uint8_t* addr = (uint8_t*)mem;
        addr = addr - sizeof(size_t);

        size_t size = 0;
        memcpy(&size, addr, sizeof(size_t));
        DEBUG_ASSERT(size > 0);

        currentEdge.memAllocated -= size;

        free(addr);
    }

    void  __csi_before_call(const csi_id_t call_id, const csi_id_t func_id,
        const call_prop_t prop) {}

    void  __csi_after_call(const csi_id_t call_id, const csi_id_t func_id,
        const call_prop_t prop) {}

    void  __attribute__((noinline))  __csi_detach(const csi_id_t detach_id, const int32_t* has_spawned) {
        out << "Spawn id " << detach_id << " (spawned: " << *has_spawned << ") - Addr: " << has_spawned
            << " - Level: " << currentLevel << "\n ";

        dag->Spawn(currentEdge, (uintptr_t)has_spawned);
        currentEdge = SPEdgeData();

        out << "-----------------------\n";

        if (runOnline && !aggregatingThread) // Start aggregation online.
            aggregatingThread = new std::thread{ AggregateComponentsOnline };
    }

    void __csi_task(const csi_id_t task_id, const csi_id_t detach_id) {}

    void __csi_task_exit(const csi_id_t task_exit_id, const csi_id_t task_id,
        const csi_id_t detach_id) {
        out << "Task exit\n";

        dag->Sync(currentEdge, 0);
        currentEdge = SPEdgeData();

        out << "-----------------------\n";
    }

    void __csi_detach_continue(const csi_id_t detach_continue_id,
        const csi_id_t detach_id) {}

    void __csi_before_sync(const csi_id_t sync_id, const int32_t* has_spawned) {}

    void  __attribute__((noinline))  __csi_after_sync(const csi_id_t sync_id, const int32_t* has_spawned) {
        out << "Sync id " << sync_id << " (spawned: " << *has_spawned << ") - Addr: " << has_spawned
            << " - Level: " << currentLevel << "\n ";

        if (*has_spawned <= 0)
            return;

        dag->Sync(currentEdge, (uintptr_t)has_spawned);
        currentEdge = SPEdgeData();

        out << "-----------------------\n";
    }
}
