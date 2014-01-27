#include "affinity.hpp"
#include <stdio.h>
#include <algorithm>
#include <pthread.h>
#include <semaphore.h>
#include <boost/lockfree/queue.hpp>
#include <string.h>
#include <vector>

int DEBUG;
FILE * comparisonFile, * orderFile, * traceFile;
wsize_t maxWindowSize;
int memoryLimit;
int sampleRateLog;
uint32_t sampleSize;
uint32_t sampleMask;
func_t maxFreqLevel;

JointFreqMap * joint_freqs;
JointFreqRangeMap * joint_freq_ranges;
list<UpdateEntry> * joint_freq_update_lists;
list<SampledWindow> trace_list;

bool * contains_func;
list<func_t>::iterator * func_trace_it;
list<SampledWindow>::iterator * func_window_it;
wsize_t trace_list_size;

list<SampledWindow>::iterator window_iter;
list<SampledWindow>::iterator top_window_iter;
list<func_t>::iterator func_iter;
list<func_t>::iterator partial_trace_list_end;
list<func_t> * last_window_trace_list;

uint32_t *** single_freq_ranges; 
uint32_t ** single_freqs;


int prevFunc;
FILE * graphFile, * debugFile;

uint32_t * null_joint_freq = new uint32_t[maxWindowSize+1]();
const char * version_str=".fabc";

sem_t affinity_sem;

void create_single_freqs(){
  for(func_t f=0; f<totalFuncs; ++f)
    for(wsize_t i=1; i<= maxWindowSize; ++i)
      for(wsize j=i; j<= maxWindowSize; ++j)
        for(wsize k=i; k<=j; ++k)
        single_freqs[f][k]+=single_freq_ranges[f][i][j];
}

void create_joint_freqs(){
  JointFreqRangeMap::iterator it_end= joint_freq_ranges->end();
  for(JointFreqRangeMap::iterator it=joint_freq_ranges->begin(); it!=it_end; ++it){	
    funcpair_t update_pair = it->first;
    uint32_t ** freq_range_matrix = it->second;
    uint32_t * freq_array;
    JointFreqMap::iterator result = joint_freqs->find(update_pair);
    if(result== joint_freqs->end())
      (*joint_freqs)[update_pair]= freq_array=new uint32_t[maxWindowSize+1]();
    else
      freq_array=result->second;

    for(int i=2; i<=maxWindowSize;++i)
      for(int j=i; j<=maxWindowSize; ++j)
        for(int k=i; k<=j; ++k){
          freq_array[k]+=freq_range_matrix[i][j];
        }
  } 
}
void commit_freq_updates(SampledWindow &sw, wsize_t max_wsize){

  while(!sw.single_update_list.empty()){
    SingleUpdateEntry sue = sw.single_update_list.front();
    sw.single_update_list.pop_front();

    assert(sue.min_wsize <= max_wsize);
    single_freq_ranges[sue.func][sue.min_wsize][max_wsize]++;
  }

  while(!sw.joint_update_list.empty()){
    JointUpdateEntry jue = sw.joint_update_list.front();
    sw.joint_update_list.pop_front();

    assert(jue.min_wsize <= max_wsize);

    uint32_t ** joint_freq_range_matrix;
    JointFreqRangeMap::iterator result=joint_freq_ranges->find(jue.func_pair);
    if(result == joint_freq_ranges->end()){
      freq_range_matrix = new uint32_t*[maxWindowSize+1];
      for(int i=1; i<=maxWindowSize; ++i)
        freq_range_matrix[i]=new uint32_t[maxWindowSize+1]();

      (*joint_freq_ranges)[jue.func_pair]= freq_range_matrix;
    }else
      freq_range_matrix=result->second;

    freq_range_matrix[jue.min_wsize][max_wsize]++;

    if(DEBUG>2){
      fprintf(debugFile,"&&&&&&&&&&&&&&&& commit\n");
      fprintf(debugFile,"(%d,%d)[%d..%d]++\n",jue.func_pair.first, jue.func_pair.second,jue.min_wsize,max_wsize);
    }
  }
}

