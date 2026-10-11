#include <darkangel/behavior_tree.hpp>
#include <behaviortree_cpp/bt_factory.h>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace darkangel {
namespace {
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
// Native blackboard shared by the leaves of one tree instance.
struct Blackboard {
    const ControllerView* view{};ControllerCommand command;std::shared_ptr<const NavigationMesh> mesh;
    std::vector<MotorVec> path;std::size_t next{1};std::uint64_t repath{};
    bool opponent()const{return view->alive&&view->opponent&&view->opponent_alive;}
    double distance()const{return std::hypot(view->opponent->position.x-view->self.position.x,view->opponent->position.z-view->self.position.z);}
};
class OpponentAlive final:public BT::ConditionNode {
public:OpponentAlive(const std::string& name,const BT::NodeConfig& config,Blackboard* board):BT::ConditionNode(name,config),board_(board){}
    static BT::PortsList providedPorts(){return {};}
    BT::NodeStatus tick()override{return board_->opponent()?BT::NodeStatus::SUCCESS:BT::NodeStatus::FAILURE;}
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
    BT::NodeStatus tick()override{if(!board_->opponent())return BT::NodeStatus::FAILURE;const auto& self=board_->view->self.position;const auto& goal=board_->view->opponent->position;if(board_->distance()>1e-6)board_->command.face=std::atan2(goal.x-self.x,goal.z-self.z);return BT::NodeStatus::SUCCESS;}
private:Blackboard* board_;
};
class Attack final:public BT::SyncActionNode {
public:Attack(const std::string& name,const BT::NodeConfig& config,Blackboard* board):BT::SyncActionNode(name,config),board_(board){}
    static BT::PortsList providedPorts(){return {BT::InputPort<unsigned>("slot")};}
    // The press is a request; authority accepts or refuses it like a player's.
    BT::NodeStatus tick()override{if(board_->view->busy)return BT::NodeStatus::FAILURE;board_->command.press=static_cast<CombatSlot>(getInput<unsigned>("slot").value());return BT::NodeStatus::SUCCESS;}
private:Blackboard* board_;
};
class Recover final:public BT::StatefulActionNode {
public:Recover(const std::string& name,const BT::NodeConfig& config,Blackboard* board):BT::StatefulActionNode(name,config),board_(board){}
    static BT::PortsList providedPorts(){return {BT::InputPort<unsigned>("ticks")};}
    BT::NodeStatus onStart()override{until_=board_->view->tick+getInput<unsigned>("ticks").value();return BT::NodeStatus::RUNNING;}
    BT::NodeStatus onRunning()override{return board_->view->tick>=until_?BT::NodeStatus::SUCCESS:BT::NodeStatus::RUNNING;}
    void onHalted()override{}
private:Blackboard* board_;std::uint64_t until_{};
};
class Approach final:public BT::StatefulActionNode {
public:Approach(const std::string& name,const BT::NodeConfig& config,Blackboard* board):BT::StatefulActionNode(name,config),board_(board){}
    static BT::PortsList providedPorts(){return {BT::InputPort<double>("range")};}
    BT::NodeStatus onStart()override{board_->path.clear();board_->repath=0;return onRunning();}
    BT::NodeStatus onRunning()override{
        auto& b=*board_;if(!b.opponent())return BT::NodeStatus::FAILURE;if(b.distance()<=getInput<double>("range").value())return BT::NodeStatus::SUCCESS;if(b.view->busy)return BT::NodeStatus::RUNNING;
        const auto& self=b.view->self.position;MotorVec corner=b.view->opponent->position;
        if(b.mesh){
            if(b.path.size()<2||b.view->tick>=b.repath){auto found=b.mesh->path(self,b.view->opponent->position);b.path=found?std::move(*found):std::vector<MotorVec>{};b.next=1;b.repath=b.view->tick+15;}
            if(b.path.size()<2)return BT::NodeStatus::FAILURE;
            while(b.next+1<b.path.size()&&std::hypot(b.path[b.next].x-self.x,b.path[b.next].z-self.z)<.3)++b.next;corner=b.path[b.next];
        }
        const double x=corner.x-self.x,z=corner.z-self.z,length=std::hypot(x,z);if(length>1e-6){b.command.move_x=x/length;b.command.move_z=z/length;b.command.face=std::atan2(x,z);}
        return BT::NodeStatus::RUNNING;
    }
    void onHalted()override{board_->path.clear();}
private:Blackboard* board_;
};
void write(std::ostringstream& xml,const BehaviorNode& node){
    if(node.type=="sequence"||node.type=="selector"){const char* tag=node.type=="sequence"?"Sequence":"Fallback";xml<<'<'<<tag<<'>';for(const auto& child:node.children)write(xml,child);xml<<"</"<<tag<<'>';}
    else if(node.type=="opponent_alive")xml<<"<DaOpponentAlive/>";
    else if(node.type=="idle")xml<<"<DaIdle/>";
    else if(node.type=="face")xml<<"<DaFace/>";
    else if(node.type=="in_range")xml<<"<DaInRange range=\""<<node.range<<"\"/>";
    else if(node.type=="approach")xml<<"<DaApproach range=\""<<node.range<<"\"/>";
    else if(node.type=="attack")xml<<"<DaAttack slot=\""<<static_cast<unsigned>(node.slot)<<"\"/>";
    else if(node.type=="recover")xml<<"<DaRecover ticks=\""<<node.ticks<<"\"/>";
    else throw std::runtime_error("Unknown behavior node type");
}
class BehaviorController final:public ActorController {
public:
    BehaviorController(const BehaviorDefinition& definition,std::shared_ptr<const NavigationMesh> mesh){
        board_.mesh=std::move(mesh);BT::BehaviorTreeFactory factory;
        factory.registerNodeType<OpponentAlive>("DaOpponentAlive",&board_);factory.registerNodeType<Idle>("DaIdle",&board_);factory.registerNodeType<InRange>("DaInRange",&board_);factory.registerNodeType<Face>("DaFace",&board_);
        factory.registerNodeType<Attack>("DaAttack",&board_);factory.registerNodeType<Recover>("DaRecover",&board_);factory.registerNodeType<Approach>("DaApproach",&board_);
        std::ostringstream xml;xml.precision(17);xml<<"<root BTCPP_format=\"4\"><BehaviorTree ID=\"Main\">";write(xml,definition.root);xml<<"</BehaviorTree></root>";
        tree_=factory.createTreeFromText(xml.str());
    }
    ControllerCommand decide(const ControllerView& view)override{board_.view=&view;board_.command={};tree_.tickOnce();board_.view=nullptr;return board_.command;}
private:Blackboard board_;BT::Tree tree_;
};
}
std::shared_ptr<ActorController> make_behavior_controller(const BehaviorDefinition& definition,std::shared_ptr<const NavigationMesh> mesh){
    require(!definition.root.type.empty(),"Behavior controller needs a decoded tree");
    try{return std::make_shared<BehaviorController>(definition,std::move(mesh));}catch(const BT::BehaviorTreeException& error){throw std::runtime_error(std::string("Behavior tree rejected by the executor: ")+error.what());}
}
}
