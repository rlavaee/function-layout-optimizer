#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Transforms/IPO.h"
#include <cstdio>

using namespace llvm;

namespace {
  class FunctionReorderer : public ModulePass {
    
  public:
    static char ID;
		const char * layout_filename;
		
    FunctionReorderer(): ModulePass(ID){
			initializeFunctionReordererPass(*PassRegistry::getPassRegistry());
    }

    FunctionReorderer(const char * layout_filename): ModulePass(ID){
			this->layout_filename = layout_filename;
			initializeFunctionReordererPass(*PassRegistry::getPassRegistry());
    }

    virtual bool runOnModule(Module &M){
  
      int totalFuncs=0;
      Module::iterator F,E;
      for (F = M.begin(), E = M.end(); F != E; ++F) {
	if(!F->isDeclaration())
	  totalFuncs++;
      }
      FILE * pFile = fopen(layout_filename,"r");
			if(pFile==NULL){
				errs() << "No such file:" << layout_filename << "\n";
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
			errs() << "The permutation is shorter that expected: " << i << " " <<totalFuncs << "\n";
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
INITIALIZE_PASS (FunctionReorderer,"reorder-functions","Reorders Functions According to a Permutation File Provided in the Environment Variable PERM_FILE",false,false)

ModulePass *llvm::createFunctionReordererPass(const char * layout_filename){
	return new FunctionReorderer(layout_filename);
}