extern "C" void record_function_exec(func_t FuncNum){
  if(prevFunc==FuncNum)
    return;
  else
    prevFunc=FuncNum;

  //fprintf(traceFile,"%hd\n",FuncNum);
  uint32_t r=rand();
  bool sampled=false;
  if((r & sampleMask)==0){
    SampledWindow sw;
    sw.wcount=1;
    trace_list.push_front(sw);
    sampled=true;
  }


  if(trace_list_size!=0 || sampled)
    trace_list.front().partial_trace_list.push_front(FuncNum);
  else
    return;


  if(DEBUG>0){
    if(sampled)
      fprintf(debugFile,"new window\n");
    print_trace(&trace_list);
  }

  if(!contains_func[FuncNum]){
    trace_list_size++;
    trace_list.front().wsize++;

    if(trace_list_size > maxWindowSize){
      if(trace_list.size()==1)
        trace_list.front().partial_trace_list.pop_front();

      commit_freq_updates(trace_list.back());
      last_window_trace_list= &(trace_list.back().partial_trace_list);			

      while(!last_window_trace_list->empty()){
        func_t oldFuncNum=last_window_trace_list->front();
        contains_func[oldFuncNum]=false;
        last_window_trace_list->pop_front();
      }
      trace_list_size-=trace_list.back().wsize;

      trace_list.pop_back();
    }

    if(trace_list_size>0){
      sequential_update_affinity(trace_list.end());
      contains_func[FuncNum]=true;
      func_window_it[FuncNum] = trace_list.begin();
      func_trace_it[FuncNum] = trace_list.begin()->partial_trace_list.begin();
    }

  }else{
    trace_list.front().wsize++;
    func_window_it[FuncNum]->wsize--;
    if(trace_list.begin()!=func_window_it[FuncNum]){

      sequential_update_affinity(func_window_it[FuncNum]);
    }
    window_iter=func_window_it[FuncNum];
    window_iter->partial_trace_list.erase(func_trace_it[FuncNum]);

    if(window_iter->partial_trace_list.empty()){
      commit_freq_updates(*window_iter);
      trace_list.erase(window_iter);
    }

      func_window_it[FuncNum] = trace_list.begin();
      func_trace_it[FuncNum] = trace_list.begin()->partial_trace_list.begin();	
  }


}

void print_optimal_layout(){
  func_t * layout = new func_t[totalFuncs];
  int count=0;
  for(int i=0; i< totalFuncs; ++i){
    //printf("i is now %d\n",i);
    if(disjointSet::sets[i]){
      disjointSet * thisSet=disjointSet::sets[i];
      for(deque<func_t>::iterator it=disjointSet::sets[i]->elements.begin(), 
          it_end=disjointSet::sets[i]->elements.end()
          ; it!=it_end ; ++it){
        //printf("this is *it:%d\n",*it);
        layout[count++]=*it;
        disjointSet::sets[*it]=0;
      }
      thisSet->elements.clear();
      delete thisSet;
    }
  }

  char affinityFilePath[80];
  strcpy(affinityFilePath,"layout");
  //strcat(affinityFilePath,(affEntryCmp==&affEntry1DCmp)?("1D"):("2D"));
  strcat(affinityFilePath,version_str);
  FILE *layoutFile = fopen(affinityFilePath,"w");  

  for(int i=0;i<totalFuncs;++i){
    if(i%20==0)
      fprintf(layoutFile, "\n");
    fprintf(layoutFile, "%u ",layout[i]);
  }
  fclose(layoutFile);
}

void print_optimal_layouts(){
  func_t * layout = new func_t[totalFuncs];
  int count=0;
  for(int i=0; i< totalFuncs; ++i){
    //printf("i is now %d\n",i);
    if(disjointSet::sets[i]){
      disjointSet * thisSet=disjointSet::sets[i];
      for(deque<func_t>::iterator it=disjointSet::sets[i]->elements.begin(), 
          it_end=disjointSet::sets[i]->elements.end()
          ; it!=it_end ; ++it){
        //printf("this is *it:%d\n",*it);
        layout[count++]=*it;
        disjointSet::sets[*it]=0;
      }
      thisSet->elements.clear();
      delete thisSet;
    }
  }


  char affinityFilePath[80];
  strcpy(affinityFilePath,"layout_");
  strcat(affinityFilePath,to_string(maxWindowSize).c_str());
  strcat(affinityFilePath,version_str);

  FILE *affinityFile = fopen(affinityFilePath,"w");  

  for(func_t i=0;i<totalFuncs;++i){
    if(i%20==0)
      fprintf(affinityFile, "\n");
    fprintf(affinityFile, "%u ",layout[i]);
  }
  fclose(affinityFile);
}

