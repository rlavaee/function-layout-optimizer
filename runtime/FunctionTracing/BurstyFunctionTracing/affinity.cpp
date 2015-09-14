#include "affinity.hpp"
#include <stdio.h>
#include <algorithm>
#include <pthread.h>
#include <semaphore.h>
#include <boost/lockfree/queue.hpp>
#include <string.h>
#include <vector>
#include <atomic>
#include <iostream>
#include <linux/unistd.h>
#include <unistd.h>
#include <sys/syscall.h>
#define MAXTHREADS 100

pid_t gettid( void )
{
		return syscall( __NR_gettid );
}

std::atomic<pid_t> prof_th;
volatile bool profiling_switch; 
pthread_t prof_switch_th;
volatile bool flush_trace;
pthread_mutex_t switch_mutex;

extern "C" bool do_exchange(){
	pid_t cur_pid = gettid();
	//std::cerr << "cur_pid is: " << cur_pid << " and prof_th is: " << prof_th.load() << "\n";
	if(prof_th.load()==cur_pid)
		return true;
	pid_t free_th = -1;
	return prof_th.compare_exchange_strong(free_th,cur_pid);
}

void * prof_switch_toggle(void *){
	while(true){
		usleep(40000);
		pthread_mutex_lock(&switch_mutex);
		profiling_switch = true;
		prof_th.store(-1);
		pthread_mutex_unlock(&switch_mutex);
		usleep(10000);
		pthread_mutex_lock(&switch_mutex);
		profiling_switch = false;
		flush_trace = true;
		pthread_mutex_unlock(&switch_mutex);

	}
}

const char * profilePath = NULL;

short prevFunc = -2;
long long added_lists=0;
long long removed_lists[MAXTHREADS];
pthread_t consumers[MAXTHREADS];
pthread_t master;
long nconsumers;
int DEBUG;

int memoryLimit;
//long sampledWindows;
unsigned totalBBs;
int sampleRate;
int sampleSize;
int sampleMask;
short maxFreqLevel;
int level_pid, version_pid;
ProfilingLevel pLevel;

affinityHashMap * affEntries[MAXTHREADS];
affinityHashMap * sum_affEntries;
std::list<SampledWindow> trace_list;

uint32_t * null_joint_freq = new uint32_t[maxWindowSize+1]();
bool * contains_func;
std::list<short>::iterator * func_trace_it;
std::list<SampledWindow>::iterator * func_window_it;
int trace_list_size;


boost::lockfree::queue< std::list<SampledWindow> * > trace_list_queue (100);

//pthread_t update_affinity_thread;


std::list<SampledWindow>::iterator tl_window_iter;
std::list<short>::iterator tl_trace_iter;

//disjointSet *** sets;
uint32_t ** freqs[MAXTHREADS];
uint32_t ** sum_freqs;

FILE * graphFile, * debugFile, * orderFile, *comparisonFile;



//pthread_mutex_t trace_list_queue_lock = PTHREAD_MUTEX_INITIALIZER;

sem_t affinity_sem;



void push_into_update_queue (std::list<SampledWindow> * trace_list_to_update){
		trace_list_queue.push(trace_list_to_update);
		added_lists++;
		sem_post(&affinity_sem);
}
//using google::sparse_hash_map;
//using google::sparse_hash_set;
//using std::tr1::hash;


