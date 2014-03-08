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
int sampleRateLog;
uint32_t sampleSize;
uint32_t sampleMask;
short maxFreqLevel;

JointFreqMap joint_freqs;
FallThroughMap * fall_through_counts;

//TODO Consider adding single freqs later on
SingleFreqMap single_freqs;

std::unordered_map<const Record,bool,RecordHash> contains_func;
std::unordered_map<const Record,list<Record>::iterator,RecordHash> rec_trace_it;
std::unordered_map<const Record,list<SampledWindow>::iterator,RecordHash> rec_window_it;

bb_t * last_bbs;

list<SampledWindow> trace_list;

wsize_t trace_list_size;

list<SampledWindow>::iterator window_iter,window_iter_prev, grown_list_iter,top_window_iter;
list<Record>::iterator func_iter, partial_trace_list_end;
list<Record> * last_window_trace_list;

set<Record>::iterator owner_iter, owner_iter_end;


FILE * graphFile, * debugFile;

uint32_t * null_joint_freq = new uint32_t[maxWindowSize]();


uint32_t * emplace(JointFreqMap &jfm, const RecordPair &rec_pair){
	JointFreqMap::iterator result = jfm.find(rec_pair);
	if(result == jfm.end())
		return (jfm[rec_pair]= new uint32_t[maxWindowSize]());
	else
		return result->second;
}


uint32_t * emplace(SingleFreqMap &sfm, const Record &rec){
	SingleFreqMap::iterator result = sfm.find(rec);
	if(result == sfm.end())
		return (sfm[rec]= new uint32_t[maxWindowSize]());
	else
		return result->second;
}

