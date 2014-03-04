#include "llvm/IR/Module.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/CallSite.h"
using namespace llvm;
void InsertCodeAnalysisInitCall(Function *, const char *, short);
void InsertBBInstrumentationCall(Instruction *II,const char *FnName,short FuncNumber, short BBNumber);
void InsertBBAnalysisInitCall(Function * MainFn, uint16_t totalFuncs, std::vector<uint16_t> &BBCountVec);
 

