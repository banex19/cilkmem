CSICLANG?=$(LLVM_BIN)/clang
CSICLANGPP?=$(LLVM_BIN)/clang++
LLVMLINK?=$(LLVM_BIN)/llvm-link

EXTRAFLAGS?=""
CXXFLAGS?=-O3 -g -std=c++11 $(EXTRAFLAGS)

all: check-vars check-files instr normal debug

check-vars:
ifndef LLVM_DIR
  $(error LLVM_DIR is undefined - please define LLVM_DIR as the directory containing the source of LLVM, e.g. /whatever/llvm)
endif
ifndef LLVM_BIN
  $(error LLVM_BIN is undefined - please define LLVM_BIN as the directory containing the binaries of LLVM, e.g. /whatever/llvm/build/bin)
endif

memoryhook.so:  MemoryHook.cpp
	$(CSICLANGPP) $(CXXFLAGS) -fPIC -shared MemoryHook.cpp -o memoryhook.so 

# Some checks that files exist.
check-files:
	@test -s $(LLVM_DIR)/projects/compiler-rt/lib/csi/csirt.c || { echo "LLVM does not contain CSI in projects/compiler-rt! Exiting."; exit 1; }
	@test -s $(LLVM_BIN)/../lib/clang/6.0.0/lib/linux/libclang_rt.csi-x86_64.a || { echo "LLVM does not contain the CSI runtime in the lib folder! Exiting."; exit 1; }

# These targets build the tool (first compiling to IR, and then to object files).
tool1.bc: hooks.cpp toolheaders
	$(CSICLANGPP) -O3 -S -emit-llvm hooks.cpp -o tool1.bc

tool2.bc: hooks2.cpp toolheaders 
	$(CSICLANGPP) -O3 -S -emit-llvm hooks2.cpp -o tool2.bc

tool3.bc: FullSPDAG.cpp toolheaders
	$(CSICLANGPP) -O3 -S -emit-llvm FullSPDAG.cpp -o tool3.bc

tool4.bc: SPComponent.cpp toolheaders
	$(CSICLANGPP) -O3 -S -emit-llvm SPComponent.cpp -o tool4.bc

tool5.bc: BareboneSPDAG.cpp toolheaders
	$(CSICLANGPP) -O3 -S -emit-llvm BareboneSPDAG.cpp -o tool5.bc

tool.bc: tool1.bc tool2.bc tool3.bc tool4.bc tool5.bc
	$(LLVMLINK) tool1.bc tool2.bc tool3.bc tool4.bc tool5.bc -o tool.bc

hooks1.o: hooks.cpp toolheaders
	$(CSICLANGPP) $(CXXFLAGS) -c hooks.cpp -o hooks1.o
	
hooks2.o: hooks2.cpp toolheaders
	$(CSICLANGPP) $(CXXFLAGS) -c hooks2.cpp -o hooks2.o

hooks3.o: FullSPDAG.cpp toolheaders
	$(CSICLANGPP) $(CXXFLAGS) -c FullSPDAG.cpp -o hooks3.o

hooks4.o: SPComponent.cpp toolheaders
	$(CSICLANGPP) $(CXXFLAGS) -c SPComponent.cpp -o hooks4.o

hooks5.o: BareboneSPDAG.cpp toolheaders
	$(CSICLANGPP) $(CXXFLAGS) -c BareboneSPDAG.cpp -o hooks5.o

tool.o: hooks1.o hooks2.o hooks3.o hooks4.o hooks5.o 
	ld -r hooks1.o hooks2.o hooks3.o hooks4.o hooks5.o -o tool.o

toolheaders: OutputPrinter.h MemPoolVector.h SeriesParallelDAG.h hooks.h common.h SPEdgeProducer.h Nullable.h SingleThreadPool.h
	touch toolheaders

# This is where the Cilk program is instrumented. This uses compile-time instrumentation, so it needs the tool's bitcode.
instr.o: tool.bc test.cpp csirt.bc config.txt
	$(CSICLANGPP) -fcilkplus $(CXXFLAGS) -c -fcsi=aftertapirloops test.cpp -mllvm -csi-config-mode -mllvm "whitelist" -mllvm -csi-config-filename -mllvm "config.txt" -mllvm -csi-tool-bitcode -mllvm "tool.bc" -mllvm -csi-runtime-bitcode -mllvm "csirt.bc" -mllvm -csi-instrument-basic-blocks=false -mllvm -csi-instrument-memory-accesses=false -mllvm -csi-instrument-atomics=false -mllvm -csi-instrument-memintrinsics=false -mllvm -csi-instrument-allocfn=false -mllvm -csi-instrument-alloca=false -o instr.o 

# This target outputs some extra information like the IR and the ASM of the Cilk program after instrumentation.
debug: tool.bc test.cpp csirt.bc 
	$(CSICLANGPP) -fcilkplus $(CXXFLAGS) -S -emit-llvm -fcsi=aftertapirloops test.cpp -mllvm -csi-tool-bitcode -mllvm "tool.bc" -mllvm -csi-runtime-bitcode -mllvm "csirt.bc"  -o ir.txt 
	$(CSICLANGPP) -fcilkplus -O3 -fverbose-asm -S -masm=intel -fcsi=aftertapirloops test.cpp -mllvm -csi-tool-bitcode -mllvm "tool.bc" -mllvm -csi-runtime-bitcode -mllvm "csirt.bc"  -o asm.txt
	touch debug

# This target simply builds the Cilk program with no instrumentation at all.
normal: test.cpp
	$(CSICLANGPP) $(CXXFLAGS) -fcilkplus  test.cpp -o normal

# Link the instrumented program together.
instr: tool.o instr.o  memoryhook.so
	$(CSICLANGPP) $(CXXFLAGS) ./memoryhook.so instr.o tool.o  $(LLVM_BIN)/../lib/clang/6.0.0/lib/linux/libclang_rt.csi-x86_64.a -lcilkrts -lpthread -o instr

# Get the bitcode of the CSI runtime.
csirt.bc: $(LLVM_DIR)/projects/compiler-rt/lib/csi/csirt.c
	$(CSICLANG) -O3 -c -emit-llvm -std=c11 $(LLVM_DIR)/projects/compiler-rt/lib/csi/csirt.c -o csirt.bc

clean:
	rm -f normal instr *.o *.bc ir.txt asm.txt *.so
