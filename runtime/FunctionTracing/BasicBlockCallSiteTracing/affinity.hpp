//#include <sparsehash/sparse_hash_map>
//#include <sparsehash/sparse_hash_set>

#ifndef AFFINITY_HPP
#define AFFINITY_HPP
#include <deque>
#include <list>
#include <functional>
#include <unordered_map>
#include <stdint.h>
using namespace std;

typedef uint16_t wsize_t;
typedef uint16_t func_t;
typedef uint16_t bb_t;
func_t totalFuncs;
bb_t * bb_count;

class Record{
  func_t fid;
  bb_t bbid;
  public:
    Record(){};
    Record(func_t a, bb_t b): fid(a),bbid(b) {};
    Record(const Record &rec): fid(rec.getFuncId()), bbid(rec.getBBId()){};

    func_t getFuncId() const{
      return fid;
    }

    bb_t getBBId() const{
      return bbid;
    }

    bool operator < (const Record &rhs) const{
      if(getFuncId() < rhs.getFuncId())
        return true;
      if(getFuncId() > rhs.getFuncId())
        return false;
      if(getBBId() < rhs.getBBId())
        return true;
      return false;
    }

    bool operator == (const Record &rhs) const{
      return getFuncId()==rhs.getFuncId() && getBBId()==rhs.getBBId();
    }

    bool operator != (const Record &rhs) const {return !(*this==rhs);}

};

typedef pair<Record,Record> RecordPair;

struct RecordHash{
  size_t operator()(const Record& rec) const{
    return hash<uint32_t>()( ((uint32_t)rec.getFuncId() << 16) + rec.getBBId());
  }
};

/*
   struct affEntry{
   func_t first,second;
   affEntry();
   affEntry(func_t,func_t);
   affEntry(const affEntry&);
   affEntry& operator= (const affEntry&);
   bool operator== (const affEntry&) const;
   };
   */


RecordPair unordered_RecordPair(Record s1,Record s2){
  return (s1<s2)?(RecordPair(s1,s2)):(RecordPair(s2,s1));
}

struct RecordPair_eq{
  bool operator()(const RecordPair &s1, const RecordPair &s2) const {
    if ((s1.first == s2.first) && (s1.second == s2.second))
      return true;
    if ((s1.second == s2.first) && (s1.first == s2.second))
      return true;
    return false;
  }
};


struct RecordPair_hash{
  size_t operator()(const RecordPair& s) const{
    Record smaller=(s.first<s.second)?(s.first):(s.second);
    Record bigger=(s.first<s.second)?(s.second):(s.first);
    return hash<uint32_t>()(totalFuncs*RecordHash()(smaller) + RecordHash()(bigger));
  }
};

/*
   struct RecordPair_eq{
   bool operator()(const RecordPair &s1, const RecordPair &s2) const{
   return (s1.first == s2.first) && (s1.second == s2.second);
   }
   };
   */

typedef std::unordered_map <const RecordPair, uint32_t *, RecordPair_hash, RecordPair_eq > JointFreqMap;
typedef std::unordered_map <const RecordPair, uint32_t **, RecordPair_hash, RecordPair_eq > JointFreqRangeMap;
typedef std::unordered_map <const Record, uint32_t *, RecordHash> SingleFreqMap;
typedef std::unordered_map <const Record, uint32_t **, RecordHash> SingleFreqRangeMap;

struct SingleUpdateEntry{
  Record rec;
  wsize_t min_wsize;
  SingleUpdateEntry(const Record &a, wsize_t b): rec(a), min_wsize(b){}
};

struct JointUpdateEntry{
  RecordPair rec_pair;
  wsize_t min_wsize;
  JointUpdateEntry(const RecordPair &a, wsize_t b):rec_pair(a), min_wsize(b){}
};


struct disjointSet {
  static std::unordered_map<const Record, disjointSet *,RecordHash> sets;
  deque<Record> elements;
  size_t size(){ return elements.size();}
  static void mergeSets(disjointSet *, disjointSet *);
  static void mergeSets(Record rec1, Record rec2){
    if(sets[rec1]!=sets[rec2])
      mergeSets(sets[rec1],sets[rec2]);
  }

  static void init_new_set(const Record rec){
    sets[rec]= new disjointSet();
    sets[rec]->elements.push_back(rec);
  }

};
  
std::unordered_map<const Record, disjointSet *,RecordHash> disjointSet::sets = std::unordered_map<const Record, disjointSet *,RecordHash>();


struct SampledWindow{
  wsize_t wsize;
  list<Record> partial_trace_list;
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
void initialize_affinity_data();
wsize_t sequential_update_affinity(list<SampledWindow>::iterator);
void affinityAtExitHandler();


bool (*affEntryCmp)(const RecordPair& pair_left, const RecordPair& pair_right);
//bool affEntry1DCmp(const RecordPair& pair_left, const RecordPair& pair_right);
bool affEntry2DCmp(const RecordPair& pair_left, const RecordPair& pair_right);
#endif /* AFFINITY_HPP */
