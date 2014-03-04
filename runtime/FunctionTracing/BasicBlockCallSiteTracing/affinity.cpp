#include "affinity.hpp"
#include <stdio.h>
#include <algorithm>
#include <pthread.h>
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
short maxFreqLevel;

JointFreqMap joint_freqs;
JointFreqRangeMap joint_freq_ranges;

SingleFreqRangeMap single_freq_ranges; 
SingleFreqMap single_freqs;

std::unordered_map<const Record,bool,RecordHash> contains_func;
std::unordered_map<const Record,list<Record>::iterator,RecordHash> rec_trace_it;
std::unordered_map<const Record,list<SampledWindow>::iterator,RecordHash> rec_window_it;

list<SampledWindow> trace_list;

wsize_t trace_list_size;

list<SampledWindow>::iterator window_iter;
list<SampledWindow>::iterator top_window_iter;
list<Record>::iterator func_iter;
list<Record>::iterator partial_trace_list_end;
list<Record> * last_window_trace_list;



FILE * graphFile, * debugFile;

uint32_t * null_joint_freq = new uint32_t[maxWindowSize+1]();
const char * version_str=".bbabc";

uint32_t * GetNewArray(){
  return new uint32_t [maxWindowSize+1]();
}

uint32_t ** GetNewMatrix(){
  uint32_t ** ret = new uint32_t *[maxWindowSize+1]();
  for(wsize_t i=0;i<=maxWindowSize; ++i)
    ret[i]=new uint32_t [maxWindowSize+1]();
  return ret;
}


void create_single_freqs(){
   SingleFreqRangeMap::iterator it_end= single_freq_ranges.end();
  for(SingleFreqRangeMap::iterator it=single_freq_ranges.begin(); it!=it_end; ++it){
    Record rec = it->first;
    uint32_t ** freq_range_matrix = it->second;
    uint32_t * freq_array =single_freqs.emplace(rec,GetNewArray()).first->second;
  
    /*
    if(result== single_freqs.end())
      single_freqs[rec]= freq_array=new uint32_t[maxWindowSize+1]();
    else
      freq_array=result->second;
      */

    for(wsize_t i=1; i<= maxWindowSize; ++i)
      for(wsize_t j=i; j<= maxWindowSize; ++j)
        for(wsize_t k=i; k<=j; ++k)
          freq_array[k]+=freq_range_matrix[i][j];
  }
}

void create_joint_freqs(){
  JointFreqRangeMap::iterator it_end= joint_freq_ranges.end();
  for(JointFreqRangeMap::iterator it=joint_freq_ranges.begin(); it!=it_end; ++it){	
    RecordPair rec_pair = it->first;
    uint32_t ** freq_range_matrix = it->second;
    uint32_t * freq_array = joint_freqs.emplace(rec_pair,GetNewArray()).first->second;

    for(int i=2; i<=maxWindowSize;++i)
      for(int j=i; j<=maxWindowSize; ++j)
        for(int k=i; k<=j; ++k)
          freq_array[k]+=freq_range_matrix[i][j];
  } 
}
void commit_freq_updates(SampledWindow &sw, wsize_t max_wsize){

  while(!sw.single_update_list.empty()){
    SingleUpdateEntry sue = sw.single_update_list.front();
    sw.single_update_list.pop_front();

    assert(sue.min_wsize <= max_wsize);
    uint32_t ** single_freq_range_matrix = single_freq_ranges.emplace(sue.rec,GetNewMatrix()).first->second;
    single_freq_range_matrix[sue.min_wsize][max_wsize]++;
  }

  while(!sw.joint_update_list.empty()){
    JointUpdateEntry jue = sw.joint_update_list.front();
    sw.joint_update_list.pop_front();

    assert(jue.min_wsize <= max_wsize);

    uint32_t ** joint_freq_range_matrix=joint_freq_ranges.emplace(jue.rec_pair,GetNewMatrix()).first->second;
    /*
    JointFreqRangeMap::iterator result=joint_freq_ranges->find(jue.rec_pair);
    if(result == joint_freq_ranges->end()){
      joint_freq_range_matrix = new uint32_t*[maxWindowSize+1];
      for(int i=1; i<=maxWindowSize; ++i)
        joint_freq_range_matrix[i]=new uint32_t[maxWindowSize+1]();

      (*joint_freq_ranges)[jue.rec_pair]= joint_freq_range_matrix;
    }else
      joint_freq_range_matrix=result->second;
    */

    joint_freq_range_matrix[jue.min_wsize][max_wsize]++;

    /*
    if(DEBUG>2){
      fprintf(debugFile,"&&&&&&&&&&&&&&&& commit\n");
      fprintf(debugFile,"(%d,%d)[%d..%d]++\n",jue.func_pair.first, jue.func_pair.second,jue.min_wsize,max_wsize);
    }*/
  }
}