/* The data allocation function (totalFuncs need to be set before entering this function) */
void initialize_affinity_data(){
  prevFunc=-1;
  trace_list_size=0;

  //debugFile=fopen("debug.txt","w");
  srand(time(NULL));

  joint_freqs=new JointFreqMap();
  joint_freq_ranges= new JointFreqRangeMap();

  single_freqs=new uint32_t* [totalFuncs];
  single_freq_ranges = new uint32_t** [totalFuncs];

  for(func_t f=0;f<totalFuncs;++f){
    single_freqs[f]=new uint32_t[maxWindowSize+1]();
    single_freq_ranges[f]=new uint32_t*[maxWindowSize+1];
    for(wsize_t i=1; i<=maxWindowSize; i++)
      single_freq_ranges[f][i]=new uint32_t[maxWindowSize+1]();
  }

  contains_func = new bool [totalFuncs]();


  func_window_it = new list<SampledWindow>::iterator [totalFuncs];
  func_trace_it = new list<func_t>::iterator  [totalFuncs];

  if(DEBUG > 9)
    traceFile = fopen("trace.txt","w");

  if(DEBUG > 0)
    debugFile = fopen("debug.txt","w");
}

void aggregate_affinity(){
  JointFreqMap::iterator iter;


  char * graphFilePath=(char*) malloc(strlen("graph")+strlen(version_str)+1);
  strcpy(graphFilePath,"graph");
  strcat(graphFilePath,version_str);

  graphFile=fopen(graphFilePath,"r");
  if(graphFile!=NULL){
    func_t u1,u2;
    uint32_t sfreq,jfreq;
    for(func_t i=0;i<totalFuncs; ++i){
      fscanf(graphFile,"(%*hd):");
      for(func_t wsize=1; wsize<=maxWindowSize; ++wsize){
        fscanf(graphFile,"%lu ",&sfreq);
        single_freqs[i][wsize]+=sfreq;
      }
    }
    while(fscanf(graphFile,"(%hd,%hd):",&u1,&u2)!=EOF){
      funcpair_t entryToAdd=funcpair_t(u1,u2);
      uint32_t * freq_array=(*joint_freqs)[entryToAdd];
      if(freq_array==NULL){
        freq_array= new uint32_t[maxWindowSize+1]();
        (*joint_freqs)[entryToAdd]=freq_array;
      }
      //printf("(%hd,%hd)\n",u1,u2);
      for(func_t wsize=1; wsize<=maxWindowSize; ++wsize){
        fscanf(graphFile,"{%lu} ",&jfreq);
        freq_array[wsize] +=jfreq;
      }
    }
    fclose(graphFile);
  }


  graphFile=fopen(graphFilePath,"w+");

  for(func_t i=0;i<totalFuncs;++i){
    fprintf(graphFile,"(%hd):",i);
    for(func_t wsize=1; wsize<=maxWindowSize;++wsize)
      fprintf(graphFile,"%lu ",single_freqs[i][wsize]);
    fprintf(graphFile,"\n");
  }
  for(iter=joint_freqs->begin(); iter!=joint_freqs->end(); ++iter){
    fprintf(graphFile,"(%hd,%hd):",iter->first.first,iter->first.second);
    for(func_t wsize=1;wsize<=maxWindowSize;++wsize)
      fprintf(graphFile,"{%lu} ",iter->second[wsize]);
    fprintf(graphFile,"\n");
  }


  fclose(graphFile);
}

/* This function builds the affinity groups based on the affinity table 
   and prints out the results */
