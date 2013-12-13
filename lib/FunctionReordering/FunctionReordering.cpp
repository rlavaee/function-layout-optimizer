#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Instrumentation.h"
#include <cstdio> 

using namespace llvm;

cl::opt<std::string>
	LayoutFile("layout",
                 cl::desc("<Layout File>"),
                 cl::Required);


namespace {
  class FunctionReorderer : public ModulePass {
    
  public:
    static char ID;
    FunctionReorderer(): ModulePass(ID){
    }

    virtual bool runOnModule(Module &M){
  
      int totalFuncs=0;
      Module::iterator F,E;
      for (F = M.begin(), E = M.end(); F != E; ++F) {
	if(!F->isDeclaration())
	  totalFuncs++;
      }
      FILE * pFile = fopen(LayoutFile.c_str(),"r");
			if(pFile==NULL){
				errs() << "No such file:" << LayoutFile.c_str() << "\n";
				exit(0);
			}
      
      Function ** newFunctionList= (Function **) malloc(sizeof(Function *)*totalFuncs);
      int * perm = (int *) malloc(sizeof(int)*totalFuncs);
      
      int funcNum,i=0;
      while(i<totalFuncs && fscanf(pFile,"%d",&funcNum)!=EOF){
	perm[funcNum]=i;
	++i;
      }
      if(i!=totalFuncs){
			errs() << "The permutation is shorter that expected\n";
			exit(0);
	}
      
      for (F = M.begin(), E = M.end(), i=0; F != E; ++F) {
	if(!F->isDeclaration())
	  newFunctionList[perm[i++]]=F;
      }
      
      for(i=0;i<totalFuncs;++i){
	M.getFunctionList().remove(newFunctionList[i]);
      }
      for(i=0;i<totalFuncs; ++i){
	M.getFunctionList().push_back(newFunctionList[i]);
      }

      return true;
    }
    

  };
  
}

char FunctionReorderer::ID = 0;
static RegisterPass<FunctionReorderer> X("rlavaee-reorder-functions",
					 "Reorders Functions According to a Permutation File Provided in the Environment Variable PERM_FILE",false,false);
 
