#pragma once
#include "Bazzalt/BinaryData.h"
#include <memory>
#include <optional>
namespace Bazzalt {
namespace Runtime {struct NetworkAccess;}
namespace Detail {struct NetworkSessionState;}
struct NetworkPeerId {
    std::uint64_t Value=0;
    bool IsValid() const {return Value!=0;}
    BAZZALT_API std::string ToString() const;
    BAZZALT_API static NetworkPeerId Parse(const std::string& text);
    bool operator==(const NetworkPeerId&) const = default;
};
enum class NetworkChannel { Reliable, Unreliable };
enum class NetworkRole { Server, Client };
enum class NetworkState { Stopped, Listening, Connecting, Connected, Disconnected, Failed };
enum class NetworkEventType { Connected, Disconnected, Message, Error };
struct NetworkSettings {
    std::uint32_t ProtocolVersion=1;
    std::uint16_t MaxClients=32;
    std::uint16_t SendRate=60;
    std::uint16_t TimeoutSeconds=10;
    std::uint16_t MaxQueuedEvents=256;
    std::uint32_t MaxQueuedEventBytes=4u*1024u*1024u;
};
struct NetworkEvent {
    NetworkEventType Type=NetworkEventType::Error;
    NetworkPeerId Peer;
    NetworkChannel Channel=NetworkChannel::Reliable;
    std::uint16_t MessageType=0;
    ByteBuffer Data;
    std::string Reason;
};
struct NetworkStatistics {
    float RoundTripMilliseconds=0,PacketLossPercent=0,SentKilobitsPerSecond=0,ReceivedKilobitsPerSecond=0;
    std::uint64_t PacketsSent=0,PacketsReceived=0,DroppedUnreliableEvents=0;
};
/// Shared endpoint handle. Transport is serviced by the engine; no user pump/init loop.
class BAZZALT_API NetworkSession final {
public:
    NetworkSession()=default;
    bool IsValid() const;
    NetworkRole GetRole() const;
    NetworkState GetState() const;
    std::string GetAddress() const;
    std::string GetLastError() const;
    std::vector<NetworkPeerId> GetPeers() const;
    NetworkStatistics GetStatistics(NetworkPeerId peer={}) const;
    /// Server-only. Deliver this bearer token through your authenticated matchmaking service.
    ByteBuffer CreateConnectionToken(NetworkPeerId client,const std::vector<std::string>& addresses,int expiresInSeconds=30) const;
    bool Connect(NetworkPeerId client,const ByteBuffer& token) const;
    bool Send(NetworkPeerId peer,std::uint16_t type,const ByteBuffer& data,NetworkChannel channel=NetworkChannel::Reliable) const;
    bool SendToServer(std::uint16_t type,const ByteBuffer& data,NetworkChannel channel=NetworkChannel::Reliable) const;
    std::size_t Broadcast(std::uint16_t type,const ByteBuffer& data,NetworkChannel channel=NetworkChannel::Reliable,NetworkPeerId excluded={}) const;
    std::optional<NetworkEvent> PollEvent() const;
    bool DisconnectPeer(NetworkPeerId peer) const;
    void Stop() const;
private:
    friend class Network;
    std::shared_ptr<Detail::NetworkSessionState> m_state;
};
class BAZZALT_API Network final {
public:
    Network()=delete;
    static NetworkSession CreateServer(const std::string& bindAddress,const NetworkSettings& settings={},const ByteBuffer& privateKey={});
    static NetworkSession CreateClient(const std::string& bindAddress="0.0.0.0:0",const NetworkSettings& settings={});
    static NetworkPeerId CreateClientId();
    static ByteBuffer CreateKey();
    static void StopAll();
};
}
