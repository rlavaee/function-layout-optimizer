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

				//print_trace(&trace_list);


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

/*
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

if(nconsumers< MAXTHREADS && ((nconsumers == 0) || ((added_lists - sum_removed)*maxWindowSize > memoryLimit))){
removed_lists[nconsumers]=0;
affEntries[nconsumers]=new JointFreqMap();
freqs[nconsumers]=new int * [totalFuncs];

for(short i=0;i<totalFuncs;++i)
freqs[nconsumers][i]=new int[maxWindowSize+1]();

pthread_create(&consumers[nconsumers],NULL,update_affinity,(void *)nconsumers );
//fprintf(stderr,"Consumer thread %d created.\n",nconsumers);
nconsumers++;
}
}
}

struct AffinityToIntSerializer {
bool operator()(FILE * fp, const pair<const affEntry, int>& value) const{
if((fwrite(&value.first.bb1, sizeof(value.first.bb1), 1, fp) != 1) || (fwrite(&value.first.bb2, sizeof(value.first.bb2), 1, fp)!=1) )
return false;
if(fwrite(&value.second, sizeof(value.second), 1, fp) != 1)
return false;
return true;
}

bool operator()(FILE* fp, pair<const affEntry, int>* value)const{
if(fread(const_cast<int*>(&value->first.bb1), sizeof(value->first.bb1), 1, fp) != 1)
return false;
if(fread(const_cast<int*>(&value->first.bb2), sizeof(value->first.bb2), 1, fp) != 1)
return false;
if(fread(const_cast<int*>(&value->second), sizeof(value->second), 1, fp) != 1)
return false;
}
};*/

/*
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
*/
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

				//affEntry empty_entry(-1,-1);

				//affEntries = new JointFreqMap();
				//unordered_map <const affEntry, int *, affEntry_hash, eqAffEntry>();

				//  affEntries->set_empty_key(empty_entry);


				//for(i=0;i<totalFuncs;++i)
				//  freqs[i]=new int[maxWindowSize+1]();

				//sem_init(&affinity_sem,0,1);
				//sem_init(&balanced_sem,0,1);

				//pthread_create(&master, NULL, (void*(*)(void *))add_threads, (void *)0);
				//pthread_create(&update_affinity_thread,NULL,(void*(*)(void *))update_affinity, (void *)0);
}

/*
	 void join_all_consumers(){
	 single_freqs = new int * [totalFuncs];
	 for(short i=0;i<totalFuncs; ++i)
	 single_freqs[i]=new int [maxWindowSize+1]();
	 joint_freqs=new JointFreqMap();

	 for(int thno=0; thno<nconsumers; ++thno){
//fprintf(stderr,"joining consumers %d\n",thno);
for(JointFreqMap::iterator iter = affEntries[thno]->begin(); iter!=affEntries[thno]->end(); ++iter){
//fprintf(stderr,"They got another one %d %d\n",iter->first.first,iter->first.second);
JointFreqMap::iterator result=joint_freqs->find(iter->first);
int * freq_array;
if(result==joint_freqs->end())
(*joint_freqs)[iter->first]= freq_array=new int[maxWindowSize+1]();
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
single_freqs[i][wsize]+=freqs[thno][i][wsize];


}


}
*/
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
				/*	
						for(vector<JointFreqMap::iterator >::iterator iter=all_affEntry_iters.begin(); iter!=all_affEntry_iters.end(); ++iter){
						fprintf(stderr,"iter is %hd %hd %x\n", (*iter)->first.first, (*iter)->first.second, (*iter)->second);
						}*/ 

				sort(all_affEntry_iters.begin(),all_affEntry_iters.end(),affEntryCmp);


				disjointSet::sets = new disjointSet *[totalFuncs];
				for(short i=0; i<totalFuncs; ++i)
								disjointSet::init_new_set(i);

				for(vector<shortpair>::iterator iter=all_affEntry_iters.begin(); iter!=all_affEntry_iters.end(); ++iter){
								disjointSet::mergeSets(iter->first, iter->second);
								//fprintf(stderr,"sets %d and %d for freqlevel=%d, wsize=%d, hlevel=%d\n",iter->first,iter->bb2,freqlevel,wsize,hlevel);
				} 

}

/* Must be called at exit*/
void affinityAtExitHandler(){
				//free immature windows
				//list_remove_all(&window_list,NULL,free);
/*
				for(int i=0; i<nconsumers; ++i){
								list<SampledWindow> * null_trace_list = new list<SampledWindow>();
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
				*/

				//join_all_consumers();
				create_joint_freqs();
				//aggregate_affinity();
				//int maxWindowSizeArray[9]={2,3,6,9,12,15,20,25,30};

				//for(int i=0;i<9;++i){

				//maxWindowSize=maxWindowSizeArray[i];
				//affEntryCmp=&affEntry1DCmp;
				//find_affinity_groups();
				//print_optimal_layout();

				affEntryCmp=&affEntry2DCmp;
				find_affinity_groups();
				print_optimal_layout();
				//print_optimal_layouts();
				//}

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
																/*
																*/
																//printf("(%d,%d):%d\t%d\n",FuncNum2, FuncNum, top_wsize,freq_array[top_wsize]);

												}

												func_iter++;
								}

								window_iter++;

				} 


}

/*
void * update_affinity(void * threadno_void){
				long threadno = (long) threadno_void;
				//fprintf (stderr,"I am thread %d \n",threadno);

				list<SampledWindow>::iterator top_window_iter;
				list<SampledWindow>::iterator window_iter;
				list<short>::iterator trace_iter;


				list<SampledWindow>::iterator trace_list_front_end;
				list<short>::iterator partial_trace_list_end;
				list<SampledWindow> * trace_list_front; 

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

												unsigned top_wsize=0;

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
																top_wsize+=top_window_iter->wsize;
																trace_iter = top_window_iter->partial_trace_list.begin();


																partial_trace_list_end = top_window_iter->partial_trace_list.end();
																while(trace_iter != partial_trace_list_end){

																				short FuncNum2= *trace_iter;
																				freqs[threadno][FuncNum2][top_wsize]++;

																				if(FuncNum2!=FuncNum){
																								shortpair trace_entry(FuncNum2,FuncNum);
																								int * freq_array;

																								JointFreqMap::iterator  result=affEntries[threadno]->find(trace_entry);
																								if(result==affEntries[threadno]->end()){
																												freq_array=new int[maxWindowSize+1]();
																												int ** aggregation_array = new int[maxWindowSize+1][maxWindowSize+1]();
																												(*affEntries[threadno])[trace_entry]= pair(freq_array, aggregation_array);
																								}else
																												freq_array=result->second->first;


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

																								freq_array[top_wsize]++;
																								//printf("(%d,%d):%d\t%d\n",FuncNum2, FuncNum, top_wsize,freq_array[top_wsize]);

																				}

																				trace_iter++;
																}

																//top_wsize+=top_window_iter->partial_trace_list.size();
																//fprintf(stderr,"\t\ttop_wsize reaching to maximum %d\n",top_wsize);
																//freqs[threadno][FuncNum][top_wsize]+=top_window_iter->wcount;
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
	*/

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

