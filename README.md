Synopsis
========

ABC-optimizer (Affinity-Based Code optimizer) uses function-level affinity information to optimize the code layout by placing closely called functions nearby. 
In order to process affinity information, the program needs to be instrumented using an LLVM pass. 
The instrumented program will then be used during the development stage and it will release the optimized code layout upon every exit. The code layout will then be changed using another LLVM pass.

LLVM Libraries
--------------
The library code used to process affinity information and release optimized code layout is located in 
"lib/FunctionTracing". The instrumentations of this pass require runtime functions which are located in 
"runtime/FunctionTracing".

The library code used to change the code layout is located in "lib/FunctionReordering".

Test code
---------
The test code and instructions on how to run the test is located in "test".

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
  
To run the test, go into the "test" directory and run

    $ make test.tracer
  
This will generate the instrumented program as the executable "test.tracer".
To run the instrumented program, make sure that the environment variables 
SAMPLE_RATE, TOTAL_FUNCS, MAX_WINDOW_SIZE, and MAX_FREQ_LEVEL are set appropriately.

SAMPLE_RATE determines the rate at which windows are sampled. The closer to one this rate is set, the 
more accurate affinity information will be obtained. However, the performance linearly depends on this rate too.
However, since the current test program goes over small number of instruction, you can set it to 1.

TOTAL_FUNCS needs to be set to a value which is at least equal to the number of functions 
in the program. The total number of functions for this test is 3.

MAX_WINDOW_SIZE controls the maximum size each sampled window can grow up to. The performance 
of the algorithm at worst case depends quadratically with this size, though in practice it is better than that.
For this test you can set it to 3.

MAX_FREQ_LEVEL controls the granularity of the analysis. The larger value it is set to, the more accurate information
will be obtained. For this test case, you can set it to 10.

Then simply run

    $ ./test.tracer
  
This will generate the new code layout in the file named "layout.abc".
To change the code layout based on this file, the environment variable PERM_FILE needs to be set to this file.
Then run

    $ make test.reordered
  
This will generate the new executable test.reordered which conforms to the code layout in "layout.abc".


IMPORTANT: always use gcc for building, for shared libraries, because otherwise you get the infamous "overflow in relocation"
error.
