#define NOMINMAX
#include <Windows.h>
#include <sddl.h>
#include <bcrypt.h>
#include <darkangel/editor_rpc.hpp>
#include <nlohmann/json.hpp>
#include <array>
#include <chrono>
#include <vector>
#include <stdexcept>
namespace darkangel {
namespace {
void require(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
std::string random_id(){std::array<unsigned char,32> bytes{};require(BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0,"Agent entropy unavailable");std::string text;for(auto b:bytes){text+="0123456789abcdef"[b>>4];text+="0123456789abcdef"[b&15];}return text;}
struct UserSecurity {
    PSECURITY_DESCRIPTOR descriptor{};SECURITY_ATTRIBUTES attributes{};
    UserSecurity(){HANDLE token{};require(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token),"Process token unavailable");DWORD count{};GetTokenInformation(token,TokenUser,nullptr,0,&count);std::vector<std::byte> user(count);auto ok=GetTokenInformation(token,TokenUser,user.data(),count,&count);CloseHandle(token);require(ok,"User SID unavailable");LPWSTR sid{};require(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid,&sid),"User SID conversion failed");auto sddl=std::wstring(L"D:P(A;;GA;;;")+sid+L")";LocalFree(sid);require(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr),"Agent ACL creation failed");attributes={sizeof(SECURITY_ATTRIBUTES),descriptor,FALSE};}
    ~UserSecurity(){if(descriptor)LocalFree(descriptor);}
};
}
struct AgentEndpoint::Impl {
    EditorRpc service;UserSecurity security;std::filesystem::path descriptor;HANDLE pipe{INVALID_HANDLE_VALUE},event{};OVERLAPPED overlap{};
    enum class Stage{Connect,Header,Body,Reply};Stage stage{Stage::Connect};bool pending{};DWORD expected{},offset{};std::array<std::byte,4> header{};std::vector<std::byte> body,reply;std::chrono::steady_clock::time_point deadline;
    Impl(EditorDocument& d,AgentAuthorization auth,const std::filesystem::path& file):service(d,auth),descriptor(file){
        auto name=std::string("\\\\.\\pipe\\DarkAngel-")+auth.host;event=CreateEventW(nullptr,TRUE,FALSE,nullptr);require(event!=nullptr,"Agent event creation failed");pipe=CreateNamedPipeA(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65540,65540,0,&security.attributes);if(pipe==INVALID_HANDLE_VALUE){CloseHandle(event);event=nullptr;throw std::runtime_error("Agent pipe creation failed");}
        try{std::filesystem::create_directories(file.parent_path());auto text=nlohmann::json{{"version",1},{"pipe",name},{"project",auth.project},{"host",auth.host},{"credential",auth.credential},{"authoring",auth.authoring}}.dump();auto handle=CreateFileW(file.c_str(),GENERIC_WRITE,0,&security.attributes,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);require(handle!=INVALID_HANDLE_VALUE,"Agent descriptor must be a new private file");DWORD written{};auto ok=WriteFile(handle,text.data(),static_cast<DWORD>(text.size()),&written,nullptr);CloseHandle(handle);require(ok && written==text.size(),"Agent descriptor write failed");start_connect();}catch(...){CloseHandle(pipe);CloseHandle(event);pipe=INVALID_HANDLE_VALUE;event=nullptr;std::error_code ignored;std::filesystem::remove(file,ignored);throw;}
    }
    ~Impl(){if(pipe!=INVALID_HANDLE_VALUE){CancelIoEx(pipe,nullptr);CloseHandle(pipe);}if(event)CloseHandle(event);std::error_code ignored;std::filesystem::remove(descriptor,ignored);}
    void reset_overlap(){ResetEvent(event);overlap={};overlap.hEvent=event;pending=false;}
    void start_connect(){stage=Stage::Connect;offset=0;expected=0;body.clear();reply.clear();reset_overlap();auto ok=ConnectNamedPipe(pipe,&overlap);auto error=GetLastError();if(ok || error==ERROR_PIPE_CONNECTED){pending=false;stage=Stage::Header;expected=4;deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);}else {require(error==ERROR_IO_PENDING,"Agent pipe connect failed");pending=true;}}
    void drop(){CancelIoEx(pipe,nullptr);if(pending){DWORD ignored{};GetOverlappedResult(pipe,&overlap,&ignored,TRUE);}DisconnectNamedPipe(pipe);start_connect();}
    void poll(){for(unsigned work=0;work<4;++work){
        if(stage!=Stage::Connect && std::chrono::steady_clock::now()>deadline){drop();return;}
        DWORD bytes{};
        if(pending){if(!GetOverlappedResult(pipe,&overlap,&bytes,FALSE)){auto error=GetLastError();if(error==ERROR_IO_INCOMPLETE)return;drop();return;}pending=false;if(stage==Stage::Connect){stage=Stage::Header;expected=4;offset=0;deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);continue;}}
        else if(stage==Stage::Connect){return;}
        else {reset_overlap();BOOL ok;if(stage==Stage::Reply)ok=WriteFile(pipe,reply.data()+offset,expected-offset,&bytes,&overlap);else {auto* destination=stage==Stage::Header?header.data():body.data();ok=ReadFile(pipe,destination+offset,expected-offset,&bytes,&overlap);}if(!ok){if(GetLastError()==ERROR_IO_PENDING){pending=true;return;}drop();return;}}
        if(!bytes){drop();return;}offset+=bytes;if(offset<expected)continue;offset=0;
        if(stage==Stage::Header){std::uint32_t size{};for(unsigned i=0;i<4;++i)size|=std::to_integer<unsigned>(header[i])<<(i*8);if(size==0 || size>65536){drop();return;}body.resize(size);expected=size;stage=Stage::Body;}
        else if(stage==Stage::Body){auto response=service.dispatch({reinterpret_cast<const char*>(body.data()),body.size()});if(response.size()>65536){drop();return;}reply.resize(response.size()+4);auto size=static_cast<std::uint32_t>(response.size());for(unsigned i=0;i<4;++i)reply[i]=static_cast<std::byte>((size>>(8*i))&255);std::memcpy(reply.data()+4,response.data(),response.size());expected=static_cast<DWORD>(reply.size());stage=Stage::Reply;}
        else if(stage==Stage::Reply){stage=Stage::Header;expected=4;deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);}
    }}
};
AgentEndpoint::AgentEndpoint(EditorDocument& d,std::string project,bool edits,const std::filesystem::path& descriptor):impl_(std::make_unique<Impl>(d,AgentAuthorization{std::move(project),random_id(),random_id(),edits},descriptor)){}
AgentEndpoint::~AgentEndpoint()=default;
void AgentEndpoint::poll(){impl_->poll();}
}
