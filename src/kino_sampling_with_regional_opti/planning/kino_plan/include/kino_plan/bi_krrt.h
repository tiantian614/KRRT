#ifndef _BIKRRT_H_
#define _BIKRRT_H_

#include "node_utils.h"
#include "kdtree.h"
#include "visualization_utils/visualization_utils.h"
#include "occ_grid/pos_checker.h"
#include "poly_traj_utils/traj_utils.hpp"
#include "bvp_solver.h"
#include "bias_sampler.h"
#include "poly_opt/traj_optimizer.h"
#include "r3_plan/a_star_search.h"

#include <vector>
#include <stack>

using Eigen::Matrix2d;
using Eigen::Matrix3d;
using Eigen::MatrixXd;
using Eigen::Vector2d;
using Eigen::Vector2i;
using Eigen::Vector3d;
using Eigen::Vector3i;
using Eigen::VectorXd;
using std::list;
using std::pair;
using std::stack;
using std::vector;

namespace kino_planner
{

//介绍BIKRRT的类
//BIKRRT类是一个路径规划器，用于规划无人机的轨迹
//BIKRRT类的成员变量有：
//  1. sampler_：BiasSampler类的对象，用于采样
//  2. traj_：Trajectory类的对象，用于存储规划的轨迹
//  3. first_traj_：Trajectory类的对象，用于存储第一次规划的轨迹
//  4. start_node_：RRTNodePtr类的对象，用于存储起始节点
//  5. goal_node_：RRTNodePtr类的对象，用于存储目标节点
//  6. close_goal_node_：RRTNodePtr类的对象，用于存储最近的目标节点
//  7. valid_start_tree_node_nums_：int类型，用于存储有效的起始树节点数
//  8. valid_sample_nums_：int类型，用于存储有效的采样数
//  9. final_traj_use_time_：double类型，用于存储最终轨迹的使用时间
//  10. first_traj_use_time_：double类型，用于存储第一次轨迹的使用时间
//  11. test_convergency_：bool类型，用于存储是否测试收敛性
//  12. traj_list_：vector<Trajectory>类型，用于存储轨迹列表
class BIKRRT
{
public:
  BIKRRT();
  BIKRRT(const ros::NodeHandle &nh);
  ~BIKRRT();

  // api
  void reset();
  void init(const ros::NodeHandle &nh);
  void setPosChecker(const PosChecker::Ptr &checker);
  void setVisualizer(const VisualRviz::Ptr &vis);
  void setRegionalOptimizer(const TrajOptimizer::Ptr &optimizer_)
  {
    optimizer_ptr_ = optimizer_;
  };
  void setSearcher(const std::shared_ptr<AstarPathFinder> &searcher)
  {
    searcher_ = searcher;
  }
  int plan(Vector3d start_pos, Vector3d start_vel, Vector3d start_acc,
           Vector3d end_pos, Vector3d end_vel, Vector3d end_acc,
           double search_time);
  void getTraj(Trajectory &traj)
  {
    traj = traj_;
  };
  void getFirstTraj(Trajectory &traj)
  {
    traj = first_traj_;
  };
  double getFirstTrajTimeUsage()
  {
    return first_traj_use_time_;
  };
  double getFinalTrajTimeUsage()
  {
    return final_traj_use_time_;
  };
  int getSampleNum()
  {
    return valid_sample_nums_;
  };
  int getTreeNodeNum()
  {
    return valid_start_tree_node_nums_;
  };
  void getConvergenceInfo(vector<Trajectory>& traj_list, vector<double>& solution_cost_list, vector<double>& solution_time_list)
  {
    traj_list = traj_list_;
    solution_time_list = solution_time_list_;
    solution_cost_list = solution_cost_list_;
  };

  // evaluation
  double evaluateTraj(const Trajectory& traj, double &traj_duration, double &traj_length, int &seg_nums, double &acc_integral, double &jerk_integral);

  // bias_sampler
  BiasSampler sampler_;