extern "C" void record_function_exec(func_t fid, bb_t bbid){

  Record rec(fid,bbid);
  //fprintf(traceFile,"%hd\n",fid);
  uint32_t r=rand();
  bool sampled=false;
  if((r & sampleMask)==0){
    SampledWindow sw;
    trace_list.push_front(sw);
    sampled=true;
  }


  if(trace_list_size!=0 || sampled)
    trace_list.front().partial_trace_list.push_front(rec);
  else
    return;


  if(DEBUG>0){
    if(sampled)
      fprintf(debugFile,"new window\n");
    print_trace(&trace_list);
  }

  if(!contains_func[rec]){
    trace_list_size++;
    trace_list.front().wsize++;

    if(trace_list_size > maxWindowSize){
      commit_freq_updates(trace_list.back(),trace_list_size-1);
      last_window_trace_list= &(trace_list.back().partial_trace_list);			

      while(!last_window_trace_list->empty()){
        Record oldRec=last_window_trace_list->front();
        contains_func[oldRec]=false;
        last_window_trace_list->pop_front();
      }
      trace_list_size-=trace_list.back().wsize;

      trace_list.pop_back();
    }

    if(trace_list_size>0){
      sequential_update_affinity(trace_list.end());
      contains_func[rec]=true;
      rec_window_it[rec] = trace_list.begin();
      rec_trace_it[rec] = trace_list.begin()->partial_trace_list.begin();
    }

  }else{
    trace_list.front().wsize++;
    rec_window_it[rec]->wsize--;
    wsize_t top_wsize = 0;
    if(trace_list.begin()!=rec_window_it[rec]){
      top_wsize = sequential_update_affinity(rec_window_it[rec]);
    }
    window_iter=rec_window_it[rec];
    window_iter->partial_trace_list.erase(rec_trace_it[rec]);

    if(window_iter->partial_trace_list.empty()){
      commit_freq_updates(*window_iter,top_wsize);
      trace_list.erase(window_iter);
    }

    rec_window_it[rec] = trace_list.begin();
    rec_trace_it[rec] = trace_list.begin()->partial_trace_list.begin();	
  }


}

void print_optimal_layout(){
  vector<Record> layout;
  for(func_t fid=0; fid<totalFuncs; ++fid){
    for(bb_t bbid=0; bbid<bb_count[fid]; ++bbid){
      Record rec(fid,bbid);
      if(disjointSet::sets[rec]){
        disjointSet * thisSet=disjointSet::sets[rec];
        for(deque<Record>::iterator it=disjointSet::sets[rec]->elements.begin(), 
            it_end=disjointSet::sets[rec]->elements.end()
            ; it!=it_end ; ++it){
          //printf("this is *it:%d\n",*it);
          layout.push_back(*it);
          disjointSet::sets[*it]=0;
        }
        thisSet->elements.clear();
        delete thisSet;
      }
    }
  }

  char affinityFilePath[80];
  strcpy(affinityFilePath,"layout");
  //strcat(affinityFilePath,(affEntryCmp==&affEntry1DCmp)?("1D"):("2D"));
  strcat(affinityFilePath,version_str);
  FILE *layoutFile = fopen(affinityFilePath,"w");  

  for(vector<Record>::iterator rec_it=layout.begin(), rec_end=layout.end(); rec_it!=rec_end; ++rec_it){
    fprintf(layoutFile, "(%hu,%hu)\n",rec_it->getFuncId(),rec_it->getBBId());
  }
  fclose(layoutFile);
}

/*
void print_optimal_layouts(){
  Record * layout = new Record[totalFuncs];
  int count=0;
  for(int i=0; i< totalFuncs; ++i){
    //printf("i is now %d\n",i);
    if(disjointSet::sets[i]){
      disjointSet * thisSet=disjointSet::sets[i];
      for(deque<Record>::iterator it=disjointSet::sets[i]->elements.begin(), 
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
  //strcat(affinityFilePath,to_string(maxWindowSize).c_str());
  strcat(affinityFilePath,version_str);

  FILE *affinityFile = fopen(affinityFilePath,"w");  

  for(Record i=0;i<totalFuncs;++i){
    if(i%20==0)
      fprintf(affinityFile, "\n");
    fprintf(affinityFile, "%u ",layout[i]);
  }
  fclose(affinityFile);
}*/

