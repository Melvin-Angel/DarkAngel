#include <darkangel/behavior_tree.hpp>
#include <behaviortree_cpp/bt_factory.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace darkangel {
namespace {
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
// Native blackboard shared by the leaves of one tree instance.
struct Blackboard {
    const ControllerView* view{};const PerceivedActor* foe{};ControllerCommand command;std::shared_ptr<const NavigationMesh> mesh;
    std::vector<MotorVec> path;std::size_t next{1};std::uint64_t repath{};
    // The leaf that decided this tick, for the debug readout.
    std::string trace;
    // The opponent is the nearest live hostile the actor detects, chosen once per decision.
    bool opponent()const{return view->alive&&foe;}
    double distance()const{return std::hypot(foe->motor.position.x-view->self.position.x,foe->motor.position.z-view->self.position.z);}
    // Walk toward a point: along the navigation mesh when there is one, straight otherwise. False when no path exists.
    bool steer(const MotorVec& goal){
        const auto& self=view->self.position;MotorVec corner=goal;
        if(mesh){
            if(path.size()<2||view->tick>=repath){auto found=mesh->path(self,goal);path=found?std::move(*found):std::vector<MotorVec>{};next=1;repath=view->tick+15;}
            if(path.size()<2)return false;
            while(next+1<path.size()&&std::hypot(path[next].x-self.x,path[next].z-self.z)<.3)++next;corner=path[next];
        }
        const double x=corner.x-self.x,z=corner.z-self.z,length=std::hypot(x,z);if(length>1e-6){command.move_x=x/length;command.move_z=z/length;command.face=std::atan2(x,z);}
        return true;
    }
};
class OpponentAlive final:public BT::ConditionNode {
public:OpponentAlive(const std::string& name,const BT::NodeConfig& config,Blackboard* board):BT::ConditionNode(name,config),board_(board){}
    static BT::PortsList providedPorts(){return {};}
    BT::NodeStatus tick()override{return board_->opponent()?BT::NodeStatus::SUCCESS:BT::NodeStatus::FAILURE;}
private:Blackboard* board_;
};
class OpponentVisible final:public BT::ConditionNode {
public:OpponentVisible(const std::string& name,const BT::NodeConfig& config,Blackboard* board):BT::ConditionNode(name,config),board_(board){}
    static BT::PortsList providedPorts(){return {};}
    BT::NodeStatus tick()override{return board_->opponent()&&board_->foe->visible?BT::NodeStatus::SUCCESS:BT::NodeStatus::FAILURE;}
private:Blackboard* board_;
};
class Idle final:public BT::ConditionNode {
public:Idle(const std::string& name,const BT::NodeConfig& config,Blackboard* board):BT::ConditionNode(name,config),board_(board){}
    static BT::PortsList providedPorts(){return {};}
    BT::NodeStatus tick()override{return board_->view->busy?BT::NodeStatus::FAILURE:BT::NodeStatus::SUCCESS;}
private:Blackboard* board_;
};
class InRange final:public BT::ConditionNode {
public:InRange(const std::string& name,const BT::NodeConfig& config,Blackboard* board):BT::ConditionNode(name,config),board_(board){}
    static BT::PortsList providedPorts(){return {BT::InputPort<double>("range")};}
    BT::NodeStatus tick()override{return board_->opponent()&&board_->distance()<=getInput<double>("range").value()?BT::NodeStatus::SUCCESS:BT::NodeStatus::FAILURE;}
private:Blackboard* board_;
};
class Face final:public BT::SyncActionNode {
public:Face(const std::string& name,const BT::NodeConfig& config,Blackboard* board):BT::SyncActionNode(name,config),board_(board){}
    static BT::PortsList providedPorts(){return {};}
    BT::NodeStatus tick()override{if(!board_->opponent())return BT::NodeStatus::FAILURE;const auto& self=board_->view->self.position;const auto& goal=board_->foe->motor.position;if(board_->distance()>1e-6)board_->command.face=std::atan2(goal.x-self.x,goal.z-self.z);return BT::NodeStatus::SUCCESS;}
private:Blackboard* board_;
};
class Attack final:public BT::SyncActionNode {
public:Attack(const std::string& name,const BT::NodeConfig& config,Blackboard* board):BT::SyncActionNode(name,config),board_(board){}
    static BT::PortsList providedPorts(){return {BT::InputPort<unsigned>("slot")};}
    // The press is a request; authority accepts or refuses it like a player's.
    BT::NodeStatus tick()override{if(board_->view->busy)return BT::NodeStatus::FAILURE;board_->command.press=static_cast<CombatSlot>(getInput<unsigned>("slot").value());board_->trace="Attack";return BT::NodeStatus::SUCCESS;}
private:Blackboard* board_;
};
class Recover final:public BT::StatefulActionNode {
public:Recover(const std::string& name,const BT::NodeConfig& config,Blackboard* board):BT::StatefulActionNode(name,config),board_(board){}
    static BT::PortsList providedPorts(){return {BT::InputPort<unsigned>("ticks")};}
    BT::NodeStatus onStart()override{until_=board_->view->tick+getInput<unsigned>("ticks").value();return BT::NodeStatus::RUNNING;}
    BT::NodeStatus onRunning()override{if(board_->view->tick>=until_)return BT::NodeStatus::SUCCESS;board_->trace="Recover ("+std::to_string(until_-board_->view->tick)+" ticks left)";return BT::NodeStatus::RUNNING;}
    void onHalted()override{}
private:Blackboard* board_;std::uint64_t until_{};
};
class Approach final:public BT::StatefulActionNode {
public:Approach(const std::string& name,const BT::NodeConfig& config,Blackboard* board):BT::StatefulActionNode(name,config),board_(board){}
    static BT::PortsList providedPorts(){return {BT::InputPort<double>("range")};}
    BT::NodeStatus onStart()override{board_->path.clear();board_->repath=0;return onRunning();}
    BT::NodeStatus onRunning()override{
        auto& b=*board_;if(!b.opponent())return BT::NodeStatus::FAILURE;if(b.distance()<=getInput<double>("range").value()&&b.foe->visible)return BT::NodeStatus::SUCCESS;if(b.view->busy)return BT::NodeStatus::RUNNING;
        if(!b.steer(b.foe->motor.position))return BT::NodeStatus::FAILURE;
        b.trace=b.mesh?"Approach along the navigation mesh":"Approach";return BT::NodeStatus::RUNNING;
    }
    void onHalted()override{board_->path.clear();}
private:Blackboard* board_;
};
// Go and look: walk to where the hostile the NPC lost most recently was last detected.
class Investigate final:public BT::StatefulActionNode {
public:Investigate(const std::string& name,const BT::NodeConfig& config,Blackboard* board):BT::StatefulActionNode(name,config),board_(board){}
    static BT::PortsList providedPorts(){return {};}
    BT::NodeStatus onStart()override{board_->path.clear();board_->repath=0;return onRunning();}
    BT::NodeStatus onRunning()override{
        auto& b=*board_;const auto* lost=b.view->alive?last_lost_hostile(*b.view):nullptr;if(!lost)return BT::NodeStatus::FAILURE;
        const auto& self=b.view->self.position;if(std::hypot(lost->motor.position.x-self.x,lost->motor.position.z-self.z)<=1.)return BT::NodeStatus::SUCCESS;if(b.view->busy)return BT::NodeStatus::RUNNING;
        if(!b.steer(lost->motor.position))return BT::NodeStatus::FAILURE;b.trace="Investigate the last known position";return BT::NodeStatus::RUNNING;
    }
    void onHalted()override{board_->path.clear();}
private:Blackboard* board_;
};
void write(std::ostringstream& xml,const BehaviorNode& node,const BehaviorTuning& tuning){
    const double range=node.preferred?tuning.preferred_range:node.range;
    if(node.type=="sequence"||node.type=="selector"){const char* tag=node.type=="sequence"?"Sequence":"Fallback";xml<<'<'<<tag<<'>';for(const auto& child:node.children)write(xml,child,tuning);xml<<"</"<<tag<<'>';}
    else if(node.type=="opponent_alive")xml<<"<DaOpponentAlive/>";
    else if(node.type=="opponent_visible")xml<<"<DaOpponentVisible/>";
    else if(node.type=="idle")xml<<"<DaIdle/>";
    else if(node.type=="face")xml<<"<DaFace/>";
    else if(node.type=="in_range")xml<<"<DaInRange range=\""<<range<<"\"/>";
    else if(node.type=="approach")xml<<"<DaApproach range=\""<<range<<"\"/>";
    else if(node.type=="attack")xml<<"<DaAttack slot=\""<<static_cast<unsigned>(node.slot)<<"\"/>";
    else if(node.type=="investigate")xml<<"<DaInvestigate/>";
    else if(node.type=="recover")xml<<"<DaRecover ticks=\""<<std::max(1u,unsigned(std::lround(node.ticks*tuning.recovery_scale)))<<"\"/>";
    else throw std::runtime_error("Unknown behavior node type");
}
class BehaviorController final:public ActorController {
public:
    BehaviorController(const BehaviorDefinition& definition,std::shared_ptr<const NavigationMesh> mesh,const BehaviorTuning& tuning){
        board_.mesh=std::move(mesh);BT::BehaviorTreeFactory factory;
        factory.registerNodeType<OpponentAlive>("DaOpponentAlive",&board_);factory.registerNodeType<OpponentVisible>("DaOpponentVisible",&board_);factory.registerNodeType<Idle>("DaIdle",&board_);factory.registerNodeType<InRange>("DaInRange",&board_);factory.registerNodeType<Face>("DaFace",&board_);
        factory.registerNodeType<Attack>("DaAttack",&board_);factory.registerNodeType<Recover>("DaRecover",&board_);factory.registerNodeType<Approach>("DaApproach",&board_);factory.registerNodeType<Investigate>("DaInvestigate",&board_);
        std::ostringstream xml;xml.precision(17);xml<<"<root BTCPP_format=\"4\"><BehaviorTree ID=\"Main\">";write(xml,definition.root,tuning);xml<<"</BehaviorTree></root>";
        tree_=factory.createTreeFromText(xml.str());
    }
    ControllerCommand decide(const ControllerView& view)override{board_.view=&view;board_.foe=view.alive?nearest_hostile(view):nullptr;board_.command={};if(board_.foe)board_.command.target=board_.foe->id;board_.trace=view.alive?(view.busy?"Acting":"Idle"):"Defeated";tree_.tickOnce();board_.view=nullptr;board_.foe=nullptr;return board_.command;}
    std::string status()const override{return board_.trace;}
private:Blackboard board_;BT::Tree tree_;
};
}
std::shared_ptr<ActorController> make_behavior_controller(const BehaviorDefinition& definition,std::shared_ptr<const NavigationMesh> mesh,BehaviorTuning tuning){
    require(!definition.root.type.empty(),"Behavior controller needs a decoded tree");validate_behavior_tuning(tuning);
    try{return std::make_shared<BehaviorController>(definition,std::move(mesh),tuning);}catch(const BT::BehaviorTreeException& error){throw std::runtime_error(std::string("Behavior tree rejected by the executor: ")+error.what());}
}
}
