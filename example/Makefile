LEVEL= ..
PROG=test
LLVM_LIBS=$(LEVEL)/Debug+Asserts/lib
ABC_LLVM_LIB=$(LEVEL)/Debug+Asserts/lib/LLVMCallSiteTracing.so
ABC_RT_LIB=$(LEVEL)/Debug+Asserts/lib/libcall_site_tracing_rt.a
BBABC_RT_LIB=$(LEVEL)/Debug+Asserts/lib/libbb_call_site_tracing_rt.a
COMPILED = $(PROG).out
ABC_TR	= $(PROG).abc.out
BBABC_TR	= $(PROG).bbabc.out
REORDERED = $(PROG).ro.out
PERF = $(PROG).perf
BRPERF = $(PROG).br.perf
SO = libtest_util.so
SOABC = libtest_util.abc.so
BUILDSHARED = test.bin

OBJS= 	\
		test_util.o \
		test.o 

.c.o:
	dwllvm  -fpic -c -O2 -o $@ $<

$(COMPILED): $(OBJS)
	dwllvm  $(OBJS) -O2 -o $@ -v


$(BBABC_TR): $(OBJS)
	dwllvm++ -g  -Wl,--plugin-opt,--debug-pass=Details -Wl,--plugin-opt,-print-after-all -Wl,--plugin-opt,-enable-machine-block-tracing  $(OBJS) -L$(LLVM_LIBS) -lbb_call_site_tracing_rt -O0 -o $@ -lpthread -v 2> debug.bbabc.out

$(ABC_TR): $(OBJS)
	dwllvm++ -g  -Wl,--plugin-opt,-enable-function-tracing -Wl,--plugin-opt,-enable-bursty-tracing  $(OBJS) -L$(LLVM_LIBS) -lbursty_function_tracing_rt -O0 -o $@ -lpthread -v 2> debug.abc.out 

$(PERF): $(OBJS)
	dwllvm++  -Wl,--plugin-opt,-enable-perf-counting -L$(LLVM_LIBS) -lpapi -lcache_counting_rt -o $@ $(OBJS)

$(BRPERF): $(OBJS)
	dwllvm++  -Wl,--plugin-opt,-enable-perf-counting -L$(LLVM_LIBS) -lpapi -lbranch_counting_rt -o $@ $(OBJS)

$(SO): test_util.o
	dwllvm++  -shared  -fPIC   -O2 -o $@ $<

$(SOABC): test_util.o
	opt-dwllvm layout.abc  -shared  -fPIC   -O0 -o $@ $<

$(BUILDSHARED): test.o $(SO)
	dwllvm   -fPIC -O2 -o $@ $< -L$(HOME)/code-layout-optimizer/example  -ltest_util 

$(BUILDSHARED).abc: test.o $(SOABC)
	opt-dwllvm layout.abc  -fPIC -o $@ $<    -L$(HOME)/code-layout-optimizer/example -ltest_util.abc  2> debug.out

clean:
	rm -rf *.o *.so *.bin *.out *.perf
