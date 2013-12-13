#include "affinity.hpp"
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <cstring>
#include <new>
//#include <queue>
#include <cstdio>
#include <pthread.h>
//#include <memory>
#include <semaphore.h>
#include <boost/lockfree/queue.hpp>
#include <sstream>
#include <string>

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


boost::lockfree::queue< std::list<SampledWindow> * > trace_list_queue (100);

pthread_t update_affinity_thread;


std::list<SampledWindow>::iterator tl_window_iter;
std::list<short>::iterator tl_trace_iter;

disjointSet *** sets;
int ** freqs;

short prevFunc;
FILE * graphFile, * debugFile;

const char * version_str=".abc";


//pthread_mutex_t trace_list_queue_lock = PTHREAD_MUTEX_INITIALIZER;

sem_t affinity_sem;
static int updatedWindowCount=0;


/*struct AffinityToIntSerializer {
  bool operator()(FILE * fp, const std::pair<const affEntry, int>& value) const{
  if((fwrite(&value.first.bb1, sizeof(value.first.bb1), 1, fp) != 1) || (fwrite(&value.first.bb2, sizeof(value.first.bb2), 1, fp)!=1) )
  return false;
  if(fwrite(&value.second, sizeof(value.second), 1, fp) != 1)
  return false;
  return true;
  }

  bool operator()(FILE* fp, std::pair<const affEntry, int>* value)const{
  if(fread(const_cast<int*>(&value->first.bb1), sizeof(value->first.bb1), 1, fp) != 1)
  return false;
  if(fread(const_cast<int*>(&value->first.bb2), sizeof(value->first.bb2), 1, fp) != 1)
  return false;
  if(fread(const_cast<int*>(&value->second), sizeof(value->second), 1, fp) != 1)
  return false;
  }
  };*/