/* The data allocation function (totalFuncs need to be set before entering this function) */
void initialize_affinity_data(){
  trace_list_size=0;

  //srand(time(NULL));
  srand(1);

  if(DEBUG > 9)
    traceFile = fopen("trace.txt","w");

  if(DEBUG > 0)
    debugFile = fopen("debug.txt","w");
}

void aggregate_affinity(){
  JointFreqMap::iterator jiter;
  SingleFreqMap::iterator siter;


  char * graphFilePath=(char*) malloc(strlen("graph")+strlen(version_str)+1);
  strcpy(graphFilePath,"graph");
  strcat(graphFilePath,version_str);

  
  graphFile=fopen(graphFilePath,"r");
  if(graphFile!=NULL){
    wsize_t file_mwsize;
    size_t sfreq_size,jfreq_size;
    fscanf(graphFile,"MaxWindowSize:%hu\tSingleFreqEntries:%zu\tJointFreqEntries:%zu\n",&file_mwsize,&sfreq_size,&jfreq_size);
    Record r1,r2;
    func_t fid1,fid2;
    bb_t bbid1, bbid2;
    uint32_t sfreq,jfreq;
    for(uint32_t i=0; i<sfreq_size; ++i){
      fscanf(graphFile,"(%hu,%hu):",&fid1,&bbid1);
      uint32_t * freq_array = single_freqs.emplace(Record(fid1,bbid1),GetNewArray()).first->second;
      for(wsize_t wsize=1; wsize<=maxWindowSize; ++wsize){
        fscanf(graphFile,"%u ",&sfreq);
        freq_array[wsize]+=sfreq;
      }
    }

    for(uint32_t i=0; i<jfreq_size; ++i){
      fscanf(graphFile,"[(%hu,%hu),(%hu,%hu)]:",&fid1,&bbid1,&fid2,&bbid2);
      RecordPair rec_pair=RecordPair(Record(fid1,bbid1),Record(fid2,bbid2));
      uint32_t * freq_array = joint_freqs.emplace(rec_pair,GetNewArray()).first->second;
      /*
      uint32_t * freq_array=joint_freqs[entryToAdd];
      if(freq_array==NULL){
        freq_array= new uint32_t[maxWindowSize+1]();
        (*joint_freqs)[entryToAdd]=freq_array;
      }
      */
      //printf("(%hd,%hd)\n",u1,u2);
      for(wsize_t wsize=1; wsize<=maxWindowSize; ++wsize){
        fscanf(graphFile,"%u ",&jfreq);
        freq_array[wsize] +=jfreq;
      }
    }
    fclose(graphFile);
  }


  graphFile=fopen(graphFilePath,"w+");
  fprintf(graphFile,"MaxWindowSize:%hu\tSingleFreqEntries:%zu\tJointFreqEntries:%zu\n",maxWindowSize,single_freqs.size(),joint_freqs.size());

  for(siter=single_freqs.begin(); siter!=single_freqs.end(); ++siter){
    fprintf(graphFile,"(%hu,%hu):",siter->first.getFuncId(),siter->first.getBBId());
    for(wsize_t wsize=1; wsize<=maxWindowSize;++wsize)
      fprintf(graphFile,"%u ",siter->second[wsize]);
    fprintf(graphFile,"\n");

  }

  for(jiter=joint_freqs.begin(); jiter!=joint_freqs.end(); ++jiter){
    fprintf(graphFile,"[(%hu,%hu),(%hu,%hu)]:",jiter->first.first.getFuncId(),jiter->first.first.getBBId(),jiter->first.second.getFuncId(),jiter->first.second.getBBId());
    for(wsize_t wsize=1;wsize<=maxWindowSize;++wsize)
      fprintf(graphFile,"%u ",jiter->second[wsize]);
    fprintf(graphFile,"\n");
  }


  fclose(graphFile);
}

/* This function builds the affinity groups based on the affinity table 
   and prints out the results */