void find_affinity_groups(){

  vector<funcpair_t> all_affEntry_iters;
  for(JointFreqMap::iterator iter=joint_freqs->begin(); iter!=joint_freqs->end(); ++iter){
    if(iter->first.first < iter->first.second)
      all_affEntry_iters.push_back(iter->first);
  }
  comparisonFile = fopen("compare.txt","w");
  sort(all_affEntry_iters.begin(),all_affEntry_iters.end(),affEntryCmp);
  fclose(comparisonFile);
  comparisonFile=NULL;

  orderFile = fopen("order.txt","w");


  disjointSet::sets = new disjointSet *[totalFuncs];
  for(func_t i=0; i<totalFuncs; ++i)
    disjointSet::init_new_set(i);

  for(vector<funcpair_t>::iterator iter=all_affEntry_iters.begin(); iter!=all_affEntry_iters.end(); ++iter){
    fprintf(orderFile,"(%d,%d) {{%lu|%lu}}\n",iter->first,iter->second,single_freqs[iter->first][maxWindowSize],single_freqs[iter->second][maxWindowSize]);
    disjointSet::mergeSets(iter->first, iter->second);
  } 

  fclose(orderFile);

}

/* Must be called at exit*/
void affinityAtExitHandler(){
  fclose(traceFile);
  for(func_t i=0;i<totalFuncs; ++i)
    commit_joint_freq_updates(i,trace_list_size);
  create_joint_freqs();
  create_single_freqs();
  aggregate_affinity();

  affEntryCmp=&affEntry2DCmp;
  find_affinity_groups();
  print_optimal_layout();

}


void print_trace(list<SampledWindow> * tlist){
  list<SampledWindow>::iterator window_iter=tlist->begin();

  list<func_t>::iterator trace_iter;

  printf("---------------------------------------------\n");
  while(window_iter!=tlist->end()){
    trace_iter =  window_iter->partial_trace_list.begin();
    printf("size: %d\n",window_iter->wsize);


    while(trace_iter!=window_iter->partial_trace_list.end()){
      printf("%d ",*trace_iter);
      trace_iter++;
    }
    printf("\n");
    window_iter++;
  }
}