int unitcmp(const void * left, const void * right){
  const int * ileft=(const int *) left;
  const int * iright=(const int *) right;
  int wsize=maxWindowSize;

  int freqlevel=maxFreqLevel-1;
  //fprintf(stderr,"%d %d %d %d %d\n",*ileft,*iright,find(&sets[freqlevel][wsize][hlevel][*ileft])->id,find(&sets[freqlevel][wsize][hlevel][*iright])->id);
  while(sets[freqlevel][wsize][*ileft].find()->id==sets[freqlevel][wsize][*iright].find()->id){
    //fprintf(stderr,"%d %d %d %d %d\n",freqlevel,wsize,hlevel,*ileft,*iright);
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
	//time_t  timev;
	//time(&timev);

	//std::stringstream ss;
	//ss << timev;
	//std::string ts = ss.str();
  char * affinityFilePath = (char *) malloc(strlen("layout")+strlen(version_str)+1);
  strcpy(affinityFilePath,"layout");
  strcat(affinityFilePath,version_str);
	//std::string affinityFilePath ="layout"+ts;

  FILE *affinityFile = fopen(affinityFilePath,"w");  

  for(i=0;i<totalUnits;++i){
    if(i%20==0)
      fprintf(affinityFile, "\n");
    fprintf(affinityFile, "%u ",layout[i]);
  }
  fclose(affinityFile);
}

/* The data allocation function (totalUnits need to be set before entering this function) */
void initialize_affinity_data(float _sampleRate, short _maxWindowSize, short _totalFuncs, short _maxFreqLevel){

  totalBBs=70000;
  totalFuncs=_totalFuncs;
  pLevel=FuncLevel;
  maxWindowSize=_maxWindowSize;
  sampleRate=_sampleRate;
  prevFunc=-1;
  maxFreqLevel=_maxFreqLevel;
  sampledWindows=0;

  trace_list_size=0;

  //debugFile=fopen("debug.txt","w");
  srand(time(NULL));

  short i,wsize;


  freqs = new int* [totalUnits];

  contains_func = new bool [totalUnits]();
  func_window_it = new std::list<SampledWindow>::iterator [totalUnits];
  func_trace_it = new std::list<short>::iterator  [totalUnits];

  //affEntry empty_entry(-1,-1);

  affEntries = new affinityHashMap();
  //unordered_map <const affEntry, int *, affEntry_hash, eqAffEntry>();

  //  affEntries->set_empty_key(empty_entry);


  for(i=0;i<totalUnits;++i)
    freqs[i]=new int[maxWindowSize+1]();

  sem_init(&affinity_sem,0,1);

  pthread_create(&update_affinity_thread,NULL,(void*(*)(void *))update_affinity, (void *)0);
}

/* This function builds the affinity groups based on the affinity table 
   and prints out the results */
void find_affinity_groups(){

  //printf("total units %d\n",totalUnits);
  affinityHashMap::iterator iter;
  int wsize,i;


  for(i=0;i<totalUnits;++i)
    for(wsize=2;wsize<=maxWindowSize;++wsize){
      freqs[i][wsize]+=freqs[i][wsize-1];
    }

  for(iter=affEntries->begin(); iter!=affEntries->end(); ++iter){
    int * freq_array= iter->second;	  
    for(wsize=2;wsize<=maxWindowSize;++wsize){
      freq_array[wsize]+=freq_array[wsize-1];
    }
  }




  char * graphFilePath=(char*) malloc(strlen("graph")+strlen(version_str)+1);
  strcpy(graphFilePath,"graph");
  strcat(graphFilePath,version_str);

  graphFile=fopen(graphFilePath,"r");
  int prev_sampledWindows=0;
  if(graphFile!=NULL){
    int u1,u2,freq;
    fscanf(graphFile,"%d",&prev_sampledWindows);
    while(fscanf(graphFile,"%d",&wsize)!=EOF){
      while(true){
        fscanf(graphFile,"%d",&u1);
        if(u1==-1)
          break;
        fscanf(graphFile,"%d",&freq);
        //fprintf(stderr,"%d %d is %d\n",wsize,u1,freq);
        //prev_final_freqs[wsize][u1]=freq;
        freqs[u1][wsize]+=freq;
      }
      while(true){
        fscanf(graphFile,"%d",&u1);
        //fprintf(stderr,"u1 is %d\n",u1);
        if(u1==-1)
          break;
        fscanf(graphFile,"%d %d",&u2,&freq);
        //fprintf(stderr,"%d %d %d\n",u1,u2,freq);
        affEntry entryToAdd(u1,u2);
        //prev_final_affEntries[wsize][entryToAdd]=freq;
        int * freq_array=(*affEntries)[entryToAdd];
        if(freq_array==NULL){
          freq_array= new int[maxWindowSize+1]();
          (*affEntries)[entryToAdd]=freq_array;
        }

        freq_array[wsize]+=freq;
      }
    }
    fclose(graphFile);
  }


  graphFile=fopen(graphFilePath,"w+");
  sampledWindows+=prev_sampledWindows;
  fprintf(graphFile,"%u\n",sampledWindows);

  for(wsize=1;wsize<=maxWindowSize;++wsize){

    fprintf(graphFile,"%u\n",wsize);
    for(i=0;i<totalUnits;++i)
      fprintf(graphFile,"%d %d\n",i,freqs[i][wsize]);
    fprintf(graphFile,"-1\n");

    for(iter=affEntries->begin(); iter!=affEntries->end(); ++iter){
      fprintf(graphFile,"%u %u %d\n",iter->first.first,
          iter->first.second,iter->second[wsize]);
    }

    fprintf(graphFile,"-1\n");
  }

  fclose(graphFile);

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

        if((rel_freq_threshold*(iter->second[wsize]) > freqs[iter->first.first][wsize]) && 
            (rel_freq_threshold*(iter->second[wsize]) > freqs[iter->first.second][wsize])){
          sets[freqlevel][wsize][iter->first.first].unionSet(&sets[freqlevel][wsize][iter->first.second]);
          //fprintf(stderr,"sets %d and %d for freqlevel=%d, wsize=%d, hlevel=%d\n",iter->first,iter->bb2,freqlevel,wsize,hlevel);
        }
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

  trace_list_queue.push(null_trace_list);
  sem_post(&affinity_sem);

  pthread_join(update_affinity_thread,NULL);


  find_affinity_groups();

  find_optimal_layout();

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
  std::list<SampledWindow>::iterator window_iter=tlist->begin();

  std::list<short>::iterator trace_iter;

  printf("trace list:\n");
  while(window_iter!=tlist->end()){
    trace_iter =  window_iter->partial_trace_list.begin();
    printf("windows: %d\n",window_iter->wcount);

    while(trace_iter!=window_iter->partial_trace_list.end()){
      printf("%d ",*trace_iter);
      trace_iter++;
    }
    printf("\n");
    window_iter++;
  }
  printf("---------------------------------------------\n");
}


/* Sampling function. This function samples windows of every length
   while moving over the trace. */


void sample_window(short FuncNum){

  //printf("trace_list is:\n");
  //print_trace(&trace_list);
	
	if(prevFunc==FuncNum)
		return;
	else
		prevFunc=FuncNum;

  int r=rand()%10000000;
	bool sampled=false;
  if(r < sampleRate*10000000){
    sampledWindows++;
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





  std::list<SampledWindow> * trace_list_to_update = NULL;


  if(!contains_func[FuncNum]){
    //printf("found FuncNum:%d in trace\n",FuncNum);
    //print_trace(&trace_list);
    trace_list_size++;
    if(trace_list_size > maxWindowSize){
      std::list<short> * last_window_trace_list= &trace_list.back().partial_trace_list;
      trace_list_size-=last_window_trace_list->size();
      while(!last_window_trace_list->empty()){
        contains_func[last_window_trace_list->front()]=false;
        last_window_trace_list->pop_front();
      }
      //last_window_iter->partial_trace_list.clear();

      trace_list.pop_back();
    }
    
    trace_list_to_update = new std::list<SampledWindow>(trace_list);	

    if(trace_list_size!=0){
      contains_func[FuncNum]=true;
      func_window_it[FuncNum] = trace_list.begin();
      func_trace_it[FuncNum] = trace_list.begin()->partial_trace_list.begin();
    }
  }else{
    trace_list_to_update = new std::list<SampledWindow>();	
    tl_window_iter=trace_list.begin();
    while(tl_window_iter != func_window_it[FuncNum]){
      trace_list_to_update->push_back(*tl_window_iter);
      tl_window_iter++;
    }

    tl_window_iter->partial_trace_list.erase(func_trace_it[FuncNum]);

    if(tl_window_iter->partial_trace_list.empty()){
      int temp_wcount=tl_window_iter->wcount;
      tl_window_iter--;
      tl_window_iter->wcount+=temp_wcount;
      tl_window_iter++;
      trace_list.erase(tl_window_iter);
    }
    func_window_it[FuncNum] = trace_list.begin();
    func_trace_it[FuncNum] = trace_list.begin()->partial_trace_list.begin();

  }


  if(!trace_list_to_update->empty()){
    trace_list_queue.push(trace_list_to_update);
    sem_post(&affinity_sem);
  }else
    delete trace_list_to_update;

  //update_affinity();

}

void update_affinity(){

  std::list<SampledWindow>::iterator top_window_iter;
  std::list<SampledWindow>::iterator window_iter;
  std::list<short>::iterator trace_iter;

  
  std::list<SampledWindow>::iterator trace_list_front_end;
  std::list<short>::iterator partial_trace_list_end;

  while(true){
    //printf("queue empty waiting\n");
    sem_wait(&affinity_sem);

    while(!trace_list_queue.empty()){

      unsigned top_wsize,wsize;
      top_wsize=0;

      std::list<SampledWindow> * trace_list_front; 
      //pthread_mutex_lock(&trace_list_queue_lock);
      trace_list_queue.pop(trace_list_front);
      //printf("poped this from queue:\n");
      //print_trace(trace_list_front);
      //pthread_mutex_unlock(&trace_list_queue_lock);
      //printf("trace_list_fonrt is:\n");


      top_window_iter = trace_list_front->begin();
      trace_iter = top_window_iter->partial_trace_list.begin();


      short FuncNum= *trace_iter;

      if(FuncNum==-1)
        return;




      trace_list_front_end = trace_list_front->end();

      while(top_window_iter!= trace_list_front_end){
        trace_iter = top_window_iter->partial_trace_list.begin();

          
        partial_trace_list_end = top_window_iter->partial_trace_list.end();
        while(trace_iter != partial_trace_list_end){

          short FuncNum2= *trace_iter;
          
          if(FuncNum2!=FuncNum){
            affEntry trace_entry(FuncNum, FuncNum2);
            int * freq_array;

            affinityHashMap::iterator  result=affEntries->find(trace_entry);
            if(result==affEntries->end())
              (*affEntries)[trace_entry]= freq_array=new int[maxWindowSize+1]();
            else
              freq_array=result->second;


            window_iter = top_window_iter;
            wsize=top_wsize;

            while(window_iter!=trace_list_front_end){
              wsize+=window_iter->partial_trace_list.size();
              //printf("wsize is %d\n",wsize);
              freq_array[wsize]+=window_iter->wcount;
              //fprintf(stderr,"%d \t %d\n",window_iter->wsize,freq_array[window_iter->wsize]);
              window_iter++;
            }
          }

          trace_iter++;
        }

        top_wsize+=top_window_iter->partial_trace_list.size();
        //printf("top_wsize is %d\n",top_wsize);
        freqs[FuncNum][top_wsize]+=top_window_iter->wcount;
        top_window_iter++;

      } 

      trace_list_front->clear();
      delete trace_list_front;
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



affEntry::affEntry(short _first, short _second){
  if(_first<_second){
    first=_first;
    second=_second;
  }else{
    first=_second;
    second=_first;
  }
}

affEntry::affEntry(){}

bool eqAffEntry::operator()(affEntry const& entry1, affEntry const& entry2) const{
  return ((entry1.first == entry2.first) && (entry1.second == entry2.second));
}



size_t affEntry_hash::operator()(affEntry const& entry)const{
  //return MurmurHash2(&entry,sizeof(entry),5381);
  return entry.first*5381+entry.second;
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
  //fprintf(stderr,"%x %d\n",set,set->id);
  if(this->parent!=this){
    this->parent=this->parent->find();
  }
  return this->parent;
}

