#include "llvm/IR/Module.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Instrumentation.h"
#include <stdio.h>
using namespace llvm;

void InsertInitCacheCountersCall(Function * MainFn, const char *FnName, char inst_or_data){
  LLVMContext &Context = MainFn->getContext();
  Type *VoidTy = Type::getVoidTy(Context);
  Type *CharTy = Type::getInt8Ty(Context);
  Module *M = MainFn->getParent();
  Constant *InitFn = M->getOrInsertFunction(FnName, VoidTy,
					   CharTy,
                                           (Type *)0);	
  std::vector<Value*> Args(1);
  Args[0]=ConstantInt::get(CharTy,inst_or_data);
  // Skip over any allocas in the entry block.
  BasicBlock *Entry = MainFn->begin();
  BasicBlock::iterator InsertPos = Entry->begin();
  while (isa<AllocaInst>(InsertPos) || isa<PHINode>(InsertPos)) ++InsertPos;

  CallInst::Create(InitFn, Args,"", InsertPos);

}

namespace {
  class CacheCounting : public ModulePass {
  public:
    CacheCounting(char ID): ModulePass(ID){}

  };

  class InstCacheCounting: public CacheCounting{
    	bool runOnModule(Module &M);
	public:
	static char ID;
	InstCacheCounting(): CacheCounting(ID){}
  };

  class DataCacheCounting: public CacheCounting{
    bool runOnModule(Module &M);
	public:
	static char ID;
	DataCacheCounting(): CacheCounting(ID){}
  };


  class AccessesCacheCounting: public CacheCounting{
    bool runOnModule(Module &M);
	public:
	static char ID;
	AccessesCacheCounting(): CacheCounting(ID){}
  };

  
}


char InstCacheCounting::ID='i';
static RegisterPass<InstCacheCounting> InstCacheCounting("add-inst-cache-counting","adds instruction cache counters");
char DataCacheCounting::ID='d';
static RegisterPass<DataCacheCounting> DataCacheCounting("add-data-cache-counting","adds data cache counters");
char AccessesCacheCounting::ID='a';
static RegisterPass<AccessesCacheCounting> AccessesCacheCounting("add-accesses-cache-counting","adds data cache counters");


bool InstCacheCounting::runOnModule(Module &M) {
  Function *Main = M.getFunction("main");
  if (Main == 0) {
    errs() << "WARNING: cannot insert basic-block trace instrumentatiom "
	   << "into a module with no main function!\n";
    return false;  // No main, no instrumentation!
  }


  // Add the initialization call to main.
  InsertInitCacheCountersCall(Main,"init_cache_counters",'i'); 
  return true;
}


bool DataCacheCounting::runOnModule(Module &M) {
  Function *Main = M.getFunction("main");
  if (Main == 0) {
    errs() << "WARNING: cannot insert basic-block trace instrumentatiom "
	   << "into a module with no main function!\n";
    return false;  // No main, no instrumentation!
  }


  // Add the initialization call to main.
  InsertInitCacheCountersCall(Main,"init_cache_counters",'d'); 
  return true;
}


bool AccessesCacheCounting::runOnModule(Module &M) {
  Function *Main = M.getFunction("main");
  if (Main == 0) {
    errs() << "WARNING: cannot insert basic-block trace instrumentatiom "
	   << "into a module with no main function!\n";
    return false;  // No main, no instrumentation!
  }


  // Add the initialization call to main.
  InsertInitCacheCountersCall(Main,"init_cache_counters",'a'); 
  return true;
}

