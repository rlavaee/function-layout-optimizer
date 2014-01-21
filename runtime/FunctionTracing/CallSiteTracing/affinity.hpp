//#include <sparsehash/sparse_hash_map>
//#include <sparsehash/sparse_hash_set>

#ifndef AFFINITY_HPP
#define AFFINITY_HPP
#include <tr1/unordered_map>
#include <deque>
#include <list>
using namespace std;

struct affEntry{
  short first,second;
  affEntry();
  affEntry(short,short);
	affEntry(const affEntry&);
	affEntry& operator= (const affEntry&);
	bool operator== (const affEntry&) const;
};

struct eqAffEntry{
  bool operator()(affEntry const&,affEntry const&)const; 
};

struct affEntry_hash{
  size_t operator()(affEntry const&)const;
};

//typedef sparse_hash_set <int, hash<int> > intHashSet;
typedef std::tr1::unordered_map <const affEntry, int *, affEntry_hash, eqAffEntry> affinityHashMap;

struct SampledWindow{
  int wcount;
	int wsize;
  std::list<short> partial_trace_list;
  SampledWindow(const SampledWindow&);
  SampledWindow();
	~SampledWindow();
};

typedef struct SampledWindow SampledWindow;


struct list_iterator_hash {
	size_t operator()(const std::list<SampledWindow>::iterator &it) const {
		return std::tr1::hash<SampledWindow*>()(&*it);
	}
};

/*
struct list_iterator_eq{
	bool operator()(const std::list<SampledWindow>::iterator &it1, const std::list<SampledWindow>::iterator &it2) const {
		return (&*it1==&*it2);
	}
};
*/
typedef std::tr1::unordered_map <const std::list<SampledWindow>::iterator, std::list<SampledWindow>::iterator, list_iterator_hash> WindowRelocationHashMap;

typedef std::tr1::unordered_map <const std::list<SampledWindow>::iterator, int, list_iterator_hash> WindowValidityHashMap;


struct ContainerWindow {
	std::list<SampledWindow>::iterator it;
	int count;
	ContainerWindow(std::list<SampledWindow>::iterator _it){
		it= _it;
		count = 1;
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


void print_trace(std::list<SampledWindow> *);
void initialize_affinity_data(float,short,short,short);
void * update_affinity(void *);
void affinityAtExitHandler();
//bool affEntryCmp(const affEntry, const affEntry);
bool (*affEntryCmp)(const affEntry, const affEntry);
bool affEntry1DCmp(const affEntry, const affEntry);
bool affEntry2DCmp(const affEntry, const affEntry);
//void record_function_exec(short);


#endif /* AFFINITY_HPP */
