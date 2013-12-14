#include <google/sparse_hash_map>
#include <boost/functional/hash.hpp>

using google::sparse_hash_map;

typedef std::pair<short,short> shortpair;
struct eqshortpair{
	bool operator()(shortpair s1, shortpair s2) const {
		if ((s1.first == s2.first) && (s1.second == s2.second))
			return true;
		if ((s1.second == s2.first) && (s1.first == s2.second))
			return true;
		return false;
	}
};

typedef sparse_hash_map< shortpair, int, boost::hash<shortpair>, eqshortpair> CGMap;
typedef std::pair< shortpair, int> CGE;

struct disjointSet {
	static disjointSet ** sets;
	std::vector<short> elements;
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


int CGECmp(const void * left, const void * right){
	const CGE * cge_left = (const CGE *) left;
	const CGE * cge_right = (const CGE *) right;
	
	return cge_left->second - cge_right->second;
}

