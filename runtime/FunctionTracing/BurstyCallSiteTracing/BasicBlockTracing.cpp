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

#include "BasicBlockTracing.hpp"
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <unistd.h>
#include <new>
#include <signal.h>
#include <pthread.h>
#include <algorithm>
#include <semaphore.h>
#include <string.h>
//#include <boost/lockfree/queue.hpp>

FILE * errFile;

int * func_count;
short * funcs;

short * preference;

int ** stage_affinity;
int * stage_affinity_sum;
int * potential_stage_windows;

bool * analysis_switch;
bool * analyzed;

short now_analyzed_func;

short analysis_set_size;

int stage_windows;

const int quantum = 1000;
bool func_counting;
int stage_time;
int stage_quantum;
//static pthread_mutex_t lock;
//static char * program_name;
//
//
//


////////affinity data////////////
short totalFuncs, maxWindowSize;
int sampledWindows;
unsigned totalBBs;
float sampleRate;
short maxFreqLevel;
int level_pid, version_pid;
ProfilingLevel pLevel;


affinityHashMap * affEntries;
std::list<SampledWindow> trace_list;

bool * contains_func;
std::list<short>::iterator * func_trace_it;
std::list<SampledWindow>::iterator * func_window_it;
int trace_list_size;


//boost::lockfree::queue< std::list<SampledWindow> * > trace_list_queue (100);

//pthread_t update_affinity_thread;


std::list<SampledWindow>::iterator tl_window_iter;
std::list<short>::iterator tl_trace_iter;

disjointSet *** sets;

short prevFunc;
FILE * graphFile, * debugFile;

const char * version_str=".babc";


//pthread_mutex_t trace_list_queue_lock = PTHREAD_MUTEX_INITIALIZER;

sem_t affinity_sem;


void save_affinity_into_file(char * affinityFilePath){
	FILE * affFile=fopen(affinityFilePath,"w");
	fprintf(affFile,"%d\n",maxWindowSize);
	affinityHashMap::iterator iter;
	for(iter=affEntries->begin(); iter!=affEntries->end(); ++iter){
		fprintf(affFile,"%hd\t%hd\t%d\t",iter->first.first,iter->first.second,iter->second.potential_windows);
		if(iter->second.actual_windows!=NULL)
			for(int i=1;i<=maxWindowSize; ++i)
				fprintf(affFile,"%d\t",iter->second.actual_windows[i]);
		else
			fprintf(affFile,"-1");
		fprintf(affFile,"\n");
	}
	fclose(affFile);
}


void read_affinity_from_file(char * affinityFilePath){
	FILE * affFile=fopen(affinityFilePath,"r");
	if(affFile==NULL)
		return;
	int mwsize;
	fscanf(affFile,"%d",&mwsize);
	short func1,func2;
	int potential_windows;
	while(fscanf(affFile,"%hd\t%hd",&func1,&func2)!=EOF){

		fscanf(affFile,"%d",&potential_windows);
		int fw;
		int * actual_windows=NULL;
		fscanf(affFile,"%d",&fw);
		if(fw!=-1){
			actual_windows=new int[maxWindowSize+1]();
			actual_windows[1]=fw;
			for(int i=2;i<=mwsize;++i)
				fscanf(affFile,"%d",&actual_windows[i]);
		}
	      	
		affPair pair=affPair(func1,func2);
		affinityHashMap::iterator result=affEntries->find(pair);

	      	if(result==affEntries->end()){
			(*affEntries)[pair] = affWcounts(potential_windows,actual_windows);
	      	}else{
			result->second.potential_windows += potential_windows;
			if(result->second.actual_windows==NULL)
		  		result->second.actual_windows = actual_windows;
			else{
		  		for(int j=1; j<=mwsize; ++j)
		 	  	 	result->second.actual_windows[j]+=actual_windows[j];
			}
	      	}
	}
	fclose(affFile);
}


