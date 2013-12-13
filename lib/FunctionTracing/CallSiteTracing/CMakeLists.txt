add_llvm_library(LLVMCallSiteTracing
	CallSiteTracing.cpp
	CommonTracing.cpp
	CommonTracing.hpp
  )

add_dependencies(LLVMCallSiteTracing intrinsics_gen)
