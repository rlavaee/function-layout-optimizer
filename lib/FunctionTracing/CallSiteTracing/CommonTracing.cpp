#include "CommonTracing.hpp"
#include "llvm/Support/raw_ostream.h"
void InsertCodeAnalysisInitCall(Function * MainFn, const char *FnName, short totalFuncs){
  LLVMContext &Context = MainFn->getContext();
  Type *SIntTy = Type::getInt16Ty(Context);
  Type *VoidTy = Type::getVoidTy(Context);
  Module *M = MainFn->getParent();
  Constant *InitFn = M->getOrInsertFunction(FnName, VoidTy,
      SIntTy,
      (Type *)0);
  std::vector<Value*> Args(1);
  Args[0] = ConstantInt::get (SIntTy, totalFuncs);

  // Skip over any allocas in the entry block.
  BasicBlock *Entry = MainFn->begin();
  BasicBlock::iterator InsertPos = Entry->begin();
  while (isa<AllocaInst>(InsertPos) || isa<PHINode>(InsertPos)) ++InsertPos;

  CallInst::Create(InitFn, Args, "", InsertPos);

}

void InsertBBAnalysisInitCall(Function * MainFn, uint16_t totalFuncs, std::vector<uint16_t> &BBCountVec){
  LLVMContext &Context = MainFn->getContext();
  Type *UInt16Ty = Type::getInt16Ty(Context);
  Type *VoidTy = Type::getVoidTy(Context);
  Module *M = MainFn->getParent();
  Constant *InitFn = M->getOrInsertFunction("start_basic_block_call_site_tracing", 
      VoidTy,
      UInt16Ty,
      (Type *)0);
  std::vector<Value*> Args(1);
  Args[0] = ConstantInt::get (UInt16Ty, totalFuncs);

  // Skip over any allocas in the entry block.
  BasicBlock *Entry = MainFn->begin();
  BasicBlock::iterator InsertPos = Entry->begin();
  while (isa<AllocaInst>(InsertPos) || isa<PHINode>(InsertPos)) ++InsertPos;

  CallInst::Create(InitFn, Args, "", InsertPos);
  
  Constant *SetBBCountFn = M->getOrInsertFunction("set_bb_count_for_fid", 
      VoidTy, 
      UInt16Ty, 
      UInt16Ty,
      (Type *)0);


  uint16_t fid = 0;
  for(std::vector<uint16_t>::iterator bb_count = BBCountVec.begin(), bb_count_end = BBCountVec.end(); bb_count!=bb_count_end ;++bb_count, ++fid){
    std::vector<Value*> Args(2);
    Args[0] = ConstantInt::get (UInt16Ty, fid);
    Args[1] = ConstantInt::get(UInt16Ty, *bb_count);
    CallInst::Create(SetBBCountFn, Args, "", InsertPos);
  }

}


void InsertBBInstrumentationCall(Instruction *II,
			       const char *FnName,
			       short FuncNumber,
			       short BBNumber){
  errs() << "######################## InsertBBInstrumentationCall (\" " << II->getName ()
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
  Args[0] = ConstantInt::get (SIntTy, FuncNumber);
  Args[1] = ConstantInt::get (SIntTy, BBNumber);                                                                                 

  // Insert the call after any alloca or PHI instructions...                                                                       
  BasicBlock::iterator InsertPos = II;
  while (isa<AllocaInst>(InsertPos) || isa<PHINode>(InsertPos))
    ++InsertPos;

  CallInst::Create(InstrFn, Args, "", InsertPos);

}

