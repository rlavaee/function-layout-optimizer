#include "affinity.hpp"
#include <stdio.h>
#include <fstream>
#include <algorithm>
#include <pthread.h>
#include <boost/lockfree/queue.hpp>
#include <string.h>
#include <vector>
#include <tgmath.h>
#include "matching.hpp"
#include <serialize.h>
#include <thread>
#include <linux/unistd.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <atomic>

#define MAX_DIST 10

pid_t gettid( void )
{
		return syscall( __NR_gettid );
}

std::atomic<pid_t> prof_th;

vector< vector< vector<bb_t> > > ucfg;

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
	std::cerr << "cur_pid is: " << cur_pid << " and prof_th is: " << prof_th.load() << "\n";
	return result;
}

const char * profilePath = NULL;

void * prof_switch_toggle(void *){
	while(true){
		cerr << "thread: " << std::this_thread::get_id() << "\n";
		uint32_t r=rand()%10000;
		usleep(10000+r);
		pthread_mutex_lock(&switch_mutex);
		profiling_switch = false;
		flush_trace = true;
		pthread_mutex_unlock(&switch_mutex);
		usleep(40000);
		pthread_mutex_lock(&switch_mutex);
		profiling_switch = true;
		fprintf(stderr,"for this period prof_th was: %d\n",prof_th.load());
		prof_th.store(-1);
		pthread_mutex_unlock(&switch_mutex);
	}
/*
	while(true){
		uint32_t r=rand()%20000;
		usleep(30000+r);
		pthread_mutex_lock(&switch_mutex);
		profiling_switch = true;
		pthread_mutex_unlock(&switch_mutex);
		usleep(10000);
		pthread_mutex_lock(&switch_mutex);
		profiling_switch = false;
		flush_trace = true;
		pthread_mutex_unlock(&switch_mutex);
	}
	*/
}

int DEBUG;
FILE * comparisonFile, * traceFile;
wsize_t mws;
int sampleRateLog;
uint32_t sampleSize;
uint32_t sampleMask;
short maxFreqLevel;
ofstream order_out(get_versioned_filename("order"));  

JointFreqMap joint_freqs;
FallThroughMap fall_thrus;

//TODO Consider adding single freqs later on
SingleFreqMap single_freqs;

bool * contains_func;
list<Block>::iterator * rec_trace_it;
list<SampledWindow>::iterator * rec_window_it;

unordered_map<std::thread::id,bb_t> last_bbs;

list<SampledWindow> trace_list;

wsize_t trace_list_size;

list<SampledWindow>::iterator window_iter,window_iter_prev, grown_list_iter,top_window_iter;
list<Block>::iterator func_iter, partial_trace_list_end;
list<Block> * last_window_trace_list;

set<Block>::iterator owner_iter, owner_iter_end;


FILE * graphFile, * debugFile;

const vector<uint32_t>  null_joint_freq(mws,0);


/*
std::vector<uint32_t>& emplace(JointFreqMap &jfm, const BlockPair &rec_pair){
		JointFreqMap::iterator result = jfm.find(rec_pair);
		if(result == jfm.end()){
				//only c++14
				//std::unique_ptr<uint32_t[]> p = std::make_unique<uint32_t[]>(mws,0);
				std::vector<uint32_t> p(mws,0);
				p->reserve
				return jfm.insert( std::pair<BlockPair, std::unique_ptr<uint32_t[]>> (rec_pair,std::move(p))).first->second;
		}else
				return result->second;
}
*/


uint32_t*  emplace(SingleFreqMap &sfm, Block rec){
		SingleFreqMap::iterator result = sfm.find(rec);
		if(result == sfm.end()){
				return (sfm[rec]= new uint32_t[mws]());
		}else
				return result->second;
}

