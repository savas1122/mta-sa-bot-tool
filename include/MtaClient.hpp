#pragma once
#include "ClientConfig.hpp"
#include "MtaTransport.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

class MtaClient
{
public:
    explicit MtaClient(ClientConfig config, std::uint16_t instanceId = 1);
    int  Run();
    void RequestStop() { stopRequested_ = true; }

    bool IsOnline()  const;
    bool IsFailed()  const;
    std::string GetStateString() const;
    std::string GetFailReason()  const;
    std::uint32_t GetPing()      const { return transport_.GetPing(); }
    std::string   GetNick()      const { return config_.nick; }
    void          SetNick(const std::string& n) { config_.nick = n; }
    std::uint16_t GetInstanceId() const { return instanceId_; }

    void QueueCommand(const std::string& cmd);
    bool SendCmd(const std::string& cmd);

private:
    enum class State
    {
        Disconnected, Connecting, TransportConnected,
        PlayerJoinSent, ModNegotiated, JoinDataSent,
        JoinComplete, JoinedGame, Maintained, Failed
    };

    void HandlePacket(const std::vector<std::uint8_t>& packet);
    void HandleRakNetPacket(std::uint8_t id, const std::vector<std::uint8_t>& packet);
    void HandleMtaPacket(std::uint8_t logicalId, const std::vector<std::uint8_t>& packet);
    bool SendMtaPacket(std::uint8_t logicalId, RakNet::BitStream& payload,
                       PacketPriority priority, PacketReliability reliability, std::uint8_t channel = 0);
    bool SendEmpty(std::uint8_t logicalId, PacketPriority priority, PacketReliability reliability, std::uint8_t channel = 0);
    bool SendPlayerJoin();
    bool SendJoinData();
    bool SendRpc(std::uint8_t rpcId);
    bool SendPlayerNetworkStatus();
    bool SendPlayerPureSync();
    void SendMaintenance();
    void Fail(const std::string& reason);

    ClientConfig            config_;
    std::uint16_t           instanceId_{ 1 };
    MtaTransport            transport_;
    std::atomic<State>      state_{ State::Disconnected };
    std::uint16_t           serverBitStreamVersion_{ 0 };
    std::chrono::steady_clock::time_point started_{};
    std::chrono::steady_clock::time_point joined_{};
    std::chrono::steady_clock::time_point lastStatus_{};
    std::chrono::steady_clock::time_point lastPureSync_{};
    std::atomic<std::uint32_t> localElementId_{ (1u << 17) - 1u };
    std::atomic<bool>          stopRequested_{};
    mutable std::mutex         commandQueueMutex_;
    std::queue<std::string>    commandQueue_;
    mutable std::mutex         failReasonMutex_;
    std::string                failReason_;
    float posX_{ 0.0f }, posY_{ 0.0f }, posZ_{ 3.0f }, rotZ_{ 0.0f };
};