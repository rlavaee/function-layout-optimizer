#include "affinity.hpp"
#include <stdio.h>
#include <algorithm>
#include <pthread.h>
#include <semaphore.h>
#include <boost/lockfree/queue.hpp>
#include <string.h>
#include <vector>
#include <atomic>
#include <fstream>
#include <iostream>
#include <linux/unistd.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <serialize.h>
#define MAXTHREADS 100

int MaxDist = 10000;

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
	if(prof_th.load()==cur_pid)
		return true;
	pid_t free_th = -1;
	bool result= prof_th.compare_exchange_strong(free_th,cur_pid);
	//std::cerr << "cur_pid is: " << cur_pid << " and prof_th is: " << prof_th.load() << "\n";
	return result;
}

void * prof_switch_toggle(void *){
	while(true){
		usleep(40000);
		pthread_mutex_lock(&switch_mutex);
		profiling_switch = true;
		//fprintf(stderr,"for this period prof_th was: %d\n",prof_th.load());
		prof_th.store(-1);
		pthread_mutex_unlock(&switch_mutex);
		usleep(100000);
		pthread_mutex_lock(&switch_mutex);
		profiling_switch = false;
		flush_trace = true;
		pthread_mutex_unlock(&switch_mutex);

	}
}

const char * profilePath = NULL;


func_t prevFunc(0,-2);
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

JointFreqMap joint_freqs[MAXTHREADS];
JointFreqMap sum_joint_freqs;
SingleFreqMap single_freqs[MAXTHREADS];
SingleFreqMap sum_single_freqs;

ObjectNameMap object_names;
std::unordered_map<const char *, short> func_count;

std::list<SampledWindow> trace_list;

vector<uint32_t>  null_joint_freq;
std::unordered_map <func_t, bool, func_hash> contains_func;
std::unordered_map <func_t, std::list<func_t>::iterator, func_hash> func_trace_it;
std::unordered_map <func_t, std::list<SampledWindow>::iterator, func_hash> func_window_it;
int trace_list_size;

boost::lockfree::queue< std::list<SampledWindow> * > trace_list_queue (100);

//pthread_t update_affinity_thread;


std::list<SampledWindow>::iterator tl_window_iter;
std::list<func_t>::iterator tl_trace_iter;

//disjointSet *** sets;




//pthread_mutex_t trace_list_queue_lock = PTHREAD_MUTEX_INITIALIZER;

sem_t affinity_sem;


std::vector<uint32_t> GetWithDef(JointFreqMap &m, const funcPair_t &key, const std::vector<uint32_t>& defval) {
	JointFreqMap::const_iterator it = m.find( key );
	if ( it == m.end() ) {
		//cerr << "returning null\n";
		return defval;
	}
	else {
		return it->second;
	}
}


void push_into_update_queue (std::list<SampledWindow> * trace_list_to_update){
	trace_list_queue.push(trace_list_to_update);
	added_lists++;
	sem_post(&affinity_sem);
}
//using google::sparse_hash_map;
//using google::sparse_hash_set;
//using std::tr1::hash;


