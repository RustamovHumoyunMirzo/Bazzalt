#include "Bazzalt/Network.h"
#include "Network/NetworkModuleApi.h"
#include "Bazzalt/Profiler.h"
#include <sodium.h>
#include <algorithm>
#include <charconv>
#include <thread>
#include <stdexcept>
namespace Bazzalt::Detail {
struct NetworkSessionState {
    void* Native=nullptr;
    NetworkRole Role;
    std::size_t Memory=0;
    std::string Address,Error;
};
}
namespace Bazzalt {
namespace {
struct Manager {
    std::thread::id Thread;
    UUID Project;
    bool Configured=false;
    std::vector<std::shared_ptr<Detail::NetworkSessionState>> Sessions;
};
Manager& Data(){static Manager state;return state;}
void Owner(){auto& state=Data();if(!state.Configured)throw std::logic_error("Networking is available after application boot");if(state.Thread!=std::this_thread::get_id())throw std::logic_error("Networking must be used on the engine thread");}
void Validate(const NetworkSettings& settings){if(!settings.ProtocolVersion||!settings.MaxClients||settings.MaxClients>64||settings.SendRate<1||settings.SendRate>120||settings.TimeoutSeconds<2||settings.TimeoutSeconds>300||settings.MaxQueuedEvents<8||settings.MaxQueuedEvents>4096||settings.MaxQueuedEventBytes<65536||settings.MaxQueuedEventBytes>16*1024*1024)throw std::invalid_argument("Invalid network settings");}
std::uint64_t Protocol(const NetworkSettings& settings){std::uint64_t hash=14695981039346656037ull;for(unsigned char c:Data().Project.ToString()){hash^=c;hash*=1099511628211ull;}return hash^(std::uint64_t(settings.ProtocolVersion)<<32)^0x425A4E4554000001ull;}
void Stop(Detail::NetworkSessionState& session){if(session.Native){session.Address=Runtime::NetworkModule().Address(session.Native);Runtime::NetworkModule().Destroy(session.Native);session.Native=nullptr;}}
void Payload(std::uint16_t type,const ByteBuffer& bytes,NetworkChannel channel){if(channel!=NetworkChannel::Reliable&&channel!=NetworkChannel::Unreliable)throw std::invalid_argument("Unknown network channel");if(!type||bytes.size()>(channel==NetworkChannel::Reliable?65536u:900u))throw std::invalid_argument("Invalid message type or network payload size");}
}
std::string NetworkPeerId::ToString()const{std::string out(16,'0');for(unsigned i=0;i<16;++i)out[15-i]="0123456789abcdef"[(Value>>(i*4))&15];return out;}
NetworkPeerId NetworkPeerId::Parse(const std::string& text){NetworkPeerId id;if(text.size()!=16)throw std::invalid_argument("Peer IDs are 16 hexadecimal characters");auto result=std::from_chars(text.data(),text.data()+text.size(),id.Value,16);if(result.ec!=std::errc{}||result.ptr!=text.data()+text.size())throw std::invalid_argument("Invalid peer ID");return id;}
ByteBuffer Network::CreateKey(){if(sodium_init()<0)throw std::runtime_error("Cryptography initialization failed");ByteBuffer key(32);randombytes_buf(key.data(),key.size());return key;}
NetworkPeerId Network::CreateClientId(){if(sodium_init()<0)throw std::runtime_error("Cryptography initialization failed");NetworkPeerId id;do{randombytes_buf(&id.Value,sizeof(id.Value));}while(!id.IsValid());return id;}
NetworkSession Network::CreateServer(const std::string& address,const NetworkSettings& settings,const ByteBuffer& key){
    Owner();Validate(settings);if(!key.empty()&&key.size()!=32)throw std::invalid_argument("Server private key must be exactly 32 bytes");
    auto& state=Data();std::size_t memory=0;for(auto& session:state.Sessions)if(session->Native)memory+=session->Memory;
    const auto cost=(std::size_t(settings.MaxClients)*4+10)*1024*1024;
    if(std::count_if(state.Sessions.begin(),state.Sessions.end(),[](auto& session){return session->Native!=nullptr;})>=16||cost>512u*1024u*1024u-memory)throw std::length_error("Network endpoint/memory budget exceeded");
    auto session=std::make_shared<Detail::NetworkSessionState>();session->Role=NetworkRole::Server;session->Memory=cost;
    session->Native=Runtime::NetworkModule().Create(session->Role,address,settings,Protocol(settings),key);session->Address=Runtime::NetworkModule().Address(session->Native);
    state.Sessions.push_back(session);NetworkSession result;result.m_state=std::move(session);return result;
}
NetworkSession Network::CreateClient(const std::string& address,const NetworkSettings& settings){
    Owner();Validate(settings);auto& state=Data();std::size_t memory=0;for(auto& session:state.Sessions)if(session->Native)memory+=session->Memory;
    if(std::count_if(state.Sessions.begin(),state.Sessions.end(),[](auto& session){return session->Native!=nullptr;})>=16||memory>508u*1024u*1024u)throw std::length_error("Network endpoint/memory budget exceeded");
    auto session=std::make_shared<Detail::NetworkSessionState>();session->Role=NetworkRole::Client;session->Memory=4*1024*1024;
    session->Native=Runtime::NetworkModule().Create(session->Role,address,settings,Protocol(settings),{});session->Address=Runtime::NetworkModule().Address(session->Native);
    state.Sessions.push_back(session);NetworkSession result;result.m_state=std::move(session);return result;
}
bool NetworkSession::IsValid()const{Owner();return m_state&&m_state->Native;}
NetworkRole NetworkSession::GetRole()const{Owner();if(!m_state)throw std::logic_error("Invalid network session");return m_state->Role;}
NetworkState NetworkSession::GetState()const{Owner();return m_state&&m_state->Native?Runtime::NetworkModule().State(m_state->Native):NetworkState::Stopped;}
std::string NetworkSession::GetAddress()const{Owner();return m_state&&m_state->Native?Runtime::NetworkModule().Address(m_state->Native):m_state?m_state->Address:std::string{};}
std::string NetworkSession::GetLastError()const{Owner();return m_state&&m_state->Native?Runtime::NetworkModule().Error(m_state->Native):m_state?m_state->Error:"Invalid network session";}
std::vector<NetworkPeerId> NetworkSession::GetPeers()const{Owner();return IsValid()?Runtime::NetworkModule().Peers(m_state->Native):std::vector<NetworkPeerId>{};}
NetworkStatistics NetworkSession::GetStatistics(NetworkPeerId peer)const{Owner();const auto state=GetState();return IsValid()&&(state==NetworkState::Connected||state==NetworkState::Listening)?Runtime::NetworkModule().Statistics(m_state->Native,peer):NetworkStatistics{};}
ByteBuffer NetworkSession::CreateConnectionToken(NetworkPeerId client,const std::vector<std::string>& addresses,int expiry)const{Owner();if(!IsValid())throw std::logic_error("Invalid network session");return Runtime::NetworkModule().Token(m_state->Native,client,addresses,expiry);}
bool NetworkSession::Connect(NetworkPeerId client,const ByteBuffer& token)const{Owner();return IsValid()&&Runtime::NetworkModule().Connect(m_state->Native,client,token);}
bool NetworkSession::Send(NetworkPeerId peer,std::uint16_t type,const ByteBuffer& data,NetworkChannel channel)const{Owner();Payload(type,data,channel);return IsValid()&&Runtime::NetworkModule().Send(m_state->Native,peer,type,data,channel);}
bool NetworkSession::SendToServer(std::uint16_t type,const ByteBuffer& data,NetworkChannel channel)const{if(GetRole()!=NetworkRole::Client)throw std::logic_error("SendToServer requires a client endpoint");return Send({},type,data,channel);}
std::size_t NetworkSession::Broadcast(std::uint16_t type,const ByteBuffer& data,NetworkChannel channel,NetworkPeerId excluded)const{if(GetRole()!=NetworkRole::Server)throw std::logic_error("Broadcast requires a server endpoint");Payload(type,data,channel);std::size_t sent=0;for(auto peer:GetPeers())if(peer!=excluded&&Send(peer,type,data,channel))++sent;return sent;}
std::optional<NetworkEvent> NetworkSession::PollEvent()const{Owner();return IsValid()?Runtime::NetworkModule().Poll(m_state->Native):std::nullopt;}
bool NetworkSession::DisconnectPeer(NetworkPeerId peer)const{Owner();return IsValid()&&Runtime::NetworkModule().Disconnect(m_state->Native,peer);}
void NetworkSession::Stop()const{Owner();if(m_state)Bazzalt::Stop(*m_state);}
void Network::StopAll(){auto& state=Data();if(!state.Configured)return;Owner();for(auto& session:state.Sessions)Stop(*session);state.Sessions.clear();}
void Runtime::NetworkAccess::Configure(UUID project){if(!project)throw std::invalid_argument("Networking requires a project UUID");Network::StopAll();auto& state=Data();state.Thread=std::this_thread::get_id();state.Project=project;state.Configured=true;}
void Runtime::NetworkAccess::Reset(){Network::StopAll();Data().Configured=false;}
void Runtime::NetworkAccess::Pump(){auto& state=Data();if(!state.Configured)return;Owner();ProfileScope profile("Network.Pump");for(auto it=state.Sessions.begin();it!=state.Sessions.end();){if(!(*it)->Native||it->use_count()==1){Stop(**it);it=state.Sessions.erase(it);}else {if((*it)->Native)NetworkModule().Update((*it)->Native,0);++it;}}}
}
