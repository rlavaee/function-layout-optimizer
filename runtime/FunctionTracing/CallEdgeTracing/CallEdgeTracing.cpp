#include "CallEdgeTracing.hpp"
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <vector>

using namespace std;

CGMap * cg_edges;

//FILE * cg_trace;


void read_graph(){
		FILE * graphFile=fopen("graph.cgc","r");
  	if(graphFile!=NULL){
			short u1,u2;
    	int freq;
    	while(fscanf(graphFile,"%hd",&u1)!=EOF){
				fscanf(graphFile,"%hd\t%d",&u2,&freq);
				(*cg_edges)[make_pair(u1,u2)]=freq;
			}
			fclose(graphFile);
		}
}


void write_graph(){
	FILE * graphFile=fopen("graph.cgc","w");
	CGMap::iterator iter;
	for(iter=cg_edges->begin(); iter!=cg_edges->end(); ++iter){
		fprintf(graphFile,"%hd %hd\t%d\n",iter->first.first,iter->first.second,iter->second);
	}
	fclose(graphFile);
}



//disjointSet ** sets;


const char * version_str=".cgc";


void disjointSet::mergeSets(disjointSet * set1, disjointSet* set2){

		disjointSet * merger = (set1->size()>=set2->size())?(set1):(set2);
		
		disjointSet * mergee = (set1->size()<set2->size())?(set1):(set2);

		shortpair frontMerger_backMergee= make_pair(merger->elements.front(), mergee->elements.back());
		shortpair backMerger_backMergee= make_pair(merger->elements.back(), mergee->elements.back());
		shortpair backMerger_frontMergee= make_pair(merger->elements.back(), mergee->elements.front());
		shortpair frontMerger_frontMergee= make_pair(merger->elements.front(), mergee->elements.front());
		shortpair conAffEntriesArray[4]={frontMerger_frontMergee, frontMerger_backMergee, backMerger_frontMergee, backMerger_backMergee};
		std::vector<shortpair> conAffEntries(conAffEntriesArray,conAffEntriesArray+4);
		std::sort(conAffEntries.begin(), conAffEntries.end(), CGECmp);

		assert(CGECmp(conAffEntries[0],conAffEntries[1]) || eqshortpair()(conAffEntries[0],conAffEntries[1]));
		assert(CGECmp(conAffEntries[1],conAffEntries[2]) || eqshortpair()(conAffEntries[1],conAffEntries[2]));
		assert(CGECmp(conAffEntries[2],conAffEntries[3]) || eqshortpair()(conAffEntries[2],conAffEntries[3]));

		bool con_mergee_front = eqshortpair()(conAffEntries[0], backMerger_frontMergee) || eqshortpair()(conAffEntries[0],frontMerger_frontMergee);
		bool con_merger_front = eqshortpair()(conAffEntries[0],frontMerger_frontMergee) || eqshortpair()(conAffEntries[0],frontMerger_backMergee);

		//fprintf(stderr, "Now merging:");
		if(con_mergee_front){
		
			for(deque<short>::iterator it=mergee->elements.begin(); it!=mergee->elements.end(); ++it){
				//fprintf(stderr, "%hd ",*it);
				if(con_merger_front)
						merger->elements.push_front(*it);
				else
						merger->elements.push_back(*it);
				disjointSet::sets[*it]=merger;
			}
		}else{
			//fprintf(stderr,"(backwards)");
			for(deque<short>::reverse_iterator rit=mergee->elements.rbegin(); rit!=mergee->elements.rend(); ++rit){
					//fprintf(stderr, "%hd ",*rit);
				if(con_merger_front)
						merger->elements.push_front(*rit);
				else
						merger->elements.push_back(*rit);
				disjointSet::sets[*rit]=merger;
			}
		}


		mergee->elements.clear();
		delete mergee;
}

/*
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
 */

