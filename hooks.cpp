
#include "hooks.h"

size_t currentLevel = 0;
size_t mainLevel = 0;

extern SPDAG* dag;

bool started = false;
bool inInstrumentation = false;
uint32_t mainThread = 0;


extern "C" {

    void program_start();
    void program_exit();

    void __csi_init() {}

    void __csi_unit_init(const char * const file_name,
        const instrumentation_counts_t counts) {}

    __attribute__((noinline))   void __csi_func_entry(const csi_id_t func_id, const func_prop_t prop) {
        inInstrumentation = true;

        char* functionName = __csi_get_func_source_loc(func_id)->name;

        if (!started && functionName != nullptr && strcmp(functionName, "main") == 0)
        {
            auto id = std::this_thread::get_id();
            mainThread = *((uint32_t*)(&id));
            program_start();
            started = true;
            mainLevel = currentLevel + 1;
        }
        
        if (started)
        {
            currentLevel++;
            dag->IncrementLevel();
        }

        inInstrumentation = false;
    }

    __attribute__((always_inline)) void __csi_func_exit(const csi_id_t func_exit_id,
        const csi_id_t func_id, const func_exit_prop_t prop) {

        inInstrumentation = true;

        char* functionName = __csi_get_func_source_loc(func_id)->name;

        if (currentLevel == mainLevel && functionName != nullptr && strcmp(functionName, "main") == 0)
        {
            program_exit();
            started = false;
        }
        
        if (started)
        {
            dag->DecrementLevel();
            currentLevel--;
        }

        inInstrumentation = false;
    }
}
