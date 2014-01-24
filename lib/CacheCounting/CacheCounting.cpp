#include "llvm/IR/Module.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Instrumentation.h"
using namespace llvm;

cl::opt<int>
AvailCounters("avail-counters",
                 cl::desc("Number of available hardware counters"),
                 cl::init(4));

void InsertInitCacheCountersCall(Function * MainFn, const char *FnName){
  Module *M = MainFn->getParent();
  LLVMContext &Context = M->getContext();
  Type *VoidTy = Type::getVoidTy(Context);
  Type *IntTy = Type::getInt32Ty(Context);
  Constant *InitFn = M->getOrInsertFunction(FnName, VoidTy,
																						IntTy,
                                           (Type *)0);	
  std::vector<Value*> Args(1);
  Args[0]=ConstantInt::get(IntTy,AvailCounters);
  // Skip over any allocas in the entry block.
  BasicBlock *Entry = MainFn->begin();
  BasicBlock::iterator InsertPos = Entry->begin();
  while (isa<AllocaInst>(InsertPos) || isa<PHINode>(InsertPos)) ++InsertPos;
  CallInst::Create(InitFn, Args,"", InsertPos);
}

namespace {
  class InstCacheCounting : public ModulePass {
  public:
		static char ID;
    InstCacheCounting(): ModulePass(ID){}
    bool runOnModule(Module &M);
		
  };

}


static RegisterPass<InstCacheCounting> X("add-inst-cache-counters","adds instruction cache counters");
char InstCacheCounting::ID=0;




bool InstCacheCounting::runOnModule(Module &M) {
  Function *Main = M.getFunction("main");
  if (Main == 0) {
    errs() << "WARNING: cannot insert basic-block trace instrumentatiom "
	   << "into a module with no main function!\n";
    return false;  // No main, no instrumentation!
  }


  // Add the initialization call to main.
  InsertInitCacheCountersCall(Main,"init_cache_counters"); 
  return true;
}


