add_llvm_library(LLVMCacheCounting
	CacheCounting.cpp
  )

add_dependencies(LLVMCacheCounting intrinsics_gen)