int unitcmp(const void * left, const void * right){
  const int * ileft=(const int *) left;
  const int * iright=(const int *) right;
  int wsize=maxWindowSize;

  int freqlevel=maxFreqLevel-1;
  //fprintf(errFile,"%d %d %d %d %d\n",*ileft,*iright,find(&sets[freqlevel][wsize][hlevel][*ileft])->id,find(&sets[freqlevel][wsize][hlevel][*iright])->id);
  while(sets[freqlevel][wsize][*ileft].find()->id==sets[freqlevel][wsize][*iright].find()->id){
    //fprintf(errFile,"%d %d %d %d %d\n",freqlevel,wsize,hlevel,*ileft,*iright);
    wsize--;
    if(wsize<1){
      freqlevel--;
      wsize=maxWindowSize;
    }
  }

  return sets[freqlevel][wsize][*ileft].find()->id - sets[freqlevel][wsize][*iright].find()->id;
}

void find_optimal_layout(){

  int * layout=new int[totalUnits];
  int i;

  for(i=0;i<totalUnits;++i)
    layout[i]=i;

  qsort(layout,totalUnits,sizeof(int),unitcmp);
  char * affinityFilePath = (char *) malloc(strlen("layout")+strlen(version_str)+1);
  strcpy(affinityFilePath,"layout");
  strcat(affinityFilePath,version_str);

  FILE *affinityFile = fopen(affinityFilePath,"w");  

  for(i=0;i<totalUnits;++i){
    if(i%20==0)
      fprintf(affinityFile, "\n");
    fprintf(affinityFile, "%u ",layout[i]);
  }
  fclose(affinityFile);
}


/* This function builds the affinity groups based on the affinity table 
   and prints out the results */
void find_affinity_groups(){


  //printf("total units %d\n",totalUnits);
  affinityHashMap::iterator iter;
  int wsize,i;



  for(iter=affEntries->begin(); iter!=affEntries->end(); ++iter){
    if(iter->second.actual_windows!=NULL)
      for(wsize=2;wsize<=maxWindowSize;++wsize){
        iter->second.actual_windows[wsize]+=iter->second.actual_windows[wsize-1];
      }
  }

  read_affinity_from_file("graph.babc");
  save_affinity_into_file("graph.babc");

  short freqlevel=0;
  float rel_freq_threshold=1.0;

  sets = new disjointSet**[maxFreqLevel];

  while(freqlevel<maxFreqLevel){
    sets[freqlevel]= new disjointSet*[1+maxWindowSize]; 
    for(wsize=1;wsize<=maxWindowSize;++wsize){
      sets[freqlevel][wsize]=new disjointSet[totalUnits];

      for(i=0;i<totalUnits;++i){

        sets[freqlevel][wsize][i].id=i;
        if(wsize==1){
          if(freqlevel==0){
            sets[freqlevel][wsize][i].initSet(i);
          }else{
            sets[freqlevel][wsize][i].parent=&sets[freqlevel][wsize][(sets[freqlevel-1][maxWindowSize][i].parent)->id];
            sets[freqlevel][wsize][i].rank=sets[freqlevel-1][maxWindowSize][i].rank;
          }
        }else{
          sets[freqlevel][wsize][i].parent=&sets[freqlevel][wsize][(sets[freqlevel][wsize-1][i].parent)->id];
          sets[freqlevel][wsize][i].rank=sets[freqlevel][wsize-1][i].rank;
        }
      }


      for(iter=affEntries->begin(); iter!=affEntries->end(); ++iter){
        if(iter->second.actual_windows!=NULL)
          if(rel_freq_threshold*(iter->second.actual_windows[wsize]) > iter->second.potential_windows )
            sets[freqlevel][wsize][iter->first.first].unionSet(&sets[freqlevel][wsize][iter->first.second]);
          //fprintf(errFile,"sets %d and %d for freqlevel=%d, wsize=%d, hlevel=%d\n",iter->first,iter->bb2,freqlevel,wsize,hlevel);
      } 
    }
    freqlevel++;
    rel_freq_threshold+=5.0/maxFreqLevel;
  }
}


/* Must be called at exit*/
void affinityAtExitHandler(){
  //free immature windows
  //list_remove_all(&window_list,NULL,free);
  //
  std::list<SampledWindow> * null_trace_list = new std::list<SampledWindow>();
  SampledWindow sw = SampledWindow();
  sw.partial_trace_list.push_front(-1);
  null_trace_list->push_front(sw);

  //trace_list_queue.push(null_trace_list);
  //sem_post(&affinity_sem);

  //pthread_join(update_affinity_thread,NULL);


  find_affinity_groups();

  find_optimal_layout();
  fflush(errFile);
  fclose(errFile);

}


