//#include <sparsehash/sparse_hash_map>
//#include <sparsehash/sparse_hash_set>

#ifndef AFFINITY_HPP
#define AFFINITY_HPP
#include <tr1/unordered_map>
#include <deque>
#include <list>
#include <stdint.h>
#include <string.h>
#include <algorithm>
#include <assert.h>
using namespace std;
const char * version_str=".fabc";
const char * one_dim_version=".1D";
const char * two_dim_version_c="";
const char * two_dim_version_l=".2Dl";
const char * count_version="";

typedef uint8_t wsize_t;
typedef uint16_t func_t;
typedef pair<func_t,func_t> funcpair_t;
func_t totalFuncs;

funcpair_t unordered_funcpair(func_t s1,func_t s2){
  return (s1<s2)?(funcpair_t(s1,s2)):(funcpair_t(s2,s1));
}

struct funcpair_eq{
  bool operator()(funcpair_t s1, funcpair_t s2) const {
    if ((s1.first == s2.first) && (s1.second == s2.second))
      return true;
    if ((s1.second == s2.first) && (s1.first == s2.second))
      return true;
    return false;
  }
};


struct funcpair_hash{
  size_t operator()(const funcpair_t& s) const{
    func_t smaller=(s.first<s.second)?(s.first):(s.second);
    func_t bigger=(s.first<s.second)?(s.second):(s.first);
    return tr1::hash<func_t>()(totalFuncs*smaller + bigger);
  }
};

typedef tr1::unordered_map <const funcpair_t, uint32_t *, funcpair_hash, funcpair_eq > JointFreqMap;
typedef tr1::unordered_map <const funcpair_t, uint32_t **, funcpair_hash, funcpair_eq > JointFreqRangeMap;

struct SingleUpdateEntry{
  func_t func;
  wsize_t min_wsize;
  SingleUpdateEntry(func_t a, wsize_t b): func(a), min_wsize(b){}
};

struct JointUpdateEntry{
  funcpair_t func_pair;
  wsize_t min_wsize;
  JointUpdateEntry(funcpair_t a, wsize_t b):func_pair(a), min_wsize(b){}
};


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
  wsize_t wsize;
  list<func_t> partial_trace_list;
  list<SingleUpdateEntry> single_update_list;
  list<JointUpdateEntry> joint_update_list;

  SampledWindow():wsize(0){}

 	void add_single_update_entry(SingleUpdateEntry &sue){
    single_update_list.push_back(sue);
  }

  void add_joint_update_entry(JointUpdateEntry &jue){
    joint_update_list.push_back(jue);
  }
};

void print_trace(list<SampledWindow> *);
void initialize_affinity_data(float,func_t,func_t,func_t);
wsize_t sequential_update_affinity(list<SampledWindow>::iterator);
void affinityAtExitHandler();


uint32_t * GetWithDef(JointFreqMap * m, const funcpair_t &key, uint32_t * defval);
bool (*affEntryCmp)(const funcpair_t& pair_left, const funcpair_t& pair_right);
bool affEntry1DCmp(const funcpair_t& pair_left, const funcpair_t& pair_right);
bool affEntry2DCmpConstantStep(const funcpair_t& pair_left, const funcpair_t& pair_right);
bool affEntryCountCmp(const funcpair_t& pair_left, const funcpair_t& pair_right);
bool affEntry2DCmpLogStep(const funcpair_t& pair_left, const funcpair_t& pair_right);
bool print_when(const funcpair_t& pair_left, const funcpair_t& pair_right);
const char * get_dim_version(){ 
	if(affEntryCmp==&affEntry1DCmp)
			return one_dim_version;
	if(affEntryCmp==&affEntry2DCmpConstantStep)
			return two_dim_version_c;
	
	if(affEntryCmp==&affEntry2DCmpLogStep)
			return two_dim_version_l;
	if(affEntryCmp==&affEntryCountCmp)
		return count_version;
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
