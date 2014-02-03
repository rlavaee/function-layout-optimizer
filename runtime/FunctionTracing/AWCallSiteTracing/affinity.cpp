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
//JointFreqRangeMap * joint_freq_ranges;
//list<SampledWindow> trace_list;
list<func_t> trace_list;

bool * contains_func;
list<func_t>::iterator * func_trace_it;
//list<SampledWindow>::iterator * func_window_it;
wsize_t trace_list_size;

//list<SampledWindow>::iterator window_iter;
//list<SampledWindow>::iterator top_window_iter;
list<func_t>::iterator func_iter;
list<func_t>::iterator partial_trace_list_end;
list<func_t> * last_window_trace_list;

//uint32_t *** single_freq_ranges; 

uint32_t * single_freqs;
uint16_t * age;
uint16_t ltime;

int prevFunc;
FILE * graphFile, * debugFile;

//uint32_t * null_joint_freq = new uint32_t[maxWindowSize+1]();
const char * version_str=".awabc";

sem_t affinity_sem;
bool misscount_cmp(const func_t &f1, const func_t &f2){
	if(single_freqs[f1] > single_freqs[f2])
		return true;
	
	if(single_freqs[f1] < single_freqs[f2])
		return false;
	
	return (f1 < f2);
}

/*
void create_single_freqs(){
  for(func_t f=0; f<totalFuncs; ++f)
    for(wsize_t i=1; i<= maxWindowSize; ++i)
      for(wsize_t j=i; j<= maxWindowSize; ++j)
        for(wsize_t k=i; k<=j; ++k)
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
      joint_freq_range_matrix = new uint32_t*[maxWindowSize+1];
      for(int i=1; i<=maxWindowSize; ++i)
        joint_freq_range_matrix[i]=new uint32_t[maxWindowSize+1]();

      (*joint_freq_ranges)[jue.func_pair]= joint_freq_range_matrix;
    }else
      joint_freq_range_matrix=result->second;

    joint_freq_range_matrix[jue.min_wsize][max_wsize]++;

    if(DEBUG>2){
      fprintf(debugFile,"&&&&&&&&&&&&&&&& commit\n");
      fprintf(debugFile,"(%d,%d)[%d..%d]++\n",jue.func_pair.first, jue.func_pair.second,jue.min_wsize,max_wsize);
    }
  }
}
*/

