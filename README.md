# Cilkmem
A memory high-water mark analysis tool for Cilk-based programs.

# To build
First, you will need to build LLVM with up-to-date CSI support. Please pull:
  * the **WIP-taskinfo** branch of Tapir-LLVM (into a folder `llvm`); 
  * the **WIP-csi-tapir-exceptions** branch of Tapir-Clang (into `llvm/tools/clang`);
  * the **WIP-cilksan-bugfixes** branch of Tapir-compiler-rti (into `llvm/projects/compiler-rt`). 

After you've pulled the three branches, build LLVM, Clang and the CSI runtime (`make clang csi` should suffice). Make sure you also have the Cilk runtime (`cilkrts`), available [here](https://github.com/CilkHub/cilkrts), installed in the system.

Then it's enough to run:
```
LLVM_BIN=/path/to/llvm/build/bin LLVM_DIR=/path/to/llvm make
```
assuming LLVM is built into `/path/to/llvm/build` and the source is in `/path/to/llvm`.

# To run
After you've built the tool, you will have two binaries (`normal` and `instr`). These binaries are the result of compiling the Cilk program defined in `test.cpp`.

The `normal` binary is not instrumented. Running `normal` will run the Cilk program without anything else happening.

The `instr` binary is instrumented with the memory high-water mark tool. Before running `instr`, **make sure you run the instrumented program with only one Cilk worker!**. To do this:
```
CILK_NWORKERS=1 ./instr
```

# Tool's options
You can use the following environmental variables to set some of the tool's options:
  * **MHWM_FullSPDAG=1** -> Make the tool keep more information on the SP DAG so that it can be output as a graph for easier visualization.
  * **MHWM_Online=1** -> Run the memory high-water mark algorithm online. If set to 0, the algorithm is run at the end of the program.
  * **MHWM_Efficient=1** -> Run the memory-efficient version of the algorithm.

You can also configure the memory limit you want to test the program against and the number of processors (respectively `M` and `p`, used to calculate `2M/p` by the algorithm):
  * **MHWM_MemLimit=(value)**
  * **MHWM_NumProcessors=(value)**
