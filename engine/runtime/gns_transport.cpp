#include <darkangel/gns_transport.hpp>
#include <steam/steamnetworkingsockets.h>
#include <atomic>
#include <map>
#include <stdexcept>
#include <thread>
namespace darkangel {
namespace {
void require(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
unsigned users{};std::thread::id sdk_owner;
class Gns;
std::map<HSteamListenSocket,Gns*> listeners;
std::map<HSteamNetConnection,Gns*> connections;
class Gns final:public Transport {
    SessionHandshake expected;TransportLimits bound;std::uint64_t token=allocate_transport_identity(),epoch{1},sequence{};bool admitted{},host_role;std::string remote_address;
    HSteamListenSocket listener{k_HSteamListenSocket_Invalid};HSteamNetConnection connection{k_HSteamNetConnection_Invalid};
    void thread() const{require(sdk_owner==std::this_thread::get_id(),"GNS owner thread mismatch");}
    ConnectionHandle peer() const{return {token,epoch,1};}
    void check(ConnectionHandle h) const{require(h==peer() && connected(),"Stale/foreign/disconnected GNS handle");}
    void close(){if(connection!=k_HSteamNetConnection_Invalid){connections.erase(connection);SteamNetworkingSockets()->CloseConnection(connection,0,"Endpoint closed",false);}connection=k_HSteamNetConnection_Invalid;admitted=false;++epoch;sequence=0;}
    static void status(SteamNetConnectionStatusChangedCallback_t* event){
        if(event->m_info.m_eState==k_ESteamNetworkingConnectionState_ClosedByPeer || event->m_info.m_eState==k_ESteamNetworkingConnectionState_ProblemDetectedLocally){auto found=connections.find(event->m_hConn);if(found!=connections.end())found->second->close();return;}
        if(event->m_info.m_eState!=k_ESteamNetworkingConnectionState_Connecting || event->m_info.m_hListenSocket==k_HSteamListenSocket_Invalid)return;
        auto found=listeners.find(event->m_info.m_hListenSocket);auto* api=SteamNetworkingSockets();
        if(found==listeners.end() || found->second->connection!=k_HSteamNetConnection_Invalid){api->CloseConnection(event->m_hConn,0,"LAN peer capacity",false);return;}
        auto* owner=found->second;if(api->AcceptConnection(event->m_hConn)==k_EResultOK){owner->connection=event->m_hConn;connections.emplace(event->m_hConn,owner);}else api->CloseConnection(event->m_hConn,0,"LAN admission failed",false);
    }
public:
    Gns(bool host,const std::string& address,SessionHandshake hello,TransportLimits limits):expected(std::move(hello)),bound(limits),host_role(host),remote_address(address){
        validate_session_handshake(expected);validate_transport_limits(bound);if(users)thread();else {SteamNetworkingErrMsg error{};require(GameNetworkingSockets_Init(nullptr,error),"GNS initialization failed");sdk_owner=std::this_thread::get_id();}++users;
        try{SteamNetworkingIPAddr ip;require(ip.ParseString(address.c_str()) && ip.m_port>0,"Invalid explicit LAN address:port");SteamNetworkingConfigValue_t options[4];options[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,reinterpret_cast<void*>(&status));options[1].SetInt32(k_ESteamNetworkingConfig_SendBufferSize,static_cast<int>(bound.queued_bytes));options[2].SetInt32(k_ESteamNetworkingConfig_RecvBufferSize,static_cast<int>(bound.queued_bytes));options[3].SetInt32(k_ESteamNetworkingConfig_RecvBufferMessages,static_cast<int>(bound.packets));
            if(host){listener=SteamNetworkingSockets()->CreateListenSocketIP(ip,4,options);require(listener!=k_HSteamListenSocket_Invalid,"GNS listen failed");listeners.emplace(listener,this);}else{connection=SteamNetworkingSockets()->ConnectByIPAddress(ip,4,options);require(connection!=k_HSteamNetConnection_Invalid,"GNS connect failed");connections.emplace(connection,this);}
        }catch(...){if(--users==0)GameNetworkingSockets_Kill();throw;}
    }
    ~Gns() override{close();if(listener!=k_HSteamListenSocket_Invalid){listeners.erase(listener);SteamNetworkingSockets()->CloseListenSocket(listener);}if(--users==0)GameNetworkingSockets_Kill();}
    ConnectionHandle open(const SessionHandshake& h) override{thread();require(compatible_session_handshake(h,expected),"GNS local descriptor mismatch");SteamNetworkingSockets()->RunCallbacks();if(!host_role && connection==k_HSteamNetConnection_Invalid){SteamNetworkingIPAddr address;require(address.ParseString(remote_address.c_str()),"Invalid LAN reconnect address");SteamNetworkingConfigValue_t options[4];options[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,reinterpret_cast<void*>(&status));options[1].SetInt32(k_ESteamNetworkingConfig_SendBufferSize,static_cast<int>(bound.queued_bytes));options[2].SetInt32(k_ESteamNetworkingConfig_RecvBufferSize,static_cast<int>(bound.queued_bytes));options[3].SetInt32(k_ESteamNetworkingConfig_RecvBufferMessages,static_cast<int>(bound.packets));connection=SteamNetworkingSockets()->ConnectByIPAddress(address,4,options);require(connection!=k_HSteamNetConnection_Invalid,"GNS reconnect failed");connections.emplace(connection,this);}admitted=true;return peer();}
    bool connected() const override{thread();SteamNetworkingSockets()->RunCallbacks();SteamNetConnectionInfo_t info{};return admitted && connection!=k_HSteamNetConnection_Invalid && SteamNetworkingSockets()->GetConnectionInfo(connection,&info) && info.m_eState==k_ESteamNetworkingConnectionState_Connected;}
    TransportLimits limits() const override{thread();return bound;}
    bool valid(ConnectionHandle h) const override{return h==peer() && connected();}
    bool send(ConnectionHandle h,Delivery delivery,std::span<const std::byte> bytes) override{
        check(h);require(bytes.size()<=bound.payload_bytes && (delivery==Delivery::ReliableOrdered || delivery==Delivery::UnreliableState || delivery==Delivery::ReliableBootstrap),"Invalid GNS payload/channel");SteamNetConnectionRealTimeStatus_t queue{};require(SteamNetworkingSockets()->GetConnectionRealTimeStatus(connection,&queue,0,nullptr)==k_EResultOK,"GNS queue unavailable");auto pending=static_cast<std::size_t>(queue.m_cbPendingReliable+queue.m_cbPendingUnreliable);if(pending+bytes.size()+9>bound.queued_bytes)return false;
        std::vector<std::byte> wire;wire.reserve(bytes.size()+9);wire.push_back(static_cast<std::byte>(delivery));for(unsigned i=0;i<8;++i)wire.push_back(static_cast<std::byte>(((sequence+1)>>(8*i))&255));wire.insert(wire.end(),bytes.begin(),bytes.end());
        auto result=SteamNetworkingSockets()->SendMessageToConnection(connection,wire.data(),static_cast<std::uint32_t>(wire.size()),(delivery==Delivery::UnreliableState?k_nSteamNetworkingSend_Unreliable:k_nSteamNetworkingSend_Reliable)|k_nSteamNetworkingSend_NoNagle,nullptr);if(result==k_EResultLimitExceeded)return false;require(result==k_EResultOK,"GNS send failed");++sequence;return true;
    }
    std::vector<TransportMessage> poll(std::size_t count) override{thread();require(count<=bound.packets,"GNS poll work limit");SteamNetworkingSockets()->RunCallbacks();std::vector<TransportMessage> result;if(connection==k_HSteamNetConnection_Invalid)return result;for(std::size_t i=0;i<count;++i){SteamNetworkingMessage_t* message{};auto n=SteamNetworkingSockets()->ReceiveMessagesOnConnection(connection,&message,1);require(n>=0,"GNS receive failed");if(!n)break;struct Guard{SteamNetworkingMessage_t* p;~Guard(){p->Release();}}guard{message};require(message->m_cbSize>=9 && static_cast<std::size_t>(message->m_cbSize)<=bound.payload_bytes+9,"Invalid GNS envelope size");auto* bytes=static_cast<const std::byte*>(message->m_pData);auto channel=std::to_integer<unsigned>(bytes[0]);require(channel<=2,"Invalid GNS envelope channel");std::uint64_t seq{};for(unsigned b=0;b<8;++b)seq|=std::uint64_t(std::to_integer<unsigned>(bytes[b+1]))<<(8*b);require(seq>0,"Invalid GNS sequence");result.push_back({peer(),static_cast<Delivery>(channel),seq,{bytes+9,bytes+message->m_cbSize}});}return result;}
    void disconnect(ConnectionHandle h) override{check(h);close();}
};
}
std::unique_ptr<Transport> create_gns_host(std::string address,SessionHandshake h,TransportLimits l){return std::make_unique<Gns>(true,address,std::move(h),l);}
std::unique_ptr<Transport> create_gns_client(std::string address,SessionHandshake h,TransportLimits l){return std::make_unique<Gns>(false,address,std::move(h),l);}
}
