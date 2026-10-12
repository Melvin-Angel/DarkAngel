#include <darkangel/behavior_tree.hpp>
#include <behaviortree_cpp/bt_factory.h>
#include <lua.h>
#include <lualib.h>
#include <chrono>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace darkangel {
namespace {
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
class ScriptHost;
// Native blackboard shared by the leaves of one tree instance.
struct Blackboard {
    const ControllerView* view{};const PerceivedActor* foe{};ControllerCommand command;std::shared_ptr<const NavigationMesh> mesh;
    // Script leaves: the controller's sandbox and the tuning value they may read.
    ScriptHost* scripts{};double preferred_range{};
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
// The Luau sandbox of one controller: every script leaf of its tree is a function in this VM with its own state table.
// A script only receives a view and returns a status and a command; any fault disables that leaf and is reported in
// the decision readout. Nothing here throws into the tree.
class ScriptHost {
public:
    ScriptHost(){
        vm_=lua_newstate(allocate,&memory_);require(vm_!=nullptr,"Behavior script sandbox allocation failed");lua_callbacks(vm_)->userdata=this;lua_callbacks(vm_)->interrupt=interrupt;
        try{luaL_openlibs(vm_);
            // No clock and a fixed seed: a run is repeatable.
            lua_pushnil(vm_);lua_setglobal(vm_,"os");lua_getglobal(vm_,"math");lua_getfield(vm_,-1,"randomseed");lua_pushnumber(vm_,1);lua_call(vm_,1,0);lua_pop(vm_,1);
            luaL_sandbox(vm_);
        }catch(...){lua_close(vm_);throw;}
    }
    ~ScriptHost(){lua_close(vm_);}
    ScriptHost(const ScriptHost&)=delete;ScriptHost& operator=(const ScriptHost&)=delete;
    unsigned add(const BehaviorNode& node){
        Leaf leaf;leaf.name=node.name;const int top=lua_gettop(vm_);
        try{const auto bytecode=compile_behavior_script(node.source);begin();
            if(luau_load(vm_,("=behavior script "+node.name).c_str(),bytecode.data(),bytecode.size(),0)!=0||lua_pcall(vm_,0,1,0)!=0)leaf.fault=text(-1);
            else if(!lua_isfunction(vm_,-1))leaf.fault="the chunk must return a function";
            else{leaf.function=lua_ref(vm_,-1);lua_newtable(vm_);leaf.state=lua_ref(vm_,-1);}
        }catch(const std::exception& error){leaf.fault=error.what();}
        lua_settop(vm_,top);leaves_.push_back(std::move(leaf));return unsigned(leaves_.size()-1);
    }
    BT::NodeStatus run(unsigned index,Blackboard& board){
        auto& leaf=leaves_.at(index);if(!leaf.fault.empty()){board.trace="Script \""+leaf.name+"\" is disabled: "+leaf.fault;return BT::NodeStatus::FAILURE;}
        const int top=lua_gettop(vm_);auto status=BT::NodeStatus::FAILURE;ControllerCommand command=board.command;std::string said;
        try{begin();lua_getref(vm_,leaf.function);view(board);lua_getref(vm_,leaf.state);
            if(lua_pcall(vm_,2,2,0)!=0)leaf.fault=text(-1);else leaf.fault=result(status,command,said);
        }catch(const std::exception& error){leaf.fault=error.what();}
        lua_settop(vm_,top);
        if(!leaf.fault.empty()){lua_unref(vm_,leaf.function);lua_unref(vm_,leaf.state);board.trace="Script \""+leaf.name+"\" is disabled: "+leaf.fault;return BT::NodeStatus::FAILURE;}
        board.command=command;if(status!=BT::NodeStatus::FAILURE)board.trace=said.empty()?"Script \""+leaf.name+"\"":said;return status;
    }
private:
    struct Leaf {std::string name,fault;int function{LUA_NOREF},state{LUA_NOREF};};
    struct Memory {std::size_t used{},limit{1024*1024};};
    static void* allocate(void* user,void* pointer,std::size_t old,std::size_t size){
        auto& memory=*static_cast<Memory*>(user);if(!pointer)old=0;if(!size){std::free(pointer);memory.used-=old;return nullptr;}
        if(size>memory.limit||memory.used-old>memory.limit-size)return nullptr;void* next=std::realloc(pointer,size);if(next)memory.used=memory.used-old+size;return next;
    }
    static void interrupt(lua_State* state,int collector){
        if(collector>=0)return;auto* host=static_cast<ScriptHost*>(lua_callbacks(state)->userdata);if(std::chrono::steady_clock::now()>host->deadline_)luaL_error(state,"the script ran longer than its time budget");
    }
    void begin(){deadline_=std::chrono::steady_clock::now()+std::chrono::milliseconds(2);}
    std::string text(int index){const char* message=lua_tostring(vm_,index);return message?std::string(message):std::string("unknown error");}
    void number(const char* key,double value){lua_pushnumber(vm_,value);lua_setfield(vm_,-2,key);}
    void point(const MotorVec& at){number("x",at.x);number("y",at.y);number("z",at.z);}
    // The read-only view a script decides from: the same opponent and memory the native leaves use.
    void view(const Blackboard& board){
        const auto& seen=*board.view;lua_newtable(vm_);number("tick",double(seen.tick));number("preferred_range",board.preferred_range);lua_pushboolean(vm_,seen.alive);lua_setfield(vm_,-2,"alive");lua_pushboolean(vm_,seen.busy);lua_setfield(vm_,-2,"busy");
        lua_newtable(vm_);point(seen.self.position);number("yaw",seen.self.yaw);lua_setreadonly(vm_,-1,true);lua_setfield(vm_,-2,"self");
        if(board.opponent()){lua_newtable(vm_);point(board.foe->motor.position);number("distance",board.distance());lua_pushboolean(vm_,board.foe->visible);lua_setfield(vm_,-2,"visible");lua_setreadonly(vm_,-1,true);lua_setfield(vm_,-2,"opponent");}
        if(const auto* lost=seen.alive?last_lost_hostile(seen):nullptr){lua_newtable(vm_);point(lost->motor.position);lua_setreadonly(vm_,-1,true);lua_setfield(vm_,-2,"lost");}
        lua_setreadonly(vm_,-1,true);
    }
    // Reads the two return values (status, command). Returns the fault, empty when the return is well formed.
    std::string result(BT::NodeStatus& status,ControllerCommand& command,std::string& said){
        if(lua_type(vm_,-2)!=LUA_TSTRING)return "the function must return \"success\", \"failure\" or \"running\"";
        const std::string word=lua_tostring(vm_,-2);if(word=="success")status=BT::NodeStatus::SUCCESS;else if(word=="running")status=BT::NodeStatus::RUNNING;else if(word=="failure")status=BT::NodeStatus::FAILURE;else return "unknown status \""+word.substr(0,24)+"\"";
        if(lua_isnil(vm_,-1))return {};if(!lua_istable(vm_,-1))return "the command must be a table or nil";
        const auto field=[&](const char* key,std::optional<double>& value)->bool{lua_getfield(vm_,-1,key);const bool absent=lua_isnil(vm_,-1),fine=absent||lua_type(vm_,-1)==LUA_TNUMBER;if(fine&&!absent)value=lua_tonumber(vm_,-1);lua_pop(vm_,1);return fine&&(!value||std::isfinite(*value));};
        std::optional<double> x,z,face;if(!field("move_x",x)||!field("move_z",z)||!field("face",face))return "move_x, move_z and face must be finite numbers";
        if(x||z){const double mx=x.value_or(0),mz=z.value_or(0);if(mx*mx+mz*mz>1.00001)return "the move command is longer than 1";command.move_x=mx;command.move_z=mz;}
        if(face)command.face=std::remainder(*face,6.283185307179586);
        lua_getfield(vm_,-1,"press");if(!lua_isnil(vm_,-1)){if(lua_type(vm_,-1)!=LUA_TSTRING){lua_pop(vm_,1);return "press must be a slot name";}try{command.press=behavior_slot(lua_tostring(vm_,-1));}catch(const std::exception&){lua_pop(vm_,1);return "press names an unknown slot";}}lua_pop(vm_,1);
        lua_getfield(vm_,-1,"say");if(!lua_isnil(vm_,-1)){if(lua_type(vm_,-1)!=LUA_TSTRING){lua_pop(vm_,1);return "say must be text";}said=std::string(lua_tostring(vm_,-1)).substr(0,96);}lua_pop(vm_,1);
        return {};
    }
    Memory memory_;lua_State* vm_{};std::chrono::steady_clock::time_point deadline_{};std::vector<Leaf> leaves_;
};
// A leaf written in Luau. It may keep running across ticks; its state lives in the sandbox.
class ScriptLeaf final:public BT::StatefulActionNode {
public:ScriptLeaf(const std::string& name,const BT::NodeConfig& config,Blackboard* board):BT::StatefulActionNode(name,config),board_(board){}
    static BT::PortsList providedPorts(){return {BT::InputPort<unsigned>("leaf")};}
    BT::NodeStatus onStart()override{return onRunning();}
    BT::NodeStatus onRunning()override{return board_->scripts->run(getInput<unsigned>("leaf").value(),*board_);}
    void onHalted()override{}
private:Blackboard* board_;
};
void write(std::ostringstream& xml,const BehaviorNode& node,const BehaviorTuning& tuning,ScriptHost*& scripts,std::unique_ptr<ScriptHost>& owned){
    const double range=node.preferred?tuning.preferred_range:node.range;
    if(node.type=="sequence"||node.type=="selector"){const char* tag=node.type=="sequence"?"Sequence":"Fallback";xml<<'<'<<tag<<'>';for(const auto& child:node.children)write(xml,child,tuning,scripts,owned);xml<<"</"<<tag<<'>';}
    else if(node.type=="opponent_alive")xml<<"<DaOpponentAlive/>";
    else if(node.type=="opponent_visible")xml<<"<DaOpponentVisible/>";
    else if(node.type=="idle")xml<<"<DaIdle/>";
    else if(node.type=="face")xml<<"<DaFace/>";
    else if(node.type=="in_range")xml<<"<DaInRange range=\""<<range<<"\"/>";
    else if(node.type=="approach")xml<<"<DaApproach range=\""<<range<<"\"/>";
    else if(node.type=="attack")xml<<"<DaAttack slot=\""<<static_cast<unsigned>(node.slot)<<"\"/>";
    else if(node.type=="investigate")xml<<"<DaInvestigate/>";
    else if(node.type=="script"){if(!owned){owned=std::make_unique<ScriptHost>();scripts=owned.get();}xml<<"<DaScript leaf=\""<<owned->add(node)<<"\"/>";}
    else if(node.type=="recover")xml<<"<DaRecover ticks=\""<<std::max(1u,unsigned(std::lround(node.ticks*tuning.recovery_scale)))<<"\"/>";
    else throw std::runtime_error("Unknown behavior node type");
}
class BehaviorController final:public ActorController {
public:
    BehaviorController(const BehaviorDefinition& definition,std::shared_ptr<const NavigationMesh> mesh,const BehaviorTuning& tuning){
        board_.mesh=std::move(mesh);BT::BehaviorTreeFactory factory;
        factory.registerNodeType<OpponentAlive>("DaOpponentAlive",&board_);factory.registerNodeType<OpponentVisible>("DaOpponentVisible",&board_);factory.registerNodeType<Idle>("DaIdle",&board_);factory.registerNodeType<InRange>("DaInRange",&board_);factory.registerNodeType<Face>("DaFace",&board_);
        factory.registerNodeType<Attack>("DaAttack",&board_);factory.registerNodeType<Recover>("DaRecover",&board_);factory.registerNodeType<Approach>("DaApproach",&board_);factory.registerNodeType<Investigate>("DaInvestigate",&board_);factory.registerNodeType<ScriptLeaf>("DaScript",&board_);board_.preferred_range=tuning.preferred_range;
        std::ostringstream xml;xml.precision(17);xml<<"<root BTCPP_format=\"4\"><BehaviorTree ID=\"Main\">";write(xml,definition.root,tuning,board_.scripts,scripts_);xml<<"</BehaviorTree></root>";
        tree_=factory.createTreeFromText(xml.str());
    }
    ControllerCommand decide(const ControllerView& view)override{board_.view=&view;board_.foe=view.alive?nearest_hostile(view):nullptr;board_.command={};if(board_.foe)board_.command.target=board_.foe->id;board_.trace=view.alive?(view.busy?"Acting":"Idle"):"Defeated";tree_.tickOnce();board_.view=nullptr;board_.foe=nullptr;return board_.command;}
    std::string status()const override{return board_.trace;}
private:Blackboard board_;std::unique_ptr<ScriptHost> scripts_;BT::Tree tree_;
};
}
std::shared_ptr<ActorController> make_behavior_controller(const BehaviorDefinition& definition,std::shared_ptr<const NavigationMesh> mesh,BehaviorTuning tuning){
    require(!definition.root.type.empty(),"Behavior controller needs a decoded tree");validate_behavior_tuning(tuning);
    try{return std::make_shared<BehaviorController>(definition,std::move(mesh),tuning);}catch(const BT::BehaviorTreeException& error){throw std::runtime_error(std::string("Behavior tree rejected by the executor: ")+error.what());}
}
}
