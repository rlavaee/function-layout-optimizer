//#include <sparsehash/sparse_hash_map>
//#include <sparsehash/sparse_hash_set>
#include <tr1/unordered_map>
#include <list>



//using google::sparse_hash_map;
//using google::sparse_hash_set;
//using std::tr1::hash;
using namespace std;

struct affEntry{
  short first,second;
  affEntry();
  affEntry(short,short);
};

struct eqAffEntry{
  bool operator()(affEntry const&,affEntry const&)const; 
};

struct affEntry_hash{
  size_t operator()(affEntry const&)const;
};

//typedef sparse_hash_set <int, hash<int> > intHashSet;
typedef std::tr1::unordered_map <const affEntry, int *, affEntry_hash, eqAffEntry> affinityHashMap;


typedef enum{
  FuncLevel,
  BBLevel
} ProfilingLevel;

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



struct SampledWindow{
  int wcount;
  std::list<short> partial_trace_list;
  SampledWindow(const SampledWindow&);
  SampledWindow();
};

#define totalUnits ((pLevel==FuncLevel)?(totalFuncs):((pLevel==BBLevel)?(totalBBs):0))

void initialize_affinity_data(float,short,short,short);
void sample_window(short);
void update_affinity(void);
void affinityAtExitHandler();