inline void record_execution(short FuncNum){

	if(flush_trace){
		while(!trace_list.empty()){
	    	std::list<short> * last_window_trace_list= &trace_list.back().partial_trace_list;
      		trace_list_size-=last_window_trace_list->size();
      	
			while(!last_window_trace_list->empty()){
        		contains_func[last_window_trace_list->front()]=false;
        		last_window_trace_list->pop_front();
      		}
		
			trace_list.pop_back();
		}

		assert(trace_list_size==0 && "could not flush the trace");
		flush_trace = false;
		return;
	}

	if(!profiling_switch)
		return;

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
    sw.wcount=1;
    trace_list.push_front(sw);
		sampled=true;
  }

  //if(!trace_list.empty())

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
    //printf("found FuncNum:%d in trace\n",FuncNum);
    //print_trace(&trace_list);
    trace_list_size++;
		if(trace_list_size > maxWindowSize){
      std::list<short> * last_window_trace_list= &trace_list.back().partial_trace_list;
			//printf("por shod: %d %u\n",trace_list_size,last_window_trace_list->size());
			//print_trace(&trace_list);
      trace_list_size-=last_window_trace_list->size();
      while(!last_window_trace_list->empty()){
        contains_func[last_window_trace_list->front()]=false;
        last_window_trace_list->pop_front();
      }
      //last_window_iter->partial_trace_list.clear();

      trace_list.pop_back();
    }
    
		if(trace_list_size>0){
      contains_func[FuncNum]=true;
      func_window_it[FuncNum] = trace_list.begin();
      func_trace_it[FuncNum] = trace_list.begin()->partial_trace_list.begin();

    	std::list<SampledWindow> * trace_list_to_update = new std::list<SampledWindow>(trace_list);	
			//printf("a new update window list\n");
			//print_trace(trace_list_to_update);
			push_into_update_queue(trace_list_to_update);
		}

  }else{
		if(trace_list.begin()!=func_window_it[FuncNum]){
    	std::list<SampledWindow> * trace_list_to_update = new std::list<SampledWindow>(trace_list.begin(),func_window_it[FuncNum]);	
			//printf("a new update window list\n");
			//print_trace(trace_list_to_update);
			push_into_update_queue(trace_list_to_update);
		}
    /*tl_window_iter=trace_list.begin();
    while(tl_window_iter != func_window_it[FuncNum]){
      trace_list_to_update->push_back(*tl_window_iter);
      tl_window_iter++;
    }*/

			tl_window_iter = func_window_it[FuncNum];
    	tl_window_iter->partial_trace_list.erase(func_trace_it[FuncNum]);

    	if(tl_window_iter->partial_trace_list.empty()){
      	uint16_t temp_wcount=tl_window_iter->wcount;
      	tl_window_iter--;
      	tl_window_iter->wcount+=temp_wcount;
      	tl_window_iter++;
      	trace_list.erase(tl_window_iter);
    	}
    	func_window_it[FuncNum] = trace_list.begin();
    	func_trace_it[FuncNum] = trace_list.begin()->partial_trace_list.begin();	
		
  }

}

extern "C" void __attribute__ ((__cdecl__)) record_function_exec(short FuncNum){
	pthread_mutex_lock(&switch_mutex);
	record_execution(FuncNum);
	pthread_mutex_unlock(&switch_mutex);
}


