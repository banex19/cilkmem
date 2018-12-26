
#include "hooks.h"

int inCSI_Table[2] = { 0,0 };

class TestClass {
public:
    TestClass()
    {
        std::cout << "Constructor\n";
        mem = new char[100];
        printf("this %p - mem: %p\n", this, mem);
    }

    ~TestClass() { std::cout << "Destructor\n"; delete[] mem; }

    char* mem = nullptr;
};

std::unordered_map<std::string, size_t> callCount;
//TestClass testClass;

size_t totalNumCalls = 0;



extern "C" {

    void program_exit();

    void __csi_init() {
    }

    void __csi_unit_init(const char * const file_name,
        const instrumentation_counts_t counts) {

        printf("Translation unit: %s\n", file_name);
    }

    __attribute__((noinline))   void __csi_func_entry(const csi_id_t func_id, const func_prop_t prop)
    {
        //  printf("Address of inCSI_Table: %p\n", inCSI_Table);

        int* inCSI = &inCSI_Table[0];
        if (*inCSI)
            return;

        totalNumCalls++;

        *inCSI = 1;

        // if (func_id != 0)
        //printf("Enter function\t%lld [%s]\n", func_id,
     //       __csi_get_func_source_loc(func_id)->name
     //   );

        // printf("Test %p - mem: %p\n", &testClass, testClass.mem);

         //printf("Call count var: %p\n", &callCount);

        callCount[__csi_get_func_source_loc(func_id)->name]++;


        *inCSI = 0;
    }

    __attribute__((always_inline)) void __csi_func_exit(const csi_id_t func_exit_id,
        const csi_id_t func_id, const func_exit_prop_t prop)
    {
        // printf("Address of inCSI_Table: %p\n", inCSI_Table);

        int* inCSI = &inCSI_Table[1];
        //  if (*inCSI)
        //      return;

        *inCSI = 1;

        // printf("Exit function\t%lld [%s]\n", func_id,
       //      __csi_get_func_source_loc(func_id)->name);

        if (strcmp(__csi_get_func_source_loc(func_id)->name, "main") == 0)
        {
            program_exit();
        }

        //   *inCSI = 0;
    }
}
/*
int main()
{
    std::cout << "main\n";
    printf("Test: %p\n", testClass.mem);
    std::cout << "Done\n";
    return 0;
}*/