void increase_age(){
	for(func_iter = trace_list.begin(); func_iter!=trace_list.end(); ++func_iter)
		age[*func_iter] >>=  1;
}
extern "C" void record_function_exec(func_t FuncNum){
	age[FuncNum] |= 0x8000;
	ltime++;
	if(ltime==0x800){
		print_trace();
		increase_age();
		ltime=0;
	}
  if(prevFunc==FuncNum){
    return;
  }else
    prevFunc=FuncNum;

	trace_list.push_front(FuncNum);
	/*
  //fprintf(traceFile,"%hd\n",FuncNum);
  uint32_t r=rand();
  bool sampled=false;
  if((r & sampleMask)==0){
    SampledWindow sw;
    trace_list.push_front(sw);
    sampled=true;
  }
*/
/*
  if(trace_list_size!=0 || sampled)
    trace_list.front().partial_trace_list.push_front(FuncNum);
  else
    return;

  if(DEBUG>0){
    if(sampled)
      fprintf(debugFile,"new window\n");
    print_trace(&trace_list);
  }
*/
  if(!contains_func[FuncNum]){
    //trace_list.front().wsize++;

    if(trace_list_size >= maxWindowSize){
     // commit_freq_updates(trace_list.back(),trace_list_size-1);
      /*last_window_trace_list= &(trace_list.back().partial_trace_list);			

      while(!last_window_trace_list->empty()){
        func_t oldFuncNum=last_window_trace_list->front();
        contains_func[oldFuncNum]=false;
        last_window_trace_list->pop_front();
      }
      trace_list_size-=trace_list.back().wsize;
*/
			contains_func[trace_list.back()]=false;
			age[trace_list.back()]=0;
      trace_list.pop_back();
    }else
			trace_list_size++;

//    if(trace_list_size>0){
      sequential_update_affinity(trace_list.end());
      contains_func[FuncNum]=true;
      //func_window_it[FuncNum] = trace_list.begin();
      //func_trace_it[FuncNum] = trace_list.begin()->partial_trace_list.begin();
			func_trace_it[FuncNum] = trace_list.begin();
    //}

  }else{
    //trace_list.front().wsize++;
    //func_window_it[FuncNum]->wsize--;
		//wsize_t top_wsize = 0;
		/*
    if(trace_list.begin()!=func_window_it[FuncNum]){
      top_wsize = sequential_update_affinity(func_window_it[FuncNum]);
    }*/
    //window_iter=func_window_it[FuncNum];
    //window_iter->partial_trace_list.erase(func_trace_it[FuncNum]);

    //if(window_iter->partial_trace_list.empty()){
      //commit_freq_updates(*window_iter,top_wsize);
			//sequential_update_affinity(func_trace_it[FuncNum]);
      trace_list.erase(func_trace_it[FuncNum]);
    //}

    //func_window_it[FuncNum] = trace_list.begin();
    func_trace_it[FuncNum] = trace_list.begin();
		//->partial_trace_list.begin();	
  }


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

  //srand(time(NULL));
	srand(1);

  joint_freqs=new JointFreqMap();
  //joint_freq_ranges= new JointFreqRangeMap();

  single_freqs=new uint32_t [totalFuncs]();
	age = new uint16_t [totalFuncs]();
  //single_freq_ranges = new uint32_t** [totalFuncs];

/*
  for(func_t f=0;f<totalFuncs;++f){
    single_freqs[f]=new uint32_t[maxWindowSize+1]();
    single_freq_ranges[f]=new uint32_t*[maxWindowSize+1];
    for(wsize_t i=1; i<=maxWindowSize; i++)
      single_freq_ranges[f][i]=new uint32_t[maxWindowSize+1]();
  }
*/
  contains_func = new bool [totalFuncs]();


  //func_window_it = new list<SampledWindow>::iterator [totalFuncs];
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
    uint32_t sfreq;
		uint64_t jfreq;
    for(func_t i=0;i<totalFuncs; ++i){
      fscanf(graphFile,"(%*hd):%u\n",&sfreq);
			single_freqs[i]+=sfreq;
    }
    while(fscanf(graphFile,"(%hd,%hd):%lu\n",&u1,&u2,&jfreq)!=EOF){
      funcpair_t entryToAdd=funcpair_t(u1,u2);
			(*joint_freqs)[entryToAdd]+=jfreq;
			/*
      uint32_t * freq_array=(*joint_freqs)[entryToAdd];
      if(freq_array==NULL){
        freq_array= new uint32_t[maxWindowSize+1]();
        (*joint_freqs)[entryToAdd]=freq_array;
      }
      //printf("(%hd,%hd)\n",u1,u2);
      for(func_t wsize=1; wsize<=maxWindowSize; ++wsize){
        fscanf(graphFile,"{%u} ",&jfreq);
        freq_array[wsize] +=jfreq;
      }*/

    }
    fclose(graphFile);
  }


  graphFile=fopen(graphFilePath,"w+");

  for(func_t i=0;i<totalFuncs;++i){
    fprintf(graphFile,"(%hd):%u\n",i,single_freqs[i]);
  }
  for(iter=joint_freqs->begin(); iter!=joint_freqs->end(); ++iter)
    fprintf(graphFile,"(%hd,%hd):%lu\n",iter->first.first,iter->first.second,iter->second);


  fclose(graphFile);
}

vector<Prefetcher> * all_saviors, * all_savees;
uint32_t current_save, opt_save;
bool * has_saved, * is_saved, * needs_savior;
vector<Prefetcher>::iterator * savior, * savee;
func_t * to_save;
func_t total_savable;


bool simple_cmp(uint32_t a, uint32_t b){ return (a>b);}
/*
uint32_t current_upbound(){
	//func_t to_save_rm = 0;
	uint32_t save_sum =0 ;
	for(func_t i=0;i<total_savable; ++i){
		func_t fid=to_save[i];
		if(!is_saved[fid] && needs_savior[fid]){
			vector<Prefetcher>::iterator savior_it, savior_it_end;
			for(savior_it=all_saviors[fid].begin(), savior_it_end = all_saviors[fid].end(); savior_it != savior_it_end; ++savior_it){
				if(!has_saved[savior_it->savior]){
					save_sum += savior_it->savecount;
					//to_save_rm++;
					break;
				}
			}
		}
	//}


	//vector<uint32_t> all_saviers_max_gains;
	//for(func_t fid=0;fid<total_savable; ++fid){	
		if(!has_saved[fid]){
			vector<Prefetcher>::iterator savee_it, savee_it_end;
			for(savee_it=all_savees[fid].begin(), savee_it_end = all_savees[fid].end(); savee_it != savee_it_end; ++savee_it){
				if(!is_saved[savee_it->savee]){
					save_sum+= savee_it->savecount;
					//all_saviers_max_gains.push_back(savee_it->savecount);
					break;
				}
			}
		}
	}
	save_sum = save_sum+current_save;
	return save_sum/2+1;
}
*/

