#pragma once
#include "ClientConfig.hpp"
#include "MtaCipher.hpp"
#include <winsock2.h>
#include <BitStream.h>
#include <DS_List.h>
#include <PacketPriority.h>
#include <RakNetTypes.h>
#include <ReliabilityLayer.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class PluginInterface;

class MtaTransport
{
public:
    enum class State { Idle, AwaitOpenReply, AwaitConnectionAccepted, Connected, Failed };

    MtaTransport();
    ~MtaTransport();

    bool Connect(const std::string& host, std::uint16_t port, const std::string& serial = {});
    void Disconnect();
    void Poll();
    bool Receive(std::vector<std::uint8_t>& packet);
    bool Send(RakNet::BitStream& stream, PacketPriority priority, PacketReliability reliability, std::uint8_t channel = 0);
    bool Send(const std::uint8_t* data, BitSize_t bits, PacketPriority priority, PacketReliability reliability, std::uint8_t channel = 0);

    bool IsConnected() const { return state_ == State::Connected; }
    bool HasFailed()   const { return state_ == State::Failed; }
    const std::string& Error() const { return error_; }
    std::uint32_t GetPing() const { return pingMs_.load(); }

private:
    bool OpenSocket(const std::string& host, std::uint16_t port);
    bool SendRawUdp(const std::uint8_t* data, std::size_t size);
    bool SendOpenRequest();
    bool HandleOpenReply(const std::uint8_t* data, std::size_t size);
    bool SendConnectedRequest();
    bool SendNewIncomingConnection();
    bool SendConnectedPing();
    bool SendRawControl(const std::uint8_t* data, std::size_t size);
    bool HandleRawControl(const std::vector<std::uint8_t>& packet);
    void PollRawControls();
    bool ParseConnectionAccepted(const std::vector<std::uint8_t>& packet);
    void DiscoverLocalAddresses();
    void HandleConnectedDatagram(const std::uint8_t* data, std::size_t size);
    void DrainReliability();
    void HandlePayload(std::vector<std::uint8_t> packet);
    void Fail(std::string reason);
    static void WriteAddress(RakNet::BitStream& stream, std::uint32_t addr, std::uint16_t port);

    WSADATA  wsa_{};
    SOCKET   socket_{ INVALID_SOCKET };
    sockaddr_in targetMtaAddr_{};
    SystemAddress remote_{ UNASSIGNED_SYSTEM_ADDRESS };
    SystemAddress externalAddress_{ UNASSIGNED_SYSTEM_ADDRESS };
    std::uint16_t systemIndex_{ UNASSIGNED_PLAYER_INDEX };
    std::uint16_t localPort_{ 0 };
    std::array<SystemAddress, 10> localAddresses_{};
    std::array<SystemAddress, 10> serverAddresses_{};
    std::array<std::uint8_t, 16>  localToken_{};
    MtaCipher        cipher_;
    ReliabilityLayer reliability_;
    DataStructures::List<PluginInterface*> plugins_;
    std::deque<std::vector<std::uint8_t>> incoming_;
    State            state_{ State::Idle };
    std::string      error_;
    std::chrono::steady_clock::time_point lastOpenRequest_{};
    std::chrono::steady_clock::time_point lastPing_{};
    std::uint64_t    lastControlPingUs_{ 0 };
    unsigned         openRequestsSent_{ 0 };
    std::string      serial_;
    std::atomic<std::uint32_t> pingMs_{ 0 };
};