LEVEL= ..
PROG=prog
ABC_RT_LIB=$(LEVEL)/Debug+Asserts/lib/libcall_site_tracing_rt.a
AABC_RT_LIB=$(LEVEL)/Debug+Asserts/lib/libadv_call_site_tracing_rt.a
AWABC_RT_LIB=$(LEVEL)/Debug+Asserts/lib/libaw_call_site_tracing_rt.a
FABC_RT_LIB=$(LEVEL)/Debug+Asserts/lib/libfine_call_site_tracing_rt.a
CGC_RT_LIB=$(LEVEL)/Debug+Asserts/lib/libcall_edge_tracing_rt.a
CGC_LLVM_LIB=$(LEVEL)/Debug+Asserts/lib/LLVMCallEdgeTracing.so
ABC_LLVM_LIB=$(LEVEL)/Debug+Asserts/lib/LLVMCallSiteTracing.so
REORDER_LIB=$(LEVEL)/Debug+Asserts/lib/LLVMFunctionReordering.so
ICC_LLVM_LIB=$(LEVEL)/Debug+Asserts/lib/LLVMCacheCounting.so
ICC_RT_LIB=$(LEVEL)/Debug+Asserts/lib/libcache_counting_rt.a
LIBPAPI=$(HOME)/usr/local/lib/libpapi.a
BITCODED = $(PROG).bc
COMPILED = $(PROG).out
ABC_TR	= $(PROG).abc_tr.out
AABC_TR = $(PROG).aabc_tr.out
FABC_TR = $(PROG).fabc_tr.out
AWABC_TR = $(PROG).awabc_tr.out
ABC_OUT	= $(PROG).abc.out

BABC_OUT = $(PROG).babc.out
ICC_OUT = $(PROG).icc.out

CGC_OUT = $(PROG).cgc.out
CGC_TR = $(PROG).cgc_tr.out


$(AWABC_TR): $(BITCODED) $(AWABC_RT_LIB) $(ABC_LLVM_LIB)
	opt -load $(ABC_LLVM_LIB) -trace-call-sites $< -o $@.bc
	llc $@.bc -o $@.s
	clang++ $@.s $(AWABC_RT_LIB) -o $@ -lpthread

$(FABC_TR): $(BITCODED) $(FABC_RT_LIB) $(ABC_LLVM_LIB)
	opt -load $(ABC_LLVM_LIB) -trace-call-sites $< -o $@.bc
	llc $@.bc -o $@.s
	clang++ $@.s $(FABC_RT_LIB) -o $@ -lpthread

$(AABC_TR): $(BITCODED) $(AABC_RT_LIB) $(ABC_LLVM_LIB)
	opt -load $(ABC_LLVM_LIB) -trace-call-sites $< -o $@.bc
	llc $@.bc -o $@.s
	clang++ $@.s $(AABC_RT_LIB) -o $@ -lpthread

$(ICC_OUT): $(BITCODED)
	opt -load $(ICC_LLVM_LIB) -add-inst-cache-counters $< <$(BITCODED)> $@.bc
	llc $@.bc -o $@.s
	gcc $@.s $(ICC_RT_LIB) $(LIBPAPI) -o $@ 

$(CGC_OUT): layout.cgc $(BITCODED)
	opt -load $(REORDER_LIB) -rlavaee-reorder-functions -layout $< <$(BITCODED)> $@.bc
	llc $@.bc -o $@.s
	gcc $@.s -o $@

$(CGC_TR): $(BITCODED)
	opt -load $(CGC_LLVM_LIB) -trace-call-edges $< -o $@.bc
	llc $@.bc -o $@.s
	clang++ $@.s $(CGC_RT_LIB) -o $@ -lpthread

$(ABC_OUT): layout.abc $(BITCODED)
	opt -load $(REORDER_LIB) -rlavaee-reorder-functions -layout $< <$(BITCODED)> $@.bc
	llc $@.bc -o $@.s
	gcc $@.s -o $@

$(ABC_TR): $(BITCODED)
	opt -load $(ABC_LLVM_LIB) -trace-call-sites $< -o $@.bc
	llc $@.bc -o $@.s
	clang++ $@.s $(ABC_RT_LIB) -o $@ -lpthread

$(BITCODED): test.c test_util.c
	clang -emit-llvm test.c -o test.bc -c -O3
	clang -emit-llvm test_util.c -o test_util.bc -c -O3
	llvm-link test.bc test_util.bc -o $@

$(COMPILED): $(BITCODED)
	llc -fno-asynchronous-unwind-tables $< -o $(PROG).s
	clang $(PROG).s -o $@
 

