/*===-- BasicBlockTracing.c - Support library for basic block tracing -----===* \
|*
|*                     The LLVM Compiler Infrastructure
|*
|* This file is distributed under the University of Illinois Open Source
|* License. See LICENSE.TXT for details.
|* 
|*===----------------------------------------------------------------------===*|
|* 
|* This file implements the call back routines for the basic block tracing
|* instrumentation pass.  This should be used with the -trace-basic-blocks
|* LLVM pass.
|*
\*===----------------------------------------------------------------------===*/

//#include "Profiling.hpp"
#include "affinity.hpp"
#include <cstdlib>
#include <cstdio>
#include <signal.h>
#include <pthread.h>

static float SavedSampleRate=0;
static short SavedTotalFuncs;
static short SavedMaxWindowSize=10;
static short SavedMaxFreqLevel;

//static pthread_mutex_t lock;
//static char * program_name;


static void check_affinity_environment_variables(void) {
  const char *SampleRateEnvVar, *TotalFuncsEnvVar, *MaxWindowSizeEnvVar, *MaxFreqLevelEnvVar;

  if ((SampleRateEnvVar = getenv("SAMPLE_RATE")) != NULL) {
    SavedSampleRate = (float)strtod(SampleRateEnvVar,NULL);
  }

  if((TotalFuncsEnvVar = getenv("TOTAL_FUNCS")) != NULL){
    SavedTotalFuncs = atoi(TotalFuncsEnvVar);
  }

  if((MaxWindowSizeEnvVar = getenv("MAX_WINDOW_SIZE")) != NULL){
    SavedMaxWindowSize = atoi(MaxWindowSizeEnvVar);
  }

  if((MaxFreqLevelEnvVar = getenv("MAX_FREQ_LEVEL")) != NULL){
    SavedMaxFreqLevel = atoi(MaxFreqLevelEnvVar);
  }

}


/* BBTraceAtExitHandler - When the program exits, just write out any remaining 
 * data and free the trace buffer.
 */
static void BBTraceAtExitHandler(void) {
  //pthread_kill(update_affinity_thread,SIGKILL);
  affinityAtExitHandler();
}

/* llvm_trace_basic_block - called upon hitting a new basic block. */
extern "C" void llvm_trace_basic_block (short FuncNum) {
    sample_window(FuncNum);
}

/* llvm_start_basic_block_tracing - This is the main entry point of the basic
 * block tracing library.  It is responsible for setting up the atexit
 * handler and allocating the trace buffer.
 */
extern "C" int llvm_start_basic_block_tracing(short _totalFuncs) {
  
  //int ret=save_arguments(argc, argv);
  /*  if(argc>1)
    program_name=argv[1];
  else{
    program_name=(char *) malloc(sizeof(char)*4);
    strcpy(program_name,"non");
    }*/
  check_affinity_environment_variables();  
  initialize_affinity_data(SavedSampleRate,SavedMaxWindowSize,_totalFuncs,SavedMaxFreqLevel);

  //pthread_mutex_init(&lock,NULL);
  //traceFile=fopen("trace.out","w");


  /* Set up the atexit handler. */
  atexit (BBTraceAtExitHandler);

  return 1;
}
