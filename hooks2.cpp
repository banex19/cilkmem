
#include "hooks.h"
#include "SeriesParallelDAG.h"

SPDAG dag;
SPEdgeData currentEdge;

extern "C" {
    void program_exit() {
        std::cout << "Exiting program\n";

        // Simulate a final sync.
        dag.Sync(currentEdge, false);

        // Print out the Series Parallel DAG.
        dag.Print();
    }

    void __csi_before_call(const csi_id_t call_id, const csi_id_t func_id,
        const call_prop_t prop)
    {
        currentEdge.memAllocated += 10;
    }

    void __csi_after_call(const csi_id_t call_id, const csi_id_t func_id,
        const call_prop_t prop)
    {

    }
    void  __attribute__((noinline))  __csi_detach(const csi_id_t detach_id)
    {
        std::cout << "Spawn\n";
        dag.Spawn(currentEdge);
        currentEdge = SPEdgeData();
    }

    void __csi_task(const csi_id_t task_id, const csi_id_t detach_id,
        void *sp)
    {

    }

    void __csi_task_exit(const csi_id_t task_exit_id, const csi_id_t task_id,
        const csi_id_t detach_id)
    {
        std::cout << "Task exit\n";
        dag.Sync(currentEdge, true);
        currentEdge = SPEdgeData();
    }

    void __csi_detach_continue(const csi_id_t detach_continue_id,
        const csi_id_t detach_id)
    {
    }

    void  __attribute__((noinline))  __csi_sync(const csi_id_t sync_id)
    {
        std::cout << "Sync\n";
        dag.Sync(currentEdge, false);
        currentEdge = SPEdgeData();
    }
}