extern "C" void trace_call_edge(short caller, short callee){		
	if(cg_edges==NULL)
		return;
	if(caller!=callee){
            pair<short,short> call_entry = make_pair(caller, callee);

            CGMap::iterator  result=cg_edges->find(call_entry);
            if(result==cg_edges->end())
              (*cg_edges)[call_entry]= 1;
            else
              (*cg_edges)[call_entry]++;
			//fprintf(stderr,"the result is %d\n",cg_edges[call_entry]);
          }
	//if(cg_trace)
	//fprintf(cg_trace,"%d ",callee);
}

void print_optimal_layout(){

	short * layout = new short[totalFuncs];
	int count = 0;
	for(int i=0; i< totalFuncs; ++i){
		//printf("i is now %d\n",i);
		if(disjointSet::sets[i])
			for(deque<short>::iterator it=disjointSet::sets[i]->elements.begin(), 
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

  //fprintf(stderr,"size is %d\n",cg_edges->size());

  std::vector<shortpair> all_cg_edges;
  for(iter=cg_edges->begin(); iter!=cg_edges->end(); ++iter){
 		all_cg_edges.push_back(iter->first);
		//fprintf(stderr,"(%d,%d) -> %d\n",iter->first.first,iter->first.second, iter->second);
  }

	std::sort(all_cg_edges.begin(),all_cg_edges.end(),CGECmp);

	disjointSet::sets = new disjointSet *[totalFuncs];
	for(short i=0; i<totalFuncs; ++i)
		disjointSet::init_new_set(i);

	orderFile= fopen("order.cgc","w");
	for(std::vector<shortpair>::iterator it=all_cg_edges.begin(); it!=all_cg_edges.end(); ++it){
    	fprintf(orderFile,"(%d,%d)\n",it->first,it->second);
		//printf("considering edge (%d,%d) -> %d\n",all_cg_edges[cge].first.first,all_cg_edges[cge].first.second, all_cg_edges[cge].second);
		if(disjointSet::get_min_index(it->first)+disjointSet::get_min_index(it->second) < 10){
			disjointSet::mergeSets(it->first,it->second);
			disjointSet::print_layout(it->first);
		}
	}

	fclose(orderFile);


}

void do_exit(){
	find_optimal_layout();
	print_optimal_layout();
	write_graph();
	//fclose(cg_trace);
	//cg_edges->serialize(CGSerializer(),cg_file);
	//fprintf(stderr,"sent to file\n");
	//fclose(cg_file);
	
}

extern "C" void set_bb_count_for_fid(short,short){}

extern "C" void start_call_edge_tracing(short _totalFuncs){
	cg_edges = new CGMap();
	totalFuncs=_totalFuncs;
	read_graph();
	//cg_trace = fopen("cg_trace.txt","w");

	//FILE * cg_file=fopen("graph.cgc","r");
	//if(cg_file){
	//	cg_edges->unserialize(CGSerializer(),cg_file);
	//	fclose(cg_file);
	//}
	atexit(do_exit);
}
bool CGECmp(const shortpair &s1, const shortpair &s2){
	//fprintf(stderr,"--------------\n");
	//fprintf(stderr,"first:%d second:%d weight:%d\n",s1.first,s1.second,(*cg_edges)[s1]);
	//fprintf(stderr,"first:%d second:%d weight:%d\n",s2.first,s2.second,(*cg_edges)[s2]);

	if((cg_edges->find(s1)!=cg_edges->end()) || (cg_edges->find(s2)!=cg_edges->end())){
		if(cg_edges->find(s1)==cg_edges->end())
			return false;
		if(cg_edges->find(s2)==cg_edges->end())
			return true;
		int s1w=(*cg_edges)[s1];
		int s2w=(*cg_edges)[s2];

		if(s1w > s2w)
			return true;
		if(s1w < s2w)
			return false;
	}

	if(s1.first > s2.first)
		return true;
	if(s1.first < s2.first)
		return false;
	
	return (s1.second > s2.second);

}


