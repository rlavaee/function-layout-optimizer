#include "affinity.hpp"
#include <stdio.h>
#include <algorithm>
#include <pthread.h>
#include <semaphore.h>
#include <boost/lockfree/queue.hpp>
#include <vector>
#include <climits>

int prevFunc=-2;
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


FILE * graphFile, * debugFile;

uint32_t * null_joint_freq = new uint32_t[maxWindowSize+1]();

sem_t affinity_sem;

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

extern "C" void record_function_exec(func_t FuncNum){
	if(prevFunc==-2)
		return;
  if(prevFunc==FuncNum)
    return;
  else
    prevFunc=FuncNum;

  //fprintf(traceFile,"%hd\n",FuncNum);
  uint32_t r=rand();
  bool sampled=false;
  if((r & sampleMask)==0){
    SampledWindow sw;
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
      commit_freq_updates(trace_list.back(),trace_list_size-1);
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
		wsize_t top_wsize = 0;
    if(trace_list.begin()!=func_window_it[FuncNum]){
      top_wsize = sequential_update_affinity(func_window_it[FuncNum]);
    }
    window_iter=func_window_it[FuncNum];
    window_iter->partial_trace_list.erase(func_trace_it[FuncNum]);

    if(window_iter->partial_trace_list.empty()){
      commit_freq_updates(*window_iter,top_wsize);
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

	
  FILE *layoutFile = fopen(get_versioned_filename("layout"),"w");  

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


  char affinitybase[80];
  strcpy(affinitybase,"layout.mws");
  strcat(affinitybase,to_string(maxWindowSize).c_str());

  FILE *affinityFile = fopen(get_versioned_filename(affinitybase),"w");  

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
        fscanf(graphFile,"%u ",&sfreq);
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
        fscanf(graphFile,"{%u} ",&jfreq);
        freq_array[wsize] +=jfreq;
      }
    }
    fclose(graphFile);
  }


  graphFile=fopen(graphFilePath,"w+");

  for(func_t i=0;i<totalFuncs;++i){
    fprintf(graphFile,"(%hd):",i);
    for(func_t wsize=1; wsize<=maxWindowSize;++wsize)
      fprintf(graphFile,"%u ",single_freqs[i][wsize]);
    fprintf(graphFile,"\n");
  }
  for(iter=joint_freqs->begin(); iter!=joint_freqs->end(); ++iter){
    fprintf(graphFile,"(%hd,%hd):",iter->first.first,iter->first.second);
    for(func_t wsize=1;wsize<=maxWindowSize;++wsize)
      fprintf(graphFile,"{%u} ",iter->second[wsize]);
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

	
  orderFile = fopen(get_versioned_filename("order"),"w");


	if(disjointSet::sets)
  	for(func_t i=0; i<totalFuncs; ++i){
			disjointSet::deallocate(i);
		}
  disjointSet::sets = new disjointSet *[totalFuncs];
  for(func_t i=0; i<totalFuncs; ++i){
  	disjointSet::init_new_set(i);
	}

  for(vector<funcpair_t>::iterator iter=all_affEntry_iters.begin(); iter!=all_affEntry_iters.end(); ++iter){
		if(iter!=all_affEntry_iters.begin() && affEntryCmp==&affEntry2DCmpLogStep){
			if(!print_when(*(--iter),*(++iter))){
				fclose(orderFile);
				assert(false);
			}
		}
    fprintf(orderFile,"(%d,%d)\t",iter->first,iter->second);
		uint32_t * jfreq = GetWithDef(joint_freqs,*iter,null_joint_freq);
		for(int wsize = 2; wsize <= maxWindowSize; ++wsize){
			int first_sfreq = (single_freqs[iter->first][wsize]>0)?(single_freqs[iter->first][wsize]):(-1);
			int second_sfreq = (single_freqs[iter->second][wsize]>0)?(single_freqs[iter->second][wsize]):(-1);
			fprintf(orderFile,"[%1.3f,%1.3f] ",(double)jfreq[wsize]/first_sfreq, (double)jfreq[wsize]/second_sfreq);
		}
		fprintf(orderFile,"\n");
		//if(disjointSet::get_min_index(iter->first)+disjointSet::get_min_index(iter->second) < 4){
    	disjointSet::mergeSets(iter->first, iter->second);
			fprintf(orderFile,"effected\n");
		//}
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

  //affEntryCmp=&affEntry1DCmp;
  //find_affinity_groups();
  //print_optimal_layout();

	//int maxWindowSizeArray[12]={2,4,6,8,10,12,14,20,25,30,35,40};
	
	//for(int i=0;i<12;++i){

	//maxWindowSize=maxWindowSizeArray[i];
		
  	affEntryCmp=&affEntryCountCmp;
  	find_affinity_groups();
 	 	print_optimal_layout();
	//}

/*
  affEntryCmp=&affEntry2DCmpLogStep;
  find_affinity_groups();
  print_optimal_layout();
*/
}


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




wsize_t sequential_update_affinity(list<SampledWindow>::iterator grown_list_end){

  wsize_t top_wsize=0;
  wsize_t wsize;
  top_window_iter = trace_list.begin();
  func_iter = top_window_iter->partial_trace_list.begin();

  func_t FuncNum= * func_iter;

  while(top_window_iter!= grown_list_end){

    
    func_iter = top_window_iter->partial_trace_list.begin();

    partial_trace_list_end = top_window_iter->partial_trace_list.end();
    while(func_iter != partial_trace_list_end){

      func_t oldFuncNum= * func_iter;
      if(oldFuncNum!=FuncNum){
        window_iter = top_window_iter;
        wsize=top_wsize;

        while(window_iter != grown_list_end){
          wsize+=window_iter->wsize;
          JointUpdateEntry jue(unordered_funcpair(FuncNum,oldFuncNum),wsize);
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
		
    top_wsize += top_window_iter->wsize;
		SingleUpdateEntry sue(FuncNum,top_wsize);
    top_window_iter->add_single_update_entry(sue);

    if(DEBUG>1){
      fprintf(debugFile,"################\n");
      fprintf(debugFile,"update single: %d[%d..]++\n",FuncNum,top_wsize);
    }

   	top_window_iter++;
	}
	
	return top_wsize;

} 

uint32_t * GetWithDef(JointFreqMap * m, const funcpair_t &key, uint32_t * defval) {
  JointFreqMap::const_iterator it = m->find( key );
  if ( it == m->end() ) {
    return defval;
  }
  else {
    return it->second;
  }
}




bool affEntry1DCmp(const funcpair_t &left_pair,const funcpair_t &right_pair){
	uint32_t * jointFreq_left = GetWithDef(joint_freqs, left_pair, null_joint_freq);
  uint32_t * jointFreq_right = GetWithDef(joint_freqs, right_pair, null_joint_freq);

  int left_pair_val, right_pair_val;

  float rel_freq_threshold=2.0;
    for(short wsize=2;wsize<=maxWindowSize;++wsize){
      	if((rel_freq_threshold*(jointFreq_left[wsize]) > single_freqs[left_pair.first][wsize]) && 
          	(rel_freq_threshold*(jointFreq_left[wsize]) > single_freqs[left_pair.second][wsize]))
        	left_pair_val = 1;
				else
					left_pair_val = -1;

      	if((rel_freq_threshold*(jointFreq_right[wsize]) > single_freqs[right_pair.first][wsize]) && 
          	(rel_freq_threshold*(jointFreq_right[wsize]) > single_freqs[right_pair.second][wsize]))
					right_pair_val = 1;
				else
					right_pair_val = -1;
	  
				if(left_pair.first != right_pair.first)
    			return (left_pair.first > right_pair.first);
		}

	if(left_pair.first!=right_pair.first)
		return left_pair.first > right_pair.first;

  return left_pair.second > right_pair.second;

}

bool affEntryCountCmp(const funcpair_t &left_pair, const funcpair_t &right_pair){
  uint32_t * jointFreq_left = GetWithDef(joint_freqs, left_pair, null_joint_freq);
  uint32_t * jointFreq_right = GetWithDef(joint_freqs, right_pair, null_joint_freq);

	uint32_t jointFreq_left_total=0;
	uint32_t jointFreq_right_total=0;
	for(short wsize=2; wsize<=maxWindowSize;++wsize){
		jointFreq_left_total+=jointFreq_left[wsize];
		jointFreq_right_total+=jointFreq_right[wsize];
	}

	if(jointFreq_left_total > jointFreq_right_total)
		return true;

	if(jointFreq_left_total < jointFreq_right_total)
		return true;

	if(left_pair.first!=right_pair.first)
		return left_pair.first > right_pair.first;
  return left_pair.second > right_pair.second;
}

bool affEntry2DCmpConstantStep(const funcpair_t &left_pair, const funcpair_t &right_pair){
  uint32_t * jointFreq_left = GetWithDef(joint_freqs, left_pair, null_joint_freq);
  uint32_t * jointFreq_right = GetWithDef(joint_freqs, right_pair, null_joint_freq);

  int left_pair_val, right_pair_val;

	int freqlevel;
	for(freqlevel = maxFreqLevel; freqlevel > 0; --freqlevel){
		left_pair_val = right_pair_val = -1;
	 	for(short wsize=2;wsize<=maxWindowSize;++wsize){
	 		if(left_pair_val==-1){
      	if((maxFreqLevel*(jointFreq_left[wsize]) > freqlevel*single_freqs[left_pair.first][wsize]) && 
          	(maxFreqLevel*(jointFreq_left[wsize]) > freqlevel*single_freqs[left_pair.second][wsize]))
        	left_pair_val = 1;
				else
					left_pair_val = -1;
			}

			if(right_pair_val==-1){
      	if((maxFreqLevel*(jointFreq_right[wsize]) > freqlevel*single_freqs[right_pair.first][wsize]) && 
          	(maxFreqLevel*(jointFreq_right[wsize]) > freqlevel*single_freqs[right_pair.second][wsize]))
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

bool affEntry2DCmpLogStep(const funcpair_t &left_pair, const funcpair_t &right_pair){
  uint32_t * jointFreq_left = GetWithDef(joint_freqs, left_pair, null_joint_freq);
  uint32_t * jointFreq_right = GetWithDef(joint_freqs, right_pair, null_joint_freq);

  int left_pair_val, right_pair_val;
	int freqlevel,top_freqlevel;
	int top_wsize,wsize;
	for(top_freqlevel = 2; top_freqlevel < maxFreqLevel; ++top_freqlevel){
		for(top_wsize=2 ; top_wsize< maxWindowSize*2; top_wsize<<=1){
			for(wsize=(top_wsize >> 1)+1; wsize<=min(top_wsize,int(maxWindowSize)); ++wsize){
				for(freqlevel= maxWindowSize/top_wsize-1; freqlevel >= 0; freqlevel--){
					int right_mult = maxWindowSize/top_wsize+(top_freqlevel-1)*freqlevel;
					int left_mult = (maxWindowSize/top_wsize)*top_freqlevel;

      			if((left_mult*(jointFreq_left[wsize]) > right_mult*single_freqs[left_pair.first][wsize]) && 
          		(left_mult*(jointFreq_left[wsize]) > right_mult*single_freqs[left_pair.second][wsize]))
        		left_pair_val = 1;
						else
							left_pair_val = -1;

      			if((left_mult*(jointFreq_right[wsize]) > right_mult*single_freqs[right_pair.first][wsize]) && 
          		(left_mult*(jointFreq_right[wsize]) > right_mult*single_freqs[right_pair.second][wsize]))
						right_pair_val = 1;
					else
						right_pair_val = -1;
				if(left_pair_val != right_pair_val)
						return (left_pair_val > right_pair_val);
				}
			}
		
			}
		}


	if(left_pair.first!=right_pair.first)
		return left_pair.first > right_pair.first;
  return left_pair.second > right_pair.second;

}

bool print_when(const funcpair_t &left_pair, const funcpair_t &right_pair){
  uint32_t * jointFreq_left = GetWithDef(joint_freqs, left_pair, null_joint_freq);
  uint32_t * jointFreq_right = GetWithDef(joint_freqs, right_pair, null_joint_freq);

  int left_pair_val, right_pair_val;
	int freqlevel,top_freqlevel;
	int top_wsize,wsize;
	for(top_freqlevel = 2; top_freqlevel < maxFreqLevel; ++top_freqlevel){
		for(top_wsize=2 ; top_wsize< maxWindowSize*2; top_wsize<<=1){
			for(wsize=(top_wsize >> 1)+1; wsize<=min(top_wsize,int(maxWindowSize)); ++wsize){
				for(freqlevel= maxWindowSize/top_wsize-1; freqlevel >= 0; freqlevel--){
					int right_mult = maxWindowSize/top_wsize+(top_freqlevel-1)*freqlevel;
					int left_mult = (maxWindowSize/top_wsize)*top_freqlevel;

      			if((left_mult*(jointFreq_left[wsize]) > right_mult*single_freqs[left_pair.first][wsize]) && 
          		(left_mult*(jointFreq_left[wsize]) > right_mult*single_freqs[left_pair.second][wsize]))
        		left_pair_val = 1;
						else
							left_pair_val = -1;

      			if((left_mult*(jointFreq_right[wsize]) > right_mult*single_freqs[right_pair.first][wsize]) && 
          		(left_mult*(jointFreq_right[wsize]) > right_mult*single_freqs[right_pair.second][wsize]))
						right_pair_val = 1;
					else
						right_pair_val = -1;

				if(left_pair_val != right_pair_val){
						fprintf(orderFile,"top_freqlevel:%d\ttop_wsize:%d\twsize:%d\tfreqlevel:%d\n",top_freqlevel,top_wsize,wsize,freqlevel);
						return (left_pair_val > right_pair_val);
				}
				}
			}
		
			}
		}


	if(left_pair.first!=right_pair.first)
		return left_pair.first > right_pair.first;
  return left_pair.second > right_pair.second;

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
