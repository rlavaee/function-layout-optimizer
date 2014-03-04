#include <tr1/unordered_map>
#include <list>
#include <deque>
#include <stdint.h>
#include <algorithm>
#include <assert.h>
using namespace std;

const char * version_str=".babc";

typedef uint16_t wsize_t;
typedef uint16_t func_t;
typedef pair<func_t,func_t> funcpair_t;
func_t totalFuncs;


struct affWcounts{
  uint32_t potential_windows;
  uint32_t * actual_windows;
  affWcounts(){};
	affWcounts(uint32_t _potential_windows, uint32_t * _actual_windows){
  	potential_windows=_potential_windows;
  	actual_windows=_actual_windows;
	}
};


struct funcpair_hash{
  size_t operator()(const funcpair_t& s)const{ 
    return tr1::hash<func_t>()(s.first*totalFuncs + s.second);
  }
};

typedef tr1::unordered_map <const funcpair_t, affWcounts, funcpair_hash> affinityHashMap;

struct disjointSet {
  static disjointSet ** sets;
  deque<func_t> elements;
  size_t size(){ return elements.size();}
  static void mergeSets(disjointSet *, disjointSet *);
  static void mergeSets(func_t id1, func_t id2){
    if(sets[id1]!=sets[id2])
      mergeSets(sets[id1],sets[id2]);
  }

  static void init_new_set(func_t id){
    sets[id]= new disjointSet();
    sets[id]->elements.push_back(id);
  }

	static void deallocate(func_t id){
		disjointSet * setp = sets[id];
		if(sets[id]){
			for(deque<func_t>::iterator it=sets[id]->elements.begin(); it!=sets[id]->elements.end(); ++it)
				sets[*it]=0;
			delete setp;
		}

	}

	static int get_min_index(func_t id){
		deque<func_t>::iterator it=find(sets[id]->elements.begin(),sets[id]->elements.end(),id);
		int index=min(sets[id]->elements.end()-it-1,it-sets[id]->elements.begin());
		assert(index>=0 && (unsigned long)index<=(sets[id]->elements.size()-1)/2);
		return index;
	}

};

disjointSet ** disjointSet::sets = 0;

struct SampledWindow{
  int wcount;
	wsize_t wsize;
  list<func_t> partial_trace_list;
  SampledWindow():wsize(0){}
};


void initialize_affinity_data(float,func_t,func_t,func_t);
void update_affinity(void);
void record_func_exec(func_t);
void affinityAtExitHandler();
bool (*affEntryCmp)(const funcpair_t& pair_left, const funcpair_t& pair_right);
bool affEntry2DCmp(const funcpair_t& pair_left, const funcpair_t& pair_right);
