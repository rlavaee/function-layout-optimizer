#include "affinity.hpp"
#include <stdio.h>
#include <algorithm>
#include <string.h>
#include <vector>

short prevFunc = -2;
int DEBUG;

int memoryLimit;
unsigned totalBBs;
int sampleRate;
int sampleSize;
int sampleMask;
short maxFreqLevel;
int level_pid, version_pid;
ProfilingLevel pLevel;

affinityHashMap * sum_affEntries;
list<SampledWindow> trace_list;

uint32_t * null_joint_freq = new uint32_t[maxWindowSize+1]();
bool * contains_func;
list<short>::iterator * func_trace_it;
list<SampledWindow>::iterator * func_window_it;
int trace_list_size;

list<SampledWindow>::iterator tl_window_iter;
list<short>::iterator tl_trace_iter;

uint32_t ** sum_freqs;

FILE * graphFile, * debugFile, * orderFile, *comparisonFile;

extern "C" void record_function_exec(short FuncNum){
	if(prevFunc==-2)
		return;
	if(prevFunc==FuncNum)
		return;
	else
		prevFunc=FuncNum;

  int r=rand();
	bool sampled=false;
  if((r & sampleMask)==0){
    //sampledWindows++;
    SampledWindow sw;
		sw.owners.insert(FuncNum);
    sw.wcount=1;
    trace_list.push_front(sw);
		sampled=true;
  }

  //if(!trace_list.empty())

	if(trace_list_size!=0 || sampled)
    trace_list.front().push_front(FuncNum);
  else
    return;
	
	if(DEBUG>0){
		if(sampled)
			fprintf(debugFile,"new window\n");
		print_trace(&trace_list);
	}


  if(!contains_func[FuncNum]){
    //printf("found FuncNum:%d in trace\n",FuncNum);
    //print_trace(&trace_list);
    trace_list_size++;
		if(trace_list_size > maxWindowSize){
      list<short> * last_window_trace_list= &trace_list.back().partial_trace_list;
			if(DEBUG>0){
				fprintf(debugFile,"trace list overflowed: %d %zu\n",trace_list_size,last_window_trace_list->size());
				print_trace(&trace_list);
			}

			trace_list_size-=trace_list.back().size();
      while(!last_window_trace_list->empty()){
        contains_func[last_window_trace_list->front()]=false;
        last_window_trace_list->pop_front();
      }

      trace_list.pop_back();
    }
    
		if(trace_list_size>0){
      contains_func[FuncNum]=true;
      func_window_it[FuncNum] = trace_list.begin();
      func_trace_it[FuncNum] = trace_list.begin()->partial_trace_list.begin();
			sequential_update_affinity(FuncNum,trace_list.end(),true);
		}

  }else{
		sequential_update_affinity(FuncNum,func_window_it[FuncNum],false);

		tl_window_iter = func_window_it[FuncNum];
		tl_window_iter->erase(func_trace_it[FuncNum]);


		if(tl_window_iter->partial_trace_list.empty()){
				list<SampledWindow>::iterator tl_window_iter_prev = tl_window_iter;
				tl_window_iter_prev--;
				for(set<short>::iterator it=tl_window_iter->owners.begin(); it!=tl_window_iter->owners.end(); ++it)
					tl_window_iter_prev->owners.insert(*it);
				tl_window_iter->owners.clear();
      	trace_list.erase(tl_window_iter);
   	}
		
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

	FILE *layoutFile = fopen(get_versioned_filename("layout"),"w");  

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


	char affinitybase[80];
	strcpy(affinitybase,"layout.mws");
	strcat(affinitybase,to_string(maxWindowSize).c_str());

  FILE *affinityFile = fopen(get_versioned_filename(affinitybase),"w");  

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

	if(DEBUG>0)
  	debugFile=fopen("debug.txt","w");

  //srand(time(NULL));
	srand(1);

	sum_affEntries = new affinityHashMap();
	sum_freqs = new uint32_t * [totalFuncs]; 
	for(int i=0; i<totalFuncs; ++i)
		sum_freqs[i] = new uint32_t [maxWindowSize+1]();
	
  contains_func = new bool [totalFuncs]();
  func_window_it = new list<SampledWindow>::iterator [totalFuncs];
  func_trace_it = new list<short>::iterator  [totalFuncs];
}


void aggregate_affinity(){
	affinityHashMap::iterator iter;

/*
  for(short i=0;i<totalFuncs;++i)
    for(short wsize=2;wsize<=maxWindowSize;++wsize){
      sum_freqs[i][wsize]+=sum_freqs[i][wsize-1];
    }

  for(iter=sum_affEntries->begin(); iter!=sum_affEntries->end(); ++iter){
    uint32_t * freq_array= iter->second;	  
    for(short wsize=2;wsize<=maxWindowSize;++wsize){
      freq_array[wsize]+=freq_array[wsize-1];
    }
  }
*/
	
	char * graphFilePath=(char*) malloc(strlen("graph")+strlen(version_str)+1);
  strcpy(graphFilePath,"graph");
  strcat(graphFilePath,version_str);

  graphFile=fopen(graphFilePath,"r");
  if(graphFile!=NULL){
    short u1,u2;
		uint32_t sfreq,jfreq;
		for(short i=0;i<totalFuncs; ++i){
			fscanf(graphFile,"(%*hd):");
			for(short wsize=1; wsize<=maxWindowSize; ++wsize){
        fscanf(graphFile,"%u ",&sfreq);
        sum_freqs[i][wsize]+=sfreq;
      }
		}
    while(fscanf(graphFile,"(%hd,%hd):",&u1,&u2)!=EOF){
			affEntry entryToAdd=affEntry(u1,u2);
      uint32_t * freq_array=(*sum_affEntries)[entryToAdd];
			if(freq_array==NULL){
				freq_array= new uint32_t[maxWindowSize+1]();
				(*sum_affEntries)[entryToAdd]=freq_array;
			}
			for(short wsize=1; wsize<=maxWindowSize; ++wsize){
        fscanf(graphFile,"{%u} ",&jfreq);
        freq_array[wsize] +=jfreq;
      }
    }
    fclose(graphFile);
  }


  graphFile=fopen(graphFilePath,"w+");

    for(short i=0;i<totalFuncs;++i){
      fprintf(graphFile,"(%hd):",i);
			for(short wsize=1; wsize<=maxWindowSize;++wsize)
				fprintf(graphFile,"%u ",sum_freqs[i][wsize]);
    	fprintf(graphFile,"\n");
		}
    for(iter=sum_affEntries->begin(); iter!=sum_affEntries->end(); ++iter){
      fprintf(graphFile,"(%hd,%hd):",iter->first.first,iter->first.second);
			for(short wsize=1;wsize<=maxWindowSize;++wsize)
				fprintf(graphFile,"{%u} ",iter->second[wsize]);
			fprintf(graphFile,"\n");
    }


  fclose(graphFile);

}

/* This function builds the affinity groups based on the affinity table 
   and prints out the results */
void find_affinity_groups(){

	vector<affEntry> all_affEntry_iters;
	for(affinityHashMap::iterator iter=sum_affEntries->begin(); iter!=sum_affEntries->end(); ++iter){
		all_affEntry_iters.push_back(iter->first);
	}
/*	
 	for(vector<affinityHashMap::iterator >::iterator iter=all_affEntry_iters.begin(); iter!=all_affEntry_iters.end(); ++iter){
		fprintf(stderr,"iter is %hd %hd %x\n", (*iter)->first.first, (*iter)->first.second, (*iter)->second);
	}*/ 

	comparisonFile = fopen("compare.txt","w");
	sort(all_affEntry_iters.begin(),all_affEntry_iters.end(),affEntryCmp);
	fclose(comparisonFile);
 
 	if(disjointSet::sets)
  	for(short i=0; i<totalFuncs; ++i){
			disjointSet::deallocate(i);
		}

	disjointSet::sets = new disjointSet *[totalFuncs];
	for(short i=0; i<totalFuncs; ++i)
		disjointSet::init_new_set(i);

	orderFile= fopen("order.txt","w");

 	for(vector<affEntry>::iterator iter=all_affEntry_iters.begin(); iter!=all_affEntry_iters.end(); ++iter){
    fprintf(orderFile,"(%d,%d)\n",iter->first,iter->second);
		//if(disjointSet::get_min_index(iter->first)+disjointSet::get_min_index(iter->second) < 4){
    	disjointSet::mergeSets(iter->first, iter->second);
			fprintf(orderFile,"effected\n");
		//}
	} 

	fclose(orderFile);

}

/* Must be called at exit*/
void affinityAtExitHandler(){
	
	if(DEBUG>0)
		fclose(debugFile);

	aggregate_affinity();
	

		//affEntryCmp=affEntry1DCmp;
		//find_affinity_groups();
  	//print_optimal_layout();

	int maxWindowSizeArray[12]={2,4,6,8,10,12,14,20,25,30,35,40};
	
	for(int i=0;i<12;++i){

		maxWindowSize=maxWindowSizeArray[i];
		
		affEntryCmp=affEntryCountCmp;
		find_affinity_groups();
		print_optimal_layouts();
	}

}





/* This function updates the affinity table based information for a window 
   void update_affinity(generalWindow& genWindow, int lastAdded){
   int wsize = genWindow.window->size();
   intHashSet::iterator iter;
   freqs[wsize][lastAdded]+=genWindow.multiple;

   for(iter=genWindow.window->begin(); iter!=genWindow.window->end(); ++iter){
   if(*iter!=lastAdded){
   affEntry entryVisit(lastAdded,*iter);
   (*affEntries[wsize])[entryVisit]+=genWindow.multiple;
//fprintf(stderr,"%d %d %d\n", lastAdded, *iter, genWindow.multiple);
}
}

}*/

void print_trace(list<SampledWindow> * tlist){
  list<SampledWindow>::iterator window_iter=tlist->begin();

  list<short>::iterator trace_iter;

  fprintf(debugFile,"---------------------------------------------\n");
  while(window_iter!=tlist->end()){
    trace_iter =  window_iter->partial_trace_list.begin();
		fprintf(debugFile,"owners:\t");
		for(set<short>::iterator owner_it=window_iter->owners.begin(); owner_it!=window_iter->owners.end(); ++owner_it)
			fprintf(debugFile,"%hd ",*owner_it);

		fprintf(debugFile,"\nelements(%zu):\t",window_iter->size());
		assert(window_iter->size()==window_iter->partial_trace_list.size());

    while(trace_iter!=window_iter->partial_trace_list.end()){
      fprintf(debugFile,"%d ",*trace_iter);
      trace_iter++;
    }
    fprintf(debugFile,"\n");
    window_iter++;
  }
}

list<SampledWindow>::iterator grown_list_iter;
set<short>::iterator owner_iter;
set<short>::iterator owner_iter_end;

void sequential_update_affinity(short FuncNum, list<SampledWindow>::iterator grown_list_end, bool missed){
      unsigned top_wsize=0;

      grown_list_iter = trace_list.begin();

      while(grown_list_iter!= grown_list_end){
				top_wsize+=grown_list_iter->size();
        owner_iter = grown_list_iter->owners.begin();
				owner_iter_end = grown_list_iter->owners.end();

        while(owner_iter != owner_iter_end){
          short FuncNum2= *owner_iter;
          
          if(FuncNum2!=FuncNum){
            affEntry trace_entry(FuncNum, FuncNum2);
            uint32_t * freq_array;

            affinityHashMap::iterator  result=sum_affEntries->find(trace_entry);
            if(result==sum_affEntries->end())
              (*sum_affEntries)[trace_entry]= freq_array=new uint32_t[maxWindowSize+1]();
            else
              freq_array=result->second;
					
						freq_array[top_wsize]+= (missed)?(1):1;
          }


          owner_iter++;
        }

        grown_list_iter++;

      } 
}

SampledWindow::SampledWindow(const SampledWindow & sw){
  wcount=sw.wcount;
	wsize=sw.wsize;
  partial_trace_list = list<short>(sw.partial_trace_list);
	owners = set<short>(sw.owners);
}

SampledWindow::SampledWindow(){
  wcount=0;
	wsize=0;
  partial_trace_list = list<short>();
	owners = set<short>();
}

SampledWindow::~SampledWindow(){}

uint32_t * GetWithDef(affinityHashMap * m, const affEntry &key, uint32_t * defval) {
  affinityHashMap::const_iterator it = m->find( key );
  if ( it == m->end() ) {
    return defval;
  }
  else {
    return it->second;
  }
}

bool affEntryCountCmp(affEntry ae_left, affEntry ae_right){
	uint32_t * jointFreq_left = GetWithDef(sum_affEntries, ae_left, null_joint_freq);
  uint32_t * jointFreq_right = GetWithDef(sum_affEntries, ae_right, null_joint_freq);

	uint32_t jointFreq_left_total =0;
	uint32_t jointFreq_right_total =0;

	for(short wsize=2;wsize<=maxWindowSize;++wsize){
		jointFreq_left_total+=jointFreq_left[wsize];
		jointFreq_right_total+=jointFreq_right[wsize];
	}

	if(jointFreq_left_total > jointFreq_right_total)
		return true;
	
	if(jointFreq_left_total < jointFreq_right_total)
		return false;

	if(ae_left.first != ae_right.first)
		return (ae_left.first > ae_right.first);
	
	return ae_left.second > ae_right.second;
}

bool affEntry1DCmp(affEntry ae_left, affEntry ae_right){
	uint32_t * jointFreq_left = GetWithDef(sum_affEntries, ae_left, null_joint_freq);
  uint32_t * jointFreq_right = GetWithDef(sum_affEntries, ae_right, null_joint_freq);

	int ae_left_val, ae_right_val;
	
	float rel_freq_threshold=2.0;
    for(short wsize=2;wsize<=maxWindowSize;++wsize){
			
        if((rel_freq_threshold*(jointFreq_left[wsize]) > sum_freqs[ae_left.first][wsize]) && 
            (rel_freq_threshold*(jointFreq_left[wsize]) > sum_freqs[ae_left.second][wsize]))
					ae_left_val = 1;
				else
					ae_left_val = -1;

				if((rel_freq_threshold*(jointFreq_right[wsize]) > sum_freqs[ae_right.first][wsize]) && 
            (rel_freq_threshold*(jointFreq_right[wsize]) > sum_freqs[ae_right.second][wsize]))
					ae_right_val = 1;
				else
					ae_right_val = -1;

				if(ae_left_val != ae_right_val)
					return (ae_left_val > ae_right_val);
    }

	if(ae_left.first != ae_right.first)
		return (ae_left.first > ae_right.first);
	
	return ae_left.second > ae_right.second;

}

bool affEntry2DCmp(affEntry ae_left, affEntry ae_right){

  uint32_t * jointFreq_left = GetWithDef(sum_affEntries, ae_left, null_joint_freq);
  uint32_t * jointFreq_right = GetWithDef(sum_affEntries, ae_right, null_joint_freq);

	int ae_left_val, ae_right_val;
	
	short freqlevel;
  for(freqlevel=maxFreqLevel-1; freqlevel>=0; --freqlevel){
    for(short wsize=2;wsize<=maxWindowSize;++wsize){
			
        if((maxFreqLevel*(jointFreq_left[wsize]) > freqlevel*sum_freqs[ae_left.first][wsize]) && 
            (maxFreqLevel*(jointFreq_left[wsize]) > freqlevel*sum_freqs[ae_left.second][wsize]))
					ae_left_val = 1;
				else
					ae_left_val = -1;

				if((maxFreqLevel*(jointFreq_right[wsize]) > freqlevel*sum_freqs[ae_right.first][wsize]) && 
            (maxFreqLevel*(jointFreq_right[wsize]) > freqlevel*sum_freqs[ae_right.second][wsize]))
					ae_right_val = 1;
				else
					ae_right_val = -1;

				if(ae_left_val != ae_right_val)
					return (ae_left_val > ae_right_val);
    }
  }

	if(ae_left.first != ae_right.first)
		return (ae_left.first > ae_right.first);
	
	return ae_left.second > ae_right.second;

}

affEntry::affEntry(short _first, short _second){
  if(_first<_second){
    first=_first;
    second=_second;
  }else{
    first=_second;
    second=_first;
  }
}

affEntry::affEntry(const affEntry& ae){
	first=ae.first;
	second=ae.second;
}

affEntry& affEntry::operator= (const affEntry &ae)
{
	first=ae.first;
	second=ae.second;
	return *this;
}

bool affEntry::operator== (const affEntry &ae) const{
	return eqAffEntry()(*this,ae);
}
affEntry::affEntry(){}

bool eqAffEntry::operator()(affEntry const& entry1, affEntry const& entry2) const{
  return ((entry1.first == entry2.first) && (entry1.second == entry2.second));
}





void disjointSet::mergeSets(disjointSet * set1, disjointSet* set2){

		disjointSet * merger = (set1->size()>=set2->size())?(set1):(set2);
		
		disjointSet * mergee = (set1->size()<set2->size())?(set1):(set2);


		affEntry frontMerger_backMergee(merger->elements.front(), mergee->elements.back());
		affEntry backMerger_backMergee(merger->elements.back(), mergee->elements.back());
		affEntry backMerger_frontMergee(merger->elements.back(), mergee->elements.front());
		affEntry frontMerger_frontMergee(merger->elements.front(), mergee->elements.front());
		affEntry conAffEntriesArray[4]={frontMerger_frontMergee, frontMerger_backMergee, backMerger_frontMergee, backMerger_backMergee};
		vector<affEntry> conAffEntries(conAffEntriesArray,conAffEntriesArray+4);
		sort(conAffEntries.begin(), conAffEntries.end(), affEntryCmp);

		assert(affEntryCmp(conAffEntries[0],conAffEntries[1]) || (conAffEntries[0]==conAffEntries[1]));
		assert(affEntryCmp(conAffEntries[1],conAffEntries[2]) || (conAffEntries[1]==conAffEntries[2]));
		assert(affEntryCmp(conAffEntries[2],conAffEntries[3]) || (conAffEntries[2]==conAffEntries[3]));

		bool con_mergee_front = (conAffEntries[0] == backMerger_frontMergee) || (conAffEntries[0] == frontMerger_frontMergee);
		bool con_merger_front = (conAffEntries[0] == frontMerger_frontMergee) || (conAffEntries[0] == frontMerger_backMergee);

		//fprintf(stderr, "Now merging:");
		if(con_mergee_front){
		
			for(deque<short>::iterator it=mergee->elements.begin(); it!=mergee->elements.end(); ++it){
				//fprintf(stderr, "%hd ",*it);
				if(con_merger_front)
						merger->elements.push_front(*it);
				else
						merger->elements.push_back(*it);
				disjointSet::sets[*it]=merger;
			}
		}else{
			//fprintf(stderr,"(backwards)");
			for(deque<short>::reverse_iterator rit=mergee->elements.rbegin(); rit!=mergee->elements.rend(); ++rit){
					//fprintf(stderr, "%hd ",*rit);
				if(con_merger_front)
						merger->elements.push_front(*rit);
				else
						merger->elements.push_back(*rit);
				disjointSet::sets[*rit]=merger;
			}
		}

		//fprintf(stderr,"\nResulting in:");

		//for(deque<short>::iterator it=merger->elements.begin(); it!=merger->elements.end(); ++it)
		//	fprintf(stderr, "%hd ",*it);
		//fprintf(stderr,"\n");
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
  
  //int ret=save_arguments(argc, argv);
  /*  if(argc>1)
    program_name=argv[1];
  else{
    program_name=(char *) malloc(sizeof(char)*4);
    strcpy(program_name,"non");
    }*/
  save_affinity_environment_variables();  
	totalFuncs = _totalFuncs;
  initialize_affinity_data();
  /* Set up the atexit handler. */
  atexit (affinityAtExitHandler);

  return 1;
}