void print_trace(list<SampledWindow> * tlist){
  std::list<SampledWindow>::iterator window_iter=tlist->begin();

  std::list<short>::iterator trace_iter;

  fprintf(errFile,"trace list:\n");
  while(window_iter!=tlist->end()){
    trace_iter =  window_iter->partial_trace_list.begin();
    fprintf(errFile,"windows: %d\n",window_iter->wcount);

    while(trace_iter!=window_iter->partial_trace_list.end()){
      fprintf(errFile,"%d ",*trace_iter);
      trace_iter++;
    }
    fprintf(errFile,"\n");
    window_iter++;
  }
  fprintf(errFile,"---------------------------------------------\n");
}





static void save_affinity_environment_variables(void) {
  const char *SampleRateEnvVar, *MaxWindowSizeEnvVar, *MaxFreqLevelEnvVar;

  if ((SampleRateEnvVar = getenv("SAMPLE_RATE")) != NULL) {
    sampleRate = (float)strtod(SampleRateEnvVar,NULL);
  }

  if((MaxWindowSizeEnvVar = getenv("MAX_WINDOW_SIZE")) != NULL){
    maxWindowSize = atoi(MaxWindowSizeEnvVar);
  }

  if((MaxFreqLevelEnvVar = getenv("MAX_FREQ_LEVEL")) != NULL){
    maxFreqLevel = atoi(MaxFreqLevelEnvVar);
  }

}


/* llvm_trace_basic_block - called upon hitting a new basic block. */
extern "C" void llvm_trace_basic_block (short FuncNum) {
    sample_window(FuncNum);
}

extern "C" void llvm_init_affinity_analysis(int _totalFuncs){
  errFile=fopen("err.out","w");
  //errFile=stdout;
  totalFuncs=_totalFuncs;
  save_affinity_environment_variables();
  sampledWindows=0;
  trace_list_size=0;
  //debugFile=fopen("debug.txt","w");
  srand(time(NULL));
  short i,wsize;
  contains_func = new bool [totalUnits]();
  func_window_it = new std::list<SampledWindow>::iterator [totalUnits];
  func_trace_it = new std::list<short>::iterator [totalUnits];
  affEntries = new affinityHashMap();
  //sem_init(&affinity_sem,0,1);

  analysis_switch = new bool [totalFuncs]();
  analyzed = new bool [totalFuncs]();
  func_count = new int [totalFuncs]();
  funcs = new short [totalFuncs];
  preference = new short [totalFuncs];
  stage_affinity = new int* [totalFuncs];
  stage_affinity_sum = new int [totalFuncs];
  potential_stage_windows = new int[totalFuncs];

  for(int i=0;i<totalFuncs; ++i){
    funcs[i]=i;
    preference[i]=i;
  }
  
  atexit (affinityAtExitHandler);
  
  // get prepared for the first counting stage 
  func_counting = true;
  stage_time = stage_quantum = quantum;

}


int compare_count (const void * p1, const void * p2){
  short func1= * (short *)p1;
  short func2= * (short *)p2;
  if(func_count[func1] > func_count[func2])
    return -1;
  if(func_count[func1] < func_count[func2])
    return 1;
  return 0;
}

int compare_pw_freq(const void * p1, const void *p2){
  short func1 = * (short *)p1;
  short func2 = * (short *) p2;

  if(func1==now_analyzed_func)
    return -1;
  if(func2==now_analyzed_func)
    return 1;

  int wsize;
  for(wsize=1; wsize<=maxWindowSize; ++wsize){
      if(stage_affinity[func1]!=NULL && (stage_affinity[func1][wsize]*5 > stage_windows))
          if(stage_affinity[func2]==NULL || (stage_affinity[func1][wsize] > stage_affinity[func2][wsize]))
        return -1;
      if(stage_affinity[func2]!=NULL && (stage_affinity[func2][wsize]*5 > stage_windows))
          if(stage_affinity[func1]==NULL || (stage_affinity[func2][wsize] > stage_affinity[func1][wsize]))
            return 1;
  }

  return 0;
  
}

