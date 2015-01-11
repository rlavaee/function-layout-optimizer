#include <vector>

using namespace std;

class BipartiteGraph {
  public:
    BipartiteGraph(int _n): n(_n){
      edges = vector<vector<pair<int,int> > >(n,vector<pair<int,int> >());
    }
    /* number of vertices in each partition */
    int n; 
    
    /* all the edges stored in the form of an incidence matrix
     * (all downward edges from upper partition to the lower)
     */
    vector< vector <pair<int,int> > > edges;
};


class MaxMatchSolver {
  public:
    /* reference to the graph */
    const BipartiteGraph& graph;

    MaxMatchSolver(const BipartiteGraph& _graph): graph(_graph){
      matchedToUpper = vector<int> (graph.n, -1);
      matchedToLower = vector<int> (graph.n, -1);
      matchedToUpperWeights = vector<int> (graph.n);
      weight = 0;
    }

    /* the matching, all considered to be upward edges */
    vector<int> matchedToUpper;

    vector<int> matchedToLower;

    vector<int> matchedToUpperWeights;

		vector<vector<int> > pathCover;


    /* total weight of the matching */
    int weight;


    void Solve();
    void PrintMatching();

    void RemoveCycles();

    void PrintPathCover();

		void  GetApproxMaxPathCover();

};

/* shortest path solver (using bellman-ford) */
class ShortestPathSolver {
  private:
    /* reference to the max matching solver */
    MaxMatchSolver& matching;

    /* parent in the shortest path tree, 
     * only defined for the vertices in the lower partition,
     * the parent for the vertices in the upper partition come 
     * from the edges in the matching 
     * */
    vector<int> parent;

    /* shortest distance from any exposed vertex in the upper partition,
     * only defined for the vertices in the lower partition
     * */
    vector<int> distance;

    vector<int> last_edge;

  public:
    ShortestPathSolver(MaxMatchSolver& _matching): matching(_matching) {};

    /* solve the shortest path problem from exposed vertices in the upper partition */
    void Solve();

    bool AugmentWithShortestPath();


};
