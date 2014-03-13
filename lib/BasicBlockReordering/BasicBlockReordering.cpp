#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Instrumentation.h"
#include <vector>
#include <cstdio> 

using namespace llvm;

cl::opt<std::string>
	LayoutFile("layout",
                 cl::desc("<Layout File>"),
                 cl::Required);

typedef std::pair<uint16_t,uint16_t> Record;


namespace {
  class BasicBlockReorderer : public ModulePass {
    
  public:
    static char ID;
		std::vector<Record> layout;
    BasicBlockReorderer(): ModulePass(ID){
    }

    virtual bool runOnModule(Module &M){
			FILE * pFile = fopen(LayoutFile.c_str(),"r");
				if(pFile==NULL){
					errs() << "No such file:" << LayoutFile.c_str() << "\n";
					exit(0);
				}

			uint16_t fn,bbn;
			while(fscanf(pFile,"(%hu,%hu)\n",&fn,&bbn)!=EOF){
				layout.push_back(Record(fn,bbn));
			}
  
      Module::iterator FF,FE;

			std::vector<Function*> OrigFunctionList, NewFunctionList;

      for (FF = M.begin(), FE = M.end(); FF != FE; ++FF)
				if(!FF->isDeclaration())
					OrigFunctionList.push_back(FF);
					
      Function::iterator BBF,BBE;
			std::vector<Record>::iterator layout_it, layout_end;

			Function * F;

			layout_it = layout.begin();
			layout_end = layout.end(); 
			while(layout_it!=layout_end){
					fn=layout_it->first;
					F = OrigFunctionList[fn];
					NewFunctionList.push_back(F);
					std::vector<BasicBlock*> OrigBBList,NewBBListFirstPart, NewBBListSecondPart;

					for(BBF=F->begin(), BBE=F->end(); BBF!=BBE; ++BBF)
						OrigBBList.push_back(BBF);

					bool first_part=false;
					for(; layout_it != layout_end && layout_it->first == fn; ++layout_it){
						if(!first_part)
							if(layout_it->second == 0)
								first_part=true;

						if(first_part)
							NewBBListFirstPart.push_back(OrigBBList[layout_it->second]);
						else
							NewBBListSecondPart.push_back(OrigBBList[layout_it->second]);
					}

					assert( (NewBBListFirstPart.size() + NewBBListSecondPart.size()) == F->getBasicBlockList().size() && "Size of functions do not match!");
		
					while(!NewBBListSecondPart.empty()){
						F->getBasicBlockList().remove(NewBBListSecondPart.back());
						F->getBasicBlockList().push_front(NewBBListSecondPart.back());
						NewBBListSecondPart.pop_back();
					}	
					while(!NewBBListFirstPart.empty()){
						F->getBasicBlockList().remove(NewBBListFirstPart.back());
						F->getBasicBlockList().push_front(NewBBListFirstPart.back());
						NewBBListFirstPart.pop_back();
					}

      	}

				while(!NewFunctionList.empty()){
					M.getFunctionList().remove(NewFunctionList.back());
					M.getFunctionList().push_front(NewFunctionList.back());
					NewFunctionList.pop_back();
				}
			return true;
		}

  };
  
}

char BasicBlockReorderer::ID = 0;
static RegisterPass<BasicBlockReorderer> X("rlavaee-reorder-basic-blocks",
					 "Reorders Basic Blocks According to a Permutation File",false,false);
 