void record_bb_exec(Block rec){


		if(flush_trace){
			while(!trace_list.empty()){
				list<uint32_t> * last_window_trace_list= &trace_list.back().partial_trace_list;

					while(!last_window_trace_list->empty()){
							auto oldRec=last_window_trace_list->front();
							contains_func[get_key(oldRec)]=false;
							last_window_trace_list->pop_front();
					}
					
					trace_list_size-=trace_list.back().size();
					trace_list.pop_back();
				}

				assert(trace_list_size==0 && "could not flush the trace");

			flush_trace = false;
			return;
		}

		if(!profiling_switch)
			return;

		uint32_t r=rand();
		bool sampled=((r & sampleMask)==0);

		if(!sampled && trace_list_size==0)
				return;

		if(sampled){
				SampledWindow sw(rec);
				trace_list.push_front(sw);
		}else if(trace_list_size){
				trace_list.front().push_front(rec);
		}


		if(DEBUG>0){
				if(sampled)
					fprintf(debugFile,"new window\n");
				print_trace(&trace_list);
		}

		auto rec_key = get_key(rec);

		if(!contains_func[rec_key]){
				trace_list_size++;

				if(trace_list_size > mws){
						if(DEBUG>0){
								fprintf(debugFile,"trace list overflowed: %d\n",trace_list_size);
								print_trace(&trace_list);
						}

						list<uint32_t> * last_window_trace_list= &trace_list.back().partial_trace_list;

						while(!last_window_trace_list->empty()){
								auto oldRec=last_window_trace_list->front();
								contains_func[get_key(oldRec)]=false;
								last_window_trace_list->pop_front();
						}
						trace_list_size-=trace_list.back().size();
						trace_list.pop_back();
				}

				if(trace_list_size>0){
						sequential_update_affinity(rec,trace_list.end());
						contains_func[rec_key]=true;
						rec_window_it[rec_key] = trace_list.begin();
						rec_trace_it[rec_key] = trace_list.begin()->partial_trace_list.begin();
				}

		}else{
				sequential_update_affinity(rec,rec_window_it[rec_key]);
				window_iter = rec_window_it[rec_key];
				window_iter->erase(rec_trace_it[rec_key]);

				if(window_iter->partial_trace_list.empty()){
						list<SampledWindow>::iterator window_iter_prev = window_iter;
						window_iter_prev--;
						for(auto owner: window_iter->owners)
								window_iter_prev->owners.insert(owner);
						window_iter->owners.clear();
						trace_list.erase(window_iter);
				}

				rec_window_it[rec_key] = trace_list.begin();
				rec_trace_it[rec_key] = trace_list.begin()->partial_trace_list.begin();	
		}


}

