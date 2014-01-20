Synopsis
========

ABC-optimizer (Affinity-Based Code optimizer) uses function-level affinity information to optimize the code layout by placing closely called functions nearby. 
In order to process affinity information, the program needs to be instrumented using an LLVM pass. 
The instrumented program will then be used during the development stage and it will release the optimized code layout upon every exit. The code layout will then be changed using another LLVM pass.

LLVM Libraries
--------------
The library code used to process affinity information and release optimized code layout is located in 
"lib/FunctionTracing/CallSiteTracing". The instrumentations of this pass require runtime functions which are located in "runtime/FunctionTracing/CallSiteTracing".

The library code used to change the code layout is located in "lib/FunctionReordering".

Test code
---------
The example test code and instructions on how to run the test is located in "example".

Requirements
============
This tool requires LLVM and Clang to be built.

http://clang.llvm.org

Configure and Build
==========================
To configure the tool, run

    $ ./configure --with-llvmsrc=path/llvm/source/tree --with-llvmobj=path/to/llvm/object/tree

To build the tool, run

    $ make
  
To run the test, go into the "example" directory and run

    $ make prog.abc_tr.out
  
This will generate the instrumented program as the executable "prog.abc_tr.out".
To run the instrumented program, make sure that the environment variables 
SAMPLE_RATE, MAX_WINDOW_SIZE, and MAX_FREQ_LEVEL, MEMORY_LIMIT are set appropriately.

SAMPLE_RATE determines the rate at which windows are sampled. The closer to one this rate is set, the 
more accurate affinity information will be obtained. However, the performance linearly depends on this rate too.
However, since the current test program goes over small number of instruction, you can set it to 1.

MAX_WINDOW_SIZE controls the maximum size each sampled window can grow up to. The performance 
of the algorithm at worst case depends quadratically with this size, though in practice it is better than that.
For this test you can set it to 3.

MAX_FREQ_LEVEL controls the granularity of the analysis. The larger value it is set to, the more accurate information
will be obtained. For this test case, you can set it to 10.

MEMORY_LIMIT (in bytes) bounds the size of the profiling runtime memory. Set it according to your memory limit.

Then simply run

    $ ./prog.abc_tr.out
  
This will generate the new code layout in the file named "layout.abc".
Then run

    $ make prog.abc.out
  
This will generate the new executable prog.abc.out which conforms to the code layout in "layout.abc".
