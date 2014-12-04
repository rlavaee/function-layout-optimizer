LEVEL= ..
PROG=test
LLVM_LIBS=$(LEVEL)/Debug+Asserts/lib
ABC_LLVM_LIB=$(LEVEL)/Debug+Asserts/lib/LLVMCallSiteTracing.so
ABC_RT_LIB=$(LEVEL)/Debug+Asserts/lib/libcall_site_tracing_rt.a
BBABC_RT_LIB=$(LEVEL)/Debug+Asserts/lib/libbb_call_site_tracing_rt.a
BITCODED = $(PROG).linked.bc
COMPILED = $(PROG).out
ABC_TR	= $(PROG).abc.out
BBABC_TR	= $(PROG).bbabc.out
REORDERED = $(PROG).ro.out

$(BITCODED): test.c test_util.c
	clang -g -flto test.c -o test.bc -c -O2
	clang -g -flto test_util.c -o test_util.bc -c -O2
	llvm-link test.bc test_util.bc -o $@

$(COMPILED): $(BITCODED)
	#clang -g -Wl,--plugin-opt,--debug-pass=Details -Wl,--plugin-opt,-print-after-all -flto $< -O2 -o $@ -v 2> debug.out
	clang++ -g -Wl,--plugin-opt,--debug-pass=Details -Wl,--plugin-opt,-print-after-all -flto $< $(ABC_RT_LIB) -O2 -o $@ -v 2> debug.out -lpthread
	#clang -g -Wl,--plugin-opt,--debug-pass=Details -flto $< -O2 -o $@ -v 2> debug.out
	#clang -flto $(BITCODED) -o $@ -O2

$(ABC_TR): $(BITCODED)
	#opt -load $(ABC_LLVM_LIB) -trace-call-sites-func-level <$(BITCODED)> $(BITCODED).abc
	#llc $(BITCODED).abc -o $@.s
	#clang $@.s -o $@ -L$(LLVM_LIBS) -lcall_site_tracing_rt
	#clang -g -Wl,--plugin-opt,--debug-pass=Details -Wl,--plugin-opt,-enable-machine-block-tracing -flto $(BITCODED).abc -O2 -o $@ -L$(LLVM_LIBS) -lcall_site_tracing_rt -v 2> debug.out
	clang++ -g -Wl,--plugin-opt,-print-after-all -Wl,--plugin-opt,-enable-machine-block-tracing -flto $< $(BBABC_RT_LIB) -O2 -o $@ -lpthread
	#clang++ -g -Wl,--plugin-opt,--debug-pass=Details -Wl,--plugin-opt,-print-after-all -flto $(BITCODED).abc -O2 -o $@ $(ABC_RT_LIB) -v 2> debug.abc.out -lpthread

$(BBABC_TR): $(BITCODED)
	#opt -bursty-tracer $(BITCODED) -o $(BITCODED).1
	clang++ -g  -Wl,--plugin-opt,-enable-machine-block-tracing -flto $< $(BBABC_RT_LIB) -O2 -o $@ -lpthread
	#llc -enable-machine-block-tracing  $<  -O2 -o $@.s 
	#clang++ -g $@.s $(BBABC_RT_LIB) -O2 -o $@ -lpthread

$(REORDERED): $(BITCODED)
	#clang -g -Wl,--plugin-opt,-disable-debug-info-print -Wl,--plugin-opt,-asm-verbose -Wl,--plugin-opt,-mbb-layout=layout.bbabc -flto $< -O2 -o $@ -v 2> debug.ro.out
	llc -mbb-layout=layout.bbabc $< -O2 -o $@.s
	clang $@.s -o $@
