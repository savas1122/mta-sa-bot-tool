#include "MtaClient.hpp"
#include "MtaJoinCodec.hpp"
#include "MtaProtocol.hpp"

#include <windows.h>
#include <iostream>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>
#include <thread>

using namespace std::chrono_literals;

namespace
{
constexpr std::uint32_t kInvalidElementId = (1u << 17) - 1u;

const char* RakName(std::uint8_t id)
{
    switch (id)
    {
        case 14: return "CONNECTION_REQUEST_ACCEPTED";
        case 15: return "CONNECTION_ATTEMPT_FAILED";
        case 18: return "NO_FREE_INCOMING_CONNECTIONS";
        case 19: return "DISCONNECTION_NOTIFICATION";
        case 20: return "CONNECTION_LOST";
        case 22: return "CONNECTION_BANNED";
        case 23: return "INVALID_PASSWORD";
        default: return "RAKNET_INTERNAL";
    }
}

void WriteFloatBits(RakNet::BitStream& s, unsigned count, float min, float max, float val)
{
    const float alpha  = (std::clamp(val, min, max) - min) / (max - min);
    auto packed = static_cast<std::uint32_t>(std::lround(alpha * static_cast<float>((1u << count) - 1u)));
    s.WriteBits(reinterpret_cast<const unsigned char*>(&packed), count, true);
}

void WriteSignedFixed(RakNet::BitStream& s, unsigned intBits, unsigned fracBits, float val)
{
    const unsigned full = intBits + fracBits;
    const double  mn    = -static_cast<double>(1u << (intBits - 1));
    const double  mx    = static_cast<double>((1u << (intBits - 1)) - 1u);
    const auto    sc    = static_cast<std::int32_t>(std::lround(std::clamp(static_cast<double>(val), mn, mx) * (1u << fracBits)));
    const auto    pk    = static_cast<std::uint32_t>(sc) & ((1u << full) - 1u);
    s.WriteBits(reinterpret_cast<const unsigned char*>(&pk), full, true);
}

void WriteKeys(RakNet::BitStream& s)
{
    std::uint8_t flags = 0;
    s.WriteBits(&flags, 8, true);
    s.Write(false); s.Write(false);
    s.Write(static_cast<std::int8_t>(0));
    s.Write(static_cast<std::int8_t>(0));
}

void WriteCamera(RakNet::BitStream& s)
{
    constexpr float pi = 3.14159265f;
    WriteFloatBits(s, 8, -pi, pi, 0.0f);
    WriteFloatBits(s, 8, -pi, pi, 0.0f);
    s.Write(false);
    std::uint8_t prec = 0;
    s.WriteBits(&prec, 2, true);
    WriteFloatBits(s, 3, -4.0f, 4.0f, 0.0f);
    WriteFloatBits(s, 3, -4.0f, 4.0f, 0.0f);
    WriteFloatBits(s, 3, -4.0f, 4.0f, 0.0f);
}
}

MtaClient::MtaClient(ClientConfig config, std::uint16_t instanceId)
    : config_(std::move(config)), instanceId_(instanceId) {}

int MtaClient::Run()
{
    started_ = std::chrono::steady_clock::now();
    state_   = State::Connecting;

    if (!transport_.Connect(config_.host, config_.port, config_.serial))
    {
        Fail(transport_.Error());
        return 2;
    }

    while (state_ != State::Failed && !stopRequested_)
    {
        transport_.Poll();
        if (transport_.HasFailed()) { Fail(transport_.Error()); break; }

        std::vector<std::uint8_t> packet;
        while (transport_.Receive(packet)) HandlePacket(packet);

        const auto now = std::chrono::steady_clock::now();
        if (state_ == State::Connecting && now - started_ > std::chrono::milliseconds(config_.connectTimeoutMs))
            Fail("connect timeout");
        else if (state_ != State::Maintained && state_ != State::Connecting && now - started_ > 30s)
            Fail("join timeout");

        if (state_ == State::Maintained)
        {
            SendMaintenance();
            std::vector<std::string> cmds;
            {
                std::lock_guard lock(commandQueueMutex_);
                while (!commandQueue_.empty()) { cmds.push_back(std::move(commandQueue_.front())); commandQueue_.pop(); }
            }
            for (const auto& c : cmds) SendCmd(c);
        }

        std::this_thread::sleep_for(1ms);
    }

    if (stopRequested_) { transport_.Disconnect(); return 0; }
    return 3;
}