  enum
  {
    FAILURE = 0, 
    SUCCESS = 1, 
    SUCCESS_CLOSE_GOAL = 2
  };
  typedef shared_ptr<BIKRRT> BIKRRTPtr;

private:
  int rrtStar(const StatePVA &x_init, const StatePVA &x_final, int n, double search_time, double radius, const bool rewire);
  double dist(const StatePVA &x0, const StatePVA &x1);
  void fillTraj(const RRTNodePtr &goal_leaf, Trajectory& traj);
  void fillTraj(const RRTNodePtr &bridge_node_start_tree, const RRTNodePtr &bridge_node_goal_tree, Trajectory& traj);
  void chooseBypass(RRTNodePtr &goal_leaf, const RRTNodePtr &tree_start_node);
  RRTNodePtr addTreeNode(RRTNodePtr& parent, const StatePVA& state, const Piece& piece, 
                        const double& cost_from_start, const double& tau_from_start, 
                        const double& cost_from_parent, const double& tau_from_parent);
  RRTNodePtr addTreeNode(RRTNodePtr& parent, const StatePVA& state, const Piece& piece, 
                        const double& cost_from_parent, const double& tau_from_parent);
  void changeNodeParent(RRTNodePtr& node, RRTNodePtr& parent, const Piece& piece, 
                        const double& cost_from_parent, const double& tau_from_parent);
  bool regionalOpt(const Piece& oringin_seg, const pair<Vector3d, Vector3d>& collide_pts_one_seg, const pair<double, double>& t_s_e);

  struct regionalCandidate
  {
    regionalCandidate();
    regionalCandidate(const RRTNodePtr &parent, const pair<Vector3d, Vector3d> &collide_pts, 
                      const pair<double, double> &collide_timestamp, const Piece &regional_seg, const double &heu) 
                      : parent(parent) , collide_pts(collide_pts), collide_timestamp(collide_timestamp), regional_seg(regional_seg), heu(heu) {};
    RRTNodePtr parent;
    pair<Vector3d, Vector3d> collide_pts;
    pair<double, double> collide_timestamp;
    Piece regional_seg;
    double heu;
    bool operator < (const regionalCandidate& candidate) const
    {
      return heu > candidate.heu; //small cost first
    }
  };
  
  // vis
  ros::Time t_start_, t_end_;
  bool debug_vis_;
  VisualRviz::Ptr vis_ptr_;
  void sampleWholeTree(const RRTNodePtr &root, vector<StatePVA> *vis_x, vector<Vector3d>& knots);

  RRTNodePtrVector node_pool_; //pre allocated in Constructor
  Trajectory traj_;
  Trajectory first_traj_; //initialized when first path found
  RRTNodePtr start_node_, goal_node_, close_goal_node_;
  int valid_start_tree_node_nums_, valid_sample_nums_;
  double final_traj_use_time_, first_traj_use_time_;
  bool test_convergency_;
  vector<Trajectory> traj_list_;
  vector<double> solution_cost_list_;
  vector<double> solution_time_list_;

  // radius for for/backward search
  double getForwardRadius(double tau, double cost);
  double getBackwardRadius(double tau, double cost);
  struct kdres *getForwardNeighbour(const StatePVA &x1, struct kdtree *kd_tree, double tau, double radius_p);
  struct kdres *getBackwardNeighbour(const StatePVA &x1, struct kdtree *kd_tree, double tau, double radius_p);

  // nodehandle params
  double radius_cost_between_two_states_;
  double rho_;
  double v_mag_sample_;
  double vel_limit_, acc_limit_, jerk_limit_;
  bool allow_close_goal_, stop_after_first_traj_found_, rewire_, use_regional_opt_;
  double search_time_;
  int tree_node_nums_;

  // environment
  PosChecker::Ptr pos_checker_ptr_;
  bool checkSegmentConstraints(const Piece &seg);
  bool getTraversalLines(const Piece &seg, vector<pair<Vector3d, Vector3d>> &traversal_lines);

  // bvp_solver
  BVPSolver::IntegratorBVP bvp_;

  // // bias_sampler
  // BiasSampler sampler_;

  // regional optimizer
  TrajOptimizer::Ptr optimizer_ptr_;
  std::shared_ptr<AstarPathFinder> searcher_;
};

} // namespace kino_planner

#endif //_BIKRRT_H_
