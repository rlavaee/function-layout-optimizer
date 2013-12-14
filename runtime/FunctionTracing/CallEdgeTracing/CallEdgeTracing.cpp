#include "CallEdgeTracing.hpp"
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <cstring>
#include <new>
#include <cstdio>

using namespace std;

short totalFuncs;
CGMap cg_edges;

//disjointSet ** sets;

FILE * graphFile, * debugFile;

const char * version_str=".cgc";


void disjointSet::mergeSets(disjointSet * set1, disjointSet* set2){

		disjointSet * merger = (set1->size()>=set2->size())?(set1):(set2);
		
		disjointSet * mergee = (set1->size()<set2->size())?(set1):(set2);

		for(vector<short>::iterator it=mergee->elements.begin(); it!=mergee->elements.end(); ++it){
			merger->elements.push_back(*it);
			sets[*it]=merger;
		}

		mergee->elements.clear();
		delete mergee;
}


struct CGSerializer {
  bool operator()(FILE * fp, const std::pair< pair<short,short>, int>& value) const{
  	if((fwrite(&value.first.first, sizeof(value.first.first), 1, fp) != 1) || (fwrite(&value.first.second, sizeof(value.first.second), 1, fp)!=1) )
  		return false;
  	if(fwrite(&value.second, sizeof(value.second), 1, fp) != 1)
  		return false;
  	return true;
  }

  bool operator()(FILE* fp, std::pair<const pair<short,short>, int>* value)const{
 	if(fread(const_cast<short*>(&value->first.first), sizeof(value->first.first), 1, fp) != 1)
  		return false;
  	if(fread(const_cast<short*>(&value->first.second), sizeof(value->first.second), 1, fp) != 1)
  		return false;
  	if(fread(const_cast<int*>(&value->second), sizeof(value->second), 1, fp) != 1)
  		return false;
	return true;
  }
};
  

extern "C" void trace_call_edge(short caller, short callee){		
	if(caller!=callee){
            pair<short,short> call_entry(caller, callee);

            CGMap::iterator  result=cg_edges.find(call_entry);
            if(result==cg_edges.end())
              cg_edges[call_entry]= 1;
            else
              cg_edges[call_entry]++;
			//fprintf(stderr,"the result is %d\n",cg_edges[call_entry]);
          }
}

void print_optimal_layout(){

	short * layout = new short[totalFuncs];
	int count=0;
	for(int i=0; i< totalFuncs; ++i){
		//printf("i is now %d\n",i);
		if(disjointSet::sets[i])
			for(vector<short>::iterator it=disjointSet::sets[i]->elements.begin(), 
					it_end=disjointSet::sets[i]->elements.end()
					; it!=it_end ; ++it){
					//printf("this is *it:%d\n",*it);
				layout[count++]=*it;
				disjointSet::sets[*it]=0;
			}
	}
	

  char * layoutFilePath = (char *) malloc(strlen("layout")+strlen(version_str)+1);
  strcpy(layoutFilePath,"layout");
  strcat(layoutFilePath,version_str);

  FILE *layoutFile = fopen(layoutFilePath,"w");  

  for(int i=0;i<totalFuncs;++i){
    if(i%20==0)
      fprintf(layoutFile, "\n");
    fprintf(layoutFile, "%u ",layout[i]);
  }
  fclose(layoutFile);
}


void find_optimal_layout(){
  CGMap::iterator iter;

  //fprintf(stderr,"size is %d\n",cg_edges.size());

  CGE * all_cg_edges = new CGE[cg_edges.size()];
  int count=0;
  for(iter=cg_edges.begin(); iter!=cg_edges.end(); ++iter){
 		all_cg_edges[count++]=*iter;
		//fprintf(stderr,"(%d,%d) -> %d\n",iter->first.first,iter->first.second, iter->second);
  }

	qsort(all_cg_edges,cg_edges.size(),sizeof(CGE),CGECmp);

	disjointSet::sets = new disjointSet *[totalFuncs];
	for(short i=0; i<totalFuncs; ++i)
		disjointSet::init_new_set(i);

	for(size_t cge=0; cge< cg_edges.size(); ++cge){
		//printf("considering edge (%d,%d) -> %d\n",all_cg_edges[cge].first.first,all_cg_edges[cge].first.second, all_cg_edges[cge].second);
		disjointSet::mergeSets(all_cg_edges[cge].first.first,all_cg_edges[cge].first.second);
	}

	delete [] all_cg_edges;

}

void do_exit(){
	find_optimal_layout();
	print_optimal_layout();
	FILE * cg_file=fopen("graph.cgc","w");
	cg_edges.serialize(CGSerializer(),cg_file);
	//fprintf(stderr,"sent to file\n");
	fclose(cg_file);
}


extern "C" void do_init(short _totalFuncs){
	totalFuncs=_totalFuncs;
	FILE * cg_file=fopen("graph.cgc","r");
	if(cg_file){
		cg_edges.unserialize(CGSerializer(),cg_file);
		fclose(cg_file);
	}
	atexit(do_exit);
}