inline void record_execution(func_t rec){


	if(flush_trace){
		if(DEBUG>0)
			print_trace(&trace_list);
		while(!trace_list.empty()){
			std::list<func_t> * last_window_trace_list= &trace_list.back().partial_trace_list;
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

	//std::cerr << gettid() << ": recording (" << rec.first << "," << rec.second << ") \n";

	if(prevFunc==func_t(0,-2))
		return;
	if(prevFunc==rec)
		return;
	else
		prevFunc=rec;

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
		trace_list.front().partial_trace_list.push_front(rec);
	else
		return;

	if(DEBUG>0){
		if(sampled)
			fprintf(debugFile,"new window\n");
		print_trace(&trace_list);
	}


	if(!contains_func[rec]){
		//printf("found rec:%d in trace\n",rec);
		//print_trace(&trace_list);
		trace_list_size++;
		if(trace_list_size > mws){
			std::list<func_t> * last_window_trace_list= &trace_list.back().partial_trace_list;
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
			contains_func[rec]=true;
			func_window_it[rec] = trace_list.begin();
			func_trace_it[rec] = trace_list.begin()->partial_trace_list.begin();

			std::list<SampledWindow> * trace_list_to_update = new std::list<SampledWindow>(trace_list);	
			//printf("a new update window list\n");
			//print_trace(trace_list_to_update);
			push_into_update_queue(trace_list_to_update);
		}

	}else{
		if(trace_list.begin()!=func_window_it[rec]){
			std::list<SampledWindow> * trace_list_to_update = new std::list<SampledWindow>(trace_list.begin(),func_window_it[rec]);	
			//printf("a new update window list\n");
			//print_trace(trace_list_to_update);
			push_into_update_queue(trace_list_to_update);
		}
		/*tl_window_iter=trace_list.begin();
			while(tl_window_iter != func_window_it[rec]){
			trace_list_to_update->push_back(*tl_window_iter);
			tl_window_iter++;
			}*/

		tl_window_iter = func_window_it[rec];
		tl_window_iter->partial_trace_list.erase(func_trace_it[rec]);

		if(tl_window_iter->partial_trace_list.empty()){
			uint16_t temp_wcount=tl_window_iter->wcount;
			tl_window_iter--;
			tl_window_iter->wcount+=temp_wcount;
			tl_window_iter++;
			trace_list.erase(tl_window_iter);
		}
		func_window_it[rec] = trace_list.begin();
		func_trace_it[rec] = trace_list.begin()->partial_trace_list.begin();	

	}

}

extern "C" void  record_function_exec(const char * str, short fid){
	pthread_mutex_lock(&switch_mutex);
	record_execution(func_t(str,fid));
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

		if((nconsumers == 0) || ((added_lists - sum_removed)*mws > memoryLimit)){
			removed_lists[nconsumers]=0;
			pthread_create(&consumers[nconsumers],NULL,update_affinity,(void *)nconsumers );
			//fprintf(stderr,"Consumer thread %d created.\n",nconsumers);
			nconsumers++;
		}
	}
}

void emit_graphFile(){



	char * graphFilePath=(char*) malloc(80);

	strcpy(graphFilePath,"");
	if(profilePath!=NULL){
		strcat(graphFilePath,profilePath);
		strcat(graphFilePath,"/");
	}
	strcat(graphFilePath,"graph");

	strcat(graphFilePath,version_str);

	std::filebuf graph_buf;
	graph_buf.open (graphFilePath,std::ios::out);

	OutputStream graph_os(&graph_buf);
	serialize(object_names,graph_os);
	serialize(sum_single_freqs,graph_os);
	serialize(sum_joint_freqs,graph_os);
	graph_buf.close();
}



void print_optimal_layout(){
	LayoutMap layout_map;
	for(auto& kv: func_count){
		const char * object_str = kv.first;
		//fprintf(stderr,"printing for str: %s length: %d\n",object_str,kv.second);
		short fcount = kv.second;
		vector <short> layout;
		layout.reserve(fcount);

		for(int i=0; i< fcount; ++i){
			func_t func(kv.first,i);
			if(disjointSet::sets[func]){
				disjointSet * thisSet=disjointSet::sets[func];
				for(deque<func_t>::iterator it=thisSet->elements.begin(), it_end=thisSet->elements.end()
						; it!=it_end ; ++it){
					layout.push_back(it->second);
					//fprintf(stderr,"pushing: %d\n",it->second);

					disjointSet::sets[*it]=0;
				}
				thisSet->elements.clear();
				delete thisSet;
			}
		}
		layout_map[object_names[object_str]] = layout;
	}

		char affinityFilePath[80];

		strcpy(affinityFilePath,"");
		if(profilePath!=NULL){
			strcat(affinityFilePath,profilePath);
			strcat(affinityFilePath,"/");
		}
		//strcat(affinityFilePath,object_str);
		strcat(affinityFilePath,"layout");
		strcat(affinityFilePath,version_str);

		std::filebuf layout_buf;
		layout_buf.open (affinityFilePath,std::ios::out);

		OutputStream layout_os(&layout_buf);

		serialize(layout_map,layout_os);


}



void join_all_consumers(){

	for(int thno=0; thno<nconsumers; ++thno){
		for(JointFreqMap::iterator iter = joint_freqs[thno].begin(); iter!=joint_freqs[thno].end(); ++iter){

			auto res = sum_joint_freqs.emplace(std::piecewise_construct,
					std::forward_as_tuple(iter->first),
					std::forward_as_tuple(mws,0));

			std::transform (iter->second.begin(), iter->second.end(), res.first->second.begin(), res.first->second.begin(), std::plus<uint32_t>());

		}
		joint_freqs[thno].clear();


		for(SingleFreqMap::iterator iter = single_freqs[thno].begin(); iter!=single_freqs[thno].end(); ++iter){
			//fprintf(stderr,"They got another one %d %d\n",iter->first.first,iter->first.second);

			auto res = sum_single_freqs.emplace(std::piecewise_construct,
					std::forward_as_tuple(iter->first),
					std::forward_as_tuple(mws,0));

			std::transform (iter->second.begin(), iter->second.end(), res.first->second.begin(), res.first->second.begin(), std::plus<uint32_t>());

		}
		single_freqs[thno].clear();

	}

}

void aggregate_affinity(){


	for(auto &kv: sum_single_freqs)
		for(short wsize=0;wsize<mws;++wsize)
			kv.second[wsize]+=kv.second[wsize-1];

	for(JointFreqMap::iterator iter=sum_joint_freqs.begin(); iter!=sum_joint_freqs.end(); ++iter){
		vector<uint32_t> freq_array= iter->second;	  
		for(short wsize=0;wsize<mws;++wsize){
			freq_array[wsize]+=freq_array[wsize-1];
		}
	}
}

void init_affinity_data(){

	char * graphFilePath=(char*) malloc(80);
	strcpy(graphFilePath,"");
	if(profilePath!=NULL){
		strcat(graphFilePath,profilePath);
		strcat(graphFilePath,"/");
	}

	strcat(graphFilePath,"graph");
	strcat(graphFilePath,version_str);

	std::filebuf graph_buf;
	graph_buf.open (graphFilePath,std::ios::in);

	if(graph_buf.is_open()){
		InputStream graph_is(&graph_buf);
		ObjectNameMap _object_names = deserialize<ObjectNameMap>(graph_is);
		//cerr << "SINGLE FREQS \n";
		SingleFreqMap _single_freqs = deserialize<SingleFreqMap>(graph_is);
		//cerr << "JOINT FREQS \n";
		JointFreqMap _joint_freqs = deserialize<JointFreqMap>(graph_is);

		boost::container::flat_map <const char *, const char *> ptr_map;



		for(auto& kv1: func_count)
			for(auto& kv2: _object_names)
				if(strcmp(kv2.second.c_str(),kv1.first)==0){
					//fprintf(stderr,"%p %p\t %s %s\n",kv2.first,kv1.first,kv2.second.c_str(),kv1.first);
					ptr_map[kv2.first] = kv1.first;
					break;
				}



		/*

		for(auto& kv3: _single_freqs){
			//fprintf(stderr,"single_freq item: %p %d %d\n",kv3.first.first,kv3.first.second,kv3.second.size());
			sum_single_freqs.emplace(std::piecewise_construct,
					std::forward_as_tuple(ptr_map[kv3.first.first],kv3.first.second),
					std::forward_as_tuple(kv3.second));
		}

		for(auto& kv4: _joint_freqs){

			sum_joint_freqs.emplace(std::piecewise_construct,
					std::forward_as_tuple(func_t(ptr_map[kv4.first.first.first],kv4.first.first.second), func_t(ptr_map[kv4.first.second.first],kv4.first.second.second)),
					std::forward_as_tuple(kv4.second));
		}
		*/
	}

}

/* This function builds the affinity groups based on the affinity table 
	 and prints out the results */
void find_affinity_groups(){

	std::vector<funcPair_t> all_pairs_iters;
	for(JointFreqMap::iterator iter=sum_joint_freqs.begin(); iter!=sum_joint_freqs.end(); ++iter){
		all_pairs_iters.push_back(iter->first);
	}

	for(auto& p: all_pairs_iters)
		//fprintf(stderr,"((%p,%d),(%p,%d))\n",p.first.first,p.first.second,p.second.first,p.second.second);

	std::sort(all_pairs_iters.begin(),all_pairs_iters.end(),funcPairCmp);

	disjointSet::sets.clear();

	for(auto& kv: func_count)
		for(short i=0;i<kv.second;++i){
			disjointSet::init_new_set(func_t(kv.first,i));
		}

	//orderFile= fopen("order.abc","w");

	for(std::vector<funcPair_t>::iterator iter=all_pairs_iters.begin(); iter!=all_pairs_iters.end(); ++iter){
		//fprintf(stderr,"joining ((%p,%d),(%p,%d))\n",iter->first.first,iter->first.second,iter->second.first,iter->second.second);
		int min_ind;
		if((min_ind = disjointSet::get_min_index(iter->first)+disjointSet::get_min_index(iter->second)) < MaxDist){
			disjointSet::mergeSets(iter->first, iter->second);
			//disjointSet::print_layout(iter->first);
			//fprintf(stderr,"effected: %d \n",min_ind);
		}
	} 

	//fclose(orderFile);

}

/* Must be called at exit*/
void affinityAtExitHandler(){
	//free immature windows
	//list_remove_all(&window_list,NULL,free);
	
	cerr << "pid: " << getpid() << "\ttid: " << gettid() << "exiting now\n";

	if(DEBUG>0)
			fclose(debugFile);

	for(int i=0; i<nconsumers; ++i){
		std::list<SampledWindow> * null_trace_list = new std::list<SampledWindow>();
		SampledWindow sw = SampledWindow();
		sw.partial_trace_list.push_front(func_t(0,-1));
		null_trace_list->push_front(sw);

		trace_list_queue.push(null_trace_list);
		sem_post(&affinity_sem);
	}

	for(int i=0; i<nconsumers; ++i){
		pthread_join(consumers[i],NULL);
	}

	added_lists=-1;

	pthread_join(master,NULL);

	//std::cerr << "now joining all consumers\n";

	join_all_consumers();
	aggregate_affinity();

	emit_graphFile();


	funcPairCmp=funcPairSumCmp;
	find_affinity_groups();
	print_optimal_layout();

	/*
		 int mwsArray[12]={2,4,6,8,10,12,14,20,25,30,35,40};

		 for(int i=0;i<12;++i){

		 mws=mwsArray[i];

		 funcPairCmp=funcPair_tSumCmp;
		 find_affinity_groups();
	//print_optimal_layout();
	print_optimal_layouts();
	}
	*/

}





void print_trace(list<SampledWindow> * tlist){
	list<SampledWindow>::iterator window_iter=tlist->begin();

	list<func_t>::iterator trace_iter;

	fprintf(debugFile,"---------------------------------------------\n");
	while(window_iter!=tlist->end()){
		trace_iter =  window_iter->partial_trace_list.begin();
		fprintf(debugFile,"count: %d\n",window_iter->wcount);


		while(trace_iter!=window_iter->partial_trace_list.end()){
			fprintf(debugFile,"(%p,%d) ",trace_iter->first,trace_iter->second);
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
	std::list<func_t>::iterator trace_iter;


	std::list<SampledWindow>::iterator trace_list_front_end;
	std::list<func_t>::iterator partial_trace_list_end;
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


			func_t rec= *trace_iter;

			if(rec==func_t(0,-1))
				pthread_exit(NULL);


			trace_list_front_end = trace_list_front->end();

			while(top_window_iter!= trace_list_front_end){
				trace_iter = top_window_iter->partial_trace_list.begin();


				partial_trace_list_end = top_window_iter->partial_trace_list.end();
				while(trace_iter != partial_trace_list_end){

					func_t oldRec= *trace_iter;

					if(rec.first==oldRec.first && rec.second!=oldRec.second){
						funcPair_t rec_pair(rec, oldRec);

						auto res = joint_freqs[threadno].emplace(std::piecewise_construct,
								std::forward_as_tuple(rec_pair),
								std::forward_as_tuple(mws,0));


						window_iter = top_window_iter;
						wsize=top_wsize;

						while(window_iter!=trace_list_front_end){
							wsize+=window_iter->partial_trace_list.size();
							//printf("wsize is %d\n",wsize);
							res.first->second[wsize-2]+=window_iter->wcount;
							//freq_array[wsize]++;
							//fprintf(stderr,"%d \t %d\n",wsize,freq_array[wsize]);
							window_iter++;
						}
					}

					trace_iter++;
				}

				top_wsize+=top_window_iter->partial_trace_list.size();
				//fprintf(stderr,"\t\ttop_wsize reaching to maximum %d\n",top_wsize);
				//single_freqs[threadno][rec][top_wsize-1]+=top_window_iter->wcount;

				auto s_res = single_freqs[threadno].emplace(std::piecewise_construct,
								std::forward_as_tuple(rec),
								std::forward_as_tuple(mws,0));
				s_res.first->second[top_wsize-1] += top_window_iter->wcount;


				//single_freqs[threadno][rec][top_wsize]++;
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
	partial_trace_list = std::list<func_t>(sw.partial_trace_list);
}

SampledWindow::SampledWindow(){
	wcount=0;
	partial_trace_list = std::list<func_t>();
}

SampledWindow::~SampledWindow(){}

double getAffinity(funcPair_t ae){
	vector<uint32_t> jointFreq = GetWithDef(sum_joint_freqs, ae, null_joint_freq);
	return (double) jointFreq[mws-1] / std::max(sum_single_freqs[ae.first][mws-1],sum_single_freqs[ae.second][mws-1]);
}

bool funcPairSumCmp(funcPair_t ae_left, funcPair_t ae_right){
	vector<uint32_t> jointFreq_left = GetWithDef(sum_joint_freqs, ae_left, null_joint_freq);
	vector<uint32_t> jointFreq_right = GetWithDef(sum_joint_freqs, ae_right, null_joint_freq);

	int ae_left_val = jointFreq_left[mws-1]*std::max(sum_single_freqs[ae_right.first][mws-1],sum_single_freqs[ae_right.second][mws-1]);
	int ae_right_val = jointFreq_right[mws-1]*std::max(sum_single_freqs[ae_left.first][mws-1],sum_single_freqs[ae_left.second][mws-1]);

	if(ae_left_val != ae_right_val)
		return (ae_left_val > ae_right_val);

	if(ae_left.first != ae_right.first)
		return (ae_left.first > ae_right.first);

	return ae_left.second > ae_right.second;

}

bool funcPair_tSizeCmp(funcPair_t ae_left, funcPair_t ae_right){
	vector<uint32_t> jointFreq_left = GetWithDef(sum_joint_freqs, ae_left, null_joint_freq);
	vector<uint32_t> jointFreq_right = GetWithDef(sum_joint_freqs, ae_right, null_joint_freq);

	int ae_left_val = jointFreq_left[mws]*std::max(sum_single_freqs[ae_right.first][mws],sum_single_freqs[ae_right.second][mws])* std::log(bb_count[ae_right.first]+bb_count[ae_right.second]);
	int ae_right_val = jointFreq_right[mws]*std::max(sum_single_freqs[ae_left.first][mws],sum_single_freqs[ae_left.second][mws])*std::log(bb_count[ae_left.first]+bb_count[ae_left.second]);

	if(ae_left_val != ae_right_val)
		return (ae_left_val > ae_right_val);

	if(ae_left.first != ae_right.first)
		return (ae_left.first > ae_right.first);

	return ae_left.second > ae_right.second;

}


bool funcPair_t1DCmp(funcPair_t ae_left, funcPair_t ae_right){
	vector<uint32_t> jointFreq_left = GetWithDef(sum_joint_freqs, ae_left, null_joint_freq);
	vector<uint32_t> jointFreq_right = GetWithDef(sum_joint_freqs, ae_right, null_joint_freq);

	int ae_left_val, ae_right_val;

	float rel_freq_threshold=2.0;
	for(short wsize=0;wsize<mws;++wsize){

		if((rel_freq_threshold*(jointFreq_left[wsize]) > sum_single_freqs[ae_left.first][wsize]) && 
				(rel_freq_threshold*(jointFreq_left[wsize]) > sum_single_freqs[ae_left.second][wsize]))
			ae_left_val = 1;
		else
			ae_left_val = -1;

		if((rel_freq_threshold*(jointFreq_right[wsize]) > sum_single_freqs[ae_right.first][wsize]) && 
				(rel_freq_threshold*(jointFreq_right[wsize]) > sum_single_freqs[ae_right.second][wsize]))
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

bool funcPair2DCmp(funcPair_t ae_left, funcPair_t ae_right){

	vector<uint32_t> jointFreq_left = GetWithDef(sum_joint_freqs, ae_left, null_joint_freq);
	vector<uint32_t> jointFreq_right = GetWithDef(sum_joint_freqs, ae_right, null_joint_freq);

	int ae_left_val, ae_right_val;




	short freqlevel;
	for(freqlevel=maxFreqLevel-1; freqlevel>=0; --freqlevel){
		for(short wsize=0;wsize<mws;++wsize){

			if((maxFreqLevel*(jointFreq_left[wsize]) > freqlevel*sum_single_freqs[ae_left.first][wsize]) && 
					(maxFreqLevel*(jointFreq_left[wsize]) > freqlevel*sum_single_freqs[ae_left.second][wsize]))
				ae_left_val = 1;
			else
				ae_left_val = -1;

			if((maxFreqLevel*(jointFreq_right[wsize]) > freqlevel*sum_single_freqs[ae_right.first][wsize]) && 
					(maxFreqLevel*(jointFreq_right[wsize]) > freqlevel*sum_single_freqs[ae_right.second][wsize]))
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



void disjointSet::mergeSets(func_t fid1, func_t fid2){

	assert(fid1.first == fid2.first);

	disjointSet * set1 = sets[fid1];
	disjointSet * set2 = sets[fid2];

	if(sets[fid1]==sets[fid2]){
		//fprintf(orderFile,"already merged!\n");
		return;
	}else {


		disjointSet * merger = (set1->size()>=set2->size())?(set1):(set2);
		disjointSet * mergee = (set1->size()<set2->size())?(set1):(set2);


		func_t merger_id = (set1->size()>=set2->size())?(fid1):(fid2);
		func_t mergee_id = (set1->size()<set2->size())?(fid1):(fid2);

		funcPair_t frontMerger_backMergee(merger->elements.front(), mergee->elements.back());
		funcPair_t backMerger_backMergee(merger->elements.back(), mergee->elements.back());
		funcPair_t backMerger_frontMergee(merger->elements.back(), mergee->elements.front());
		funcPair_t frontMerger_frontMergee(merger->elements.front(), mergee->elements.front());
		funcPair_t conAffEntriesArray[4]={frontMerger_frontMergee, frontMerger_backMergee, backMerger_frontMergee, backMerger_backMergee};
		std::vector<funcPair_t> conAffEntries(conAffEntriesArray,conAffEntriesArray+4);
		std::sort(conAffEntries.begin(), conAffEntries.end(), funcPairCmp);

		assert(funcPairCmp(conAffEntries[0],conAffEntries[1]) || (conAffEntries[0]==conAffEntries[1]));
		assert(funcPairCmp(conAffEntries[1],conAffEntries[2]) || (conAffEntries[1]==conAffEntries[2]));
		assert(funcPairCmp(conAffEntries[2],conAffEntries[3]) || (conAffEntries[2]==conAffEntries[3]));

		int ind = -1;


		int merger_dist = get_dist(merger_id);
		int mergee_dist = get_dist(mergee_id);

		int total_dist;

		bool con_mergee_front; 
		bool con_merger_front;

		do{
			ind++;
			assert(ind<4 && "assertion failed!\n");
			total_dist = 0;
			con_mergee_front = (conAffEntries[ind] == backMerger_frontMergee) || (conAffEntries[ind] == frontMerger_frontMergee);
			con_merger_front = (conAffEntries[ind] == frontMerger_frontMergee) || (conAffEntries[ind] == frontMerger_backMergee);
			total_dist += (con_mergee_front)?(mergee_dist):(mergee->total_bbs-mergee_dist-bb_count[mergee_id]);
			total_dist += (con_merger_front)?(merger_dist):(merger->total_bbs-merger_dist-bb_count[merger_id]);
			//std::cerr << "total dist is: " << total_dist << "\n";
		}while(total_dist >= MaxDist);


		//fprintf(orderFile,"merging directions: ");
		//for(auto ae: conAffEntries)
		//	fprintf(orderFile,"%f ", getAffinity(ae));


		//fprintf(orderFile,"\n");
		//fprintf(orderFile,"total dist: %d\n",total_dist);

		//fprintf(stderr, "Now merging:");
		if(con_mergee_front){

			for(deque<func_t>::iterator it=mergee->elements.begin(); it!=mergee->elements.end(); ++it){
				//fprintf(stderr, "%hd ",*it);
				if(con_merger_front)
					merger->elements.push_front(*it);
				else
					merger->elements.push_back(*it);
				disjointSet::sets[*it]=merger;
			}
		}else{
			//fprintf(stderr,"(backwards)");
			for(deque<func_t>::reverse_iterator rit=mergee->elements.rbegin(); rit!=mergee->elements.rend(); ++rit){
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
		merger->total_bbs += mergee->total_bbs;
		delete mergee;
	}
}






static void save_affinity_environment_variables(void) {
	const char *SampleRateEnvVar, *MaxWindowSizeEnvVar, *MaxFreqLevelEnvVar, *MemoryLimitEnvVar, *DebugEnvVar, *MaxDistEnvVar;

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
		mws = atoi(MaxWindowSizeEnvVar) - 1;
		null_joint_freq.resize(mws);
	}

	if((MaxFreqLevelEnvVar = getenv("MAX_FREQ_LEVEL")) != NULL){
		maxFreqLevel = atoi(MaxFreqLevelEnvVar);
	}

	if((MaxDistEnvVar = getenv("MAX_DIST"))!=NULL)
		MaxDist = atoi(MaxDistEnvVar);

	profilePath = getenv("ABC_PROF_PATH");

}
extern "C" void set_bb_count_for_fid(const char * str, short fid, short bbid){
	bb_count[func_t(str,fid)] = bbid;
	//std::cerr << "bbs for "<< fid << ": " << bbid << "\n";
}

extern "C" void set_func_count(const char * str, short fcount){
	//fprintf(stderr,"TOTAL FUNCS for %p %s IS: %d\n",str,str,fcount);
	object_names[str]=std::string(str);
	func_count[str] = fcount;
	/*
		 contains_func[str] = new bool [fcount]();
		 func_window_it[str] = new std::list<SampledWindow>::iterator [fcount];
		 func_trace_it[str] = new std::list<func_t>::iterator [fcount];
		 */
	sum_func_count += fcount;

}




/* llvm_start_basic_block_tracing - This is the main entry point of the basic
 * block tracing library.  It is responsible for setting up the atexit
 * handler and allocating the trace buffer.
 */
extern "C" void start_call_site_tracing() {
	init_affinity_data();
	flush_trace = false;
	profiling_switch = false;
	pthread_create(&prof_switch_th,NULL,prof_switch_toggle, (void *) 0);

	save_affinity_environment_variables();  

	prevFunc=func_t(0,-1);;
	nconsumers=0;
	trace_list_size=0;

	if(DEBUG>0)
		debugFile=fopen("debug.txt","w");

	srand(1);
	sem_init(&affinity_sem,0,1);

	pthread_create(&master, NULL, (void*(*)(void *))add_threads, (void *)0);
	atexit(affinityAtExitHandler);
}


extern "C" void initialize_post_bb_count_data(){}

extern "C" void printStr(const char * str){
	fprintf(stderr, "Name of the object: %s\n",str);
}
