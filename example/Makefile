LEVEL= ..
PROG=prog
TRACE_RT_LIB=$(LEVEL)/Debug+Asserts/lib/libCallEdgeTracing_rt.a
TRACE_LLVM_LIB=$(LEVEL)/Debug+Asserts/lib/LLVMCallEdgeTracing.so
REORDER_LIBS=$(LEVEL)/Debug+Asserts/lib/LLVMFunctionReordering.so
BITCODED = $(PROG).bc
COMPILED = $(PROG).out
TRACER 	= $(PROG).tracer.out
OPTIMIZED	= $(PROG).opt.out

$(REORDER):
	opt -load $(REORDER_LIBS) -rlavaee-reorder-functions <$(COMPILE)> $(PROG).opt.bc
	llc $(PROG).opt.bc -o $(PROG).opt.s
	gcc $(PROG).opt.s -o $@

$(BITCODED): test.c test_util.c
	clang -emit-llvm test.c -o test.bc -c -O3
	clang -emit-llvm test_util.c -o test_util.bc -c -O3
	llvm-link test.bc test_util.bc -o $@


$(COMPILED): $(BITCODED)
	llc $< -o $(PROG).s
	clang $(PROG).s -o $@
 
$(TRACER): $(BITCODED)
	opt -load $(TRACE_LLVM_LIB) -trace-call-edges $< -o $(PROG).tracer.bc
	llc $(PROG).tracer.bc -o $(PROG).tracer.s
	clang++ $(PROG).tracer.s $(TRACE_RT_LIB) -o $@ -lpthread

clean:
	rm $(REORDER) $(TRACE) $(COMPILE)

