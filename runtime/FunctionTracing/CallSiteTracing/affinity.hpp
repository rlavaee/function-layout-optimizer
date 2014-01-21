//#include <sparsehash/sparse_hash_map>
//#include <sparsehash/sparse_hash_set>

#ifndef AFFINITY_HPP
#define AFFINITY_HPP
#include <tr1/unordered_map>
#include <deque>
#include <list>
using namespace std;

typedef pair<short,short> shortpair;
short totalFuncs;

/*
struct affEntry{
  short first,second;
  affEntry();
  affEntry(short,short);
	affEntry(const affEntry&);
	affEntry& operator= (const affEntry&);
	bool operator== (const affEntry&) const;
};
*/

struct shortpair_hash{
  size_t operator()(const shortpair& s) const{
            return std::tr1::hash<short>()(totalFuncs*s.first + s.second);
        }
};

/*
struct shortpair_eq{
	bool operator()(const shortpair &s1, const shortpair &s2) const{
		return (s1.first == s2.first) && (s1.second == s2.second);
	}
};
*/

typedef std::tr1::unordered_map <const shortpair, int *, shortpair_hash> JointFreqMap;
typedef std::tr1::unordered_map <const shortpair, int **, shortpair_hash> JointFreqRangeMap;

struct UpdateEntry{
	short func;
	int min_wsize;
	UpdateEntry(short _func, int _min_wsize){
		func = _func;
		min_wsize = _min_wsize;
	}
};


struct disjointSet {
	static disjointSet ** sets;
	std::deque<short> elements;
	size_t size(){ return elements.size();}
	static void mergeSets(disjointSet *, disjointSet *);
	static void mergeSets(short id1, short id2){
		if(sets[id1]!=sets[id2])
			mergeSets(sets[id1],sets[id2]);
	}
	
	static void init_new_set(short id){
		sets[id]= new disjointSet();
		sets[id]->elements.push_back(id);
	}
	
};

disjointSet ** disjointSet::sets = 0;


struct SampledWindow{
  int wcount;
  int wsize;
  std::list<short> partial_trace_list;

  SampledWindow(const SampledWindow & sw){
    wcount=sw.wcount;
		wsize= sw.wsize;
    partial_trace_list = std::list<short>(sw.partial_trace_list);
  }

  SampledWindow(){
    wcount=0;
    wsize=0;
    partial_trace_list = std::list<short>();
  }
	~SampledWindow(){}

};

void print_trace(std::list<SampledWindow> *);
void initialize_affinity_data(float,short,short,short);
//void * update_affinity(void *);
void sequential_update_affinity(list<SampledWindow>::iterator);
void affinityAtExitHandler();


bool (*affEntryCmp)(const shortpair& pair_left, const shortpair& pair_right);
bool affEntry1DCmp(const shortpair& pair_left, const shortpair& pair_right);
bool affEntry2DCmp(const shortpair& pair_left, const shortpair& pair_right);
#endif /* AFFINITY_HPP */