void MtaClient::HandlePacket(const std::vector<std::uint8_t>& packet)
{
    if (packet.empty()) return;
    const auto id = packet[0];
    if (id < mta::kWireUserPacketBase)
        HandleRakNetPacket(id, packet);
    else
        HandleMtaPacket(static_cast<std::uint8_t>(id - mta::kWireUserPacketBase), packet);
}

void MtaClient::HandleRakNetPacket(std::uint8_t id, const std::vector<std::uint8_t>& packet)
{
    switch (id)
    {
        case 14:
            state_ = State::TransportConnected;
            if (!SendPlayerJoin()) Fail("PlayerJoin failed");
            break;
        case 15: case 18: case 19: case 20: case 22: case 23:
            Fail(std::string("RakNet: ") + RakName(id));
            break;
        default: break;
    }
}

void MtaClient::HandleMtaPacket(std::uint8_t logicalId, const std::vector<std::uint8_t>& packet)
{
    RakNet::BitStream payload(const_cast<unsigned char*>(packet.data() + 1),
        static_cast<unsigned int>(packet.size() - 1), false);

    if (logicalId == static_cast<std::uint8_t>(mta::PacketId::ModName))
    {
        std::uint16_t len = 0; std::string mod;
        if (!payload.Read(serverBitStreamVersion_) || !payload.Read(len) || len > 128) { Fail("bad MOD_NAME"); return; }
        mod.resize(len);
        if (len && !payload.Read(mod.data(), len)) { Fail("truncated MOD_NAME"); return; }
        config_.bitStreamVersion         = serverBitStreamVersion_;
        config_.gameplayBitStreamVersion = serverBitStreamVersion_;
        config_.netcodeVersion = mta::kNetcodeVersion;
        config_.mtaVersion     = mta::kMtaVersion;
        config_.build          = mta::kBuild;
        config_.playerVersion  = mta::kPlayerVersion;
        if (state_ != State::PlayerJoinSent) return;
        state_ = State::ModNegotiated;
        if (!SendJoinData()) Fail("JoinData failed");
    }
    else if (logicalId == static_cast<std::uint8_t>(mta::PacketId::ServerJoinComplete))
    {
        state_ = State::JoinComplete;
        if (!SendRpc(mta::kRpcIngame)) Fail("PLAYER_INGAME_NOTICE failed");
    }
    else if (logicalId == static_cast<std::uint8_t>(mta::PacketId::ServerJoinedGame))
    {
        localElementId_ = 0;
        payload.ReadBits(reinterpret_cast<unsigned char*>(&localElementId_), mta::kElementIdBits, true);
        state_          = State::JoinedGame;
        joined_         = std::chrono::steady_clock::now();
        lastStatus_     = joined_ - std::chrono::milliseconds(mta::kStatusIntervalMs);
        lastPureSync_   = joined_ - std::chrono::milliseconds(mta::kPureSyncIntervalMs);
        if (!SendRpc(mta::kRpcInitial)) Fail("INITIAL_DATA_STREAM failed");
        else state_ = State::Maintained;
    }
    else if (logicalId == static_cast<std::uint8_t>(mta::PacketId::ServerDisconnected))
    {
        std::uint8_t type = 0; std::uint16_t len = 0; std::string reason;
        if (payload.ReadBits(&type, mta::kDisconnectTypeBits, true) && payload.Read(len) && len <= 1024)
        {
            reason.resize(len);
            if (len) payload.Read(reason.data(), len);
        }
        Fail("disconnected: " + reason);
    }
}

bool MtaClient::SendMtaPacket(std::uint8_t logicalId, RakNet::BitStream& payload,
    PacketPriority priority, PacketReliability reliability, std::uint8_t channel)
{
    std::uint8_t wireLogical = logicalId;
    if (logicalId == mta::kSwapA)       wireLogical = mta::kSwapB;
    else if (logicalId == mta::kSwapB)  wireLogical = mta::kSwapA;
    const auto wireId = static_cast<std::uint8_t>(mta::kWireUserPacketBase + wireLogical);
    RakNet::BitStream wire;
    wire.Write(wireId);
    if (payload.GetNumberOfBitsUsed())
    {
        payload.ResetReadPointer();
        wire.Write(&payload, payload.GetNumberOfBitsUsed());
    }
    return transport_.Send(wire, priority, reliability, channel);
}

bool MtaClient::SendEmpty(std::uint8_t logicalId, PacketPriority priority, PacketReliability reliability, std::uint8_t channel)
{
    RakNet::BitStream empty;
    return SendMtaPacket(logicalId, empty, priority, reliability, channel);
}

