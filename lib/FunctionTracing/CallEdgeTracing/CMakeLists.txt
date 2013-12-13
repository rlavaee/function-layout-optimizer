
add_llvm_library(LLVMCallEdgeTracing
	CallEdgeTracing.cpp
	../CommonTracing.cpp
	)

add_dependencies(LLVMCallEdgeTracing intrinsics_gen)