void save_miss_count(func_t f_i, func_t total_to_save){
	if(total_to_save == 0){
			//printf("better update:%u\n",current_save);
			opt_save = current_save;
			return;
	}
	vector<Prefetcher>::iterator savior_it, savior_it_end;
			
			for(savior_it = all_saviors[to_save[f_i]].begin(), savior_it_end = all_saviors[to_save[f_i]].end(); savior_it != savior_it_end ;savior_it++){
				if(!has_saved[savior_it->savior]){
					savior[to_save[f_i]]=savior_it;
					savee[savior_it->savior]=savior_it;
					current_save+=savior_it->savecount;
					has_saved[savior_it->savior]=true;
					is_saved[to_save[f_i]]=true;
				//	uint32_t upbound;
					//if(!opt_save_set){ //(total_to_save*2 > total_savable) || (total_to_save%2 == 0))
						return save_miss_count(f_i+1,total_to_save-1);
					/*}else if((upbound =current_upbound()) > opt_save){
						save_miss_count(f_i+1,total_to_save-1);
					}else{
						printf("Pruned at %hu: current: %u, upbound:%u, opt:%u\n",f_i,current_save,upbound,opt_save);
					}
					is_saved[to_save[f_i]]=false;
					has_saved[savior_it->savior]=false;
					current_save-=savior_it->savecount;
					*/
				}
			}
			
			is_saved[to_save[f_i]]=true;
			needs_savior[to_save[f_i]]=false;
			return save_miss_count(f_i+1,total_to_save-1);
}

/* This function builds the affinity groups based on the affinity table 
   and prints out the results */
void find_affinity_groups(){

	savior = new vector<Prefetcher>::iterator [totalFuncs];
	savee = new vector<Prefetcher>::iterator [totalFuncs];

	is_saved = new bool[totalFuncs]();
	has_saved = new bool[totalFuncs]();
	needs_savior = new bool[totalFuncs];

	to_save = new func_t [totalFuncs];
	
	total_savable = 0;
	for(func_t i=0; i<totalFuncs; ++i){
		//printf("fid %d-> single_freqs: %d\n",i,single_freqs[i]);
		if(single_freqs[i] == 0){
			is_saved[i]=true;
			needs_savior[i]=false;
		}
		else{
			to_save[total_savable++]=i;
			needs_savior[i]=true;
		}
	}

	sort(to_save, to_save+total_savable, misscount_cmp);


	all_saviors = new vector<Prefetcher> [totalFuncs];
	all_savees = new vector<Prefetcher> [totalFuncs];
  for(JointFreqMap::iterator iter=joint_freqs->begin(); iter!=joint_freqs->end(); ++iter){
		if(needs_savior[iter->first.first]){
			Prefetcher p(iter->first.first,iter->first.second,single_freqs[iter->first.first],iter->second);
			all_saviors[iter->first.first].push_back(p);
			all_savees[iter->first.second].push_back(p);
		}
  }

	for(func_t i=0; i<totalFuncs; ++i){
			//printf("(savior[%hu]: before purify: %zu\n",i,all_saviors[i].size());
			all_saviors[i].erase(partition(all_saviors[i].begin(), all_saviors[i].end(), Prefetcher::is_significant),all_saviors[i].end());
			//printf("after purify: %zu\n",all_saviors[i].size());
			sort(all_saviors[i].begin(), all_saviors[i].end(), Prefetcher::prefetcher_cmp);
			
			//printf("savee[%hu]: before purify: %zu\n",i,all_savees[i].size());
			all_savees[i].erase(partition(all_savees[i].begin(), all_savees[i].end(), Prefetcher::is_significant),all_savees[i].end());
			//printf("after purify: %zu\n",all_savees[i].size());
			sort(all_savees[i].begin(), all_savees[i].end(), Prefetcher::prefetcher_cmp);
		}

		save_miss_count(0,total_savable);
		//printf("this the optimal:%d\n",opt_save);
}