void find_affinity_groups(){

  vector<RecordPair> all_affEntry_iters;
  for(JointFreqMap::iterator iter=joint_freqs.begin(); iter!=joint_freqs.end(); ++iter){
    if(iter->first.first < iter->first.second)
      all_affEntry_iters.push_back(iter->first);
  }
  comparisonFile = fopen("compare.txt","w");
  sort(all_affEntry_iters.begin(),all_affEntry_iters.end(),affEntryCmp);
  fclose(comparisonFile);
  comparisonFile=NULL;

  orderFile = fopen("order.txt","w");


  for(func_t fid=0; fid<totalFuncs; ++fid)
    for(bb_t bbid=0; bbid<bb_count[fid]; ++bbid)
      disjointSet::init_new_set(Record(fid,bbid));

  for(vector<RecordPair>::iterator iter=all_affEntry_iters.begin(); iter!=all_affEntry_iters.end(); ++iter){
    fprintf(orderFile,"[(%hu,%hu),(%hu,%hu)]\n",iter->first.getFuncId(),iter->first.getBBId(),iter->second.getFuncId(),iter->second.getBBId());
    disjointSet::mergeSets(iter->first, iter->second);
  } 

  fclose(orderFile);

}

/* Must be called at exit*/
void affinityAtExitHandler(){
  if(DEBUG>9)
    fclose(traceFile);
  wsize_t top_wsize=0;
  while(!trace_list.empty()){
    SampledWindow sw = trace_list.front();
    top_wsize += sw.wsize;
    commit_freq_updates(sw,top_wsize);
    trace_list.pop_front();
  }
  if(DEBUG>0)
    fclose(debugFile);
  create_joint_freqs();
  create_single_freqs();
  aggregate_affinity();

  affEntryCmp=&affEntry2DCmp;
  find_affinity_groups();
  print_optimal_layout();

}


void print_trace(list<SampledWindow> * tlist){
  list<SampledWindow>::iterator window_iter=tlist->begin();

  list<Record>::iterator trace_iter;

  fprintf(debugFile,"---------------------------------------------\n");
  while(window_iter!=tlist->end()){
    trace_iter =  window_iter->partial_trace_list.begin();
    fprintf(debugFile,"size: %d\n",window_iter->wsize);


    while(trace_iter!=window_iter->partial_trace_list.end()){
      fprintf(debugFile,"(%hu,%hu) ",trace_iter->getFuncId(),trace_iter->getBBId());
      trace_iter++;
    }
    fprintf(debugFile,"\n");
    window_iter++;
  }
}




wsize_t sequential_update_affinity(list<SampledWindow>::iterator grown_list_end){

  wsize_t top_wsize=0;
  wsize_t wsize;
  top_window_iter = trace_list.begin();
  func_iter = top_window_iter->partial_trace_list.begin();

  Record rec= * func_iter;

  while(top_window_iter!= grown_list_end){


    func_iter = top_window_iter->partial_trace_list.begin();

    partial_trace_list_end = top_window_iter->partial_trace_list.end();
    while(func_iter != partial_trace_list_end){

      Record oldRec= * func_iter;
      if(oldRec!=rec){
        window_iter = top_window_iter;
        wsize=top_wsize;

        while(window_iter != grown_list_end){
          wsize+=window_iter->wsize;
          JointUpdateEntry jue(unordered_RecordPair(rec,oldRec),wsize);
          window_iter->add_joint_update_entry(jue);

          if(DEBUG>1){
            fprintf(debugFile,"****************\n");
            fprintf(debugFile,"update pair: (%d,%d),(%d,%d)[%d..]++\n",oldRec.getFuncId(),oldRec.getBBId(),rec.getFuncId(),rec.getBBId(),wsize);
          }            
          window_iter++;
        }
      }

      func_iter++;
    }

    top_wsize += top_window_iter->wsize;
    SingleUpdateEntry sue(rec,top_wsize);
    top_window_iter->add_single_update_entry(sue);

    if(DEBUG>1){
      fprintf(debugFile,"################\n");
      fprintf(debugFile,"update single: (%d,%d)[%d..]++\n",rec.getFuncId(),rec.getBBId(),top_wsize);
    }

    top_window_iter++;
  }

  return top_wsize;

} 



