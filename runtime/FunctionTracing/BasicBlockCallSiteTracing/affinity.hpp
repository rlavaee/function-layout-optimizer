//#include <sparsehash/sparse_hash_map>
//#include <sparsehash/sparse_hash_set>

#ifndef AFFINITY_HPP
#define AFFINITY_HPP
#include <deque>
#include <list>
#include <functional>
#include <unordered_map>
#include <stdint.h>
#include <set>
#include <cstring>

#define MAX_FILE_NAME 30
using namespace std;

const char * version_str = ".bbabc";

typedef uint16_t wsize_t;
typedef uint16_t func_t;
typedef uint16_t bb_t;
func_t totalFuncs;
bb_t * bb_count;
uint32_t * bb_count_cum;
uint32_t ** fall_through_counts;

typedef pair<bb_t,bb_t> bb_pair_t;

class Record{
  public:
  func_t fid;
  bb_t bbid;
  Record(){};
  Record(func_t a, bb_t b): fid(a),bbid(b) {};
  Record(const Record &rec): fid(rec.fid), bbid(rec.bbid){};

	uint32_t get_key() const{
		return bb_count_cum[fid]+bbid;
	}

  bool operator < (const Record &rhs) const{
    if(fid < rhs.fid)
      return true;
    if(fid > rhs.fid)
      return false;
    if(bbid < rhs.bbid)
      return true;
    return false;
  }

  bool operator == (const Record &rhs) const{
    return fid==rhs.fid && bbid==rhs.bbid;
  }

  bool operator != (const Record &rhs) const {return !(*this==rhs);}

	bool gets_paired_with(const Record &rec) const {
		if(fid!=rec.fid)
			return rec.bbid==0;
		else
			return bbid!=rec.bbid;
	}

};

typedef pair<Record,Record> RecordPair;

bool haveSameFunctions(const RecordPair &rec_pair){
  return rec_pair.first.fid == rec_pair.second.fid;
}

struct RecordHash{
  size_t operator()(const Record &rec) const{
    return hash<uint32_t>()( ((uint32_t) rec.fid << 16) + rec.bbid);
  }
};

RecordPair unordered_RecordPair(const Record &s1,const Record &s2){
  return (s1<s2)?(RecordPair(s1,s2)):(RecordPair(s2,s1));
}


struct RecordPair_hash{
  size_t operator()(const RecordPair& rec_pair) const{
    return hash<uint32_t>()((rec_pair.first.get_key() << 16) + rec_pair.second.get_key());
  }
};


struct bb_pair_hash{
  size_t operator()(const bb_pair_t &bbp) const{
    return hash<uint32_t>()( ((uint32_t)bbp.first << 16) + bbp.second);
  }
};

typedef std::unordered_map <const RecordPair, uint32_t *, RecordPair_hash > JointFreqMap;
typedef std::unordered_map <const Record, uint32_t *, RecordHash> SingleFreqMap;
typedef std::unordered_map <const bb_pair_t, uint32_t , bb_pair_hash> FallThroughMap;


struct disjointSet {
  static std::unordered_map<const Record, disjointSet *,RecordHash> sets;
  deque<Record> elements;
  size_t size(){ return elements.size();}

  static void init_new_set(const Record &rec){
    sets[rec]= new disjointSet();
    sets[rec]->elements.push_back(rec);
  }

  static bool is_connected_to_right(const Record &rec){
    return sets[rec]->elements.back()!=rec;
  }

  static bool is_connected_to_left(const Record &rec){
    return sets[rec]->elements.front()!=rec;
  }

  static void mergeBasicBlocksSameFunction(func_t fid, const bb_pair_t &bb_pair);
	static void mergeFunctions(const RecordPair &rec_pair);
  static void mergeSets(const Record &left_rec, const Record &right_rec);

};

std::unordered_map<const Record, disjointSet *,RecordHash> disjointSet::sets = std::unordered_map<const Record, disjointSet *,RecordHash>();

struct SampledWindow{
  wsize_t wsize;
  list<Record> partial_trace_list;
  set<Record> owners;

  SampledWindow() :wsize(0){};

	SampledWindow(const Record &rec) :wsize(0){
		owners.insert(rec);
		push_front(rec);
	}

  ~SampledWindow(){};
  void erase(const list<Record>::iterator &trace_iter){
    partial_trace_list.erase(trace_iter);
    wsize--;
  }

  void push_front(const Record &rec){
    partial_trace_list.push_front(rec);
    wsize++;
  }

  size_t size(){
    return wsize;
  } 
};



void print_trace(list<SampledWindow> *);
void initialize_affinity_data();
void sequential_update_affinity(const Record &rec, const list<SampledWindow>::iterator& grown_window_end, bool missed);
void affinityAtExitHandler();


bool jointFreqSameFunctionsCmp(const RecordPair& pair_left, const RecordPair& pair_right);
bool jointFreqCountCmp(const RecordPair& pair_left, const RecordPair& pair_right);

bool fall_through_cmp (const bb_pair_t &left_pair, const bb_pair_t &right_pair);
uint32_t get_fall_through_count (func_t fid, const bb_pair_t &bb_pair){
	return fall_through_counts[fid][bb_pair.first*bb_count[fid]+bb_pair.second];
}

char * get_versioned_filename(const char * basename){
  char * versioned_name = new char [MAX_FILE_NAME];
  strcpy(versioned_name,basename);
  strcat(versioned_name,version_str);
  return versioned_name;
}

#endif /* AFFINITY_HPP */