void add_threads(){
	int wait_time = 100000;
	while(true){
		usleep(wait_time);
		if(added_lists==-1)
			return;
		long long sum_removed=0;
		for(int thno=0;thno<nconsumers; ++thno){
			sum_removed+=removed_lists[thno];
		}
		//fprintf(stderr,"added lists: %lld removed lists: %lld number of consumers: %d\n",added_lists,sum_removed,nconsumers);

		if((nconsumers == 0) || ((added_lists - sum_removed)*maxWindowSize > memoryLimit)){
			removed_lists[nconsumers]=0;
			affEntries[nconsumers]=new affinityHashMap();
			freqs[nconsumers]=new uint32_t * [totalFuncs];

  		for(short i=0;i<totalFuncs;++i)
    		freqs[nconsumers][i]=new uint32_t[maxWindowSize+1]();

  		pthread_create(&consumers[nconsumers],NULL,update_affinity,(void *)nconsumers );
			//fprintf(stderr,"Consumer thread %d created.\n",nconsumers);
			nconsumers++;
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

	strcpy(affinityFilePath,"");
  	if(profilePath!=NULL){
  		strcat(affinityFilePath,profilePath);
		strcat(affinityFilePath,"/");
 	 }
	strcat(affinityFilePath,"layout");
	strcat(affinityFilePath,version_str);
	strcat(affinityFilePath,(affEntryCmp==affEntry1DCmp)?(".1D"):(""));
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
		if(disjointSet::sets[i]){
			disjointSet * thisSet=disjointSet::sets[i];
			for(deque<short>::iterator it=disjointSet::sets[i]->elements.begin(), 
					it_end=disjointSet::sets[i]->elements.end()
					; it!=it_end ; ++it){
				layout[count++]=*it;
				disjointSet::sets[*it]=0;
			}
			thisSet->elements.clear();
			delete thisSet;
		}
	}


	char affinitybase[80];
	strcpy(affinitybase,"layout.mws");
	char mwsize_str[10];
	sprintf(mwsize_str, "%d", maxWindowSize);
	strcat(affinitybase,mwsize_str);
	//strcat(affinitybase,to_string(maxWindowSize).c_str());

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
  //sampledWindows=0;
	nconsumers=0;
  trace_list_size=0;

	if(DEBUG>0)
  	debugFile=fopen("debug.txt","w");

  //srand(time(NULL));
	srand(1);

 // freqs = new int* [totalFuncs];

  contains_func = new bool [totalFuncs]();
  func_window_it = new std::list<SampledWindow>::iterator [totalFuncs];
  func_trace_it = new std::list<short>::iterator  [totalFuncs];

  //affEntry empty_entry(-1,-1);

  //affEntries = new affinityHashMap();
  //unordered_map <const affEntry, int *, affEntry_hash, eqAffEntry>();

  //  affEntries->set_empty_key(empty_entry);


  //for(i=0;i<totalFuncs;++i)
  //  freqs[i]=new int[maxWindowSize+1]();

  sem_init(&affinity_sem,0,1);
	//sem_init(&balanced_sem,0,1);

	pthread_create(&master, NULL, (void*(*)(void *))add_threads, (void *)0);
  //pthread_create(&update_affinity_thread,NULL,(void*(*)(void *))update_affinity, (void *)0);
}


void join_all_consumers(){
	sum_freqs = new uint32_t * [totalFuncs];
	for(short i=0;i<totalFuncs; ++i)
		sum_freqs[i]=new uint32_t [maxWindowSize+1]();
	sum_affEntries=new affinityHashMap();

	for(int thno=0; thno<nconsumers; ++thno){
		//fprintf(stderr,"joining consumers %d\n",thno);
		for(affinityHashMap::iterator iter = affEntries[thno]->begin(); iter!=affEntries[thno]->end(); ++iter){
			//fprintf(stderr,"They got another one %d %d\n",iter->first.first,iter->first.second);
			affinityHashMap::iterator result=sum_affEntries->find(iter->first);
			uint32_t * freq_array;
			if(result==sum_affEntries->end())
				(*sum_affEntries)[iter->first]= freq_array=new uint32_t[maxWindowSize+1]();
			else
				freq_array=result->second;
			
			for(int wsize=2; wsize<=maxWindowSize;++wsize){
				freq_array[wsize]+=iter->second[wsize];
				//fprintf(stderr,"here is %d : %d\n",wsize,iter->second[wsize]);
			}
		}
		delete affEntries[thno];
		
		for(short i=0; i<totalFuncs; ++i)
			for(int wsize=1; wsize<=maxWindowSize;++wsize)
				sum_freqs[i][wsize]+=freqs[thno][i][wsize];


	}


}

void aggregate_affinity(){
	affinityHashMap::iterator iter;


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
	
  char * graphFilePath=(char*) malloc(80);
  strcpy(graphFilePath,"");
  if(profilePath!=NULL){
  	strcat(graphFilePath,profilePath);
	strcat(graphFilePath,"/");
  }

  strcat(graphFilePath,"graph");
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

	std::vector<affEntry> all_affEntry_iters;
	for(affinityHashMap::iterator iter=sum_affEntries->begin(); iter!=sum_affEntries->end(); ++iter){
		all_affEntry_iters.push_back(iter->first);
	}
/*	
 	for(std::vector<affinityHashMap::iterator >::iterator iter=all_affEntry_iters.begin(); iter!=all_affEntry_iters.end(); ++iter){
		fprintf(stderr,"iter is %hd %hd %x\n", (*iter)->first.first, (*iter)->first.second, (*iter)->second);
	}*/ 

	//comparisonFile = fopen("compare.txt","w");
	std::sort(all_affEntry_iters.begin(),all_affEntry_iters.end(),affEntryCmp);
	//fclose(comparisonFile);
 
 	if(disjointSet::sets)
  	for(short i=0; i<totalFuncs; ++i){
			disjointSet::deallocate(i);
		}

	disjointSet::sets = new disjointSet *[totalFuncs];
	for(short i=0; i<totalFuncs; ++i)
		disjointSet::init_new_set(i);

	orderFile= fopen("order.abc","w");

 	for(std::vector<affEntry>::iterator iter=all_affEntry_iters.begin(); iter!=all_affEntry_iters.end(); ++iter){
    	fprintf(orderFile,"(%d,%d)\n",iter->first,iter->second);
		//if(disjointSet::get_min_index(iter->first)+disjointSet::get_min_index(iter->second) < 4){
    	disjointSet::mergeSets(iter->first, iter->second);
			//fprintf(orderFile,"effected\n");
		//}
	} 

	//fclose(orderFile);

}

/* Must be called at exit*/
void affinityAtExitHandler(){
  //free immature windows
  //list_remove_all(&window_list,NULL,free);
	
	if(DEBUG>0)
		fclose(debugFile);

 	for(int i=0; i<nconsumers; ++i){
  	std::list<SampledWindow> * null_trace_list = new std::list<SampledWindow>();
  	SampledWindow sw = SampledWindow();
  	sw.partial_trace_list.push_front(-1);
  	null_trace_list->push_front(sw);
	
  	trace_list_queue.push(null_trace_list);
  	sem_post(&affinity_sem);
	}

 	for(int i=0; i<nconsumers; ++i){
  		pthread_join(consumers[i],NULL);
	}

	added_lists=-1;

	pthread_join(master,NULL);

	join_all_consumers();
	aggregate_affinity();
	

	affEntryCmp=affEntry2DCmp;
	find_affinity_groups();
  print_optimal_layout();

/*
	int maxWindowSizeArray[12]={2,4,6,8,10,12,14,20,25,30,35,40};
	
	for(int i=0;i<12;++i){

		maxWindowSize=maxWindowSizeArray[i];
		
		affEntryCmp=affEntry2DCmp;
		find_affinity_groups();
  	print_optimal_layout();
			print_optimal_layouts();
	}
	*/

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
    fprintf(debugFile,"count: %d\n",window_iter->wcount);


    while(trace_iter!=window_iter->partial_trace_list.end()){
      fprintf(debugFile,"%d ",*trace_iter);
      trace_iter++;
    }
    fprintf(debugFile,"\n");
    window_iter++;
  }
}