void update_overal_affinity(){

  for(short funcNum=0; funcNum<totalFuncs; ++funcNum){
    if(stage_affinity[funcNum]!=NULL){
      affPair pair(now_analyzed_func, funcNum);
      affinityHashMap::iterator result=affEntries->find(pair);

      if(result==affEntries->end()){
        (*affEntries)[pair] = affWcounts(potential_stage_windows[funcNum],stage_affinity[funcNum]);
      }else{
        result->second.potential_windows += potential_stage_windows[funcNum];
        if(result->second.actual_windows==NULL)
          result->second.actual_windows = stage_affinity[funcNum];
        else{
          for(int j=1; j<=maxWindowSize; ++j)
            result->second.actual_windows[j]+=stage_affinity[funcNum][j];
          delete[] stage_affinity[funcNum];
        }
      }

    }

  }

}

void cut_analysis_set_in_half(){
  qsort(preference, analysis_set_size, sizeof(short), compare_pw_freq);
  int sum_affinity =0;
  short i;
  
  for(i=0;i<analysis_set_size; ++i){
    potential_stage_windows[preference[i]] = stage_windows;
    if(stage_affinity_sum[preference[i]]!=0)
        sum_affinity += stage_affinity_sum[preference[i]];
	else
		break;
  }

  int half_sum_affinity=0;

  for(i=0;i<analysis_set_size && (half_sum_affinity*2 < sum_affinity) ; ++i)
	half_sum_affinity += stage_affinity_sum[preference[i]];
    //if(stage_affinity_sum[preference[i]]!=0)

  for(short j=i;j<analysis_set_size; ++j)
    analysis_switch[preference[j]]=false;

  analysis_set_size = i;

  //fprintf(errFile,"analysis set size is now halved: %d\n",analysis_set_size);

  /*
   * We stop analysis if the analysis set size falls below 6
   */
  if(analysis_set_size <= 1){
    /*
     * We update the overal affinity (hashtable)
     */
    update_overal_affinity();
    
    trace_list.clear();
    for(short j=0; j<totalFuncs; ++j)
      contains_func[j]=false;

    trace_list_size=0;

    func_counting=true;
    for(i=0; i<totalFuncs; ++i){
      analysis_switch[i]=false;
		func_count[i]=0;
    }
    stage_time = stage_quantum = quantum;
  }else{
    stage_time = stage_quantum = stage_quantum*2;
  }

}

void proceed_to_next_stage(){

  /*
   * If were previously counting function records, it means that we now need to
   * pick the function to analyze according the function counts.
   * We pick the one that is not analyzed before and has the biggest count.
   * If all functions have already been analyzed, we start over.
   */
  if(func_counting){
    qsort(funcs, totalFuncs, sizeof(short), compare_count);
    short i=0;
    while(i<totalFuncs){
      if(!analyzed[funcs[i]]){
        now_analyzed_func=funcs[i];
        analyzed[funcs[i]]=true;
        break;
      }
      i++;
    }

    /*
     * If all functions have been analyzed, we start over (we set the analyzed
     * bit of every function and pick funcs[0] 
     */
    if(i==totalFuncs){
      for(short j=0; j<totalFuncs; ++j)
        analyzed[j]=false;
      now_analyzed_func = funcs[0];
      analyzed[funcs[0]]=true;
    }
    
    /*
     * Initially we turn on the analysis switch for all functions. These switches
     * will be turned off over time.
     */
    for(i=0; i<totalFuncs; ++i){
      analysis_switch[i]=true;
      potential_stage_windows[i]=0;
      stage_affinity_sum[i]=0;
      stage_affinity[i]=NULL;
    }


    /*
     * Let the first stage run for quantum time.
     */
    stage_time = stage_quantum = quantum;

    /*
     * Turn of function counting (although it does not hurt turning it on, we
     * do this just to avoid the overhead)
     */
    func_counting = false;

    /*
     * Reset the number of windows for this stage
     */
    stage_windows=0;

    /*
     * Analysis set is full
     */
    analysis_set_size = totalFuncs;

  }
  /*
   * Otherwise, if we weren't counting functions, we cut the analysis set in half.
   */
  else{
    cut_analysis_set_in_half();
  }
/*
  fprintf(errFile,"this is the current stage:\n");
  if(func_counting)
    fprintf(errFile,"function counting\n");
  else
    fprintf(errFile,"analyzing function %d, with analysis set size %d\n",now_analyzed_func, analysis_set_size);
*/


}


