#include "CommonTracing.hpp"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Transforms/Utils/Cloning.h"
using namespace llvm;

namespace {
  class CallEdgeTracer : public ModulePass {
    bool runOnModule(Module &M);
  public:
    static char ID;
    
		CallEdgeTracer(): ModulePass(ID){
			initializeCallEdgeTracerPass(*PassRegistry::getPassRegistry());
		}

    virtual const char * getPassName() const{
      return "Call Edge Tracer";
    }
  };
  
}

char CallEdgeTracer::ID = 0;
INITIALIZE_PASS (CallEdgeTracer,"trace-call-edges","Profiles calls and releases a layout", false, false)

ModulePass *llvm::createCallEdgeTracerPass(){
	return new CallEdgeTracer();
}


void InsertInstrumentationCall(Instruction *II,
			       const char *FnName,
			       short Caller,
			       short Callee,
			       unsigned BBNumber){
  errs() << "######################## InsertInstrumentationCall (\" " << II->getName ()
	 << "\", \"" << FnName << "\", " << BBNumber << ")\n";
  BasicBlock *BB = II->getParent();
  Function *Fn = BB->getParent();
  Module *M = Fn->getParent ();
  LLVMContext &Context = Fn->getContext();
  Type *VoidTy = Type::getVoidTy(Context);
  Type *SIntTy = Type::getInt16Ty(Context);
  Constant *InstrFn = M->getOrInsertFunction (FnName, VoidTy,
                                              SIntTy, SIntTy, (Type *)0);
  std::vector<Value*> Args (2);
  Args[0] = ConstantInt::get (SIntTy, Caller);
  Args[1] = ConstantInt::get (SIntTy, Callee);
  //Args[1] = ConstantInt::get (UIntTy, BBNumber);                                                                                 

  // Insert the call after any alloca or PHI instructions...                                                                       
  BasicBlock::iterator InsertPos = II;
  while (isa<AllocaInst>(InsertPos) || isa<PHINode>(InsertPos))
    ++InsertPos;

  CallInst::Create(InstrFn, Args, "", InsertPos);

}



bool CallEdgeTracer::runOnModule(Module &M) {
  Function *Main = M.getFunction("main");
  if (Main == 0) {
    errs() << "WARNING: cannot insert basic-block trace instrumentatiom "
	   << "into a module with no main function!\n";
    return false;  // No main, no instrumentation!
  }

  std::map <std::string, int> func_ids;

  short FuncNumber = 0;
  for(Module::iterator F= M.begin(), E=M.end(); F!=E; ++F){
  	if(!F->isDeclaration())
		func_ids[F->getName().str()]=FuncNumber++;
		
  }


  unsigned BBNumber = 0;
  FuncNumber = 0;
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F){
    
    if(!F->isDeclaration()){

      for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
	for (BasicBlock::iterator II = BB->begin(), E= BB->end(); II != E; ++II){
	  CallSite CS(cast<Value>(II));
	  if (CS) {
	    const Function *Callee = CS.getCalledFunction();
	    //	    errs() << Callee->getName() <<" "<< Callee->isDeclaration() << "\n";
	    if (Callee && !Callee->isDeclaration()){
	      ++II;
	      if(II!=E)
		InsertInstrumentationCall(II, "trace_call_edge", FuncNumber, 
					func_ids[Callee->getName().str()],BBNumber);
	      --II;
	    }
	  }
	}
	++BBNumber;
      }
      


      ++FuncNumber;
    }
  }


  // Add the initialization call to main.
  InsertCodeAnalysisInitCall(Main,"do_init", FuncNumber); 
  return true;
}