bool MtaClient::SendPlayerJoin()
{
    const bool ok = SendEmpty(static_cast<std::uint8_t>(mta::PacketId::PlayerJoin), HIGH_PRIORITY, RELIABLE_ORDERED);
    if (ok) state_ = State::PlayerJoinSent;
    return ok;
}

bool MtaClient::SendJoinData()
{
    RakNet::BitStream join;
    mta::BuildJoinData(config_, join);
    const bool ok = SendMtaPacket(static_cast<std::uint8_t>(mta::PacketId::PlayerJoinData), join, HIGH_PRIORITY, RELIABLE_ORDERED);
    if (ok) state_ = State::JoinDataSent;
    return ok;
}

bool MtaClient::SendRpc(std::uint8_t rpcId)
{
    RakNet::BitStream p;
    p.Write(rpcId);
    return SendMtaPacket(static_cast<std::uint8_t>(mta::PacketId::Rpc), p, HIGH_PRIORITY, RELIABLE_ORDERED);
}

bool MtaClient::SendPlayerNetworkStatus()
{
    RakNet::BitStream p;
    p.Write(mta::kNetStatusType);
    p.Write(static_cast<std::uint32_t>(GetTickCount64()));
    return SendMtaPacket(static_cast<std::uint8_t>(mta::PacketId::PlayerNetworkStatus), p, LOW_PRIORITY, UNRELIABLE_SEQUENCED);
}

bool MtaClient::SendPlayerPureSync()
{
    RakNet::BitStream p;
    p.Write(mta::kPureSyncType);
    WriteKeys(p);
    std::uint16_t flags = mta::kPureSyncFlags;
    p.WriteBits(reinterpret_cast<const unsigned char*>(&flags), 15, true);
    WriteSignedFixed(p, 14, 10, posX_);
    WriteSignedFixed(p, 14, 10, posY_);
    p.Write(posZ_);
    constexpr float pi = 3.14159265f;
    WriteFloatBits(p, 16, -pi, pi, rotZ_);
    WriteFloatBits(p, 8, 0.0f, 255.0f, 100.0f);
    WriteFloatBits(p, 8, 0.0f, 127.5f, 0.0f);
    WriteFloatBits(p, 12, -pi, pi, 0.0f);
    WriteCamera(p);
    p.Write(false);
    return SendMtaPacket(static_cast<std::uint8_t>(mta::PacketId::PlayerPureSync), p, MEDIUM_PRIORITY, UNRELIABLE_SEQUENCED);
}

bool MtaClient::SendCmd(const std::string& cmd)
{
    if (cmd.empty()) return false;
    RakNet::BitStream p;
    p.Write(cmd.data(), static_cast<unsigned int>(cmd.size()));
    return SendMtaPacket(static_cast<std::uint8_t>(mta::PacketId::Command), p, HIGH_PRIORITY, RELIABLE_ORDERED, 1);
}

void MtaClient::SendMaintenance()
{
    const auto now = std::chrono::steady_clock::now();
    if (now - lastStatus_ >= std::chrono::milliseconds(mta::kStatusIntervalMs)) { lastStatus_ = now; SendPlayerNetworkStatus(); }
    if (!config_.noPureSync && now - lastPureSync_ >= std::chrono::milliseconds(mta::kPureSyncIntervalMs)) { lastPureSync_ = now; SendPlayerPureSync(); }
}

void MtaClient::QueueCommand(const std::string& cmd)
{
    std::lock_guard lock(commandQueueMutex_);
    commandQueue_.push(cmd);
}

void MtaClient::Fail(const std::string& reason)
{
    std::lock_guard lock(failReasonMutex_);
    failReason_ = reason;
    state_ = State::Failed;
}

bool MtaClient::IsOnline() const
{
    const auto s = state_.load();
    return s == State::Maintained || s == State::JoinedGame;
}

bool MtaClient::IsFailed() const { return state_.load() == State::Failed; }

std::string MtaClient::GetStateString() const
{
    switch (state_.load())
    {
        case State::Connecting:           return "CONNECTING";
        case State::TransportConnected:   return "TRANSPORT_OK";
        case State::PlayerJoinSent:       return "HANDSHAKE";
        case State::ModNegotiated:        return "MOD_OK";
        case State::JoinDataSent:         return "JOIN_SENT";
        case State::JoinComplete:         return "JOIN_COMPLETE";
        case State::JoinedGame:
        case State::Maintained:           return "ONLINE";
        case State::Failed:               return "FAILED";
        default:                          return "IDLE";
    }
}

std::string MtaClient::GetFailReason() const
{
    std::lock_guard lock(failReasonMutex_);
    if (!failReason_.empty()) return failReason_;
    return transport_.Error();
}