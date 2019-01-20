
#include "hooks.h"
#include <thread>


const bool fullSPDAG = true;
const bool runOnline = true;

OutputPrinter out{ std::cout };
OutputPrinter alwaysOut{ std::cout };
SPDAG* dag = nullptr;
SPEdgeData currentEdge;

inline std::string demangle(const char* name) {
    int status = -1;

    std::unique_ptr<char, void(*)(void*)> res{ abi::__cxa_demangle(name, NULL, NULL, &status), std::free };
    if (status != 0)
        return name;

    std::string demangled = res.get();

    if (demangled.find_first_of(' ') < demangled.find_first_of('('))
        demangled = demangled.substr(demangled.find_first_of(' '));

    if (demangled.find_first_of('<') < demangled.find_first_of('('))
        return demangled.substr(0, demangled.find_first_of('<'));
    else
        return demangled.substr(0, demangled.find_first_of('('));
}

extern size_t currentLevel;

std::thread* aggregatingThread = nullptr;

extern "C" {

    void AggregateComponentsOnline() {
        int64_t memLimit = 10000;
        int64_t p = 2;
        int64_t threshold = memLimit / (2 * p);

        SPEdgeProducer* producer = nullptr;

        if (fullSPDAG)
            producer = new SPEdgeFullOnlineProducer{ static_cast<FullSPDAG*>(dag) };

        SPComponent aggregated = dag->AggregateComponents(producer, threshold);

        aggregated.Print();

        int64_t watermark = aggregated.GetWatermark(threshold);

        alwaysOut << "Memory high-water mark: " << watermark << "\n";
        if (watermark <= (memLimit / 2))
        {
            // alwaysOut << "Program will use LESS than " << memLimit << " bytes\n";
        }
        else
        {
            // alwaysOut << "Program will use AT LEAST " << (memLimit / 2) << " bytes\n";
        }

        delete producer;
    }

    void program_start() {
        if (!dag)
        {
            if (fullSPDAG)
                dag = new FullSPDAG(out);
            else
            {
                alwaysOut << "ERROR: Barebone SPDAG not supported yet\n";
                exit(-1);
            }
        }

        if (!debugVerbose)
            out.DisablePrinting();
    }

    void program_exit() {
        out << "Exiting program\n";

        // dag->WriteDotFile("sp.dot");

        // Simulate a final sync.
        dag->Sync(currentEdge, false);

        // Print out the Series Parallel dag.
        // dag->Print();

        // dag->WriteDotFile("sp.dot");

        // if (!aggregatingThread) // Start aggregation if it wasn't being done online.
        //   aggregatingThread = new std::thread{ AggregateComponentsOnline };

        aggregatingThread->join();
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

    void __csi_task(const csi_id_t task_id, const csi_id_t detach_id,
        void *sp) {}

    void __csi_task_exit(const csi_id_t task_exit_id, const csi_id_t task_id,
        const csi_id_t detach_id) {
        out << "Task exit\n";

        dag->Sync(currentEdge, 0);
        currentEdge = SPEdgeData();

        out << "-----------------------\n";
    }

    void __csi_detach_continue(const csi_id_t detach_continue_id,
        const csi_id_t detach_id) {}

    void  __attribute__((noinline))  __csi_sync(const csi_id_t sync_id, const int32_t* has_spawned) {
        out << "Sync id " << sync_id << " (spawned: " << *has_spawned << ") - Addr: " << has_spawned
            << " - Level: " << currentLevel << "\n ";

        if (*has_spawned <= 0)
            return;

        dag->Sync(currentEdge, (uintptr_t)has_spawned);
        currentEdge = SPEdgeData();

        out << "-----------------------\n";
    }
}
