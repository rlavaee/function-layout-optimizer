#include "CommonProfiling.hpp"
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