void print_optimal_layout(){
  func_t * layout = new func_t[totalFuncs];	
	func_t count = 0;
	int * marked = new int[totalFuncs]();
	for(func_t fid=0;fid<totalFuncs; ++fid){
		func_t ffid=fid;
		while(needs_savior[ffid] && marked[ffid]==0){
			marked[ffid]=1;
			ffid = savior[ffid]->savior;
		}
		if(marked[ffid]!=2){
			layout[count++]=ffid;
			marked[ffid]=2;
			//printf("%hu ",ffid);
			while(has_saved[ffid]){
				ffid = savee[ffid]->savee;
				if(marked[ffid]!=2){
					layout[count++]=ffid;
					//printf("%hu ",ffid);
					marked[ffid]=2;
				}else
					break;
			}
			//printf("---------------\n");
		}
	}

	for(func_t fid=0; fid<totalFuncs; ++fid)
		if(!marked[fid]){
			marked[fid]=true;
			layout[count++]=fid;
		}
	
	assert(count == totalFuncs);

  char affinityFilePath[80];
  strcpy(affinityFilePath,"layout");
  strcat(affinityFilePath,version_str);
  FILE *layoutFile = fopen(affinityFilePath,"w");  

  for(int i=0;i<totalFuncs;++i){
    if(i%20==0)
      fprintf(layoutFile, "\n");
    fprintf(layoutFile, "%u ",layout[i]);
  }
  fclose(layoutFile);
}


/* Must be called at exit*/
void affinityAtExitHandler(){
	if(DEBUG>9)
		fclose(traceFile);
	wsize_t top_wsize=0;
	/*
	while(!trace_list.empty()){
		SampledWindow sw = trace_list.front();
		top_wsize += sw.wsize;
		//commit_freq_updates(sw,top_wsize);
		trace_list.pop_front();
	}
	*/
	if(DEBUG>0)
		fclose(debugFile);
  //create_joint_freqs();
  //create_single_freqs();
  aggregate_affinity();

  affEntryCmp=&affEntry2DCmp;
  find_affinity_groups();
  print_optimal_layout();

}
void print_trace(){
	for(func_iter = trace_list.begin(); func_iter!=trace_list.end(); ++func_iter)
		fprintf(debugFile,"(%hu: %xu) ",*func_iter,age[*func_iter]);
	
	fprintf(debugFile,"\n");
}

/*
void print_trace(list<SampledWindow> * tlist){
  list<SampledWindow>::iterator window_iter=tlist->begin();

  list<func_t>::iterator trace_iter;

  fprintf(debugFile,"---------------------------------------------\n");
  while(window_iter!=tlist->end()){
    trace_iter =  window_iter->partial_trace_list.begin();
    fprintf(debugFile,"size: %d\n",window_iter->wsize);


    while(trace_iter!=window_iter->partial_trace_list.end()){
      fprintf(debugFile,"%d ",*trace_iter);
      trace_iter++;
    }
    fprintf(debugFile,"\n");
    window_iter++;
  }
}
*/