extern "C" bool get_switch(short FuncNumber){
  //fprintf(errFile,"FuncNumber:%d\tstage time:%d\tanalysis_switch:%d\n",FuncNumber,stage_time,analysis_switch[FuncNumber]);
  //return true;
  if(stage_time==0)
    proceed_to_next_stage();
  if(func_counting)
    func_count[FuncNumber]++;
  --stage_time;
  //return (FuncNumber!=1584 && FuncNumber!=1691 && FuncNumber!=3245);
  	return analysis_switch[FuncNumber];
}

void update_stage_affinity(short FuncNum, std::list<SampledWindow>::iterator update_window_end){
    /* We move toward the tail of the list until we hit update_window_end 
     * For every partial trace lis (window), we update the affinity between
     * now_analyzed_func and FuncNum.
     */
  if(!analysis_switch[FuncNum])
    return;
  int window_size = 0;
  tl_window_iter=trace_list.begin();
  //print_trace(&trace_list);
  while(tl_window_iter != update_window_end){
    //fprintf(errFile,"iterators are % and end is %s\n",trace_list.begin(),update_window_end);
    window_size += tl_window_iter->partial_trace_list.size();
    if(stage_affinity[FuncNum]==NULL){
      stage_affinity[FuncNum]=new int[maxWindowSize+1]();
	//fprintf(errFile,"newed one time for (%d,%d)\n",now_analyzed_func,FuncNum);
	}
    //printf("window size is %d\n",window_size);
    stage_affinity[FuncNum][window_size]+=tl_window_iter->wcount;
    stage_affinity_sum[FuncNum]+=tl_window_iter->wcount;
    //trace_list_to_update->push_back(*tl_window_iter);  
    tl_window_iter++;
  }

}


