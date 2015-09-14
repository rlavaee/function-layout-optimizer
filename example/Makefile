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

OBJS= 	\
		test_util.o \
		test.o 

.c.o:
	clang -c -g -flto -O2 -o $@ $<

$(COMPILED): $(OBJS)
	clang -g -flto $(OBJS) -O2 -o $@ -v


$(BBABC_TR): $(OBJS)
	clang++ -g  -Wl,--plugin-opt,--debug-pass=Details -Wl,--plugin-opt,-print-after-all -Wl,--plugin-opt,-enable-machine-block-tracing -flto $(OBJS) -L$(LLVM_LIBS) -lbb_call_site_tracing_rt -O2 -o $@ -lpthread -v 2> debug.bbabc.out

$(ABC_TR): $(OBJS)
	clang++ -g  -Wl,--plugin-opt,-enable-function-tracing -Wl,--plugin-opt,-enable-bursty-tracing -flto $(OBJS) -L$(LLVM_LIBS) -lbursty_function_tracing_rt -O2 -o $@ -lpthread -v 2> debug.abc.out 
