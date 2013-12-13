
add_llvm_library(LLVMCallEdgeTracing
	CallEdgeTracing.cpp
	CommonTracing.cpp
	CommonTracing.hpp
	)

add_dependencies(LLVMCallEdgeTracing intrinsics_gen)