/*
   bool affEntry1DCmp(const RecordPair &left_pair,const RecordPair &right_pair){

   int * jointFreq_left = (*joint_freqs)[left_pair];
   int * jointFreq_right = (*joint_freqs)[right_pair];
   if(jointFreq_left == NULL && jointFreq_right != NULL)
   return false;
   if(jointFreq_left != NULL && jointFreq_right == NULL)
   return true;

   if(jointFreq_left != NULL){
   int left_pair_val, right_pair_val;

   float rel_freq_threshold=2.0;
   for(Record wsize=2;wsize<=maxWindowSize;++wsize){

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

uint32_t * GetWithDef(JointFreqMap  &m, const RecordPair &key, uint32_t * defval) {
  JointFreqMap::const_iterator it = m.find( key );
  if ( it == m.end() ) {
    return defval;
  }
  else {
    return it->second;
  }
}

bool affEntry2DCmp(const RecordPair &left_pair, const RecordPair &right_pair){
  uint32_t * jointFreq_left = GetWithDef(joint_freqs, left_pair, null_joint_freq);
  uint32_t * jointFreq_right = GetWithDef(joint_freqs, right_pair, null_joint_freq);

  int left_pair_val, right_pair_val;

  short freqlevel;
  float rel_freq_threshold;
  for(freqlevel=0, rel_freq_threshold=1.0; freqlevel<maxFreqLevel; ++freqlevel, rel_freq_threshold+=5.0/maxFreqLevel){
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
    freqlevel++;
    rel_freq_threshold+=5.0/maxFreqLevel;
  }

  if(left_pair.first != right_pair.first)
    return (right_pair.first < left_pair.first);

  return right_pair.second < left_pair.second;

}




void disjointSet::mergeSets(disjointSet * set1, disjointSet* set2){

  disjointSet * merger = (set1->size()>=set2->size())?(set1):(set2);

  disjointSet * mergee = (set1->size()<set2->size())?(set1):(set2);


  RecordPair frontMerger_backMergee(merger->elements.front(), mergee->elements.back());
  RecordPair backMerger_backMergee(merger->elements.back(), mergee->elements.back());
  RecordPair backMerger_frontMergee(merger->elements.back(), mergee->elements.front());
  RecordPair frontMerger_frontMergee(merger->elements.front(), mergee->elements.front());
  RecordPair conAffEntriesArray[4]={frontMerger_frontMergee, frontMerger_backMergee, backMerger_frontMergee, backMerger_backMergee};
  vector<RecordPair> conAffEntries(conAffEntriesArray,conAffEntriesArray+4);
  sort(conAffEntries.begin(), conAffEntries.end(), affEntryCmp);

  assert(affEntryCmp(conAffEntries[0],conAffEntries[1]) || (conAffEntries[0]==conAffEntries[1]));
  assert(affEntryCmp(conAffEntries[1],conAffEntries[2]) || (conAffEntries[1]==conAffEntries[2]));
  assert(affEntryCmp(conAffEntries[2],conAffEntries[3]) || (conAffEntries[2]==conAffEntries[3]));

  bool con_mergee_front = (conAffEntries[0] == backMerger_frontMergee) || (conAffEntries[0] == frontMerger_frontMergee);
  bool con_merger_front = (conAffEntries[0] == frontMerger_frontMergee) || (conAffEntries[0] == frontMerger_backMergee);

  if(con_mergee_front){

    for(deque<Record>::iterator it=mergee->elements.begin(); it!=mergee->elements.end(); ++it){
      if(con_merger_front)
        merger->elements.push_front(*it);
      else
        merger->elements.push_back(*it);
      disjointSet::sets[*it]=merger;
    }
  }else{
    for(deque<Record>::reverse_iterator rit=mergee->elements.rbegin(); rit!=mergee->elements.rend(); ++rit){
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
extern "C" int start_basic_block_call_site_tracing(func_t _totalFuncs) {

  save_affinity_environment_variables();  
  totalFuncs = _totalFuncs;
  bb_count = new bb_t[totalFuncs];
  initialize_affinity_data();
  /* Set up the atexit handler. */
  atexit (affinityAtExitHandler);

  return 1;
}

extern "C" void set_bb_count_for_fid(func_t fid, bb_t bbid){
  bb_count[fid]=bbid;
}

/*
   int main(){
   start_call_site_tracing(10);
   FILE * inputTraceFile=fopen("input.txt","r");
   Record func;
   while(fscanf(inputTraceFile,"%hd",&func)!=EOF){
   record_function_exec(func);
   }
   return 0;
   }*/
