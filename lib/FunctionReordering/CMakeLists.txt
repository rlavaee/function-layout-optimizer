add_llvm_library(LLVMFunctionReordering
	FunctionReordering.cpp
  )

add_dependencies(LLVMFunctionReordering intrinsics_gen)
