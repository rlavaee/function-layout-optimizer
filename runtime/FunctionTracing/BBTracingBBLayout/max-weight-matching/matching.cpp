#include "matching.hpp"
#include <cstdint>
#include <iostream>
#include <fstream>

//by rahman lavaee

void ShortestPathSolver::Solve(){ 
  distance = vector<int>(graph.n,INT_MAX);
  parent = vector<int> (graph.n);
  last_edge = vector<int> (graph.n);
  vector<bool> changed(graph.n,false);
  bool any_changed = false;

  /* initialize distance for vertices which are connected to exposed vertices in the upper
   * position.
   * */
  for(int v=0; v < graph.n; ++v){
    if(matching.matchedToLower[v]==-1)
    for(auto edge: graph.edges[v]){
      if(distance[edge.first] > -edge.second){
        distance[edge.first] = -edge.second;
        parent[edge.first] = v;
        last_edge[edge.first] = -edge.second;
        changed[edge.first] = true;
        any_changed = true;
      }
    }
  }

   // cerr << "ANY_CHANGED" << any_changed << endl;

  while(any_changed){
    any_changed = false;
    vector<bool> new_changed(graph.n,false);

    for(int v=0; v < graph.n; ++v){
      int matched_v = matching.matchedToUpper[v];
      if(changed[v] and matched_v!=-1){
        for(auto edge: graph.edges[matched_v]){
          int new_dist = -edge.second + distance[v] + matching.matchedToUpperWeights[v];
          if(distance[edge.first] > new_dist){
            distance[edge.first] = new_dist;
            parent[edge.first] = matched_v;
            last_edge[edge.first] = -edge.second;
            new_changed[edge.first] = true;
            any_changed = true;
          }
        }
      }
    }

    changed = new_changed;
    //cerr << "ANY_CHANGED" << any_changed << endl;
  
  }

  
}

bool ShortestPathSolver::AugmentWithShortestPath(){
  int closest = -1;
  int closest_dist;
  for(int v=0; v<graph.n; ++v)
    if(matching.matchedToUpper[v]==-1 and (closest == -1 or distance[v] < closest_dist)){
      closest = v;
      closest_dist = distance[v];
    }

  if(closest_dist == INT_MAX or closest_dist >= 0)
    return false;

  int v = closest;

  //cerr << "closest is: " << closest << " dist: " << closest_dist << endl;

  int next_v;

  do{
    //cerr << "matching updating" << endl;
    next_v = matching.matchedToLower[parent[v]];
    matching.matchedToLower[parent[v]] = v;
    matching.matchedToUpper[v] = parent[v];
    matching.matchedToUpperWeights[v] = -last_edge[v];
    v = next_v;
    //cerr << "v is " << v << endl;
  }while(v != -1);

  matching.weight -= closest_dist;

  return true;

}

void MaxMatchSolver::Solve(){
  bool updated = true;
  ShortestPathSolver path_solver(*this);
  while(updated){
    /*
    for(int v=0; v< graph.n; ++v) 
      cerr << matchedToLower[v] << "\t";

    cerr << endl;
    */


    path_solver.Solve();
    updated = path_solver.AugmentWithShortestPath();
  }

}

void MaxMatchSolver::PrintMatching(){
  for(int v=0; v< graph.n; ++v){
    if(matchedToUpper[v]!=-1)
      cout << "(" << matchedToUpper[v] << "," << v << ") : " << matchedToUpperWeights[v] << endl;
  }
}

void MaxMatchSolver::RemoveCycles(){
  vector<int> mark = vector<int>(graph.n,-1);
  for(int v=0; v<graph.n; ++v){
    if(mark[v]==-1 && matchedToUpper[v]!=-1){
      int color = v;
      int min_edge = matchedToUpperWeights[v];
      int victim = v;
      while(matchedToUpper[v]!=-1){
        if(mark[v]==-1)
          mark[v]=color;
        else if(mark[v]==color){
          // found a cycle: remove the victim
          matchedToLower[matchedToUpper[victim]]=-1;
          matchedToUpper[victim] = -1;
        }else{
          // found a path: no action required
          break;
        }

        if(min_edge > matchedToUpperWeights[v]){
          min_edge = matchedToUpperWeights[v];
          victim = v;
        }
        v = matchedToUpper[v];
      }
    }
  }

}


void MaxMatchSolver::PrintPathCover(){
  for(int u=0; u<graph.n; ++u){
    if(matchedToUpper[u]==-1){
      int v = u;
      while(v!=-1){
        cout << v << "\t";
        v = matchedToLower[v];
      }
      cout << endl;
    }
  }
}

vector<vector<int> >& MaxMatchSolver::GetApproxMaxPathCover(){
	RemoveCycles();

	vector<vector<int> > path_cover;

	for(int u=0; u<graph.n; ++u){
    if(matchedToUpper[u]==-1){
			path_cover.push_back(vector<int>());
      int v = u;
      while(v!=-1){
				path_cover.back().push_back(v);
        v = matchedToLower[v];
      }
    }
  }

	return path_cover;

}

int main(){
  ifstream fin("input.in");
  int n,m, u,v,w;
  fin >> n >> m;
  BipartiteGraph g(n);
  for(int i=0;i<m;++i){
    fin >> u >> v >> w;
    g.edges[u].push_back(pair<int,int>(v,w));
  }

  MaxMatchSolver matching_solver(g);
  matching_solver.Solve();
  matching_solver.RemoveCycles();
  matching_solver.PrintMatching();
  matching_solver.PrintPathCover();
}

