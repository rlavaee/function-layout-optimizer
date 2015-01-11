#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/Constants.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include <vector>
#include <cstdio>
using namespace llvm;


namespace {
  class BurstyCallSiteTracer : public ModulePass {
    bool runOnModule(Module &M);
  public:
    static char ID;
    BurstyCallSiteTracer(): ModulePass(ID){}


    virtual const char * getPassName() const{
      return "Bursty Call Site Tracer";
    }
  };
  
}

char BurstyCallSiteTracer::ID = 0;
RegisterPass<BurstyCallSiteTracer> Y("bursty-tracer","Insert instrumentation for basic block tracing", false, false);

//INITIALIZE_PASS (BurstyCallSiteTracer,"bursty-tracer","prepares for bursty profiling.",false,false)
//RegisterPass<BurstyCallSiteTracer> Y("bursty-tracer","prepares for bursty profiling.",false,false);



void InsertSwitchCall ( Function * original, Function * profiling, short FuncNumber){

  Module * M = original->getParent();
  LLVMContext &Context = original->getContext(); 
  Type *SIntTy = Type::getInt16Ty(Context);

  std::vector<Value*> Args (1);
  Args[0] = ConstantInt::get (SIntTy,FuncNumber);

  //Constant *getSwitchFn = M->getOrInsertFunction ("get_switch", Type::getInt8Ty(Context), SIntTy, (Type *) 0);

  BasicBlock * oldEntry = original->begin();

  BasicBlock * newEntry = BasicBlock::Create(Context, "entry", original, oldEntry);


  //BasicBlock * BB = original->begin();
  // Insert the call after any alloca or PHI instructions...
  //BasicBlock::iterator InsertPos = newEntry->begin();
  //while (isa<AllocaInst>(InsertPos) || isa<PHINode>(InsertPos))
  //  ++InsertPos;
  //CallInst * prof_switch = CallInst::Create(getSwitchFn, Args, "get.profiling.switch", newEntry);
	
	IRBuilder<> IRB(newEntry);
	Constant* prof_switch = M->getOrInsertGlobal("profiling_switch",Type::getInt8Ty(Context));
  Constant* zero = ConstantInt::get(Type::getInt8Ty(Context),0);

  Value * cmp = new ICmpInst(*newEntry, ICmpInst::ICMP_NE, IRB.CreateLoad(prof_switch), zero, "cmp.with.zero");


  //BasicBlock * ifFalse = BasicBlock::Create(Context, "if prof_switch is zero", original, newEntry);
  BasicBlock * ifTrue = BasicBlock::Create(Context, "switch.is.zero", original, oldEntry);

  BranchInst::Create(ifTrue, oldEntry, cmp, newEntry);

  std::vector<Value *> prof_call_args;
  for(Function::arg_iterator arg=original->arg_begin(), end = original->arg_end(); arg != end; arg++) {
    prof_call_args.push_back((Argument *)arg);
  }


  CallInst * prof_call = CallInst::Create(profiling,prof_call_args, "",ifTrue);
  //prof_call->setCallingConv(original->getCallingConv());
  if(original->getReturnType()!= Type::getVoidTy(Context))
    ReturnInst::Create(Context, prof_call, ifTrue);
  else
    ReturnInst::Create(Context, ifTrue);
}





bool BurstyCallSiteTracer::runOnModule(Module &M) {
  Function *Main = M.getFunction("main");
  if (Main == 0) {
    errs() << "WARNING: cannot insert bursty trace instrumentatiom "
	   << "into a module with no main function!\n";
    return false;  // No main, no instrumentation!
  }




  unsigned BBNumber = 0;
  short FuncNumber = 0;
  FILE * funcMapFile=fopen("functionMapping.txt","w");
  std::vector<Function*> profFunctionList;
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F){
    
    if(!F->isDeclaration()){
		if(F->isVarArg()){ //make f itself profiling
			//errs() << "VarArg function: " << F->getName() << "\n";
			F->addFnAttr("profiling");
			profFunctionList.push_back(F);
		}else{ //not vararg
      fprintf(funcMapFile,"%s\t%d\n",F->getName().data(),FuncNumber);
      //Clone the function
      ValueToValueMapTy mapTy;
      Function * prof_F = CloneFunction(F,mapTy,true);

      //Set the name of the profiling function
      prof_F->setName("_prof_" + F->getName());
	//Set calling convention equal to the original's calling convention
	//prof_F->setCallingConv(F->getCallingConv());
	//prof_F->setAttributes(F->getAttributes());
			prof_F->addFnAttr(Attribute::NoInline);
			prof_F->addFnAttr("profiling");
			//errs() << "here is a function that has to have: " << prof_F->hasFnAttribute("profiling") << "\t" << prof_F->hasFnAttribute(Attribute::NoInline) << "\n";
        prof_F->setLinkage(F->getLinkage());
	//prof_F->addFnAttr(llvm::Attribute::AlwaysInline);
      //insert instrumented copy of the function at the beginning
	

      //M.getFunctionList().push_back(prof_F);
			/* uncomment me to activate profiling
      InsertInstrumentationCall (prof_F->begin(), "llvm_trace_basic_block", FuncNumber, BBNumber);
      for (Function::iterator BB = prof_F->begin(), E = prof_F->end(); BB != E; ++BB) {
	for (BasicBlock::iterator II = BB->begin(), E= BB->end(); II != E; ++II){
	  CallSite CS(cast<Value>(II));
	  if (CS) {
	    const Function *Callee = CS.getCalledFunction();
	    //	    errs() << Callee->getName() <<" "<< Callee->isDeclaration() << "\n";
	    if (Callee && !Callee->isDeclaration()){
	      ++II;
	      if(II!=E)
		InsertInstrumentationCall(II, "llvm_trace_basic_block", FuncNumber, BBNumber);
	      --II;
	    }
	  }
	}
	++BBNumber;
      }

			*/
      


      InsertSwitchCall(F,prof_F, FuncNumber);
			profFunctionList.push_back(prof_F);
	}
      ++FuncNumber;
    }
  }

	for(std::vector<Function*>::reverse_iterator it=profFunctionList.rbegin(),eit=profFunctionList.rend(); it!=eit; ++it){
		if((*it)->isVarArg())
			M.getFunctionList().remove(*it);
		M.getFunctionList().push_front(*it);
	}

  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F)
		fprintf(funcMapFile,"%s\n",F->getName().data());
  
	fclose(funcMapFile);


  // Add the initialization call to main.
  //InsertCodeAnalysisInitCall(Main,"llvm_init_affinity_analysis", FuncNumber); 
  return true;
}