void sample_window(short FuncNum){
  //printf("contains_func[%d]=%d\n",FuncNum,contains_func[FuncNum]);
  //printf("size of the trace list is %d\n",trace_list_size);
  
  //printf("now analyzed func is %d\n",now_analyzed_func);
  //fprintf(errFile,"stage time is %d\n",stage_time);
  //printf("stage quantum time is %d\n",stage_quantum);
  //print_trace(&trace_list);

  //int r=rand()%10000000;
  //if(r < sampleRate*10000000){
  if(FuncNum==now_analyzed_func){
    stage_windows++;
    SampledWindow sw;
    sw.wcount=1;
    //sw.partial_trace_list.push_front(FuncNum);
    trace_list.push_front(sw);
  }

  if(!trace_list.empty())
    trace_list.front().partial_trace_list.push_front(FuncNum);
  else
    return;

  //std::list<SampledWindow> * trace_list_to_update = NULL;

  /*
   * Check if the same function record exists somewhere in the trace.
   * If it does not exist, we don't need to refine the trace anymore.
   */
  if(!contains_func[FuncNum]){
    //printf("found FuncNum:%d in trace\n",FuncNum);
    //print_trace(&trace_list);
    
    //Increment the overal length of the trace list.
    trace_list_size++;

    /*
     * If the length of the trace overflows, remove one partial trace list
     * from the tail of the list.
     */
    if(trace_list_size > maxWindowSize){

      // Get the last partial trace list
      std::list<short> * last_window_trace_list= &trace_list.back().partial_trace_list;

      // Decrement the overal size of the trace list
      trace_list_size-=last_window_trace_list->size();

      // Clear the partial trace list and deallocate the memory
      while(!last_window_trace_list->empty()){
        contains_func[last_window_trace_list->front()]=false;
        last_window_trace_list->pop_front();
      }

      // Remove the tail of the trace list
      trace_list.pop_back();
    }
    
    // Do the update in the if statement
    //trace_list_to_update = new std::list<SampledWindow>(trace_list);
    

    /*
     * If after managing the possible overflow, the trace list is not empty
     * we need to insert the pointers to this function record in func_window_it
     * and func_trace_it arrays and set the corresponding bit of contains_func 
     * pointer.
     */
    if(trace_list_size!=0){
      contains_func[FuncNum]=true;
      func_window_it[FuncNum] = trace_list.begin();
      func_trace_it[FuncNum] = trace_list.begin()->partial_trace_list.begin();

      // Update the affinity between now_analyzed_func and FuncNum according to
      // all the windows in the trace list.
      update_stage_affinity(FuncNum,trace_list.end());
    }
  }
  /*
   * On the other hand, if the same function records already exists in the 
   * trace list, we need to refine the trace list (remove the previous record
   * and add the new one. The size of the trace list will not change.
   */
  else{
    //trace_list_to_update = new std::list<SampledWindow>();

    update_stage_affinity(FuncNum,func_window_it[FuncNum]);
    /*tl_window_iter=trace_list.begin();
    while(tl_window_iter != func_window_it[FuncNum]){
      trace_list_to_update->push_back(*tl_window_iter);
      
      tl_window_iter++;
    }*/


    tl_window_iter = func_window_it[FuncNum];
    tl_window_iter->partial_trace_list.erase(func_trace_it[FuncNum]);

    if(tl_window_iter->partial_trace_list.empty()){
      int temp_wcount=tl_window_iter->wcount;
      tl_window_iter--;
      tl_window_iter->wcount+=temp_wcount;
      tl_window_iter++;
      trace_list.erase(tl_window_iter);
    }

    // Reassign the pointers to the function record for FuncNum
    func_window_it[FuncNum] = trace_list.begin();
    func_trace_it[FuncNum] = trace_list.begin()->partial_trace_list.begin();

  }

/*
  if(!trace_list_to_update->empty()){
    trace_list_queue.push(trace_list_to_update);
    sem_post(&affinity_sem);
  }else
    delete trace_list_to_update;
*/
  //update_affinity();

}


SampledWindow::SampledWindow(const SampledWindow & sw){
  wcount=sw.wcount;
  partial_trace_list = std::list<short>(sw.partial_trace_list);
}

SampledWindow::SampledWindow(){
  wcount=0;
  partial_trace_list = std::list<short>();
}



affPair::affPair(short _first, short _second){
  //if(_first<_second){
    first=_first;
    second=_second;
  //}else{
  //  first=_second;
  //  second=_first;
  //}
}

affPair::affPair(){}

affWcounts::affWcounts(int _potential_windows, int * _actual_windows){
  potential_windows=_potential_windows;
  actual_windows=_actual_windows;
}

affWcounts::affWcounts(){}

bool eqAffPair::operator()(affPair const& pair1, affPair const& pair2) const{
  //return ((entry1.first == entry2.first) && (entry1.second == entry2.second));
  return (pair1.first == pair2.first);
}



size_t affPair_hash::operator()(affPair const& pair)const{
  //return MurmurHash2(&entry,sizeof(entry),5381);
  return pair.first*5381+pair.second;
}




void disjointSet::initSet(unsigned _id){
  id=_id;
  parent = this;
  rank=0;
  size=1;
}


void disjointSet::unionSet(disjointSet* set2){

  disjointSet * root1=this->find();
  disjointSet * root2=set2->find();
  if(root1==root2)
    return;

  //x and y are not already in the same set. merge them.
  if(root1->rank < root2->rank){
    root1->parent=root2;
    root2->size+=root1->size;
  }else if(root1->rank > root2->rank){
    root2->parent=root1;
    root1->size+=root2->size;
  }else{
    root2->parent=root1;
    root1->rank++;
    root1->size+=root2->size;
  }
}

unsigned disjointSet::getSize(){
  return find()->size;
}

disjointSet* disjointSet::find(){
  //fprintf(errFile,"%x %d\n",set,set->id);
  if(this->parent!=this){
    this->parent=this->parent->find();
  }
  return this->parent;
}

