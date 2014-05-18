#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Instrumentation.h"
#include <vector>
#include <map>
#include <cstdio> 

using namespace llvm;

cl::opt<std::string>
	LayoutFile("layout",
                 cl::desc("<Layout File>"),
                 cl::Required);

cl::opt<std::string>
	NewLayoutFile("new-layout",
                 cl::desc("<New Layout File>"),
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
			FILE * layout_file= fopen(LayoutFile.c_str(),"r");
				if(layout_file==NULL){
					errs() << "No such file:" << LayoutFile.c_str() << "\n";
					exit(0);
				}

			FILE * new_layout_file = fopen(NewLayoutFile.c_str(),"w");
				if(layout_file==NULL){
					errs() << "No such file:" << NewLayoutFile.c_str() << "\n";
					exit(0);
				}
			uint16_t fn,bbn;
			while(fscanf(layout_file,"(%hu,%hu)\n",&fn,&bbn)!=EOF){
				layout.push_back(Record(fn,bbn));
			}
			fclose(layout_file);
  
      Module::iterator FF,FE;

			std::vector<Function*> OrigFunctionList, NewFunctionList;

      for (FF = M.begin(), FE = M.end(); FF != FE; ++FF)
				if(!FF->isDeclaration())
					OrigFunctionList.push_back(FF);

			std::vector<BasicBlock*> * OrigBBList;
			std::vector<uint16_t> * NewBBListFirstPart, * NewBBListSecondPart, * NewBBList;
			std::map<uint16_t,uint16_t> * NewBBMap;

			OrigBBList = new std::vector<BasicBlock*>[OrigFunctionList.size()];
			NewBBListFirstPart = new std::vector<uint16_t>[OrigFunctionList.size()];
			NewBBListSecondPart = new std::vector<uint16_t>[OrigFunctionList.size()];
			NewBBList = new std::vector<uint16_t>[OrigFunctionList.size()];
			NewBBMap = new std::map<uint16_t,uint16_t>[OrigFunctionList.size()];
					
      Function::iterator BBF,BBE;
			std::vector<Function*>::iterator Func;

      for (fn=0; fn<OrigFunctionList.size(); fn++)
					for(BBF=OrigFunctionList[fn]->begin(), BBE=OrigFunctionList[fn]->end(); BBF!=BBE; ++BBF)
						OrigBBList[fn].push_back(BBF);

			std::vector<Record>::iterator layout_it, layout_end;

			Function * F;

			layout_end=layout.end();
			for(layout_it=layout.begin(); layout_it!=layout_end; ){
					fn=layout_it->first;
					F = OrigFunctionList[fn];

					bool first_part=false;
					for(; layout_it != layout_end && layout_it->first == fn; ++layout_it){
						if(!first_part)
							if(layout_it->second == 0)
								first_part=true;

						if(first_part)
							NewBBListFirstPart[fn].push_back(layout_it->second);
						else
							NewBBListSecondPart[fn].push_back(layout_it->second);
					}
				
      }

			std::vector<uint16_t>::iterator bb_it,bb_it_end;
			for(fn=0; fn<OrigFunctionList.size(); ++fn){

				std::vector<uint16_t>::iterator bb_it,bb_it_end;
				for(bb_it=NewBBListFirstPart[fn].begin(), bb_it_end=NewBBListFirstPart[fn].end(); bb_it!=bb_it_end; bb_it++)
						NewBBList[fn].push_back(*bb_it);

				for(bb_it=NewBBListSecondPart[fn].begin(), bb_it_end=NewBBListSecondPart[fn].end(); bb_it!=bb_it_end; bb_it++)
						NewBBList[fn].push_back(*bb_it);

				for(bb_it=NewBBList[fn].begin(), bb_it_end=NewBBList[fn].end(); bb_it!=bb_it_end; bb_it++){
						OrigFunctionList[fn]->getBasicBlockList().remove(OrigBBList[fn].at(*bb_it));
						OrigFunctionList[fn]->getBasicBlockList().push_back(OrigBBList[fn].at(*bb_it));
						NewBBMap[fn][*bb_it]= bb_it - NewBBList[fn].begin();
				}
			}

			
			for(layout_it=layout.begin(); layout_it!=layout_end; layout_it++){
					fn=layout_it->first;
					F = OrigFunctionList[fn];
					uint16_t bb_index = NewBBMap[fn][layout_it->second];
					assert (bb_index < NewBBList[fn].size() && "Didn't find the index");
					fprintf(new_layout_file,"(%hu,%hu)\n",fn,bb_index);
			}
			fclose(new_layout_file);

/*
				while(!NewFunctionList.empty()){
					M.getFunctionList().remove(NewFunctionList.back());
					M.getFunctionList().push_front(NewFunctionList.back());
					NewFunctionList.pop_back();
				}
			*/
			return true;
		}

  };
  
}

char BasicBlockReorderer::ID = 0;
static RegisterPass<BasicBlockReorderer> X("rlavaee-reorder-basic-blocks",
					 "Reorders Basic Blocks According to a Permutation File",false,false);
 
