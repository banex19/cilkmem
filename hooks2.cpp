
#include "hooks.h"


SPDAG dag;
SPEdgeData currentEdge;

inline std::string demangle(const char* name)
{
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

std::unordered_map<void*, size_t> allocations;
extern size_t currentLevel;

extern "C" {
    void program_exit() {
        std::cout << "Exiting program\n";

        dag.WriteDotFile("sp.dot");

        // Simulate a final sync.
        dag.Sync(currentEdge, false);

        // Print out the Series Parallel DAG.
        dag.Print();

        dag.WriteDotFile("sp.dot");

        int64_t memLimit = 10000;
        int64_t p = 2;
        int64_t threshold = memLimit / (2 * p);

        SPComponent aggregated = dag.AggregateComponents(threshold);

        aggregated.Print();

        int64_t watermark = aggregated.GetWatermark(threshold);

        std::cout << "Watermark: " << watermark << "\n";
        if (watermark <= (memLimit / 2))
        {
            std::cout << "Program will use LESS than " << memLimit << " bytes\n";
        }
        else {
            std::cout << "Program will use AT LEAST " << (memLimit / 2) << " bytes\n";
        }

    }

    void* __csi_interpose_malloc(size_t size) {
        void* mem = malloc(size);

        if (size == 0) // Treat zero-allocations as non-zero for sake of testing.
            size = 1;

        currentEdge.memAllocated += size;

        if (currentEdge.memAllocated > currentEdge.maxMemAllocated)
            currentEdge.maxMemAllocated = currentEdge.memAllocated;

        allocations[mem] = size;

        return mem;
    }

    void  __csi_interpose_free(void* mem) {
        size_t size = allocations[mem];
        assert(size > 0);

        currentEdge.memAllocated -= size;

        allocations[mem] = 0;
        free(mem);
    }

    void __attribute__((noinline))  __csi_before_call(const csi_id_t call_id, const csi_id_t func_id,
        const call_prop_t prop)
    {
        /*  std::string funcName = demangle(__csi_get_callsite_source_loc(call_id)->name);
          if (funcName == "malloc")
              currentEdge.memAllocated += 10;
          std::cout << "Calling function " << funcName
              << " (" << __csi_get_callsite_source_loc(call_id)->line_number << ")\n"; */
    }

    void __attribute__((noinline))  __csi_after_call(const csi_id_t call_id, const csi_id_t func_id,
        const call_prop_t prop)
    {
        //  std::cout << "Return from call to " << __csi_get_callsite_source_loc(call_id)->name
        //      << " (" << __csi_get_callsite_source_loc(call_id)->line_number << ")\n";
    }

    void  __attribute__((noinline))  __csi_detach(const csi_id_t detach_id, const int32_t* has_spawned)
    {
        if (debugVerbose)
            std::cout << "Spawn id " << detach_id << " (spawned: " << *has_spawned << ") - Addr: " << has_spawned
            << " - Level: " << currentLevel << "\n ";

        dag.Spawn(currentEdge, (uintptr_t)has_spawned);
        currentEdge = SPEdgeData();

        if (debugVerbose)
            std::cout << "-----------------------\n";
    }

    void __csi_task(const csi_id_t task_id, const csi_id_t detach_id,
        void *sp)
    {

    }

    void __csi_task_exit(const csi_id_t task_exit_id, const csi_id_t task_id,
        const csi_id_t detach_id)
    {
        if (debugVerbose)
            std::cout << "Task exit\n";

        dag.Sync(currentEdge, 0);
        currentEdge = SPEdgeData();

        if (debugVerbose)
            std::cout << "-----------------------\n";
    }

    void __csi_detach_continue(const csi_id_t detach_continue_id,
        const csi_id_t detach_id)
    {
    }

    void  __attribute__((noinline))  __csi_sync(const csi_id_t sync_id, const int32_t* has_spawned)
    {
        if (debugVerbose)
            std::cout << "Sync id " << sync_id << " (spawned: " << *has_spawned << ") - Addr: " << has_spawned
            << " - Level: " << currentLevel << "\n ";

        if (*has_spawned <= 0)
            return;

        dag.Sync(currentEdge, (uintptr_t)has_spawned);
        currentEdge = SPEdgeData();

        if (debugVerbose)
            std::cout << "-----------------------\n";

    }
}