void * update_affinity(void * threadno_void){
	long threadno = (long) threadno_void;
	//fprintf (stderr,"I am thread %d \n",threadno);

  std::list<SampledWindow>::iterator top_window_iter;
  std::list<SampledWindow>::iterator window_iter;
  std::list<short>::iterator trace_iter;

  
  std::list<SampledWindow>::iterator trace_list_front_end;
  std::list<short>::iterator partial_trace_list_end;
	std::list<SampledWindow> * trace_list_front; 

  while(true){
    //printf("queue empty waiting\n");
    sem_wait(&affinity_sem);

		//empty() is not thread-safe
    //while(!trace_list_queue.empty()){
		while(trace_list_queue.pop(trace_list_front)){
			//fprintf(stderr,"popped one from the queue\n");
			//print_trace(trace_list_front);
			//fprintf(stderr,"added : %d\t removed: %d\n",added_lists,removed_lists[threadno]);
			//fprintf(stderr,"queue size is %d\n",trace_list_queue.size());

      unsigned top_wsize,wsize;
      top_wsize=0;

      //pthread_mutex_lock(&trace_list_queue_lock);
      //printf("poped this from queue:\n");
      //print_trace(trace_list_front);
      //pthread_mutex_unlock(&trace_list_queue_lock);
      //printf("trace_list_fonrt is:\n");


      top_window_iter = trace_list_front->begin();
      trace_iter = top_window_iter->partial_trace_list.begin();


      short FuncNum= *trace_iter;

      if(FuncNum==-1)
        pthread_exit(NULL);


      trace_list_front_end = trace_list_front->end();

      while(top_window_iter!= trace_list_front_end){
        trace_iter = top_window_iter->partial_trace_list.begin();

          
        partial_trace_list_end = top_window_iter->partial_trace_list.end();
        while(trace_iter != partial_trace_list_end){

          short FuncNum2= *trace_iter;
          
          if(FuncNum2!=FuncNum){
            affEntry trace_entry(FuncNum, FuncNum2);
            uint32_t * freq_array;

            affinityHashMap::iterator  result=affEntries[threadno]->find(trace_entry);
            if(result==affEntries[threadno]->end())
              (*affEntries[threadno])[trace_entry]= freq_array=new uint32_t[maxWindowSize+1]();
            else
              freq_array=result->second;


            window_iter = top_window_iter;
            wsize=top_wsize;

            while(window_iter!=trace_list_front_end){
              wsize+=window_iter->partial_trace_list.size();
              //printf("wsize is %d\n",wsize);
              freq_array[wsize]+=window_iter->wcount;
              //freq_array[wsize]++;
              //fprintf(stderr,"%d \t %d\n",wsize,freq_array[wsize]);
              window_iter++;
            }
          }

          trace_iter++;
        }

        top_wsize+=top_window_iter->partial_trace_list.size();
      	//fprintf(stderr,"\t\ttop_wsize reaching to maximum %d\n",top_wsize);
        freqs[threadno][FuncNum][top_wsize]+=top_window_iter->wcount;
        //freqs[threadno][FuncNum][top_wsize]++;
        top_window_iter++;

      } 

      //fprintf(stderr,"top_wsize is %d\n",top_wsize);
      //trace_list_front->clear();
      delete trace_list_front;
			removed_lists[threadno]++;
    }


  }
}

SampledWindow::SampledWindow(const SampledWindow & sw){
  wcount=sw.wcount;
  partial_trace_list = std::list<short>(sw.partial_trace_list);
}

SampledWindow::SampledWindow(){
  wcount=0;
  partial_trace_list = std::list<short>();
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
		std::vector<affEntry> conAffEntries(conAffEntriesArray,conAffEntriesArray+4);
		std::sort(conAffEntries.begin(), conAffEntries.end(), affEntryCmp);

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

  profilePath = getenv("ABC_PROF_PATH");

}
extern "C" void set_bb_count_for_fid(short fid, short bbid){}




/* llvm_start_basic_block_tracing - This is the main entry point of the basic
 * block tracing library.  It is responsible for setting up the atexit
 * handler and allocating the trace buffer.
 */
extern "C" int start_call_site_tracing(short _totalFuncs) {

	flush_trace = false;
	profiling_switch = false;

  	pthread_create(&prof_switch_th,NULL,prof_switch_toggle, (void *) 0);
  
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
extern "C" int start_bb_call_site_tracing(short _totalFuncs){
	return start_call_site_tracing(_totalFuncs);
}


extern "C" void initialize_post_bb_count_data(){}

