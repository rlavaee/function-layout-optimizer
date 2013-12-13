add_llvm_library(LLVMCallSiteTracing
	CallSiteTracing.cpp
	../CommonTracing.cpp
  )

add_dependencies(LLVMCallSiteTracing intrinsics_gen)
