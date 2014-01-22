#include "affinity.hpp"
#include <stdio.h>
#include <algorithm>
#include <pthread.h>
#include <semaphore.h>
#include <boost/lockfree/queue.hpp>
#include <string.h>
#include <vector>

bool DEBUG=false;
FILE * comparisonFile, * orderFile, * traceFile;
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
    pair<int,int> ** freq_range_matrix = it->second;
    pair<int,int> * freq_array;
    JointFreqMap::iterator result = joint_freqs->find(update_pair);
    if(result== joint_freqs->end())
      (*joint_freqs)[update_pair]= freq_array=new pair<int,int>[maxWindowSize+1]();
    else
      freq_array=result->second;

    for(int i=2; i<=maxWindowSize;++i)
      for(int j=i; j<=maxWindowSize; ++j)
        for(int k=i; k<=j; ++k){
          freq_array[k].first+=freq_range_matrix[i][j].first;
          freq_array[k].second+=freq_range_matrix[i][j].second;
				}
  } 
}
void commit_joint_freq_updates(short func,int max_wsize){
	while(!joint_freq_update_lists[func].empty()){
		UpdateEntry update_entry = joint_freq_update_lists[func].front();
		joint_freq_update_lists[func].pop_front();
    shortpair update_pair=make_pair(func,update_entry.func);
    pair<int,int> ** freq_range_matrix;
    JointFreqRangeMap::iterator result=joint_freq_ranges->find(update_pair);
    if(result == joint_freq_ranges->end()){
      freq_range_matrix = new pair<int,int>*[maxWindowSize+1];
      for(int i=1; i<=maxWindowSize; ++i)
        freq_range_matrix[i]=new pair<int,int>[maxWindowSize+1]();
      (*joint_freq_ranges)[update_pair]= freq_range_matrix;
    }else
      freq_range_matrix=result->second;

		if(update_entry.first_window)
    	freq_range_matrix[update_entry.min_wsize][max_wsize].first++;
		else
			freq_range_matrix[update_entry.min_wsize][max_wsize].second++;
		if(DEBUG){
			printf("&&&&&&&&&&&&&&&&\n");
			printf("(%d,%d)->(%d..%d)\n",update_pair.first, update_pair.second,update_entry.min_wsize,max_wsize);
			printf("&&&&&&&&&&&&&&&&\n");
		}
  }
}

extern "C" void record_function_exec(short FuncNum){
  if(prevFunc==FuncNum)
    return;
  else
    prevFunc=FuncNum;

	fprintf(traceFile,"%hd\n",FuncNum);
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

	if(DEBUG){
		print_trace(&trace_list);
	}



  if(!contains_func[FuncNum]){
    trace_list_size++;
		trace_list.front().wsize++;

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
      sequential_update_affinity(trace_list.end());
      contains_func[FuncNum]=true;
      func_window_it[FuncNum] = trace_list.begin();
      func_trace_it[FuncNum] = trace_list.begin()->partial_trace_list.begin();
    }

  }else{
		trace_list.front().wsize++;
    func_window_it[FuncNum]->wsize--;
    if(trace_list.begin()!=func_window_it[FuncNum]){

      int top_wsize = 0;
      tl_window_iter = trace_list.begin();
      while(tl_window_iter!=func_window_it[FuncNum]){
        top_wsize+=tl_window_iter->wsize;
				tl_window_iter++;
      }
			top_wsize+=tl_window_iter->wsize;
      commit_joint_freq_updates(FuncNum,top_wsize);
      sequential_update_affinity(func_window_it[FuncNum]);
		}
			tl_window_iter=func_window_it[FuncNum];
      tl_window_iter->partial_trace_list.erase(func_trace_it[FuncNum]);

      if(tl_window_iter->partial_trace_list.empty())
        trace_list.erase(tl_window_iter);

      func_window_it[FuncNum] = trace_list.begin();
      func_trace_it[FuncNum] = trace_list.begin()->partial_trace_list.begin();	
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
  strcpy(affinityFilePath,"layout_2D");
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
	traceFile = fopen("trace.txt","w");
}

