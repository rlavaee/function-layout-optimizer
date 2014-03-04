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
#include <string.h>
#include <vector>

uint32_t * func_count;
func_t * funcs;

func_t * preference;

uint32_t ** stage_affinity;
uint32_t * stage_affinity_sum;
uint32_t * potential_stage_windows;

bool * analysis_switch;
bool * analyzed;

func_t now_analyzed_func;

func_t analysis_set_size;

uint32_t stage_windows;

const uint32_t quantum = 1000;
bool func_counting;
uint32_t stage_time;
uint32_t stage_quantum;


////////affinity data////////////
wsize_t maxWindowSize;
int memoryLimit;
int sampleRateLog;
uint32_t sampleSize;
uint32_t sampleMask;
short maxFreqLevel;

affinityHashMap * affEntries;
list<SampledWindow> trace_list;

bool * contains_func;
list<func_t>::iterator * func_trace_it;
list<SampledWindow>::iterator * func_window_it;
wsize_t trace_list_size;


list<SampledWindow>::iterator tl_window_iter;
list<func_t>::iterator tl_trace_iter;

short prevFunc;
int DEBUG;
FILE * graphFile, * debugFile, * orderFile;

uint32_t * zero_count_array;
affWcounts zero_aff_wcount;

void save_affinity_into_file(const char * affinityFilePath){
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


void read_affinity_from_file(const char * affinityFilePath){
	FILE * affFile=fopen(affinityFilePath,"r");
	if(affFile==NULL)
		return;
	wsize_t mwsize;
	fscanf(affFile,"%hu",&mwsize);
	func_t func1,func2;
	uint32_t potential_windows;
	while(fscanf(affFile,"%hu\t%hu",&func1,&func2)!=EOF){

		fscanf(affFile,"%u",&potential_windows);
		uint32_t * actual_windows=NULL;
		actual_windows=new uint32_t[maxWindowSize+1]();
		for(int i=1;i<=mwsize;++i)
			fscanf(affFile,"%u",&actual_windows[i]);
		funcpair_t fp(func1,func2);
		affinityHashMap::iterator result=affEntries->find(fp);

	  if(result==affEntries->end()){
			(*affEntries)[fp] = affWcounts(potential_windows,actual_windows);
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

void print_optimal_layout(){
	 func_t * layout = new func_t[totalFuncs];
  int count=0;
  for(func_t i=0; i< totalFuncs; ++i){
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

  char * affinityFilePath = (char *) malloc(strlen("layout")+strlen(version_str)+1);
  strcpy(affinityFilePath,"layout");
  strcat(affinityFilePath,version_str);

  FILE *affinityFile = fopen(affinityFilePath,"w");  

  for(func_t i=0;i<totalFuncs;++i){
    if(i%20==0)
      fprintf(affinityFile, "\n");
    fprintf(affinityFile, "%hu ",layout[i]);
  }
  fclose(affinityFile);
}

affWcounts GetWithDef(affinityHashMap * m, const funcpair_t &key, const affWcounts &defval) {
  affinityHashMap::const_iterator it = m->find( key );
  if ( it == m->end() ) {
    return defval;
  }
  else {
    return it->second;
  }
}

void print_affinity (const funcpair_t &p){
	affWcounts wcount = GetWithDef(affEntries, p, zero_aff_wcount);
  affWcounts rev_wcount = GetWithDef(affEntries, funcpair_t(p.second,p.first), zero_aff_wcount);
	fprintf(orderFile,"(%hu,%hu) ---> out of (%u,%u)\t\t",p.first,p.second,wcount.potential_windows,rev_wcount.potential_windows);
	if(rev_wcount.actual_windows == zero_count_array)
		fprintf(orderFile,"zeros\t");
	for(wsize_t wsize=2; wsize <= maxWindowSize; ++wsize)
		fprintf(orderFile,"(%u+%u)\t",wcount.actual_windows[wsize],rev_wcount.actual_windows[wsize]);
	fprintf(orderFile,"\n");
}
int32_t get_affinity(const funcpair_t &p, wsize_t wsize){
	affWcounts wcount = GetWithDef(affEntries, p, zero_aff_wcount);
  affWcounts rev_wcount = GetWithDef(affEntries, funcpair_t(p.second,p.first), zero_aff_wcount);

	return wcount.actual_windows[wsize]+ rev_wcount.actual_windows[wsize];
}

bool affinity_satisfied(const funcpair_t &p, wsize_t wsize, int freqlevel){
	affWcounts wcount = GetWithDef(affEntries, p, zero_aff_wcount);
  affWcounts rev_wcount = GetWithDef(affEntries, funcpair_t(p.second,p.first), zero_aff_wcount);

	return (maxFreqLevel * (wcount.actual_windows[wsize]+ rev_wcount.actual_windows[wsize]) >
																freqlevel  * (wcount.potential_windows + rev_wcount.potential_windows));
}

bool affEntry2DCmp(const funcpair_t &left_pair, const funcpair_t &right_pair){

	affWcounts left_pair_wcount = GetWithDef(affEntries, left_pair, zero_aff_wcount);
  affWcounts left_pair_rev_wcount = GetWithDef(affEntries, funcpair_t(left_pair.second,left_pair.first), zero_aff_wcount);

	affWcounts right_pair_wcount = GetWithDef(affEntries, right_pair, zero_aff_wcount);
  affWcounts right_pair_rev_wcount = GetWithDef(affEntries, funcpair_t(right_pair.second,right_pair.first), zero_aff_wcount);

	for(wsize_t wsize=2; wsize=maxWindowSize; ++wsize){
		uint32_t left_pair_wcount = left_pair_wcount.actual_windows[wsize]+left_pair_rev_wcount.actual_windows[wsize];
		uint32_t right_pair_wcount = right_pair_wcount.actual_windows[wsize]+right_pair_rev_wcount.actual_windows[wsize];

		uint32_t max_wcount 
	}
	

bool affEntry2DCmp(const funcpair_t &left_pair, const funcpair_t &right_pair){

	int freqlevel;
	for(freqlevel = maxFreqLevel-1; freqlevel > 0; --freqlevel){
		left_pair_val = right_pair_val = -1;
	 	for(wsize_t wsize=2;wsize<=maxWindowSize;++wsize){
	 		if(left_pair_val==-1){
      	if( affinity_satisfied(left_pair,wsize,freqlevel))
        	left_pair_val = 1;
				else
					left_pair_val = -1;
			}

			if(right_pair_val==-1){
				if(get_satisfied(right_pair,wsize,freqlevel))
        	right_pair_val = 1;
				else
					right_pair_val = -1;
			}

			if(left_pair_val != right_pair_val)
				return (left_pair_val > right_pair_val);
		}
	}
	
	if(left_pair.first!=right_pair.first)
		return left_pair.first > right_pair.first;
  return left_pair.second > right_pair.second;
}

void aggregate_affinity(){
  for(affinityHashMap::iterator iter=affEntries->begin(); iter!=affEntries->end(); ++iter){
    if(iter->second.actual_windows!=NULL)
      for(wsize_t wsize=2;wsize<=maxWindowSize;++wsize){
        iter->second.actual_windows[wsize]+=iter->second.actual_windows[wsize-1];
      }
  }
}


/* This function builds the affinity groups based on the affinity table 
   and prints out the results */
void find_affinity_groups(){

	vector<funcpair_t> all_affEntry_iters;
  for(affinityHashMap::iterator iter=affEntries->begin(); iter!=affEntries->end(); ++iter){
    if(iter->first.first < iter->first.second)
      all_affEntry_iters.push_back(iter->first);
  }
  sort(all_affEntry_iters.begin(),all_affEntry_iters.end(),affEntryCmp);
 
	orderFile = fopen("order.babc","w");


	if(disjointSet::sets)
  	for(func_t i=0; i<totalFuncs; ++i){
			disjointSet::deallocate(i);
		}
  disjointSet::sets = new disjointSet *[totalFuncs];
  for(func_t i=0; i<totalFuncs; ++i){
  	disjointSet::init_new_set(i);
	}

  for(vector<funcpair_t>::iterator iter=all_affEntry_iters.begin(); iter!=all_affEntry_iters.end(); ++iter){
			print_affinity(*iter);
    	disjointSet::mergeSets(iter->first, iter->second);
  } 

  fclose(orderFile);

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



/* Must be called at exit*/
void affinityAtExitHandler(){
	aggregate_affinity();
	read_affinity_from_file("graph.babc");
  save_affinity_into_file("graph.babc");
	affEntryCmp=affEntry2DCmp;
  find_affinity_groups();
  print_optimal_layout();
	if(DEBUG>0)
  	fclose(debugFile);
}


void print_trace(list<SampledWindow> * tlist){
  list<SampledWindow>::iterator window_iter=tlist->begin();

  list<func_t>::iterator trace_iter;

  fprintf(debugFile,"trace list:\n");
  while(window_iter!=tlist->end()){
    trace_iter =  window_iter->partial_trace_list.begin();
    fprintf(debugFile,"windows: %d\n",window_iter->wcount);

    while(trace_iter!=window_iter->partial_trace_list.end()){
      fprintf(debugFile,"%d ",*trace_iter);
      trace_iter++;
    }
    fprintf(debugFile,"\n");
    window_iter++;
  }
  fprintf(debugFile,"---------------------------------------------\n");
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

extern "C" void llvm_trace_basic_block(func_t funcNum){
	record_func_exec(funcNum);
}

extern "C" void llvm_init_affinity_analysis(int _totalFuncs){
  totalFuncs=_totalFuncs;
  save_affinity_environment_variables();
  trace_list_size=0;
  srand(time(NULL));
  contains_func = new bool [totalFuncs]();
  func_window_it = new list<SampledWindow>::iterator [totalFuncs];
  func_trace_it = new list<func_t>::iterator [totalFuncs];
  affEntries = new affinityHashMap();

  analysis_switch = new bool [totalFuncs]();
  analyzed = new bool [totalFuncs]();
  func_count = new uint32_t [totalFuncs]();
  funcs = new func_t [totalFuncs];
  preference = new func_t [totalFuncs];
  stage_affinity = new uint32_t* [totalFuncs];
  stage_affinity_sum = new uint32_t [totalFuncs];
  potential_stage_windows = new uint32_t[totalFuncs];

  for(func_t i=0;i<totalFuncs; ++i){
    funcs[i]=i;
    preference[i]=i;
  }
  
  atexit (affinityAtExitHandler);
  
  // get prepared for the first counting stage 
  func_counting = true;
  stage_time = stage_quantum = quantum;
  if(DEBUG > 0)
    debugFile = fopen("debug.txt","w");

	zero_count_array = new uint32_t [maxWindowSize+1]();
	zero_aff_wcount = affWcounts(0,zero_count_array);
}


int compare_count (const void * p1, const void * p2){
  func_t func1= * (func_t *)p1;
  func_t func2= * (func_t *)p2;
  if(func_count[func1] > func_count[func2])
    return -1;
  if(func_count[func1] < func_count[func2])
    return 1;
  return 0;
}

int compare_stage_affinity_sum(const void * p1, const void *p2){
  func_t func1 = * (func_t *)p1;
  func_t func2 = * (func_t *) p2;

  if(func1==now_analyzed_func)
    return -1;
  if(func2==now_analyzed_func)
    return 1;

	if(stage_affinity_sum[func1] > stage_affinity_sum[func2])
			return -1;
	
	if(stage_affinity_sum[func2] > stage_affinity_sum[func1])
			return 1;

  return 0;
  
}

void update_overal_affinity(){

  for(func_t funcNum=0; funcNum<totalFuncs; ++funcNum){
    if(stage_affinity[funcNum]!=NULL){
			funcpair_t fp(now_analyzed_func, funcNum);
      affinityHashMap::iterator result=affEntries->find(fp);

      if(result==affEntries->end()){
        (*affEntries)[fp] = affWcounts(potential_stage_windows[funcNum],stage_affinity[funcNum]);
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
  qsort(preference, analysis_set_size, sizeof(func_t), compare_stage_affinity_sum);
  int sum_affinity =0;
  func_t i;
  
	if(DEBUG>0){
		fprintf(debugFile,"*************************\n");
  	fprintf(debugFile,"cutting the analysis set in half.\n");
		fprintf(debugFile,"Number of windows in the current stage: %u\n",stage_windows);
	}

  for(i=0;i<analysis_set_size; ++i){
    potential_stage_windows[preference[i]] = stage_windows;
		assert(stage_windows >= stage_affinity_sum[preference[i]]);
		if(DEBUG>0)
			fprintf(debugFile,"stage_affinity_sum[%hu]=%u\n",preference[i],stage_affinity_sum[preference[i]]);
    if(stage_affinity_sum[preference[i]]!=0 || preference[i]==now_analyzed_func)
        sum_affinity += stage_affinity_sum[preference[i]];
		else
			break;
  }

  int half_sum_affinity=0;

  for(i=0;i<analysis_set_size && (half_sum_affinity*2 < sum_affinity) ; ++i){
		half_sum_affinity += stage_affinity_sum[preference[i]];
		if(half_sum_affinity*2 >= sum_affinity)
			break;
	}

	/*
	 * Turn off the analysis switch for the second half
	 */
  for(func_t j=i;j<analysis_set_size; ++j)
    analysis_switch[preference[j]]=false;

  analysis_set_size = i;

	if(DEBUG>0)
		fprintf(debugFile,"New analysis set size: %hu\n",analysis_set_size);


  /*
   * We stop analysis if the analysis set size falls below 1
   */
  if(analysis_set_size <= 2){
		if(DEBUG>0)
			fprintf(debugFile,"analysis stage is over. Starting function counting\n");
    /*
     * We update the overal affinity (hashtable)
     */
    update_overal_affinity();
    
    trace_list.clear();
    for(func_t j=0; j<totalFuncs; ++j)
      contains_func[j]=false;

    trace_list_size=0;

    func_counting=true;
    for(i=0; i<totalFuncs; ++i){
      analysis_switch[i]=false;
			func_count[i]=0;
    }
    stage_time = stage_quantum = quantum;
  }else{
    stage_time = stage_quantum = stage_quantum;
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
		if(DEBUG>0)
			fprintf(debugFile,"function counting is over. Sorting functions to pick the new analyzed function.\n");
		uint32_t total_counts=0;
		for(func_t i=0; i < totalFuncs; i++){
			if(func_count[i]){
				total_counts += func_count[i];
				if(rand()%total_counts < func_count[i])
					now_analyzed_func = i;
			}
		}

		if(DEBUG>0)
			fprintf(debugFile,"Now analyzing function:%hu\n",now_analyzed_func);
		/*
    qsort(funcs, totalFuncs, sizeof(func_t), compare_count);
    func_t i=0;
    while(i<totalFuncs){
			if(DEBUG>1)
				fprintf(debugFile,"func_count[%hu]=%u\n",funcs[i],func_count[funcs[i]]);
      if(!analyzed[funcs[i]]){
					if(DEBUG>0)
						fprintf(debugFile,"Now analyzing function:%hu\n",funcs[i]);
        	now_analyzed_func=funcs[i];
        	analyzed[funcs[i]]=true;
        break;
      }
      i++;
    }
		*/

    /*
     * If all functions have been analyzed, we start over (we set the analyzed
     * bit of every function and pick funcs[0] 
     
    if(i==totalFuncs){
			if(DEBUG>0)
				fprintf(debugFile,"One pass of analysis is done. Starting over.\n");
      for(func_t j=0; j<totalFuncs; ++j)
        analyzed[j]=false;
			if(DEBUG>0)
					fprintf(debugFile,"Now analyzing function:%hu\n",funcs[0]);
      now_analyzed_func = funcs[0];
      analyzed[funcs[0]]=true;
    }
		*/
    
    /*
     * Initially we turn on the analysis switch for all functions. These switches
     * will be turned off over time.
     */
    for(func_t i=0; i<totalFuncs; ++i){
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


}


extern "C" bool get_switch(func_t FuncNumber){
  if(stage_time==0)
    proceed_to_next_stage();
  if(func_counting)
    func_count[FuncNumber]++;
  --stage_time;
  return analysis_switch[FuncNumber];
}

void update_stage_affinity(func_t FuncNum, list<SampledWindow>::iterator update_window_end){
    /* We move toward the tail of the list until we hit update_window_end 
     * For every partial trace lis (window), we update the affinity between
     * now_analyzed_func and FuncNum.
     */
 	wsize_t window_size = 0;
  tl_window_iter=trace_list.begin();

  while(tl_window_iter != update_window_end){
    window_size += tl_window_iter->partial_trace_list.size();
    if(stage_affinity[FuncNum]==NULL){
      stage_affinity[FuncNum]=new uint32_t[maxWindowSize+1]();
		}
		if(DEBUG>0)
			fprintf(debugFile,"incrementing affinity pair (%hu,%hu)[%hu] by %d\n",now_analyzed_func,FuncNum,window_size,tl_window_iter->wcount);
    stage_affinity[FuncNum][window_size]+=tl_window_iter->wcount;
    stage_affinity_sum[FuncNum]+=tl_window_iter->wcount;
    tl_window_iter++;
  }

}


void record_func_exec(func_t FuncNum){
  if(DEBUG>2){
		fprintf(debugFile,"****************************\n");
		fprintf(debugFile,"stage time is %d\n",stage_time);
		fprintf(debugFile,"recording function %hu\n",FuncNum);
		print_trace(&trace_list);
	}

	if(!analysis_switch[FuncNum])
		return;

  if(FuncNum==now_analyzed_func){
    stage_windows++;
    SampledWindow sw;
    sw.wcount=1;
    trace_list.push_front(sw);
  }

  if(!trace_list.empty())
    trace_list.front().partial_trace_list.push_front(FuncNum);
  else
    return;
	
  /*
   * Check if the same function record exists somewhere in the trace.
   * If it does not exist, we don't need to refine the trace anymore.
   */
  if(!contains_func[FuncNum]){
    
    //Increment the overal length of the trace list.
    trace_list_size++;

    /*
     * If the length of the trace overflows, remove one partial trace list
     * from the tail of the list.
     */
    if(trace_list_size > maxWindowSize){

      // Get the last partial trace list
      list<func_t> * last_window_trace_list= &trace_list.back().partial_trace_list;

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
    //trace_list_to_update = new list<SampledWindow>(trace_list);
    

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
			if(FuncNum!=now_analyzed_func)
      	update_stage_affinity(FuncNum,trace_list.end());
    }
  }
  /*
   * On the other hand, if the same function records already exists in the 
   * trace list, we need to refine the trace list (remove the previous record
   * and add the new one. The size of the trace list will not change.
   */
  else{
    //trace_list_to_update = new list<SampledWindow>();
		if(FuncNum!=now_analyzed_func)
    	update_stage_affinity(FuncNum,func_window_it[FuncNum]);

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


}