void print_optimal_layout(){
		vector<std::pair<uint16_t,uint16_t>> layout;
		for(func_t fid=0; fid<totalFuncs; ++fid){
				for(bb_t bbid=0; bbid<bb_count[fid]; ++bbid){
						Block rec = (fid << 16) | bbid;
						if(disjointSet::sets[rec]){
								disjointSet * thisSet=disjointSet::sets[rec];
								for(deque<Block>::iterator it=disjointSet::sets[rec]->elements.begin(), 
												it_end=disjointSet::sets[rec]->elements.end()
												; it!=it_end ; ++it){
										layout.push_back(std::pair<uint16_t,uint16_t>((*it) >> 16,*it & 0xFFFF));
										disjointSet::sets[*it]=0;
								}
								thisSet->elements.clear();
								delete thisSet;
						}
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

		std::filebuf layout_buf;
		layout_buf.open (affinityFilePath,std::ios::out);

		OutputStream layout_os(&layout_buf);

		serialize(layout,layout_os);

		vector< vector<std::pair<bool,bb_t>>> best_target_bbs (totalFuncs);

		for(size_t i=0; i<fall_thrus.size(); ++i){
			best_target_bbs[i].resize(fall_thrus[i].size());
			for(size_t j=0; j<fall_thrus[i].size(); ++j){
				best_target_bbs[i][j] = std::pair<bool,bb_t>(false,0);
				uint32_t max = 0;
				for(auto &p: fall_thrus[i][j])
					if(p.second > max){
						max = p.second;
						best_target_bbs[i][j] = std::pair<bool,bb_t>(true,p.first);
					}
			}
		}


		serialize(best_target_bbs,layout_os);



		/*
		for(auto rec: layout)
				layout_out << std::hex << "(" << (rec>>16) << "," << (rec&0xFFFF) << ")\n";
		layout_out.close();
		*/

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
			joint_freqs = deserialize<JointFreqMap>(graph_is);
			fall_thrus = deserialize<FallThroughMap>(graph_is);
		}

}

void aggregate_affinity(){
		JointFreqMap::iterator jiter;
		SingleFreqMap::iterator siter;
		/*
		
		for(jiter=joint_freqs.begin(); jiter!=joint_freqs.end(); ++jiter){
				uint32_t * freq_array= jiter->second;	  
				for(wsize_t wsize=1;wsize<mws;++wsize){
						freq_array[wsize]+=freq_array[wsize-1];
				}
		}


		for(siter=single_freqs.begin(); siter!=single_freqs.end(); ++siter){
				uint32_t * freq_array= siter->second;	  
				for(wsize_t wsize=1;wsize<mws;++wsize){
						freq_array[wsize]+=freq_array[wsize-1];
				}
		}

		*/


		char * graphFilePath=(char*) malloc(80);

		strcpy(graphFilePath,"");
  		if(profilePath!=NULL){
  			strcat(graphFilePath,profilePath);
			strcat(graphFilePath,"/");
 	 	}
		strcat(graphFilePath,"graph");

		strcat(graphFilePath,version_str);
}


void emit_graphFile(){


		JointFreqMap::iterator jiter;
		SingleFreqMap::iterator siter;

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
		serialize(joint_freqs,graph_os);
		serialize(fall_thrus,graph_os);

		/*
		for(size_t i=0; i<fall_thrus.size(); ++i)
			for(size_t j=0; j<fall_thrus[i].size(); ++j)
				for(auto &p: fall_thrus[i][j])
					cout << i <<"[" << j <<"," << p.first << "]:" << p.second << "\n";
		*/
}

func_t cur_fid;


/* This function builds the affinity groups based on the affinity table 
   and prints out the results */
void find_affinity_groups(){


			FILE * cfg_in = fopen("cfg.out","r");

			
			func_t fid;
			bb_t num_bbs;

			ucfg.resize(totalFuncs,vector< vector< bb_t> >());

			while(fscanf(cfg_in,"F#%hx:%hx\n",&fid,&num_bbs)!=EOF){
				assert(fid<totalFuncs && "fid out of range");
				assert(num_bbs==bb_count[fid] && "inconsistent number of bbs.");
				ucfg[fid].resize(num_bbs,vector<bb_t>());
				for(bb_t i=0; i<num_bbs; ++i){
					bb_t bbid1;
					int br;
					assert(fscanf(cfg_in,"BB#%hx:%d\n",&bbid1,&br)==2);
					assert(bbid1==i && "inconsistent bbid");
					for(int j=0; j<br; ++j){
						bb_t bbid2;
						assert(fscanf(cfg_in,"%hx\n",&bbid2)==1);
						ucfg[fid][bbid1].push_back(bbid2);
					}
				}
			}

			fclose(cfg_in);


		ofstream matching_out("matching.out");

		ofstream wcfg_out("wcfg.out");


		for(func_t fid=0; fid < totalFuncs; ++fid){
				wcfg_out << "function is: " << fid << endl;
				//cout << "function is: " << fid << endl;
				BipartiteGraph wcfg(bb_count[fid]);


				vector<bb_pair_t> all_bb_pairs;
				for(bb_t bbid1=0; bbid1<bb_count[fid]; ++bbid1){
						for(bb_t bbid2: ucfg[fid][bbid1]){

							auto res = fall_thrus[fid][bbid1].find(bbid2);
							uint32_t fallt = (res==fall_thrus[fid][bbid1].end())?(0):(res->second);
							int total_count = fallt + 2/ucfg[fid][bbid1].size();
							wcfg.edges[bbid1].push_back(pair<int,int>(bbid2, total_count));
							wcfg_out << "ucfg edge : (" << bbid1  << "," << bbid2 << "): " << total_count << endl;
						}


						if(ucfg[fid][bbid1].empty())
								for(auto &p: fall_thrus[fid][bbid1]){
										wcfg.edges[bbid1].push_back(p);
										wcfg_out << "no ucfg edge : (" << bbid1  << "," << p.first << "): " << p.second << endl;
								}
				}

				MaxMatchSolver matching_solver(wcfg);
				matching_solver.Solve();
				matching_solver.GetApproxMaxPathCover();




			

				for(auto path: matching_solver.pathCover){
					Block first_bb = fid<<16 | path.front();;
    			disjointSet::sets[first_bb]= new disjointSet();
					for(auto bbid: path){
						Block bb = fid<<16 | bbid;
						if(path.size() > 1)
							matching_out << "(" << fid << "," << bbid <<")" << "\t"; 
						disjointSet::sets[first_bb]->elements.push_back(bb);
						disjointSet::sets[bb]=disjointSet::sets[first_bb];
					}
					if(path.size() > 1)
						matching_out << endl;
				}		

		}

			matching_out.close();
			wcfg_out.close();



		vector<BlockPair> all_affEntry_iters;
		for(JointFreqMap::iterator iter=joint_freqs.begin(); iter!=joint_freqs.end(); ++iter){
				all_affEntry_iters.push_back(iter->first);
		}

		sort(all_affEntry_iters.begin(),all_affEntry_iters.end(),jointFreqCountCmp);

		for(vector<BlockPair>::iterator iter=all_affEntry_iters.begin(); iter!=all_affEntry_iters.end(); ++iter){
				//if((iter->first >> 16 ) == (iter->second >> 16))
					disjointSet::mergeFunctions(*iter);
		}


}

/* Must be called at exit*/
void affinityAtExitHandler(){
		if(DEBUG>0)
				fclose(debugFile);
		cerr << "came here\n";
		cerr << "came here\n";
		cerr << "came here\n";
		cerr << "came here\n";

		aggregate_affinity();
		find_affinity_groups();
		emit_graphFile();
		print_optimal_layout();

}


void print_trace(list<SampledWindow> * tlist){
		window_iter=tlist->begin();

		list<Block>::iterator trace_iter;

		fprintf(debugFile,"---------------------------------------------\n");
		while(window_iter!=tlist->end()){
				trace_iter =  window_iter->partial_trace_list.begin();
				fprintf(debugFile,"owners:\t");
				for(set<Block>::iterator owner_it=window_iter->owners.begin(); owner_it!=window_iter->owners.end(); ++owner_it)
						for(auto owner: window_iter->owners)
								fprintf(debugFile,"(%u,%u) ",owner>>16,owner & 0xFFFF);
				fprintf(debugFile,"\n");

				while(trace_iter!=window_iter->partial_trace_list.end()){
						fprintf(debugFile,"(%u,%u) ",(*trace_iter)>>16,(*trace_iter) & 0xFFFF);
						trace_iter++;
				}
				fprintf(debugFile,"\n");
				window_iter++;
		}
}


void sequential_update_affinity(Block rec, const list<SampledWindow>::iterator &grown_list_end){
		unsigned top_wsize=0;

		grown_list_iter = trace_list.begin();

		while(grown_list_iter!= grown_list_end){
				top_wsize+=grown_list_iter->size();
				owner_iter = grown_list_iter->owners.begin();
				owner_iter_end = grown_list_iter->owners.end();

				if(*owner_iter==rec)
						owner_iter++;

				while(owner_iter != owner_iter_end){
						Block oldRec= *owner_iter;

						if(get_paired(oldRec,rec)){
								BlockPair rec_pair(oldRec,rec);
								auto res = joint_freqs.emplace(std::piecewise_construct,
														std::forward_as_tuple(rec_pair),
														std::forward_as_tuple(mws,0));
								res.first->second[top_wsize-2]++;
								//emplace(joint_freqs,rec_pair)[top_wsize-2]++;
						}

						owner_iter++;
				}

				grown_list_iter++;

		} 

} 


const std::vector<uint32_t>& GetWithDef(JointFreqMap &m, const BlockPair &key, const std::vector<uint32_t>& defval) {
		JointFreqMap::const_iterator it = m.find( key );
		if ( it == m.end() ) {
				return defval;
		}
		else {
				return it->second;
		}
}

/*
   bool jointFreqSameFunctionsCmp(const BlockPair &left_pair, const BlockPair &right_pair){
   uint32_t * jointFreq_left = GetWithDef(joint_freqs, left_pair, null_joint_freq);
   uint32_t * jointFreq_right = GetWithDef(joint_freqs, right_pair, null_joint_freq);

   for(wsize_t wsize=0; wsize<mws; ++wsize){
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
   */

bool jointFreqCountCmp(const BlockPair &left_pair, const BlockPair &right_pair){

		auto &jointFreq_left = GetWithDef(joint_freqs, left_pair, null_joint_freq);
		auto &jointFreq_right = GetWithDef(joint_freqs, right_pair, null_joint_freq);

		int left_val, right_val;
		left_val = right_val = 0;

		for(wsize_t wsize=0; wsize<mws; ++wsize){
				left_val+=jointFreq_left[wsize]*((int)(log2(mws-wsize)));
				right_val+=jointFreq_right[wsize]*((int)(log2(mws-wsize)));
		}

		if(left_val > right_val)
				return true;
		if(left_val < right_val)
				return false;
		/*
		   for(freqlevel=maxFreqLevel-1; freqlevel>=0; --freqlevel){
		   for(wsize_t wsize=0; wsize<mws; ++wsize){
		   uint32_t left_val = jointFreq_left[wsize];
		   uint32_t right_val = jointFreq_right[wsize];
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
		   */

		if(left_pair.first != right_pair.first)
				return (right_pair.first < left_pair.first);

		return right_pair.second < left_pair.second;

}


void disjointSet::mergeBasicBlocksSameFunction(func_t fid, const bb_pair_t &bb_pair){
		if(bb_pair.second==0 || bb_pair.first == bb_pair.second)
				return;

		Block left_rec = fid<<16 | bb_pair.first;
		Block right_rec = fid<<16 | bb_pair.second;


		if(is_connected_to_right(left_rec) || is_connected_to_left(right_rec))
				return;
		else
				mergeSets(left_rec,right_rec);
}

void disjointSet::mergeSets(Block left_rec, Block right_rec){
		//cerr << "merging left_rec: " << left_rec << "\t and right_rec: " << right_rec << endl;
		disjointSet * left_set, * right_set;

		left_set = sets[left_rec];
		right_set = sets[right_rec];

		assert (left_set && right_set);


		if(left_set==right_set)
				return;

		order_out << "[ (" << (left_rec >> 16) << "," << (left_rec&0xFFFF) << ") , (" << (right_rec >> 16) << "," << (right_rec&0xFFFF) << ")" << "]\t";

		order_out << std::dec << "sizes:\t" << left_set->elements.size() << "\t" << right_set->elements.size() << endl;

		deque<Block>::iterator left_rec_it = find(left_set->elements.begin(),left_set->elements.end(),left_rec);
		deque<Block>::iterator right_rec_it = find(right_set->elements.begin(),right_set->elements.end(),right_rec);

		int left_rec_dist_head = left_rec_it-left_set->elements.begin();
		int left_rec_dist_tail = left_set->elements.end()-left_rec_it-1;

		int right_rec_dist_head = right_rec_it-right_set->elements.begin();
		int right_rec_dist_tail = right_set->elements.end()-right_rec_it-1;

		int left_then_right_dist = left_rec_dist_tail + right_rec_dist_head;
		int right_then_left_dist = right_rec_dist_tail + left_rec_dist_head;

		int rec_dist_min = min(left_then_right_dist,right_then_left_dist);

		if(rec_dist_min > MAX_DIST)
			return;

		bool left_then_right = (rec_dist_min == left_then_right_dist);

		if(left_then_right){
			order_out << "left<->right" << endl;
			for(deque<Block>::iterator it=right_set->elements.begin(); it!=right_set->elements.end(); ++it){
				left_set->elements.push_back(*it);
				sets[*it]=left_set;
			}
		}else{
			order_out << "right<->left" << endl;
			for(deque<Block>::reverse_iterator rit=right_set->elements.rbegin(); rit!=right_set->elements.rend(); ++rit){
				left_set->elements.push_front(*rit);
				sets[*rit]=left_set;
			}

		}
		/*
		
		int left_rec_dist_min = min( left_rec_dist_head, left_rec_dist_tail);
		int right_rec_dist_min = min( right_rec_dist_head, right_rec_dist_tail);

		int rec_dist = left_rec_dist_min + right_rec_dist_min;

		if(rec_dist > MAX_DIST)
				return;

		bool right_close_head = (left_rec_dist_min == left_rec_dist_head);
		bool left_close_tail = (right_rec_dist_min == right_rec_dist_tail);

		order_out << "\tdistances:\t(" << left_rec_dist_min << ", " <<  right_rec_dist_min << ")" << "\t";

		if(right_close_head && left_close_tail){
			order_out << "tail<->head" << endl;
			for(deque<block>::iterator it=right_set->elements.begin(); it!=right_set->elements.end(); ++it){
				left_set->elements.push_back(*it);
				disjointset::sets[*it]=left_set;
			}
		}else if(right_close_head){ //left_close_head
			order_out << "head<->head" << endl;
			for(deque<Block>::iterator it=right_set->elements.begin(); it!=right_set->elements.end(); ++it){
				left_set->elements.push_back(*it);
				disjointSet::sets[*it]=left_set;
			}
		}else if(left_close_tail){ //right_close_tail
			order_out << "tail<->tail" << endl;
			for(deque<Block>::reverse_iterator rit=right_set->elements.rbegin(); rit!=right_set->elements.rend(); ++rit){
				left_set->elements.push_front(*rit);
				disjointSet::sets[*rit]=left_set;
			}
		}else{ //right_close_tail & left_close_head
			order_out << "head<->tail" << endl;
			for(deque<Block>::reverse_iterator rit=right_set->elements.rbegin(); rit!=right_set->elements.rend(); ++rit){
				left_set->elements.push_front(*rit);
				disjointSet::sets[*rit]=left_set;
			}
		}
		*/

		right_set->elements.clear();
		delete right_set;
}

void disjointSet::mergeFunctions(const BlockPair &rec_pair){
		mergeSets(rec_pair.first,rec_pair.second);
}

/*
   void disjointSet::mergeSetsDifferentFunctions(const BlockPair &p){

   assert(!haveSameFunctions(p));

   disjointSet * first_set, * second_set;
   first_set = sets[p.first];
   second_set = sets[p.second];

   disjointSet * merger = (first_set->size() >= second_set->size())?(first_set):(second_set);
   disjointSet * mergee = (merger == first_set) ? (second_set) : (first_set);


   BlockPair frontMerger_backMergee(merger->elements.front(), mergee->elements.back());
   BlockPair backMerger_backMergee(merger->elements.back(), mergee->elements.back());
   BlockPair backMerger_frontMergee(merger->elements.back(), mergee->elements.front());
   BlockPair frontMerger_frontMergee(merger->elements.front(), mergee->elements.front());

   BlockPair conAffEntriesArray[4]={frontMerger_frontMergee, frontMerger_backMergee, backMerger_frontMergee, backMerger_backMergee};

   vector<BlockPair> conAffEntries(conAffEntriesArray,conAffEntriesArray+4);

   sort(conAffEntries.begin(), conAffEntries.end(), jointFreqCountCmp);

   bool con_mergee_front = (conAffEntries[0] == backMerger_frontMergee) || (conAffEntries[0] == frontMerger_frontMergee);
   bool con_merger_front = (conAffEntries[0] == frontMerger_frontMergee) || (conAffEntries[0] == frontMerger_backMergee);

   if(con_mergee_front){

   for(deque<Block>::iterator it=mergee->elements.begin(); it!=mergee->elements.end(); ++it){
   if(con_merger_front)
   merger->elements.push_front(*it);
   else
   merger->elements.push_back(*it);
   disjointSet::sets[*it]=merger;
   }
   }else{
   for(deque<Block>::reverse_iterator rit=mergee->elements.rbegin(); rit!=mergee->elements.rend(); ++rit){
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
				//cerr << sampleMask << "\t" << RAND_MAX << "\t" << sampleRateLog << "\n";
		}

		if((MaxWindowSizeEnvVar = getenv("MAX_WINDOW_SIZE")) != NULL){
				mws = atoi(MaxWindowSizeEnvVar)-1;
		}

		if((MaxFreqLevelEnvVar = getenv("MAX_FREQ_LEVEL")) != NULL){
				maxFreqLevel = atoi(MaxFreqLevelEnvVar);
		}

  	profilePath = getenv("BBABC_PROF_PATH");

}



/* llvm_start_basic_block_tracing - This is the main entry point of the basic
 * block tracing library.  It is responsible for setting up the atexit
 * handler and allocating the trace buffer.
 */
extern "C" int start_bb_call_site_tracing(func_t _totalFuncs) {

		pthread_mutex_lock(&switch_mutex);
		flush_trace = false;
		//profiling_switch = false;
		profiling_switch = true;
		prof_th.store(-1);
		pthread_mutex_unlock(&switch_mutex);

  	pthread_create(&prof_switch_th,NULL,prof_switch_toggle, (void *) 0);

		save_affinity_environment_variables();  
		totalFuncs = _totalFuncs;
		bb_count = new bb_t[totalFuncs];
		bb_count_cum = new uint32_t[totalFuncs+1]();
		fall_thrus.resize(totalFuncs);
		initialize_affinity_data();
		/* Set up the atexit handler. */
		atexit (affinityAtExitHandler);

		return 1;
}

extern "C" void set_bb_count_for_fid(func_t fid, bb_t bbid){
		bb_count[fid]=bbid;
		fall_thrus[fid].resize(bbid);
}

extern "C" void initialize_post_bb_count_data(){
		for(func_t fid=0; fid < totalFuncs; ++fid){
				bb_count_cum[fid+1]=bb_count[fid]+bb_count_cum[fid];
		}

		contains_func = new bool [bb_count_cum[totalFuncs]]();
		rec_trace_it = new list<Block>::iterator [bb_count_cum[totalFuncs]];
		rec_window_it = new list<SampledWindow>::iterator [bb_count_cum[totalFuncs]];
}

extern "C" void record_bb_entry(Block fid_bbid){

		if(totalFuncs==0)
			return;
		pthread_mutex_lock(&switch_mutex);
		// is checked: assert((last_bb.fid == fid) && (bbid!=0));
		bb_t bbid = fid_bbid & 0xFFFF;
		func_t fid = fid_bbid >> 16;

		std::thread::id tid = std::this_thread::get_id();

		auto res = fall_thrus[fid][last_bbs[tid]].emplace(bbid,1);
		//cerr << fid << "[" << last_bb << "," << bbid << "]\n";

		if(!res.second)
			res.first->second++;

		//cout << fid << "[" << last_bb << "," << bbid << "]:" << res.first->second << "\n";

		last_bbs[tid]= bbid;
		record_bb_exec(fid_bbid);
		pthread_mutex_unlock(&switch_mutex);
}

extern "C" void  record_func_entry(Block fid_bbid){

		if(totalFuncs==0)
			return;
		std::thread::id tid = std::this_thread::get_id();
		last_bbs[tid] = 0;
		pthread_mutex_lock(&switch_mutex);
		record_bb_exec(fid_bbid);
		pthread_mutex_unlock(&switch_mutex);
}

extern "C" void  record_callsite(Block fid_bbid){
		if(totalFuncs==0)
			return;
		std::thread::id tid = std::this_thread::get_id();
		last_bbs[tid] = fid_bbid & 0xFFFF;
		pthread_mutex_lock(&switch_mutex);
		record_bb_exec(fid_bbid);
		pthread_mutex_unlock(&switch_mutex);
}


