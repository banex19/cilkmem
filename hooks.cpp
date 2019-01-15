
#include "hooks.h"

size_t currentLevel = 0;

extern SPDAG dag;

extern "C" {

    void program_exit();

    void __csi_init() {
    }

    void __csi_unit_init(const char * const file_name,
        const instrumentation_counts_t counts) {
    }

    __attribute__((noinline))   void __csi_func_entry(const csi_id_t func_id, const func_prop_t prop)
    {
        currentLevel++;
        dag.IncrementLevel();
    }

    __attribute__((always_inline)) void __csi_func_exit(const csi_id_t func_exit_id,
        const csi_id_t func_id, const func_exit_prop_t prop)
    {
        if (strcmp(__csi_get_func_source_loc(func_id)->name, "main") == 0)
        {
            program_exit();
        }

        dag.DecrementLevel();
        currentLevel--;
    }
}
