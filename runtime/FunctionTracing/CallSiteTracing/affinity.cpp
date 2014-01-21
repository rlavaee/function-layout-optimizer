#include "affinity.hpp"
#include <stdio.h>
#include <algorithm>
#include <pthread.h>
#include <semaphore.h>
#include <boost/lockfree/queue.hpp>
#include <string.h>
#include <vector>

short maxWindowSize;
int memoryLimit;
int sampleRate;
int sampleSize;
int sampleMask;
short maxFreqLevel;

JointFreqMap * joint_freqs;
JointFreqRangeMap * joint_freq_ranges;
list<UpdateEntry> * joint_freq_update_lists;
list<SampledWindow> trace_list;

bool * contains_func;
list<short>::iterator * func_trace_it;
list<SampledWindow>::iterator * func_window_it;
int trace_list_size;

list<SampledWindow>::iterator tl_window_iter;
list<short>::iterator tl_trace_iter;
list<short> * last_window_trace_list;

int ** single_freqs;

short prevFunc;
FILE * graphFile, * debugFile;

const char * version_str=".abc";

sem_t affinity_sem;

void create_joint_freqs(){
  JointFreqRangeMap::iterator it_end= joint_freq_ranges->end();
  for(JointFreqRangeMap::iterator it=joint_freq_ranges->begin(); it!=it_end; ++it){	
    shortpair update_pair = it->first;
    int ** freq_range_matrix = it->second;
    int * freq_array;
    JointFreqMap::iterator result = joint_freqs->find(update_pair);
    if(result== joint_freqs->end())
      (*joint_freqs)[update_pair]= freq_array=new int[maxWindowSize+1]();
    else
      freq_array=result->second;

    for(int i=2; i<=maxWindowSize;++i)
      for(int j=i; j<=maxWindowSize; ++j)
        for(int k=i; k<=j; ++k)
          freq_array[k]+=freq_range_matrix[i][j];
  } 
}
void commit_joint_freq_updates(short func,int max_wsize){
  list<UpdateEntry>::iterator update_entry_it = joint_freq_update_lists[func].begin();
  for(;update_entry_it != joint_freq_update_lists[func].end(); ++update_entry_it){
    shortpair update_pair(func,update_entry_it->func);
    int ** freq_range_matrix;
    JointFreqRangeMap::iterator result=joint_freq_ranges->find(update_pair);
    if(result == joint_freq_ranges->end()){
      freq_range_matrix = new int*[maxWindowSize+1];
      for(int i=2; i<=maxWindowSize; ++i)
        freq_range_matrix[i]=new int[maxWindowSize+1]();
      (*joint_freq_ranges)[update_pair]= freq_range_matrix;
    }else
      freq_range_matrix=result->second;

    freq_range_matrix[update_entry_it->min_wsize][max_wsize]++;
  }
}

extern "C" void record_function_exec(short FuncNum){
  if(prevFunc==FuncNum)
    return;
  else
    prevFunc=FuncNum;

  int r=rand();
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

  print_trace(&trace_list);


  if(!contains_func[FuncNum]){
    trace_list_size++;

    if(trace_list_size > maxWindowSize){
      if(trace_list.size()==1)
        trace_list.front().partial_trace_list.pop_front();

      last_window_trace_list= &(trace_list.back().partial_trace_list);			

      while(!last_window_trace_list->empty()){
        short oldFuncNum=last_window_trace_list->front();
        commit_joint_freq_updates(oldFuncNum,trace_list_size-1);
        contains_func[oldFuncNum]=false;
        last_window_trace_list->pop_front();
      }
      trace_list_size-=trace_list.back().wsize;

      trace_list.pop_back();
    }

    if(trace_list_size>0){
      trace_list.front().wsize++;
      sequential_update_affinity(trace_list.end());
      contains_func[FuncNum]=true;
      func_window_it[FuncNum] = trace_list.begin();
      func_trace_it[FuncNum] = trace_list.begin()->partial_trace_list.begin();
    }

  }else{
    if(trace_list.begin()!=func_window_it[FuncNum]){

      int top_wsize = 0;
      tl_window_iter = trace_list.begin();
      do{
        top_wsize+=tl_window_iter->wsize;
      }while(tl_window_iter!=func_window_it[FuncNum]);
      commit_joint_freq_updates(FuncNum,top_wsize);


      tl_window_iter->partial_trace_list.erase(func_trace_it[FuncNum]);

      if(tl_window_iter->partial_trace_list.empty())
        trace_list.erase(tl_window_iter);

      trace_list.front().wsize++;
      func_window_it[FuncNum]->wsize--;

      sequential_update_affinity(func_window_it[FuncNum]);

      func_window_it[FuncNum] = trace_list.begin();
      func_trace_it[FuncNum] = trace_list.begin()->partial_trace_list.begin();	
    }
  }


}

