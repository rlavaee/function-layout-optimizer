#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

namespace {
  class MBBTracingPass : public MachineFunctionPass {
    public:
      static char ID;
      explicit MBBTracingPass(): MachineFunctionPass(ID){}
      
      bool runOnMachineFunction(MachineFunction &MF) override;
  }
}

char MBBTracingPass::ID = 0;

INITIALIZE_PASS(MBBTracingPass, "machine-block-trace",
                "Inserts profiling information in every machine basic block", 
                false, false)

bool MBBTracingPass::runOnMachineFunction(MachineFunction &MF){
  for (MachineFunction::iterator I = MF.begin(), E = MF.end(); I != E; 
      I++){
     MachineBasicBlock *MBB = I;
    
    for (MachineBasicBlock::iterator II = MBB->begin(), IE = MBB->end(); II != IE;
        II++){
      MachineInstr *MI = II;
      if(MI->isCall()){
        errs () << *MI << "\n---------\n";
      }
      
    }

  }

  return false;
}
