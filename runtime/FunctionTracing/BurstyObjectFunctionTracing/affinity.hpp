#ifndef AFFINITY_HPP
#define AFFINITY_HPP
#include <unordered_map>
#include <deque>
#include <list>
#include <stdint.h>
#include <assert.h>
#include <algorithm>
#include <cstring>
#include <boost/container/flat_map.hpp>
#include <boost/unordered_map.hpp>
using namespace std;

FILE * graphFile, * debugFile;

typedef std::pair<const char *,short> func_t;
typedef std::pair<func_t,func_t> funcPair_t;

short sum_func_count=0, mws;
const char * version_str=".abc";


struct func_hash{
  size_t operator()(func_t const &func)const{
	return std::hash<short>()(short((func.first and 0xFFFF) xor func.second));
  }
};

std::unordered_map<func_t, short, func_hash> bb_count;


struct funcPair_eq{
  bool operator()(funcPair_t const& fp1, funcPair_t const& fp2) const{
		return (fp1.first == fp2.first && fp1.second==fp2.second) || (fp1.first == fp2.second && fp1.second==fp2.first);
	}
};

struct funcPair_hash{
  size_t operator()(funcPair_t const &entry)const{
	  return std::hash<short>()(short(entry.first.first and entry.second.first) xor (entry.first.second+entry.second.second));
  }
};

typedef std::unordered_map <funcPair_t, std::vector<uint32_t>, funcPair_hash, funcPair_eq> JointFreqMap;
typedef std::unordered_map <func_t, std::vector<uint32_t>, func_hash>  SingleFreqMap;
typedef std::unordered_map <std::string, std::vector<short>>  LayoutMap;
typedef boost::container::flat_map <const char *, std::string> ObjectNameMap;


class disjointSet {
	public:
  static std::unordered_map<func_t,disjointSet*,func_hash> sets;
  std::deque<func_t> elements;
  int total_bbs;
  size_t size(){ return elements.size();}
  static void mergeSets(func_t id1, func_t id2);

  static void init_new_set(func_t id){
	sets[id]= new disjointSet();
	sets[id]->elements.push_back(id);
	sets[id]->total_bbs=bb_count[id];
  }



  static int get_dist(func_t id){
	deque<func_t>::iterator it=find(sets[id]->elements.begin(),sets[id]->elements.end(),id);
	int dist = 0;
	for(deque<func_t>::iterator _it = sets[id]->elements.begin(); _it!=it; ++_it)
	  dist+= bb_count[*_it];
	return dist;
  }

  static int get_min_index(func_t id){
	int dist = get_dist(id);
	int min_dist = std::min(sets[id]->total_bbs-dist-bb_count[id],dist);
	return min_dist;
	//int index=min(sets[id]->elements.end()-it-1,it-sets[id]->elements.begin());
	//assert(index>=0 && (unsigned long)index<=(sets[id]->elements.size()-1)/2);
	//return index;
  }

  static void print_layout(func_t id){
	//for(auto fid: sets[id]->elements)
	  //fprintf(orderFile,"(%p,%d)",fid.first,fid.second);

	//fprintf(orderFile,"\n");
  }

  static void deallocate(func_t id){
	disjointSet * setp = sets[id];
	if(sets[id]){
	  for(deque<func_t>::iterator it=sets[id]->elements.begin(); it!=sets[id]->elements.end(); ++it)
		sets[*it]=0;
	  delete setp;
	}

  }




};

std::unordered_map<func_t,disjointSet *,func_hash> disjointSet::sets;


struct SampledWindow{
  uint32_t wcount;
  std::list<func_t> partial_trace_list;
  SampledWindow(const SampledWindow&);
  SampledWindow();
  ~SampledWindow();
};

void print_trace(std::list<SampledWindow> *);
void * update_affinity(void *);
void affinityAtExitHandler();
bool (*funcPairCmp)(const funcPair_t, const funcPair_t);
bool funcPairSumCmp(const funcPair_t, const funcPair_t);
bool funcPairSizeCmp(const funcPair_t, const funcPair_t);
bool funcPair2DCmp(const funcPair_t, const funcPair_t);

#endif /* AFFINITY_HPP */