void print_optimal_layout(){
  short * layout = new short[totalFuncs];
  int count=0;
  for(int i=0; i< totalFuncs; ++i){
    //printf("i is now %d\n",i);
    if(disjointSet::sets[i]){
      disjointSet * thisSet=disjointSet::sets[i];
      for(deque<short>::iterator it=disjointSet::sets[i]->elements.begin(), 
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
  strcat(affinityFilePath,(affEntryCmp==&affEntry1DCmp)?("1D"):("2D"));
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
  short * layout = new short[totalFuncs];
  int count=0;
  for(int i=0; i< totalFuncs; ++i){
    //printf("i is now %d\n",i);
    if(disjointSet::sets[i]){
      disjointSet * thisSet=disjointSet::sets[i];
      for(deque<short>::iterator it=disjointSet::sets[i]->elements.begin(), 
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

  for(short i=0;i<totalFuncs;++i){
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
  single_freqs=new int * [totalFuncs];

  for(short i=0;i<totalFuncs;++i)
    single_freqs[i]=new int[maxWindowSize+1]();
  contains_func = new bool [totalFuncs]();
  func_window_it = new list<SampledWindow>::iterator [totalFuncs];
  func_trace_it = new list<short>::iterator  [totalFuncs];

  joint_freq_update_lists = new list<UpdateEntry> [totalFuncs];
}

void aggregate_affinity(){
  JointFreqMap::iterator iter;


  char * graphFilePath=(char*) malloc(strlen("graph")+strlen(version_str)+1);
  strcpy(graphFilePath,"graph");
  strcat(graphFilePath,version_str);

  graphFile=fopen(graphFilePath,"r");
  //int prev_sampledWindows=0;
  if(graphFile!=NULL){
    short u1,u2,wsize;
    int freq;
    //fscanf(graphFile,"%d",&prev_sampledWindows);
    while(fscanf(graphFile,"%hd",&wsize)!=EOF){
      while(true){
        fscanf(graphFile,"%hd",&u1);
        if(u1==-1)
          break;
        fscanf(graphFile,"%d",&freq);
        //fprintf(stderr,"%d %d is %d\n",wsize,u1,freq);
        //prev_final_freqs[wsize][u1]=freq;
        single_freqs[u1][wsize]+=freq;
      }
      while(true){
        fscanf(graphFile,"%hd",&u1);
        //fprintf(stderr,"u1 is %d\n",u1);
        if(u1==-1)
          break;
        fscanf(graphFile,"%hd %d",&u2,&freq);
        //fprintf(stderr,"%d %d %d\n",u1,u2,freq);
        shortpair entryToAdd(u1,u2);
        //prev_final_affEntries[wsize][entryToAdd]=freq;
        int * freq_array=(*joint_freqs)[entryToAdd];
        if(freq_array==NULL){
          freq_array= new int[maxWindowSize+1]();
          (*joint_freqs)[entryToAdd]=freq_array;
        }

        freq_array[wsize]+=freq;
      }
    }
    fclose(graphFile);
  }


  graphFile=fopen(graphFilePath,"w+");
  //sampledWindows+=prev_sampledWindows;
  //fprintf(graphFile,"%ld\n",sampledWindows);

  for(short wsize=1;wsize<=maxWindowSize;++wsize){

    fprintf(graphFile,"%hd\n",wsize);
    for(short i=0;i<totalFuncs;++i)
      fprintf(graphFile,"%hd %d\n",i,single_freqs[i][wsize]);
    fprintf(graphFile,"-1\n");

    for(iter=joint_freqs->begin(); iter!=joint_freqs->end(); ++iter){
      fprintf(graphFile,"%hd %hd %d\n",iter->first.first,
          iter->first.second,iter->second[wsize]);
    }

    fprintf(graphFile,"-1\n");
  }

  fclose(graphFile);


}

/* This function builds the affinity groups based on the affinity table 
   and prints out the results */
void find_affinity_groups(){

  vector<shortpair> all_affEntry_iters;
  for(JointFreqMap::iterator iter=joint_freqs->begin(); iter!=joint_freqs->end(); ++iter){
    if(iter->first.first < iter->first.second)
      all_affEntry_iters.push_back(iter->first);
  }
  sort(all_affEntry_iters.begin(),all_affEntry_iters.end(),affEntryCmp);


  disjointSet::sets = new disjointSet *[totalFuncs];
  for(short i=0; i<totalFuncs; ++i)
    disjointSet::init_new_set(i);

  for(vector<shortpair>::iterator iter=all_affEntry_iters.begin(); iter!=all_affEntry_iters.end(); ++iter){
    disjointSet::mergeSets(iter->first, iter->second);
  } 

}

/* Must be called at exit*/
void affinityAtExitHandler(){
  create_joint_freqs();
  aggregate_affinity();

  affEntryCmp=&affEntry2DCmp;
  find_affinity_groups();
  print_optimal_layout();

}


void print_trace(list<SampledWindow> * tlist){
  list<SampledWindow>::iterator window_iter=tlist->begin();

  list<short>::iterator trace_iter;

  printf("trace list:\n");
  while(window_iter!=tlist->end()){
    trace_iter =  window_iter->partial_trace_list.begin();
    printf("windows: %d\n",window_iter->wcount);
    printf("size: %d\n",window_iter->wsize);


    while(trace_iter!=window_iter->partial_trace_list.end()){
      printf("%d ",*trace_iter);
      trace_iter++;
    }
    printf("\n");
    window_iter++;
  }
  printf("---------------------------------------------\n");
}



list<SampledWindow>::iterator window_iter;
list<short>::iterator func_iter;
list<short>::iterator partial_trace_list_end;

void sequential_update_affinity(list<SampledWindow>::iterator grown_list_end){

  unsigned top_wsize=0;
  window_iter = trace_list.begin();
  func_iter = window_iter->partial_trace_list.begin();

  short FuncNum= * func_iter;

  while(window_iter!= grown_list_end){
    top_wsize += window_iter->wsize;
    func_iter = window_iter->partial_trace_list.begin();

    partial_trace_list_end = window_iter->partial_trace_list.end();
    while(func_iter != partial_trace_list_end){

      short oldFuncNum= * func_iter;
      single_freqs[oldFuncNum][top_wsize]++;

      if(oldFuncNum!=FuncNum){
        UpdateEntry update_entry(FuncNum, top_wsize);
        joint_freq_update_lists[oldFuncNum].push_back(update_entry);
      }

      func_iter++;
    }

    window_iter++;

  } 


}


bool affEntry1DCmp(const shortpair &left_pair,const shortpair &right_pair){

  int * jointFreq_left = (*joint_freqs)[left_pair];
  int * jointFreq_right = (*joint_freqs)[right_pair];
  if(jointFreq_left == NULL && jointFreq_right != NULL)
    return false;
  if(jointFreq_left != NULL && jointFreq_right == NULL)
    return true;

  if(jointFreq_left != NULL){
    int left_pair_val, right_pair_val;

    float rel_freq_threshold=2.0;
    for(short wsize=2;wsize<=maxWindowSize;++wsize){

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


int * GetWithDef(JointFreqMap * m, const shortpair &key, int * defval) {
  JointFreqMap::const_iterator it = m->find( key );
  if ( it == m->end() ) {
    return defval;
  }
  else {
    return it->second;
  }
}
bool affEntry2DCmp(const shortpair &left_pair, const shortpair &right_pair){

  shortpair left_pair_rev = shortpair(left_pair.second,left_pair.first);
  shortpair right_pair_rev = shortpair(right_pair.second,right_pair.first);

  int * jointFreq_left = GetWithDef(joint_freqs, left_pair, NULL);
  int * jointFreq_right = GetWithDef(joint_freqs, right_pair, NULL);

  int * jointFreq_left_rev = GetWithDef(joint_freqs, left_pair_rev, NULL);
  int * jointFreq_right_rev = GetWithDef(joint_freqs, right_pair_rev, NULL);

  if((jointFreq_left == NULL && jointFreq_left_rev == NULL) && (jointFreq_right != NULL || jointFreq_right_rev != NULL))
    return false;
  if((jointFreq_left != NULL || jointFreq_left_rev != NULL) && (jointFreq_right == NULL && jointFreq_right_rev == NULL))
    return true;

  if(jointFreq_left != NULL || jointFreq_left_rev != NULL){
    int left_pair_val, right_pair_val;

    short freqlevel;
    float rel_freq_threshold;
    for(freqlevel=0, rel_freq_threshold=1.0; freqlevel<maxFreqLevel; ++freqlevel, rel_freq_threshold+=5.0/maxFreqLevel){
      for(short wsize=2;wsize<=maxWindowSize;++wsize){

        int jointFreq_left_wsize = (jointFreq_left==NULL)?(0):(jointFreq_left[wsize]);
        int jointFreq_left_rev_wsize = (jointFreq_left_rev==NULL)?(0):(jointFreq_left_rev[wsize]);

        if((rel_freq_threshold*(jointFreq_left_wsize) >= single_freqs[left_pair.first][wsize]) ||
            (rel_freq_threshold*(jointFreq_left_rev_wsize) >= single_freqs[left_pair_rev.first][wsize]))
          left_pair_val = 1;
        else
          left_pair_val = -1;

        int jointFreq_right_wsize = (jointFreq_right==NULL)?(0):(jointFreq_right[wsize]);
        int jointFreq_right_rev_wsize = (jointFreq_right_rev==NULL)?(0):(jointFreq_right_rev[wsize]);

        if((rel_freq_threshold*(jointFreq_right_wsize) >= single_freqs[right_pair.first][wsize]) ||
            (rel_freq_threshold*(jointFreq_right_rev_wsize) >= single_freqs[right_pair_rev.first][wsize]))
          right_pair_val = 1;
        else
          right_pair_val = -1;

        if(left_pair_val != right_pair_val)
          return (left_pair_val > right_pair_val);
      }
      freqlevel++;
      rel_freq_threshold+=5.0/maxFreqLevel;
    }
  }

  if(left_pair.first != right_pair.first)
    return (left_pair.first < right_pair.first);

  return left_pair.second < right_pair.second;

}


void disjointSet::mergeSets(disjointSet * set1, disjointSet* set2){

  disjointSet * merger = (set1->size()>=set2->size())?(set1):(set2);

  disjointSet * mergee = (set1->size()<set2->size())?(set1):(set2);


  shortpair frontMerger_backMergee(merger->elements.front(), mergee->elements.back());
  shortpair backMerger_backMergee(merger->elements.back(), mergee->elements.back());
  shortpair backMerger_frontMergee(merger->elements.back(), mergee->elements.front());
  shortpair frontMerger_frontMergee(merger->elements.front(), mergee->elements.front());
  shortpair conAffEntriesArray[4]={frontMerger_frontMergee, frontMerger_backMergee, backMerger_frontMergee, backMerger_backMergee};
  vector<shortpair> conAffEntries(conAffEntriesArray,conAffEntriesArray+4);
  sort(conAffEntries.begin(), conAffEntries.end(), affEntryCmp);

  assert(affEntryCmp(conAffEntries[0],conAffEntries[1]) || (conAffEntries[0]==conAffEntries[1]));
  assert(affEntryCmp(conAffEntries[1],conAffEntries[2]) || (conAffEntries[1]==conAffEntries[2]));
  assert(affEntryCmp(conAffEntries[2],conAffEntries[3]) || (conAffEntries[2]==conAffEntries[3]));

  bool con_mergee_front = (conAffEntries[0] == backMerger_frontMergee) || (conAffEntries[0] == frontMerger_frontMergee);
  bool con_merger_front = (conAffEntries[0] == frontMerger_frontMergee) || (conAffEntries[0] == frontMerger_backMergee);

  if(con_mergee_front){

    for(deque<short>::iterator it=mergee->elements.begin(); it!=mergee->elements.end(); ++it){
      if(con_merger_front)
        merger->elements.push_front(*it);
      else
        merger->elements.push_back(*it);
      disjointSet::sets[*it]=merger;
    }
  }else{
    for(deque<short>::reverse_iterator rit=mergee->elements.rbegin(); rit!=mergee->elements.rend(); ++rit){
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
  const char *SampleRateEnvVar, *MaxWindowSizeEnvVar, *MaxFreqLevelEnvVar, *MemoryLimitEnvVar;

  if ((MemoryLimitEnvVar = getenv("MEMORY_LIMIT")) != NULL) {
    memoryLimit = atoi(MemoryLimitEnvVar);
  }

  if ((SampleRateEnvVar = getenv("SAMPLE_RATE")) != NULL) {
    sampleRate = atoi(SampleRateEnvVar);
    sampleSize= RAND_MAX >> sampleRate;
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
extern "C" int start_call_site_tracing(short _totalFuncs) {

  save_affinity_environment_variables();  
  totalFuncs = _totalFuncs;
  initialize_affinity_data();
  /* Set up the atexit handler. */
  atexit (affinityAtExitHandler);

  return 1;
}

