//#include <sparsehash/sparse_hash_map>
//#include <sparsehash/sparse_hash_set>

#ifndef AFFINITY_HPP
#define AFFINITY_HPP
#include <tr1/unordered_map>
#include <deque>
#include <list>
#include <stdint.h>
#include <assert.h>
#include <algorithm>
#include <cstring>
using namespace std;

FILE * graphFile, * debugFile, * orderFile, *comparisonFile;

short totalFuncs, maxWindowSize;
const char * version_str=".abc";
const char * one_dim_version=".1D";
const char * two_dim_version_c="";
const char * two_dim_version_l=".2Dl";

short * bb_count;

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
  size_t operator()(affEntry const &entry)const{
  	return std::tr1::hash<short>()(entry.first*totalFuncs+entry.second);
	}
};

//typedef sparse_hash_set <int, hash<int> > intHashSet;
typedef std::tr1::unordered_map <const affEntry, uint32_t *, affEntry_hash, eqAffEntry> affinityHashMap;


typedef enum{
  FuncLevel,
  BBLevel
} ProfilingLevel;

/*
struct disjointSet{
  unsigned id;
  unsigned rank;
  unsigned size;
  disjointSet * parent;
  
  void unionSet(disjointSet*);
  void initSet(unsigned);
  disjointSet* find();
  unsigned getSize();
};
*/
struct disjointSet {
	static disjointSet ** sets;
	std::deque<short> elements;
	int total_bbs;
	size_t size(){ return elements.size();}
	static void mergeSets(short id1, short id2);
	
	static void init_new_set(short id){
		sets[id]= new disjointSet();
		sets[id]->elements.push_back(id);
		sets[id]->total_bbs=bb_count[id];
	}


	static int get_dist(short id){
		deque<short>::iterator it=find(sets[id]->elements.begin(),sets[id]->elements.end(),id);
		int dist = 0;
		for(deque<short>::iterator _it = sets[id]->elements.begin(); _it!=it; ++_it)
			dist+= bb_count[*_it];
		return dist;
	}

	static int get_min_index(short id){
		int dist = get_dist(id);
		int min_dist = std::min(sets[id]->total_bbs-dist-bb_count[id],dist);
		return min_dist;
		//int index=min(sets[id]->elements.end()-it-1,it-sets[id]->elements.begin());
		//assert(index>=0 && (unsigned long)index<=(sets[id]->elements.size()-1)/2);
		//return index;
	}

	static void print_layout(short id){
		for(auto fid: sets[id]->elements)
			fprintf(orderFile,"%d ",fid);

		fprintf(orderFile,"\n");
	}

	static void deallocate(short id){
		disjointSet * setp = sets[id];
		if(sets[id]){
			for(deque<short>::iterator it=sets[id]->elements.begin(); it!=sets[id]->elements.end(); ++it)
				sets[*it]=0;
			delete setp;
		}

	}



	
};

disjointSet ** disjointSet::sets = 0;


struct SampledWindow{
  uint32_t wcount;
  std::list<short> partial_trace_list;
  SampledWindow(const SampledWindow&);
  SampledWindow();
	~SampledWindow();
};

void print_trace(std::list<SampledWindow> *);
void initialize_affinity_data(float,short,short,short);
void * update_affinity(void *);
void affinityAtExitHandler();
//bool affEntryCmp(const affEntry, const affEntry);
bool (*affEntryCmp)(const affEntry, const affEntry);
bool affEntry1DCmp(const affEntry, const affEntry);
bool affEntrySumCmp(const affEntry, const affEntry);
bool affEntrySizeCmp(const affEntry, const affEntry);
bool affEntry2DCmp(const affEntry, const affEntry);
//void record_function_exec(short);


const char * get_dim_version(){ 
	if(affEntryCmp==&affEntry1DCmp || affEntryCmp==&affEntrySumCmp)
			return one_dim_version;
	if(affEntryCmp==&affEntry2DCmp)
			return two_dim_version_c;
	assert(false);
}


char * get_versioned_filename(const char * basename){
	char * versioned_name = new char [80];
	strcpy(versioned_name,basename);
	strcat(versioned_name,version_str);
	strcat(versioned_name,get_dim_version());
	return versioned_name;
}


#endif /* AFFINITY_HPP */