extern "C" void record_function_exec(func_t fid, bb_t bbid){

  Record rec(fid,bbid);
  //fprintf(traceFile,"%hd\n",fid);
  uint32_t r=rand();
  bool sampled=false;
  if((r & sampleMask)==0){
    SampledWindow sw;
		sw.owners.insert(rec);
    trace_list.push_front(sw);
    sampled=true;
  }

	if(last_bbs[fid]!=bbid){
		fall_through_counts[fid][bb_pair_t(last_bbs[fid],bbid)]++
		last_bbs[fid]=bbid;
	}

  if(trace_list_size!=0 || sampled){
    trace_list.front().push_front(rec); //partial_trace_list.push_front(rec);
  }else
    return;


  if(DEBUG>0){
    if(sampled)
      fprintf(debugFile,"new window\n");
    print_trace(&trace_list);
  }

  if(!contains_func[rec]){
    trace_list_size++;

    if(trace_list_size > maxWindowSize){
      if(DEBUG>0){
        fprintf(debugFile,"trace list overflowed: %d\n",trace_list_size);
        print_trace(&trace_list);
      }

      list<Record> * last_window_trace_list= &trace_list.back().partial_trace_list;

      while(!last_window_trace_list->empty()){
        Record oldRec=last_window_trace_list->front();
        contains_func[oldRec]=false;
        last_window_trace_list->pop_front();
      }
      trace_list_size-=trace_list.back().size();
      trace_list.pop_back();
    }

    if(trace_list_size>0){
      sequential_update_affinity(rec,trace_list.end(),true);
      contains_func[rec]=true;
      rec_window_it[rec] = trace_list.begin();
      rec_trace_it[rec] = trace_list.begin()->partial_trace_list.begin();
    }

  }else{
    sequential_update_affinity(rec,rec_window_it[rec],false);
    window_iter = rec_window_it[rec];
    window_iter->erase(rec_trace_it[rec]);

    if(window_iter->partial_trace_list.empty()){
      list<SampledWindow>::iterator window_iter_prev = window_iter;
      window_iter_prev--;
      for(set<Record>::iterator it=window_iter->owners.begin(); it!=window_iter->owners.end(); ++it)
        window_iter_prev->owners.insert(*it);
      window_iter->owners.clear();
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

  FILE *layoutFile = fopen(get_versioned_filename("layout"),"w");  

  for(vector<Record>::iterator rec_it=layout.begin(), rec_end=layout.end(); rec_it!=rec_end; ++rec_it){
    fprintf(layoutFile, "(%hu,%hu)\n",rec_it->getFuncId(),rec_it->getBBId());
  }
  fclose(layoutFile);
}


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

	for(jiter=joint_freqs.begin(); jiter!=joint_freqs.end(); ++jiter){
    uint32_t * freq_array= jiter->second;	  
    for(wsize_t wsize=1;wsize<maxWindowSize;++wsize){
      freq_array[wsize]+=freq_array[wsize-1];
    }
  }

	for(siter=single_freqs.begin(); siter!=single_freqs.end(); ++siter){
    uint32_t * freq_array= siter->second;	  
    for(wsize_t wsize=1;wsize<maxWindowSize;++wsize){
      freq_array[wsize]+=freq_array[wsize-1];
    }
  }



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
      uint32_t * freq_array = emplace(single_freqs,Record(fid1,bbid1));
      for(wsize_t wsize=0; wsize<maxWindowSize; ++wsize){
        fscanf(graphFile,"%u ",&sfreq);
        freq_array[wsize]+=sfreq;
      }
    }

    for(uint32_t i=0; i<jfreq_size; ++i){
      fscanf(graphFile,"[(%hu,%hu),(%hu,%hu)]:",&fid1,&bbid1,&fid2,&bbid2);
      RecordPair rec_pair=RecordPair(Record(fid1,bbid1),Record(fid2,bbid2));
      uint32_t * freq_array = emplace(joint_freqs,rec_pair);
      for(wsize_t wsize=0; wsize<maxWindowSize; ++wsize){
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
    for(wsize_t wsize=0; wsize<maxWindowSize;++wsize)
      fprintf(graphFile,"%u ",siter->second[wsize]);
    fprintf(graphFile,"\n");

  }

  for(jiter=joint_freqs.begin(); jiter!=joint_freqs.end(); ++jiter){
    fprintf(graphFile,"[(%hu,%hu),(%hu,%hu)]:",jiter->first.first.getFuncId(),jiter->first.first.getBBId(),jiter->first.second.getFuncId(),jiter->first.second.getBBId());
    for(wsize_t wsize=0;wsize<maxWindowSize;++wsize)
      fprintf(graphFile,"%u ",jiter->second[wsize]);
    fprintf(graphFile,"\n");
  }


  fclose(graphFile);
}

/* This function builds the affinity groups based on the affinity table 
   and prints out the results */
void find_affinity_groups(){
	
	for(func_t fid=0; fid < totalFuncs; ++fid){
		vector<bb_pair_t> all_bb_pairs;
		for(FallThroughMap::iterator iter=fall_through_counts[fid].begin(); iter!=fall_through_counts[fid].end(); ++iter){
			all_bb_pairs.push_back(iter->first);
		}
	sort(all_bb_pairs.begin(), all_bb_pairs.end(), fall_through_cmp);

  vector<RecordPair> all_affEntry_iters;
  for(JointFreqMap::iterator iter=joint_freqs.begin(); iter!=joint_freqs.end(); ++iter){
    if(iter->first.first < iter->first.second)
      all_affEntry_iters.push_back(iter->first);
  }

/*
  FILE *comparisonFile = fopen(get_versioned_filename("comparison"),"w");  

  sort(all_affEntry_iters.begin(),all_affEntry_iters.end(),jointFreqCountCmp);
  fclose(comparisonFile);
  comparisonFile=NULL;
*/


  FILE *orderFile = fopen(get_versioned_filename("order"),"w");  

  for(func_t fid=0; fid<totalFuncs; ++fid)
    for(bb_t bbid=0; bbid<bb_count[fid]; ++bbid)
      disjointSet::init_new_set(Record(fid,bbid));

  //for(vector<RecordPair>::iterator iter=all_affEntry_iters.begin(); iter!=all_affEntry_iters.end(); ++iter){
	for(vector<bb_pair_t>::iter=all_bb_pairs.begin(); iter!=all_bb_pairs.end(); ++iter){
    fprintf(orderFile,"[(%hu,%hu),(%hu,%hu)]\n",iter->first.getFuncId(),iter->first.getBBId(),iter->second.getFuncId(),iter->second.getBBId());
    disjointSet::mergeSets(*iter);
  } 

  fclose(orderFile);

}

/* Must be called at exit*/
void affinityAtExitHandler(){
  if(DEBUG>0)
    fclose(debugFile);

  aggregate_affinity();
  find_affinity_groups();
  print_optimal_layout();

}


void print_trace(list<SampledWindow> * tlist){
  window_iter=tlist->begin();

  list<Record>::iterator trace_iter;

  fprintf(debugFile,"---------------------------------------------\n");
  while(window_iter!=tlist->end()){
    trace_iter =  window_iter->partial_trace_list.begin();
		fprintf(debugFile,"owners:\t");
		for(set<Record>::iterator owner_it=window_iter->owners.begin(); owner_it!=window_iter->owners.end(); ++owner_it)
			fprintf(debugFile,"(%hu,%hu) ",owner_it->getFuncId(),owner_it->getBBId());
		fprintf(debugFile,"\n");

    while(trace_iter!=window_iter->partial_trace_list.end()){
      fprintf(debugFile,"(%hu,%hu) ",trace_iter->getFuncId(),trace_iter->getBBId());
      trace_iter++;
    }
    fprintf(debugFile,"\n");
    window_iter++;
  }
}


void sequential_update_affinity(const Record &rec, list<SampledWindow>::iterator grown_list_end, bool missed){
  unsigned top_wsize=0;

  grown_list_iter = trace_list.begin();

  while(grown_list_iter!= grown_list_end){
    top_wsize+=grown_list_iter->size();
    owner_iter = grown_list_iter->owners.begin();
    owner_iter_end = grown_list_iter->owners.end();

    while(owner_iter != owner_iter_end){
      Record oldRec= *owner_iter;

      if(oldRec!=rec){
        RecordPair rec_pair(oldRec,rec);
        emplace(joint_freqs,rec_pair)[top_wsize-2] += (missed)?(10):(1);
      }

      owner_iter++;
    }

    grown_list_iter++;

  } 

} 


uint32_t * GetWithDef(JointFreqMap&m, const RecordPair &key, uint32_t * defval) {
  JointFreqMap::const_iterator it = m.find( key );
  if ( it == m.end() ) {
    return defval;
  }
  else {
    return it->second;
  }
}


bool jointFreqSameFunctionsCmp(const RecordPair &left_pair, const RecordPair &right_pair){
  uint32_t * jointFreq_left = GetWithDef(joint_freqs, left_pair, null_joint_freq);
  uint32_t * jointFreq_right = GetWithDef(joint_freqs, right_pair, null_joint_freq);

  for(wsize_t wsize=0; wsize<maxWindowSize; ++wsize){
    uint32_t left_val = jointFreq_left[wsize];
    uint32_t right_val = jointFreq_right[wsize];

    if(left_val > right_val)
      return true;

    if( left_val < right_val)
      return false;
  }

  if(left_pair.first != right_pair.first)
    return (right_pair.first < left_pair.first);

  return right_pair.second < left_pair.second;
}


bool jointFreqCountCmp(const RecordPair &left_pair, const RecordPair &right_pair){
  bool left_pair_same_func = haveSameFunctions(left_pair);
  bool right_pair_same_func = haveSameFunctions(right_pair);

		if (left_pair_same_func && !right_pair_same_func)
			return true;
	
		if (!left_pair_same_func && right_pair_same_func)
			return false;

  RecordPair left_pair_rev(left_pair.second,left_pair.first);
  RecordPair right_pair_rev(right_pair.second,right_pair.first);

  uint32_t * jointFreq_left = GetWithDef(joint_freqs, left_pair, null_joint_freq);
  uint32_t * jointFreq_right = GetWithDef(joint_freqs, right_pair, null_joint_freq);
  uint32_t * jointFreq_left_rev = GetWithDef(joint_freqs, left_pair_rev, null_joint_freq);
  uint32_t * jointFreq_right_rev = GetWithDef(joint_freqs, right_pair_rev, null_joint_freq);



  uint32_t left_mult = (left_pair_same_func)?(2):(1);
  uint32_t right_mult = (right_pair_same_func)?(2):(1);



  for(wsize_t wsize=0; wsize<maxWindowSize; ++wsize){
    uint32_t left_val = jointFreq_left[wsize]+jointFreq_left_rev[wsize];
    uint32_t right_val = jointFreq_right[wsize]+jointFreq_right_rev[wsize];

    if(left_val*left_mult > right_val*right_mult)
      return true;

    if( left_val*left_mult < right_val*right_mult)
      return false;
  }

	if (left_pair_same_func && !right_pair_same_func)
		return true;
	
	if (!left_pair_same_func && right_pair_same_func)
		return false;

/*	
	if(left_pair_same_func && right_pair_same_func){
		bb_t left_bb_diff = abs(left_pair.second.getBBId()-left_pair.second.getBBId());
		bb_t right_bb_diff = abs(right_pair.second.getBBId()-right_pair.second.getBBId());
		if(left_bb_diff < right_bb_diff)
			return true;
		if(left_bb_diff > right_bb_diff)
			return false;
	}
*/

  if(left_pair.first != right_pair.first)
    return (right_pair.first < left_pair.first);

  return right_pair.second < left_pair.second;

}

bool fall_through_cmp (const bb_pair_t &left_pair, const bb_pair_t &right_pair){
	return (fall_through_counts[cur_fid][left_pair] > fall_through_counts[cur_fid][left_pair]);
}

void disjointSet::mergeBasicBlocksSameFunction(func_t fid, const bb_pair_t &bb_pair){
	if(bb_pair.second==0 || bb_pair.first == bb_pair.second)
		return;

	Record left_rec(fid,bb_pair.first);
	Record right_rec(fid,bb_pair.second);

	if(disjointSet::is_connected_to_right(left_rec) || disjointSet::is_connected_to_left(right_rec))
		return;
	else
		mergeSets(left_rec,right_rec);
}

void disjointSet::mergeSets(Record const &left_rec, Record const &right_rec){
	disjointSet * left_set, * right_set;

	left_set = sets[left_rec];
	right_set = sets[right_rec];

	for(deque<Record>::iterator it=right_set->elements.begin(); it!=right_set->elements.end(); ++it){
		left_set->elements.push_front(*it);
		disjointSet::sets[*it]=left_set;
	}

	right_set->elements.clear();
	delete right_set;
}

/*
void disjointSet::mergeSetsDifferentFunctions(const RecordPair &p){

  assert(!haveSameFunctions(p));

  disjointSet * first_set, * second_set;
  first_set = sets[p.first];
  second_set = sets[p.second];

  disjointSet * merger = (first_set->size() >= second_set->size())?(first_set):(second_set);
  disjointSet * mergee = (merger == first_set) ? (second_set) : (first_set);


  RecordPair frontMerger_backMergee(merger->elements.front(), mergee->elements.back());
  RecordPair backMerger_backMergee(merger->elements.back(), mergee->elements.back());
  RecordPair backMerger_frontMergee(merger->elements.back(), mergee->elements.front());
  RecordPair frontMerger_frontMergee(merger->elements.front(), mergee->elements.front());

  RecordPair conAffEntriesArray[4]={frontMerger_frontMergee, frontMerger_backMergee, backMerger_frontMergee, backMerger_backMergee};

  vector<RecordPair> conAffEntries(conAffEntriesArray,conAffEntriesArray+4);

  sort(conAffEntries.begin(), conAffEntries.end(), jointFreqCountCmp);

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
*/

static void save_affinity_environment_variables(void) {
  const char *SampleRateEnvVar, *MaxWindowSizeEnvVar, *MaxFreqLevelEnvVar,  *DebugEnvVar;

  if((DebugEnvVar = getenv("DEBUG")) !=NULL){
    DEBUG = atoi(DebugEnvVar);
  }

  if ((SampleRateEnvVar = getenv("SAMPLE_RATE")) != NULL) {
    sampleRateLog = atoi(SampleRateEnvVar);
    sampleSize= RAND_MAX >> sampleRateLog;
    sampleMask = sampleSize ^ RAND_MAX;
  }

  if((MaxWindowSizeEnvVar = getenv("MAX_WINDOW_SIZE")) != NULL){
    maxWindowSize = atoi(MaxWindowSizeEnvVar)-1;
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
	last_bbs = new bb_t[totalFuncs]();
	fall_through_counts = new FallThroughMap[totalFuncs];
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
