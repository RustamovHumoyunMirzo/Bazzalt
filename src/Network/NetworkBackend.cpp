#include "Network/NetworkModuleApi.h"
#include <yojimbo.h>
#include <netcode.h>
#include <sodium.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <memory>
#include <stdexcept>
using namespace Bazzalt;
namespace {
constexpr int ReliableLimit=65536,UnreliableLimit=900;
struct SmallMessage:yojimbo::Message {
    int Kind=1,Count=0;std::uint8_t Bytes[UnreliableLimit]{};
    template<class Stream> bool Serialize(Stream& stream){serialize_int(stream,Kind,1,65535);serialize_int(stream,Count,0,UnreliableLimit);serialize_bytes(stream,Bytes,Count);return true;}
    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS()
};
struct LargeMessage:yojimbo::BlockMessage {
    int Kind=1;
    template<class Stream> bool Serialize(Stream& stream){serialize_int(stream,Kind,1,65535);return true;}
    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS()
};
YOJIMBO_MESSAGE_FACTORY_START(Messages,2);
YOJIMBO_DECLARE_MESSAGE_TYPE(0,SmallMessage);
YOJIMBO_DECLARE_MESSAGE_TYPE(1,LargeMessage);
YOJIMBO_MESSAGE_FACTORY_FINISH();
struct Endpoint;
struct Adapter:yojimbo::Adapter {
    Endpoint* Owner=nullptr;
    yojimbo::MessageFactory* CreateMessageFactory(yojimbo::Allocator& allocator)override{return YOJIMBO_NEW(allocator,Messages,allocator);}
    void OnServerClientConnected(int index)override;
    void OnServerClientDisconnected(int index)override;
};
double Now(){static auto start=std::chrono::steady_clock::now();return 100.0+std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();}
void Initialize(){static const bool ready=[] {if(!InitializeYojimbo())throw std::runtime_error("Networking initialization failed");yojimbo_log_level(YOJIMBO_LOG_LEVEL_ERROR);return true;}();(void)ready;}
struct Endpoint {
    NetworkRole Role;
    NetworkSettings Settings;
    std::uint64_t Protocol;
    Adapter Events;
    std::array<std::uint8_t,32> Key{};
    std::unique_ptr<yojimbo::Server> Server;
    std::unique_ptr<yojimbo::Client> Client;
    std::deque<NetworkEvent> Queue;
    std::size_t QueuedBytes=0;
    std::uint64_t Dropped=0;
    double NextSend=0;
    NetworkState LastState=NetworkState::Stopped;
    std::string LastError,InternalAddress;
    std::array<NetworkPeerId,64> PeerIds{};
    std::size_t Cursor=0;
    ~Endpoint(){if(Client)Client->Disconnect();if(Server)Server->Stop();sodium_memzero(Key.data(),Key.size());}
    NetworkState State()const{if(Server)return Server->IsRunning()?NetworkState::Listening:NetworkState::Stopped;if(Client->IsConnected())return NetworkState::Connected;if(Client->IsConnecting())return NetworkState::Connecting;return Client->ConnectionFailed()?NetworkState::Failed:NetworkState::Disconnected;}
    int Index(NetworkPeerId id)const{if(!Server||!id.IsValid())return -1;for(int i=0;i<Server->GetMaxClients();++i)if(Server->IsClientConnected(i)&&Server->GetClientId(i)==id.Value)return i;return -1;}
    bool Enqueue(NetworkEvent event){
        const auto bytes=event.Data.size()+event.Reason.size();
        if(Queue.size()>=Settings.MaxQueuedEvents||bytes>Settings.MaxQueuedEventBytes-QueuedBytes){
            if(event.Type==NetworkEventType::Message&&event.Channel==NetworkChannel::Unreliable){++Dropped;return true;}
            if(event.Type==NetworkEventType::Message)return false;
            while(!Queue.empty()&&(Queue.size()>=Settings.MaxQueuedEvents||bytes>Settings.MaxQueuedEventBytes-QueuedBytes)){QueuedBytes-=Queue.front().Data.size()+Queue.front().Reason.size();Queue.pop_front();}
        }
        QueuedBytes+=bytes;Queue.push_back(std::move(event));return true;
    }
};
void Adapter::OnServerClientConnected(int i){auto id=NetworkPeerId{Owner->Server->GetClientId(i)};Owner->PeerIds[i]=id;Owner->Enqueue({NetworkEventType::Connected,id});}
void Adapter::OnServerClientDisconnected(int i){auto id=Owner->PeerIds[i];Owner->PeerIds[i]={};Owner->Enqueue({NetworkEventType::Disconnected,id,NetworkChannel::Reliable,0,{},yojimbo::GetServerClientDisconnectReasonString(Owner->Server->GetClientDisconnectReason(i))});}
yojimbo::ClientServerConfig Configuration(const NetworkSettings& settings,std::uint64_t protocol){
    yojimbo::ClientServerConfig config;config.protocolId=protocol;config.timeout=settings.TimeoutSeconds;config.maxConnectTokenLifetime=30;config.networkSimulator=false;
    config.clientMemory=4*1024*1024;config.serverGlobalMemory=10*1024*1024;config.serverPerClientMemory=4*1024*1024;config.numChannels=2;config.maxPacketSize=1200;
    for(int i=0;i<2;++i){auto& c=config.channel[i];c.type=i==0?yojimbo::CHANNEL_TYPE_RELIABLE_ORDERED:yojimbo::CHANNEL_TYPE_UNRELIABLE_UNORDERED;c.messageSendQueueSize=16;c.messageReceiveQueueSize=16;c.maxMessagesPerPacket=16;c.maxBlockSize=ReliableLimit;c.disableBlocks=i!=0;}
    return config;
}
void* Create(NetworkRole role,const std::string& address,const NetworkSettings& settings,std::uint64_t protocol,const ByteBuffer& key){
    Initialize();yojimbo::Address bind(address.c_str());if(!bind.IsValid())throw std::invalid_argument("Network bind address must be a numeric IPv4/IPv6 address with a port");
    auto endpoint=std::make_unique<Endpoint>();endpoint->InternalAddress=address;endpoint->Role=role;endpoint->Settings=settings;endpoint->Protocol=protocol;endpoint->Events.Owner=endpoint.get();
    const auto config=Configuration(settings,protocol);
    if(role==NetworkRole::Server){if(key.empty())randombytes_buf(endpoint->Key.data(),endpoint->Key.size());else std::copy(key.begin(),key.end(),endpoint->Key.begin());endpoint->Server=std::make_unique<yojimbo::Server>(yojimbo::GetDefaultAllocator(),endpoint->Key.data(),bind,config,endpoint->Events,Now());if(!endpoint->Server->Start(settings.MaxClients))throw std::runtime_error("Could not start network server");}
    else endpoint->Client=std::make_unique<yojimbo::Client>(yojimbo::GetDefaultAllocator(),bind,config,endpoint->Events,Now());
    endpoint->LastState=endpoint->State();return endpoint.release();
}
void Update(void* pointer,double){
    auto& e=*static_cast<Endpoint*>(pointer);const auto now=Now();
    if(e.Server){e.Server->AdvanceTime(now);e.Server->ReceivePackets();}else{e.Client->AdvanceTime(now);e.Client->ReceivePackets();}
    if(e.Client){const auto state=e.State();if(state!=e.LastState){
        if(state==NetworkState::Connected)e.Enqueue({NetworkEventType::Connected,{e.Client->GetClientId()}});
        else if(state==NetworkState::Disconnected||state==NetworkState::Failed){e.LastError=yojimbo::GetClientDisconnectReasonString(e.Client->GetDisconnectReason());e.Enqueue({NetworkEventType::Disconnected,{e.Client->GetClientId()},NetworkChannel::Reliable,0,{},e.LastError});}
        e.LastState=state;
    }}
    unsigned remaining=256;const auto peers=e.Server?e.Server->GetMaxClients():1;
    for(int visited=0;visited<peers&&remaining;++visited){const auto index=int((e.Cursor+visited)%peers);if(e.Server&&!e.Server->IsClientConnected(index))continue;if(e.Client&&!e.Client->IsConnected())break;
        for(int channel=0;channel<2&&remaining;++channel)for(int n=0;n<16&&remaining;++n){auto* message=e.Server?e.Server->ReceiveMessage(index,channel):e.Client->ReceiveMessage(channel);if(!message)break;--remaining;
            NetworkEvent event;event.Type=NetworkEventType::Message;event.Peer={e.Server?e.Server->GetClientId(index):e.Client->GetClientId()};event.Channel=channel==0?NetworkChannel::Reliable:NetworkChannel::Unreliable;
            if(message->GetType()==0){auto* data=static_cast<SmallMessage*>(message);event.MessageType=std::uint16_t(data->Kind);event.Data.assign(data->Bytes,data->Bytes+data->Count);}
            else {auto* data=static_cast<LargeMessage*>(message);event.MessageType=std::uint16_t(data->Kind);if(data->GetBlockSize()>0)event.Data.assign(data->GetBlockData(),data->GetBlockData()+data->GetBlockSize());}
            if(e.Server)e.Server->ReleaseMessage(index,message);else e.Client->ReleaseMessage(message);
            if(!e.Enqueue(std::move(event))){e.LastError="Receive event queue budget exceeded";e.Enqueue({NetworkEventType::Error,{},NetworkChannel::Reliable,0,{},e.LastError});if(e.Server)e.Server->DisconnectClient(index);else e.Client->Disconnect();break;}
        }
    }
    e.Cursor=(e.Cursor+1)%std::size_t(peers);
    if(now>=e.NextSend){if(e.Server)e.Server->SendPackets();else e.Client->SendPackets();e.NextSend=now+1.0/e.Settings.SendRate;}
}
std::vector<NetworkPeerId> Peers(void* p){auto& e=*static_cast<Endpoint*>(p);std::vector<NetworkPeerId> out;if(e.Server){for(int i=0;i<e.Server->GetMaxClients();++i)if(e.Server->IsClientConnected(i))out.push_back({e.Server->GetClientId(i)});}else if(e.Client->IsConnected())out.push_back({e.Client->GetClientId()});return out;}
ByteBuffer Token(void* p,NetworkPeerId client,const std::vector<std::string>& addresses,int expiry){auto& e=*static_cast<Endpoint*>(p);if(!e.Server)throw std::logic_error("Only a server can issue connection tokens");if(expiry!=30)throw std::invalid_argument("Connection token lifetime is 30 seconds");if(!client.IsValid()||addresses.empty()||addresses.size()>8)throw std::invalid_argument("Invalid token client or addresses");std::vector<const char*> pointers;for(auto& text:addresses){yojimbo::Address address(text.c_str());if(!address.IsValid()||!address.GetPort())throw std::invalid_argument("Invalid token server address");pointers.push_back(text.c_str());}std::vector<const char*> internal(pointers.size(),e.InternalAddress.c_str());ByteBuffer bytes(yojimbo::ConnectTokenBytes);std::uint8_t user[256]{};if(netcode_generate_connect_token(int(pointers.size()),pointers.data(),internal.data(),expiry,e.Settings.TimeoutSeconds,client.Value,e.Protocol,e.Key.data(),user,bytes.data())!=NETCODE_OK)throw std::runtime_error("Connection token creation failed");return bytes;}
bool Connect(void* p,NetworkPeerId id,const ByteBuffer& token){auto& e=*static_cast<Endpoint*>(p);if(!e.Client)throw std::logic_error("Server endpoints cannot connect as clients");if(!id.IsValid()||token.size()!=std::size_t(yojimbo::ConnectTokenBytes))throw std::invalid_argument("Invalid client ID or connection token");// netcode tokens carry their protocol after the fixed 13-byte version field.
    // Check our application protocol as well as netcode's cryptographic verification.
    std::uint64_t protocol=0;for(unsigned i=0;i<8;++i)protocol|=std::uint64_t(token[13+i])<<(i*8);
    if(protocol!=e.Protocol){e.LastError="Connection token belongs to another application/protocol version";return false;}
    return e.Client->Connect(id.Value,const_cast<std::uint8_t*>(token.data()));}
bool Send(void* p,NetworkPeerId peer,std::uint16_t type,const ByteBuffer& bytes,NetworkChannel channel){auto& e=*static_cast<Endpoint*>(p);const int c=channel==NetworkChannel::Reliable?0:1;const auto index=e.Server?e.Index(peer):0;if(index<0||(e.Client&&!e.Client->IsConnected()))return false;if(e.Server?!e.Server->CanSendMessage(index,c):!e.Client->CanSendMessage(c))return false;
    const bool block=bytes.size()>UnreliableLimit;auto* message=e.Server?e.Server->CreateMessage(index,block?1:0):e.Client->CreateMessage(block?1:0);if(!message)return false;
    if(block){auto* data=static_cast<LargeMessage*>(message);data->Kind=type;auto* buffer=e.Server?e.Server->AllocateBlock(index,int(bytes.size())):e.Client->AllocateBlock(int(bytes.size()));if(!buffer){if(e.Server)e.Server->ReleaseMessage(index,message);else e.Client->ReleaseMessage(message);return false;}std::memcpy(buffer,bytes.data(),bytes.size());if(e.Server)e.Server->AttachBlockToMessage(index,message,buffer,int(bytes.size()));else e.Client->AttachBlockToMessage(message,buffer,int(bytes.size()));}
    else {auto* data=static_cast<SmallMessage*>(message);data->Kind=type;data->Count=int(bytes.size());std::copy(bytes.begin(),bytes.end(),data->Bytes);}
    if(e.Server)e.Server->SendMessage(index,c,message);else e.Client->SendMessage(c,message);return true;
}
NetworkStatistics Statistics(void* p,NetworkPeerId peer){auto& e=*static_cast<Endpoint*>(p);yojimbo::NetworkInfo info{};if(e.Server){auto i=e.Index(peer);if(i<0)return {};e.Server->GetNetworkInfo(i,info);}else e.Client->GetNetworkInfo(info);return {info.RTT,info.packetLoss,info.sentBandwidth,info.receivedBandwidth,info.numPacketsSent,info.numPacketsReceived,e.Dropped};}
std::optional<NetworkEvent> Poll(void* p){auto& e=*static_cast<Endpoint*>(p);if(e.Queue.empty())return {};auto out=std::move(e.Queue.front());e.Queue.pop_front();e.QueuedBytes-=out.Data.size()+out.Reason.size();return out;}
bool Disconnect(void* p,NetworkPeerId peer){auto& e=*static_cast<Endpoint*>(p);if(e.Client){e.Client->Disconnect();return true;}auto index=e.Index(peer);if(index<0)return false;e.Server->DisconnectClient(index);return true;}
const Bazzalt::Runtime::NetworkModuleApi Api{1,sizeof(Bazzalt::Runtime::NetworkModuleApi),Create,[](void* p){delete static_cast<Endpoint*>(p);},Update,[](void* p){return static_cast<Endpoint*>(p)->State();},[](void* p){auto& e=*static_cast<Endpoint*>(p);char text[256]{};(e.Server?e.Server->GetAddress():e.Client->GetAddress()).ToString(text,sizeof(text));return std::string(text);},[](void* p){return static_cast<Endpoint*>(p)->LastError;},Peers,Statistics,Token,Connect,Send,Poll,Disconnect};
}
#if defined(_WIN32)
#define NET_EXPORT __declspec(dllexport)
#else
#define NET_EXPORT __attribute__((visibility("default")))
#endif
extern "C" NET_EXPORT const Bazzalt::Runtime::NetworkModuleApi* BazzaltGetNetworkModuleApi(std::uint32_t version,std::uint32_t size){return version==1&&size==sizeof(Api)?&Api:nullptr;}