void sequential_update_affinity(list<func_t>::iterator grown_list_end){

  //wsize_t top_wsize=0;
  wsize_t wsize=1;
  //top_window_iter = trace_list.begin();
  //func_iter = top_window_iter->partial_trace_list.begin();
	func_iter = trace_list.begin();

  func_t FuncNum= * func_iter;
	single_freqs[FuncNum]++;
	func_iter++;

	while(func_iter!=grown_list_end){
		(*joint_freqs)[funcpair_t(FuncNum,*func_iter)]+= age[*func_iter];
		++func_iter;
	}
/*
  //while(top_window_iter!= grown_list_end){

    
    //func_iter = top_window_iter->partial_trace_list.begin();

    //partial_trace_list_end = top_window_iter->partial_trace_list.end();
    //while(func_iter != partial_trace_list_end){

      //func_t oldFuncNum= * func_iter;
      if(oldFuncNum!=FuncNum){
        window_iter = top_window_iter;
        wsize=top_wsize;

        while(window_iter != grown_list_end){
          wsize+=window_iter->wsize;
					(*joint_freqs)[funcpair_t(FuncNum,oldFuncNum)]++;
					if(DEBUG>1){
						fprintf(debugFile,"****************\n");
						fprintf(debugFile,"update pair: (%d,%d)[%d..]++\n",oldFuncNum,FuncNum,wsize);
					}            
         	window_iter++;
        }
     	}

     	func_iter++;
   	}
		
    top_wsize += top_window_iter->wsize;
		*/
		/*
		SingleUpdateEntry sue(FuncNum,top_wsize);
    top_window_iter->add_single_update_entry(sue);
		
		single_freqs[FuncNum]++;
    if(DEBUG>1){
      fprintf(debugFile,"################\n");
      fprintf(debugFile,"update single: %d[%d..]++\n",FuncNum,top_wsize);
    }

   	top_window_iter++;
	}
	
	return top_wsize;
	*/

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

uint32_t GetWithDef(JointFreqMap * m, const funcpair_t &key, uint32_t defval) {
  JointFreqMap::const_iterator it = m->find( key );
  if ( it == m->end() ) {
    return defval;
  }
  else {
    return it->second;
  }
}

bool affEntry2DCmp(const funcpair_t &left_pair, const funcpair_t &right_pair){
  //uint32_t jointFreq_left = GetWithDef(joint_freqs, left_pair, null_joint_freq);
  //uint32_t jointFreq_right = GetWithDef(joint_freqs, right_pair, null_joint_freq);
	
	//if(funcpair_eq()(left_pair,right_pair))
	//	return (&left_pair > &right_pair);

	uint64_t left_pair_jf = (*joint_freqs)[left_pair];
	uint64_t left_pair_rev_jf = (*joint_freqs)[reverse_pair(left_pair)];
	uint64_t right_pair_jf = (*joint_freqs)[right_pair];
	uint64_t right_pair_rev_jf = (*joint_freqs)[reverse_pair(right_pair)];
	uint64_t total_left_pair = left_pair_jf + left_pair_rev_jf;
	uint64_t total_right_pair = right_pair_jf + right_pair_rev_jf;

	if(comparisonFile!=NULL){
		fprintf(comparisonFile, "(%hd,%hd):%lu",left_pair.first,left_pair.second,total_left_pair);
		fprintf(comparisonFile,(total_left_pair > total_right_pair)?(">"):("<"));
		fprintf(comparisonFile, " (%hd,%hd):%lu\n",right_pair.first,right_pair.second,total_right_pair);
	}

	if (total_left_pair < total_right_pair)
		return true;
	
	if (total_left_pair > total_right_pair)
		return false;

	if(left_pair.first < right_pair.first)
		return true;
	else
		return (left_pair.second < right_pair.second);


	//return (((float)(*joint_freqs)[left_pair])/single_freqs[left_pair.first] +((float)(*joint_freqs)[reverse_pair(left_pair)])/single_freqs[left_pair.second]) > (((float)(*joint_freqs)[right_pair])/single_freqs[right_pair.first]+((float)(*joint_freqs)[reverse_pair(right_pair)])/single_freqs[right_pair.second]);
	/*
  int left_pair_val, right_pair_val;

  short freqlevel;
  float rel_freq_threshold;
  for(freqlevel=0, rel_freq_threshold=1.0; freqlevel<maxFreqLevel; ++freqlevel, rel_freq_threshold+=5.0/maxFreqLevel){
    //for(short wsize=2;wsize<=maxWindowSize;++wsize){

      if((rel_freq_threshold*jointFreq_left[wsize] >= single_freqs[left_pair.first][wsize]) && 
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
    //}
    freqlevel++;
    rel_freq_threshold+=5.0/maxFreqLevel;
  }

  if(left_pair.first != right_pair.first)
    return (left_pair.first > right_pair.first);

  return left_pair.second > right_pair.second;
	*/

}




void disjointSet::mergeSets(disjointSet * set1, disjointSet* set2){

  disjointSet * merger = (set1->size()>=set2->size())?(set1):(set2);

  disjointSet * mergee = (set1->size()<set2->size())?(set1):(set2);


  funcpair_t frontMerger_backMergee = unordered_funcpair(merger->elements.front(), mergee->elements.back());
  funcpair_t backMerger_backMergee = unordered_funcpair(merger->elements.back(), mergee->elements.back());
  funcpair_t backMerger_frontMergee = unordered_funcpair (merger->elements.back(), mergee->elements.front());
  funcpair_t frontMerger_frontMergee = unordered_funcpair (merger->elements.front(), mergee->elements.front());
  funcpair_t conAffEntriesArray[4]={frontMerger_frontMergee, frontMerger_backMergee, backMerger_frontMergee, backMerger_backMergee};
  vector<funcpair_t> conAffEntries(conAffEntriesArray,conAffEntriesArray+4);
  sort(conAffEntries.begin(), conAffEntries.end(), affEntryCmp);

  assert(affEntryCmp(conAffEntries[0],conAffEntries[1]) || funcpair_eq()(conAffEntries[0],conAffEntries[1]));
  assert(affEntryCmp(conAffEntries[1],conAffEntries[2]) || funcpair_eq()(conAffEntries[1],conAffEntries[2]));
  assert(affEntryCmp(conAffEntries[2],conAffEntries[3]) || funcpair_eq()(conAffEntries[2],conAffEntries[3]));

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
