LEVEL= ..
PROG=test
BB_ABC_RT_LIB=$(LEVEL)/Debug+Asserts/lib/libbb_call_site_tracing_rt.a
ABC_RT_LIB=$(LEVEL)/Debug+Asserts/lib/libcall_site_tracing_rt.a
BABC_RT_LIB=$(LEVEL)/Debug+Asserts/lib/libbursty_call_site_tracing_rt.a
AABC_RT_LIB=$(LEVEL)/Debug+Asserts/lib/libadv_call_site_tracing_rt.a
AWABC_RT_LIB=$(LEVEL)/Debug+Asserts/lib/libaw_call_site_tracing_rt.a
FABC_RT_LIB=$(LEVEL)/Debug+Asserts/lib/libfine_call_site_tracing_rt.a
CGC_RT_LIB=$(LEVEL)/Debug+Asserts/lib/libcall_edge_tracing_rt.a
CGC_LLVM_LIB=$(LEVEL)/Debug+Asserts/lib/LLVMCallEdgeTracing.so
ABC_LLVM_LIB=$(LEVEL)/Debug+Asserts/lib/LLVMCallSiteTracing.so
REORDER_LIB=$(LEVEL)/Debug+Asserts/lib/LLVMFunctionReordering.so
BB_REORDER_LIB=$(LEVEL)/Debug+Asserts/lib/LLVMBBReordering.so
ICC_LLVM_LIB=$(LEVEL)/Debug+Asserts/lib/LLVMCacheCounting.so
ICC_RT_LIB=$(LEVEL)/Debug+Asserts/lib/libcache_counting_rt.a
LIBPAPI=$(HOME)/usr/local/lib/libpapi.a
BITCODED = $(PROG).linked.bc
COMPILED = $(PROG).out
BB_ABC_TR	= $(PROG).bbabc.tr
ABC_TR	= $(PROG).abc_tr.out
BABC_TR	= $(PROG).babc_tr.out
AABC_TR = $(PROG).aabc_tr.out
FABC_TR = $(PROG).fabc_tr.out
AWABC_TR = $(PROG).awabc_tr.out
ABC_OUT	= $(PROG).abc.out
BB_ABC_OUT = $(PROG).bbabc.out

BABC_OUT = $(PROG).babc.out
ICC_OUT = $(PROG).icc.out

CGC_OUT = $(PROG).cgc.out
CGC_TR = $(PROG).cgc_tr.out

$(BITCODED): test.c test_util.c
	clang  -flto test.c -o test.bc -c -O2
	clang  -flto test_util.c -o test_util.bc -c -O2
	llvm-link test.bc test_util.bc -o $@

$(COMPILED): $(BITCODED)
	clang -flto $(BITCODED) -o $@ -O2