void aggregate_affinity(){
  JointFreqMap::iterator iter;


  char * graphFilePath=(char*) malloc(strlen("graph")+strlen(version_str)+1);
  strcpy(graphFilePath,"graph");
  strcat(graphFilePath,version_str);

  graphFile=fopen(graphFilePath,"r");
  //int prev_sampledWindows=0;
  if(graphFile!=NULL){
    short u1,u2;
		int sfreq,freq1,freq2;
    while(fscanf(graphFile,"%hd",&u1)!=EOF){
			if(u1==-1)
				break;
			for(short wsize=1; wsize<=maxWindowSize; ++wsize){
        fscanf(graphFile,"%d",&sfreq);
        single_freqs[u1][wsize]+=sfreq;
      }
		}
    while(fscanf(graphFile,"%hd",&u1)!=EOF){
			fscanf(graphFile,"%hd",&u2);
			shortpair entryToAdd=make_pair(u1,u2);
      pair<int,int> * freq_array=(*joint_freqs)[entryToAdd];
			if(freq_array==NULL){
				freq_array= new pair<int,int>[maxWindowSize+1]();
				(*joint_freqs)[entryToAdd]=freq_array;
			}
			for(short wsize=1; wsize<=maxWindowSize; ++wsize){
        fscanf(graphFile,"%d %d",&freq1,&freq2);
        freq_array[wsize].first+=freq1;
        freq_array[wsize].second+=freq2;
      }
    }
    fclose(graphFile);
  }


  graphFile=fopen(graphFilePath,"w+");

    for(short i=0;i<totalFuncs;++i){
      fprintf(graphFile,"(%hd):",i);
			for(short wsize=1; wsize<=maxWindowSize;++wsize)
				fprintf(graphFile,"%d ",single_freqs[i][wsize]);
    	fprintf(graphFile,"\n");
		}
		fprintf(graphFile,"-1\n");

    for(iter=joint_freqs->begin(); iter!=joint_freqs->end(); ++iter){
      fprintf(graphFile,"(%hd,%hd):",iter->first.first,iter->first.second);
			for(short wsize=1;wsize<=maxWindowSize;++wsize)
				fprintf(graphFile,"{%d %d} ",iter->second[wsize].first,iter->second[wsize].second);
			fprintf(graphFile,"\n");
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
	comparisonFile = fopen("compare.txt","w");
  sort(all_affEntry_iters.begin(),all_affEntry_iters.end(),affEntryCmp);
	fclose(comparisonFile);
	comparisonFile=NULL;

	orderFile = fopen("order.txt","w");


  disjointSet::sets = new disjointSet *[totalFuncs];
  for(short i=0; i<totalFuncs; ++i)
    disjointSet::init_new_set(i);

  for(vector<shortpair>::iterator iter=all_affEntry_iters.begin(); iter!=all_affEntry_iters.end(); ++iter){
		fprintf(orderFile,"(%d,%d) {{%d|%d}}\n",iter->first,iter->second,single_freqs[iter->first][maxWindowSize],single_freqs[iter->second][maxWindowSize]);
    disjointSet::mergeSets(iter->first, iter->second);
  } 

	fclose(orderFile);

}

/* Must be called at exit*/
void affinityAtExitHandler(){
	fclose(traceFile);
	for(short i=0;i<totalFuncs; ++i)
		commit_joint_freq_updates(i,trace_list_size);
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
			if(DEBUG){
				printf("########################\n");
				printf("update single: %d\t%d\n",oldFuncNum,top_wsize);
				printf("########################\n");
			}
      single_freqs[oldFuncNum][top_wsize]++;
      if(oldFuncNum!=FuncNum){
			if(DEBUG){
				printf("************************\n");
				printf("update pair: (%d,%d)\t%d\n",oldFuncNum,FuncNum,top_wsize);
				printf("************************\n");
			}
        UpdateEntry update_entry(FuncNum, top_wsize, window_iter==trace_list.begin());
        joint_freq_update_lists[oldFuncNum].push_back(update_entry);
      }

      func_iter++;
    }

    window_iter++;

  } 


}

/*
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
*/

pair<int,int> * GetWithDef(JointFreqMap * m, const shortpair &key, pair<int,int> * defval) {
  JointFreqMap::const_iterator it = m->find( key );
  if ( it == m->end() ) {
    return defval;
  }
  else {
    return it->second;
  }
}
bool affEntry2DCmp(const shortpair &left_pair, const shortpair &right_pair){

  pair<int,int> * jointFreq_left = GetWithDef(joint_freqs, left_pair, NULL);
  pair<int,int> * jointFreq_right = GetWithDef(joint_freqs, right_pair, NULL);

  if(jointFreq_left == NULL && jointFreq_right != NULL)
    return false;
  if(jointFreq_left != NULL  && jointFreq_right == NULL)
    return true;

  if(jointFreq_left != NULL){
    int left_pair_val, right_pair_val;

    short freqlevel;
    float rel_freq_threshold;
    for(freqlevel=0, rel_freq_threshold=1.0; freqlevel<maxFreqLevel; ++freqlevel, rel_freq_threshold+=5.0/maxFreqLevel){
      for(short wsize=2;wsize<=maxWindowSize;++wsize){

        int jointFreq_left_wsize = (jointFreq_left==NULL)?(0):(jointFreq_left[wsize].first+jointFreq_left[wsize].second);
				int total_left = single_freqs[left_pair.first][wsize]+single_freqs[left_pair.second][wsize]-jointFreq_left[wsize].first;

        if(rel_freq_threshold*(jointFreq_left_wsize) >= total_left) 
          left_pair_val = 1;
        else
          left_pair_val = -1;

        int jointFreq_right_wsize = (jointFreq_right==NULL)?(0):(jointFreq_right[wsize].first+jointFreq_right[wsize].second);
				int total_right=single_freqs[right_pair.first][wsize]+single_freqs[right_pair.second][wsize]-jointFreq_right[wsize].first;

        if(rel_freq_threshold*(jointFreq_right_wsize) >= total_right)
          right_pair_val = 1;
        else
          right_pair_val = -1;

        if(left_pair_val != right_pair_val){
					if(comparisonFile!=NULL){
					fprintf(comparisonFile,"(%d,%d):(%d/%d) ",left_pair.first,left_pair.second,jointFreq_left_wsize,total_left); 
					fprintf(comparisonFile,(left_pair_val > right_pair_val)?(">"):("<"));
					fprintf(comparisonFile," (%d,%d):(%d/%d) [wsize:%d, threshold:%f]\n",right_pair.first,right_pair.second, jointFreq_right_wsize, total_right,wsize,rel_freq_threshold);
					}
          return (left_pair_val > right_pair_val);
				}
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

/*
int main(){
	start_call_site_tracing(10);
	FILE * inputTraceFile=fopen("input.txt","r");
	short func;
	while(fscanf(inputTraceFile,"%hd",&func)!=EOF){
		record_function_exec(func);
	}
	return 0;
}*/
