
#include "hooks.h"
#include <thread>
#include <stdlib.h>
#include <cstring>
#include <fstream>


bool fullSPDAG = true;
bool runOnline = false;
bool runEfficient = false;
bool runNaive = true;
bool debugVerbose = false;
bool showSource = true;

std::string outputFile = "";

int64_t memLimit = 10000;
size_t p = 2;


OutputPrinter out{ std::cout };
OutputPrinter alwaysOut{ std::cout };
SPDAG* dag = nullptr;
SPEdgeData currentEdge;

extern size_t currentLevel;
extern bool inInstrumentation;

std::thread* aggregatingThread = nullptr;

template <typename T>
void SetOption(T* option, const char* envVarName) {
    char* string = getenv(envVarName);

    if (string == nullptr)
        return;

    T val = std::atoll(string);
    if (val != 0)
        *option = val;
}

void SetOption(std::string& option, const char* envVarName) {
    char * string = getenv(envVarName);

    if (string == nullptr)
        return;

    option = string;
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
    SetOption(&debugVerbose, "MHWM_Debug", "1", "0");
    SetOption(&runNaive, "MHWM_Naive", "1", "0");
    SetOption(&showSource, "MHWM_Source", "1", "0");
    SetOption(&memLimit, "MHWM_MemLimit");
    SetOption(&p, "MHWM_NumProcessors");
    SetOption(outputFile, "MHWM_OutputFile");

    if (p <= 0)
    {
        alwaysOut << "ERROR: p must be set to a positive value\n";
        exit(-1);
    }
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

        int64_t watermark = 0;
        int64_t watermarkCompare = memLimit / 2;

        if (!runNaive && runEfficient)
        {
            auto aggregated = dag->AggregateComponentsEfficient(producer, eventProducer, threshold);
            aggregated.Print();
            watermark = aggregated.GetWatermark(threshold);
        }
        else if (runNaive)
        {
            SPNaiveComponent aggregated{ p };
            if (runEfficient)
            {
                aggregated = dag->AggregateComponentsNaiveEfficient(producer, eventProducer, threshold, p);
            }
            else
            {
                aggregated = dag->AggregateComponentsNaive(producer, eventProducer, threshold, p);
            }

            std::ofstream* file = nullptr;
            if (outputFile != "")
            {
                file = new std::ofstream{ outputFile };
            }

            for (size_t i = 1; i <= p; ++i)
            {
                watermark = aggregated.GetWatermark(i);

                if (file && *file)
                {
                    *file << "Memory high-water mark for p = " << i << " : " << watermark << "\n";
                }

                alwaysOut << "Memory high-water mark for p = " << i << " : " << watermark << "\n";
            }
         

            if (file)
            {
                file->close();
                delete file;
            }

            watermarkCompare = memLimit;
        }
        else
        {
            auto aggregated = dag->AggregateComponents(producer, eventProducer, threshold);
            aggregated.Print();
            watermark = aggregated.GetWatermark(threshold);
        }

        if (!runNaive)
        {
            alwaysOut << "Memory high-water mark: " << watermark << "\n";
            if (watermark <= watermarkCompare)
            {
                alwaysOut << "The real high-water mark is LESS than " << memLimit << " bytes\n";
            }
            else
            {
                alwaysOut << "The real high-water mark is AT LEAST " << watermarkCompare << " bytes\n";
            }
        }


        delete producer;
        delete eventProducer;
    }

    void program_start() {
        GetOptionsFromEnvironment();
        out.SetActive(debugVerbose);

        if (!dag)
        {
            if (fullSPDAG)
                dag = new FullSPDAG(out);
            else
                dag = new BareboneSPDAG(out);
        }
    }

    void program_exit() {
        out << "Exiting program\n";

        // Simulate a final sync.
        dag->Sync(currentEdge, 0);

        DEBUG_ASSERT(dag->IsComplete());

        // Print out the Series Parallel dag.
        // dag->Print();

        if (!runOnline && fullSPDAG)
            dag->WriteDotFile("sp.dot");

        if (!runOnline)
        {
            aggregatingThread = new std::thread{ AggregateComponentsOnline };

            // AggregateComponentsOnline();
        }

        if (aggregatingThread)
            aggregatingThread->join();

        delete dag;
    }

    std::unordered_map<void*, size_t> allocs;

    // Prepend the size to each allocated block so it can be retrieved
    // when calling free().
    void* __csi_interpose_malloc(size_t size) {
        return malloc(size);
        /*  uint8_t* mem = (uint8_t*)malloc(sizeof(size_t) + size);

          if (size == 0) // Treat zero-allocations as non-zero for sake of testing.
              size = 1;

          currentEdge.memAllocated += size;

          if (currentEdge.memAllocated > currentEdge.maxMemAllocated)
              currentEdge.maxMemAllocated = currentEdge.memAllocated;

          // Store the size of the allocation.
          memcpy(mem, &size, sizeof(size_t));

          allocs[mem] = size;

          return mem + sizeof(size_t); */
    }

    void  __csi_interpose_free(void* mem) {
        free(mem);
        /*   DEBUG_ASSERT(allocs.find(mem) != allocs.end());

           uint8_t* addr = (uint8_t*)mem;
           addr = addr - sizeof(size_t);

           size_t size = 0;
           memcpy(&size, addr, sizeof(size_t));
           DEBUG_ASSERT(size > 0);
           DEBUG_ASSERT(allocs[mem] == size);

           currentEdge.memAllocated -= size;

           free(addr); */
    }

    void  __csi_before_call(const csi_id_t call_id, const csi_id_t func_id,
        const call_prop_t prop) {}

    void  __csi_after_call(const csi_id_t call_id, const csi_id_t func_id,
        const call_prop_t prop) {}

    void  __attribute__((noinline))  __csi_detach(const csi_id_t detach_id, const int32_t* has_spawned) {
        inInstrumentation = true;
        out << "Spawn id " << detach_id << " (spawned: " << *has_spawned << ") - Addr: " << has_spawned
            << " - Level: " << currentLevel;
        if (__csi_get_detach_source_loc(detach_id)->name != nullptr)
            out << " - Source: " << __csi_get_detach_source_loc(detach_id)->name << ":" << __csi_get_detach_source_loc(detach_id)->line_number;
        out << "\n";

        dag->Spawn(currentEdge, (uintptr_t)has_spawned);
        if (showSource && __csi_get_detach_source_loc(detach_id)->name != nullptr)
            dag->SetLastNodeLocation((char*)__csi_get_detach_source_loc(detach_id)->name, __csi_get_detach_source_loc(detach_id)->line_number);

        currentEdge = SPEdgeData();

        out << "-----------------------\n";

        if (runOnline && !aggregatingThread) // Start aggregation online.
            aggregatingThread = new std::thread{ AggregateComponentsOnline };

        inInstrumentation = false;
    }

    void __csi_task(const csi_id_t task_id, const csi_id_t detach_id) {}

    void __csi_task_exit(const csi_id_t task_exit_id, const csi_id_t task_id,
        const csi_id_t detach_id) {
        inInstrumentation = true;

        out << "Task exit ";
        out << " - Source: " << __csi_get_task_exit_source_loc(task_exit_id)->name << ":" << __csi_get_task_exit_source_loc(task_exit_id)->line_number;
        out << "\n";

        dag->Sync(currentEdge, 0);
        if (showSource &&  __csi_get_task_exit_source_loc(task_exit_id)->name != nullptr)
            dag->SetLastNodeLocation((char*)__csi_get_task_exit_source_loc(task_exit_id)->name, __csi_get_task_exit_source_loc(task_exit_id)->line_number);

        currentEdge = SPEdgeData();

        out << "-----------------------\n";

        inInstrumentation = false;
    }

    void __csi_detach_continue(const csi_id_t detach_continue_id,
        const csi_id_t detach_id) {}

    void __csi_before_sync(const csi_id_t sync_id, const int32_t* has_spawned) {}

    void  __attribute__((noinline))  __csi_after_sync(const csi_id_t sync_id, const int32_t* has_spawned) {

        inInstrumentation = true;

        out << "Sync id " << sync_id << " (spawned: " << *has_spawned << ") - Addr: " << has_spawned
            << " - Level: " << currentLevel;
        if (__csi_get_sync_source_loc(sync_id)->name != nullptr)
            out << " - Source: " << __csi_get_sync_source_loc(sync_id)->name << ":" << __csi_get_sync_source_loc(sync_id)->line_number;
        out << "\n";

        if (*has_spawned <= 0)
            return;

        dag->Sync(currentEdge, (uintptr_t)has_spawned);
        if (showSource && __csi_get_sync_source_loc(sync_id)->name != nullptr)
            dag->SetLastNodeLocation((char*)__csi_get_sync_source_loc(sync_id)->name, __csi_get_sync_source_loc(sync_id)->line_number);

        currentEdge = SPEdgeData();

        out << "-----------------------\n";

        inInstrumentation = false;
    }
}
