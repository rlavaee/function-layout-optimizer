#include "CommonTracing.hpp"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Transforms/Utils/Cloning.h"
using namespace llvm;

namespace {
  class BasicBlockTracer : public ModulePass {
    bool runOnModule(Module &M);
  public:
    static char ID;
    BasicBlockTracer(): ModulePass(ID){}

    virtual const char * getPassName() const{
      return "Basic Block Tracer";
    }
  };
  
}

char BasicBlockTracer::ID = 0;
static RegisterPass<BasicBlockTracer> X("trace-call-sites","Insert instrumentation for basic block tracing", false, false);

void InsertInstrumentationCall(Instruction *II,
			       const char *FnName,
			       short FuncNumber,
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
                                              SIntTy, (Type *)0);
  std::vector<Value*> Args (1);
  Args[0] = ConstantInt::get (SIntTy, FuncNumber);
  //Args[1] = ConstantInt::get (UIntTy, BBNumber);                                                                                 

  // Insert the call after any alloca or PHI instructions...                                                                       
  BasicBlock::iterator InsertPos = II;
  while (isa<AllocaInst>(InsertPos) || isa<PHINode>(InsertPos))
    ++InsertPos;

  CallInst::Create(InstrFn, Args, "", InsertPos);

}



void InsertInstrumentationCall (BasicBlock *BB, 
				const char *FnName,
				short FuncNumber,
				unsigned BBNumber) {
  errs() << "InsertInstrumentationCall (\"" << BB->getName ()
                  << "\", \"" << FnName << "\", " << BBNumber << ")\n";
  Function *Fn = BB->getParent();
  Module *M = Fn->getParent ();
  LLVMContext &Context = Fn->getContext();
  Type *VoidTy = Type::getVoidTy(Context);
  Type *SIntTy = Type::getInt16Ty(Context);
  Constant *InstrFn = M->getOrInsertFunction (FnName, VoidTy,
					      SIntTy, (Type *)0);
  std::vector<Value*> Args (1);
  Args[0] = ConstantInt::get (SIntTy, FuncNumber);
  //Args[1] = ConstantInt::get (UIntTy, BBNumber);

  // Insert the call after any alloca or PHI instructions...
  BasicBlock::iterator InsertPos = BB->begin();
  while (isa<AllocaInst>(InsertPos) || isa<PHINode>(InsertPos))
    ++InsertPos;

  CallInst::Create(InstrFn, Args, "", InsertPos);
}

void InsertSwitchCall ( Function * original, Function * profiling, short FuncNumber){

  Module * M = original->getParent();
  LLVMContext &Context = original->getContext(); 
  Type *SIntTy = Type::getInt16Ty(Context);

  std::vector<Value*> Args (1);
  Args[0] = ConstantInt::get (SIntTy,FuncNumber);

  Constant *getSwitchFn = M->getOrInsertFunction ("get_switch", Type::getInt8Ty(Context), SIntTy, (Type *) 0);

  BasicBlock * oldEntry = original->begin();

  BasicBlock * newEntry = BasicBlock::Create(Context, "entry", original, oldEntry);


  //BasicBlock * BB = original->begin();
  // Insert the call after any alloca or PHI instructions...
  //BasicBlock::iterator InsertPos = newEntry->begin();
  //while (isa<AllocaInst>(InsertPos) || isa<PHINode>(InsertPos))
  //  ++InsertPos;
  CallInst * prof_switch = CallInst::Create(getSwitchFn, Args, "get.profiling.switch", newEntry);

  Constant* zero = ConstantInt::get(Type::getInt8Ty(Context),0);
  Value * cmp = new ICmpInst(*newEntry, ICmpInst::ICMP_NE, prof_switch, zero, "cmp.with.zero");


  //BasicBlock * ifFalse = BasicBlock::Create(Context, "if prof_switch is zero", original, newEntry);
  BasicBlock * ifTrue = BasicBlock::Create(Context, "switch.is.zero", original, oldEntry);

  BranchInst::Create(ifTrue, oldEntry, cmp, newEntry);

  errs() << "function is "<< original->getName() << "\n";
  std::vector<Value *> prof_call_args;
  for(Function::arg_iterator arg=original->arg_begin(), end = original->arg_end(); arg != end; arg++) {
    prof_call_args.push_back((Argument *)arg);
    errs() << (Argument *)arg << "\n";
  }
  errs() << original->arg_size() << "\t" << prof_call_args.size() << "\n";


  CallInst * prof_call = CallInst::Create(profiling,prof_call_args, "",ifTrue);
  //prof_call->setCallingConv(original->getCallingConv());
  if(original->getReturnType()!= Type::getVoidTy(Context))
    ReturnInst::Create(Context, prof_call, ifTrue);
  else
    ReturnInst::Create(Context, ifTrue);
}


bool BasicBlockTracer::runOnModule(Module &M) {
  Function *Main = M.getFunction("main");
  if (Main == 0) {
    errs() << "WARNING: cannot insert basic-block trace instrumentatiom "
	   << "into a module with no main function!\n";
    return false;  // No main, no instrumentation!
  }


  unsigned BBNumber = 0;
  short FuncNumber = 0;
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F){
    
    if(!F->isDeclaration()){

      InsertInstrumentationCall (F->begin(), "record_function_exec", FuncNumber, BBNumber);
      for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
	for (BasicBlock::iterator II = BB->begin(), E= BB->end(); II != E; ++II){
	  CallSite CS(cast<Value>(II));
	  if (CS) {
	    const Function *Callee = CS.getCalledFunction();
	    //	    errs() << Callee->getName() <<" "<< Callee->isDeclaration() << "\n";
	    if (Callee && !Callee->isDeclaration()){
	      ++II;
	      if(II!=E)
		InsertInstrumentationCall(II, "record_function_exec", FuncNumber, BBNumber);
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
  InsertCodeAnalysisInitCall(Main,"start_call_site_tracing", FuncNumber); 
  return true;
}