void sequential_update_affinity(list<SampledWindow>::iterator grown_list_end){

  wsize_t top_wsize=0;
  wsize_t wsize;
  window_iter = trace_list.begin();
  func_iter = window_iter->partial_trace_list.begin();

  func_t FuncNum= * func_iter;

  while(top_window_iter!= grown_list_end){
    top_wsize += top_window_iter->wsize;

    SingleUpdateEntry sue(FuncNum,top_wsize);
    top_window_iter->add_single_update_entry(sue);
          
    if(DEBUG>1){
            fprintf(debugFile,"################\n");
            fprintf(debugFile,"update single: %d[%d..]++\n",FuncNum,top_wsize);
          }

    func_iter = window_iter->partial_trace_list.begin();

    partial_trace_list_end = window_iter->partial_trace_list.end();
    while(func_iter != partial_trace_list_end){

      func_t oldFuncNum= * func_iter;
      if(oldFuncNum!=FuncNum){
        window_iter = top_window_iter;
        wsize=top_wsize;

        while(window_iter != grown_list_end){
          wsize+=window_iter->wsize;

                    JointUpdateEntry jue(unordered_pair(FuncNum,oldFuncNum);
          window_iter->add_joint_update_entry(jue);

          if(DEBUG>1){
            fprintf(debugFile,"****************\n");
            fprintf(debugFile,"update pair: (%d,%d)[%d..]++\n",oldFuncNum,FuncNum,wsize);
          }            
          window_iter++;
          }
        }

              func_iter++;
      }

              top_window_iter++;
              }


              } 



/*
   bool affEntry1DCmp(const funcpair_t &left_pair,const funcpair_t &right_pair){

   int * jointFreq_left = (*joint_freqs)[left_pair];
   int * jointFreq_right = (*joint_freqs)[right_pair];
   if(jointFreq_left == NULL && jointFreq_right != NULL)
   return false;
   if(jointFreq_left != NULL && jointFreq_right == NULL)
   return true;

   if(jointFreq_left != NULL){
   int left_pair_val, right_pair_val;

   float rel_freq_threshold=2.0;
   for(func_t wsize=2;wsize<=maxWindowSize;++wsize){

   if((rel_freq_threshold*(jointFreq_left[wsize]) >= single_freqs[left_pair.first][wsize]) && 
   (rel_freq_threshold*(jointFreq_left[wsize]) >= single_freqs[left_pair.second][wsize]))
   left_pair_val = 1;
   else
   left_pair_val = -1;

   if((rel_freq_threshold*(jointFreq_right[wsize]) >= single_freqs[right_pair.first][wsize]) && 
   (rel_freq_threshold*(jointFreq_right[wsize]) >= single_freqs[right_pair.second][wsize]))
   right_pair_val = 1;
   else
   right_pair_val = -1;

   if(left_pair_val != right_pair_val)
   return (left_pair_val > right_pair_val);
   }
   }

   if(left_pair.first != right_pair.first)
   return (left_pair.first > right_pair.first);

   return left_pair.second > right_pair.second;

   }
   */

uint32_t * GetWithDef(JointFreqMap * m, const funcpair_t &key, uint32_t * defval) {
  JointFreqMap::const_iterator it = m->find( key );
  if ( it == m->end() ) {
    return defval;
  }
  else {
    return it->second;
  }
}
bool affEntry2DCmp(const funcpair_t &left_pair, const funcpair_t &right_pair){

  funcpair_t left_pair_rev = funcpair_t(left_pair.second, left_pair.first);
  funcpair_t right_pair_rev = funcpair_t(right_pair.second, right_pair.first);

  uint32_t * joint_freq_left = GetWithDef(joint_freqs, left_pair, null_joint_freq);
  uint32_t * joint_freq_right = GetWithDef(joint_freqs, right_pair, null_joint_freq);

  uint32_t * joint_freq_left_rev = GetWithDef(joint_freqs, left_pair_rev, null_joint_freq);
  uint32_t * joint_freq_right_rev = GetWithDef(joint_freqs, right_pair_rev, null_joint_freq);

  int left_pair_val, right_pair_val;

  func_t freqlevel;
  float rel_freq_threshold;
  for(freqlevel=0, rel_freq_threshold=1.0; freqlevel<maxFreqLevel; ++freqlevel, rel_freq_threshold+=5.0/maxFreqLevel){
    for(func_t wsize=2;wsize<=maxWindowSize;++wsize){

      uint32_t joint_freq_left_wsize = joint_freq_left[wsize]+joint_freq_left_rev[wsize];
      uint32_t single_freq_left_wsize = single_freqs[left_pair.first][wsize]+single_freqs[left_pair.second][wsize];

      if(rel_freq_threshold*joint_freq_left_wsize >= single_freq_left_wsize) 
        left_pair_val = 1;
      else
        left_pair_val = -1;

      uint32_t joint_freq_right_wsize = joint_freq_right[wsize]+joint_freq_right_rev[wsize];
      uint32_t single_freq_right_wsize = single_freqs[right_pair.first][wsize]+single_freqs[right_pair.second][wsize];

      if(rel_freq_threshold*joint_freq_right_wsize >= single_freq_right_wsize)
        right_pair_val = 1;
      else
        right_pair_val = -1;

      if(left_pair_val != right_pair_val){
        if(comparisonFile!=NULL){
          fprintf(comparisonFile,"(%d,%d):(%lu/%lu) ",left_pair.first,left_pair.second,joint_freq_left_wsize,single_freq_left_wsize); 
          fprintf(comparisonFile,(left_pair_val > right_pair_val)?(">"):("<"));
          fprintf(comparisonFile," (%d,%d):(%lu/%lu) [wsize:%d, threshold:%f]\n",right_pair.first,right_pair.second, joint_freq_right_wsize, single_freq_right_wsize ,wsize,rel_freq_threshold);
        }
        return (left_pair_val > right_pair_val);
      }
    }
  }

  if(comparisonFile!=NULL)
    fprintf(comparisonFile,"(%d,%d) <> (%d,%d)\n",left_pair.first,left_pair.second, right_pair.first, right_pair.second);

  if(left_pair.first != right_pair.first)
    return (left_pair.first < right_pair.first);

  return left_pair.second < right_pair.second;

}


void disjointSet::mergeSets(disjointSet * set1, disjointSet* set2){

  disjointSet * merger = (set1->size()>=set2->size())?(set1):(set2);

  disjointSet * mergee = (set1->size()<set2->size())?(set1):(set2);


  funcpair_t frontMerger_backMergee(merger->elements.front(), mergee->elements.back());
  funcpair_t backMerger_backMergee(merger->elements.back(), mergee->elements.back());
  funcpair_t backMerger_frontMergee(merger->elements.back(), mergee->elements.front());
  funcpair_t frontMerger_frontMergee(merger->elements.front(), mergee->elements.front());
  funcpair_t conAffEntriesArray[4]={frontMerger_frontMergee, frontMerger_backMergee, backMerger_frontMergee, backMerger_backMergee};
  vector<funcpair_t> conAffEntries(conAffEntriesArray,conAffEntriesArray+4);
  sort(conAffEntries.begin(), conAffEntries.end(), affEntryCmp);

  assert(affEntryCmp(conAffEntries[0],conAffEntries[1]) || (conAffEntries[0]==conAffEntries[1]));
  assert(affEntryCmp(conAffEntries[1],conAffEntries[2]) || (conAffEntries[1]==conAffEntries[2]));
  assert(affEntryCmp(conAffEntries[2],conAffEntries[3]) || (conAffEntries[2]==conAffEntries[3]));

  bool con_mergee_front = (conAffEntries[0] == backMerger_frontMergee) || (conAffEntries[0] == frontMerger_frontMergee);
  bool con_merger_front = (conAffEntries[0] == frontMerger_frontMergee) || (conAffEntries[0] == frontMerger_backMergee);

  if(con_mergee_front){

    for(deque<func_t>::iterator it=mergee->elements.begin(); it!=mergee->elements.end(); ++it){
      if(con_merger_front)
        merger->elements.push_front(*it);
      else
        merger->elements.push_back(*it);
      disjointSet::sets[*it]=merger;
    }
  }else{
    for(deque<func_t>::reverse_iterator rit=mergee->elements.rbegin(); rit!=mergee->elements.rend(); ++rit){
      if(con_merger_front)
        merger->elements.push_front(*rit);
      else
        merger->elements.push_back(*rit);
      disjointSet::sets[*rit]=merger;
    }
  }

  mergee->elements.clear();
  delete mergee;
}

static void save_affinity_environment_variables(void) {
  const char *SampleRateEnvVar, *MaxWindowSizeEnvVar, *MaxFreqLevelEnvVar, *MemoryLimitEnvVar, *DebugEnvVar;

  if((DebugEnvVar = getenv("DEBUG")) !=NULL){
    DEBUG = atoi(DebugEnvVar);
  }

  if ((MemoryLimitEnvVar = getenv("MEMORY_LIMIT")) != NULL) {
    memoryLimit = atoi(MemoryLimitEnvVar);
  }

  if ((SampleRateEnvVar = getenv("SAMPLE_RATE")) != NULL) {
    sampleRateLog = atoi(SampleRateEnvVar);
    sampleSize= RAND_MAX >> sampleRateLog;
    sampleMask = sampleSize ^ RAND_MAX;
  }

  if((MaxWindowSizeEnvVar = getenv("MAX_WINDOW_SIZE")) != NULL){
    maxWindowSize = atoi(MaxWindowSizeEnvVar);
  }

  if((MaxFreqLevelEnvVar = getenv("MAX_FREQ_LEVEL")) != NULL){
    maxFreqLevel = atoi(MaxFreqLevelEnvVar);
  }

}



/* llvm_start_basic_block_tracing - This is the main entry point of the basic
 * block tracing library.  It is responsible for setting up the atexit
 * handler and allocating the trace buffer.
 */
extern "C" int start_call_site_tracing(func_t _totalFuncs) {

  save_affinity_environment_variables();  
  totalFuncs = _totalFuncs;
  initialize_affinity_data();
  /* Set up the atexit handler. */
  atexit (affinityAtExitHandler);

  return 1;
}

/*
   int main(){
   start_call_site_tracing(10);
   FILE * inputTraceFile=fopen("input.txt","r");
   func_t func;
   while(fscanf(inputTraceFile,"%hd",&func)!=EOF){
   record_function_exec(func);
   }
   return 0;
   }*/
