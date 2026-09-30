#include "MtaTransport.hpp"
#include "MtaProtocol.hpp"
#include <ws2tcpip.h>
#include <SocketLayer.h>
#include <GetTime.h>
#include <RakMemoryOverride.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;

namespace
{
std::uint64_t ClockUs()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void AppendU32(std::vector<std::uint8_t>& p, std::uint32_t v)
{
    const auto* b = reinterpret_cast<const std::uint8_t*>(&v);
    p.insert(p.end(), b, b + 4);
}

void GenerateLocalToken(std::array<std::uint8_t, 16>& token)
{
    std::array<std::uint32_t, 4> w{};
    w[0] = static_cast<std::uint32_t>(ClockUs());
    for (std::size_t i = 1; i < w.size(); ++i)
    {
        Sleep(1); Sleep(0);
        w[i] = static_cast<std::uint32_t>(ClockUs());
        for (unsigned s = 0; s < 28; s += 4)
        {
            const auto b = ClockUs(); Sleep(1); Sleep(0);
            w[i] ^= static_cast<std::uint32_t>(ClockUs() - b) << 28 >> s;
        }
    }
    std::memcpy(token.data(), w.data(), token.size());
}
}

MtaTransport::MtaTransport()
{
    if (WSAStartup(MAKEWORD(2, 2), &wsa_) != 0)
        throw std::runtime_error("WSAStartup failed");
    reliability_.SetTimeoutTime(mta::kReliabilityTimeoutMs);
    reliability_.SetDatagramTransform(&MtaCipher::Transform, &cipher_);
    localAddresses_.fill(SystemAddress{0xFFFFFFFFu, 0});
    serverAddresses_.fill(UNASSIGNED_SYSTEM_ADDRESS);
}

MtaTransport::~MtaTransport()
{
    if (socket_ != INVALID_SOCKET) closesocket(socket_);
    WSACleanup();
}

bool MtaTransport::Connect(const std::string& host, std::uint16_t port, const std::string& serial)
{
    if (state_ != State::Idle) return false;
    serial_ = serial;
    GenerateLocalToken(localToken_);
    if (!OpenSocket(host, port)) return false;
    openRequestsSent_ = 0;
    lastControlPingUs_ = 0;
    lastOpenRequest_   = std::chrono::steady_clock::now() - 500ms;
    state_ = State::AwaitOpenReply;
    return true;
}

bool MtaTransport::OpenSocket(const std::string& host, std::uint16_t port)
{
    addrinfo hints{}, *result = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result)
    {
        Fail("cannot resolve host: " + host);
        return false;
    }
    const sockaddr_in* sel = nullptr;
    for (auto* e = result; e; e = e->ai_next)
        if (e->ai_family == AF_INET && e->ai_addrlen >= sizeof(sockaddr_in))
            { sel = reinterpret_cast<const sockaddr_in*>(e->ai_addr); break; }
    if (!sel) { freeaddrinfo(result); Fail("no IPv4 for host: " + host); return false; }
    remote_.binaryAddress = sel->sin_addr.s_addr;
    remote_.port = port;
    freeaddrinfo(result);

    socket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ == INVALID_SOCKET) { Fail("socket creation failed"); return false; }
    const int rb = 0x40000, sb = 0x4000; const BOOL bc = TRUE;
    u_long nb = 1;
    setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rb), sizeof(rb));
    setsockopt(socket_, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sb), sizeof(sb));
    setsockopt(socket_, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&bc), sizeof(bc));
    ioctlsocket(socket_, FIONBIO, &nb);

    sockaddr_in local{};
    local.sin_family      = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(socket_, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR)
    {
        Fail("UDP bind failed: " + std::to_string(WSAGetLastError()));
        return false;
    }
    int ll = sizeof(local);
    if (getsockname(socket_, reinterpret_cast<sockaddr*>(&local), &ll) == 0)
        localPort_ = ntohs(local.sin_port);
    DiscoverLocalAddresses();
    return true;
}

bool MtaTransport::SendRawUdp(const std::uint8_t* data, std::size_t size)
{
    if (socket_ == INVALID_SOCKET || !data || !size) return false;
    sockaddr_in target{};
    target.sin_family      = AF_INET;
    target.sin_port        = htons(remote_.port);
    target.sin_addr.s_addr = remote_.binaryAddress;
    return sendto(socket_, reinterpret_cast<const char*>(data), static_cast<int>(size), 0,
        reinterpret_cast<const sockaddr*>(&target), sizeof(target)) > 0;
}

void MtaTransport::Disconnect()
{
    if (state_ == State::Connected)
    {
        RakNet::BitStream s;
        s.Write(mta::kDisconnectId);
        Send(s, SYSTEM_PRIORITY, RELIABLE_ORDERED);
        const auto dl = std::chrono::steady_clock::now() + 150ms;
        while (state_ == State::Connected && std::chrono::steady_clock::now() < dl)
        { Poll(); std::this_thread::sleep_for(1ms); }
    }
    state_ = State::Idle;
}

void MtaTransport::Poll()
{
    if (state_ == State::Failed || state_ == State::Idle) return;

    std::array<std::uint8_t, 1492> buf{};
    for (;;)
    {
        sockaddr_in from{}; int fl = sizeof(from);
        const int len = recvfrom(socket_, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0,
            reinterpret_cast<sockaddr*>(&from), &fl);
        if (len == SOCKET_ERROR) { if (WSAGetLastError() != WSAEWOULDBLOCK) Fail("recvfrom error"); break; }
        if (len == 0) break;
        if (from.sin_addr.s_addr != remote_.binaryAddress || ntohs(from.sin_port) != remote_.port) continue;
        if (state_ == State::AwaitOpenReply)
        {
            if (len > 512) continue;
            if (buf[0] == mta::kOpenReplyId && !HandleOpenReply(buf.data(), static_cast<std::size_t>(len))) break;
        }
        else HandleConnectedDatagram(buf.data(), static_cast<std::size_t>(len));
    }

    if (state_ == State::AwaitOpenReply && std::chrono::steady_clock::now() - lastOpenRequest_ > 500ms)
    {
        if (openRequestsSent_ < mta::kOpenRequestCount) SendOpenRequest();
        else { incoming_.push_back({15}); state_ = State::Idle; }
    }

    if (state_ == State::AwaitConnectionAccepted || state_ == State::Connected)
    {
        reliability_.Update(socket_, remote_, mta::kMtu, RakNet::GetTimeNS(), 0, plugins_);
        DrainReliability();
        PollRawControls();
        if (reliability_.IsDeadConnection()) Fail("RakNet reliability timeout");
    }

    if (state_ == State::Connected && std::chrono::steady_clock::now() - lastPing_ >= std::chrono::milliseconds(mta::kConnectedPingMs))
        SendConnectedPing();
}

bool MtaTransport::Receive(std::vector<std::uint8_t>& packet)
{
    if (incoming_.empty()) return false;
    packet = std::move(incoming_.front());
    incoming_.pop_front();
    return true;
}

bool MtaTransport::Send(RakNet::BitStream& stream, PacketPriority priority, PacketReliability reliability, std::uint8_t channel)
{
    return Send(stream.GetData(), stream.GetNumberOfBitsUsed(), priority, reliability, channel);
}

bool MtaTransport::Send(const std::uint8_t* data, BitSize_t bits, PacketPriority priority, PacketReliability reliability, std::uint8_t channel)
{
    if ((state_ != State::AwaitConnectionAccepted && state_ != State::Connected) || !data || !bits) return false;
    return reliability_.Send(reinterpret_cast<char*>(const_cast<std::uint8_t*>(data)), bits,
        priority, reliability, channel, true, mta::kMtu, RakNet::GetTimeNS());
}

bool MtaTransport::SendOpenRequest()
{
    std::vector<std::uint8_t> req;
    req.reserve(64);
    req.push_back(mta::kOpenRequestId);
    req.push_back(mta::kOpenRequestProto);
    req.insert(req.end(), localToken_.begin(), localToken_.end());
    req.insert(req.end(), mta::kOfflineMagic.begin(), mta::kOfflineMagic.end());
    req.insert(req.end(), mta::kClientHello.begin(), mta::kClientHello.end());
    bool ok = SendRawUdp(req.data(), req.size());
    ++openRequestsSent_;
    lastOpenRequest_ = std::chrono::steady_clock::now();
    return ok;
}

bool MtaTransport::HandleOpenReply(const std::uint8_t* data, std::size_t size)
{
    std::string err;
    if (!cipher_.InitializeFromOpenReply(data, size, err)) { Fail(err); return false; }
    reliability_.SetDatagramTransformOverhead(cipher_.Mode() == mta::kModeCipher ? mta::kHeaderExtra : 0u);
    state_ = State::AwaitConnectionAccepted;
    return SendConnectedRequest();
}

bool MtaTransport::SendConnectedRequest()
{
    RakNet::BitStream s;
    s.Write(mta::kConnectionRequestId);
    s.WriteAlignedBytes(mta::kOfflineMagic.data(), static_cast<unsigned int>(mta::kOfflineMagic.size()));
    s.WriteAlignedBytes(localToken_.data(), static_cast<unsigned int>(localToken_.size()));
    return Send(s, SYSTEM_PRIORITY, RELIABLE);
}

bool MtaTransport::SendNewIncomingConnection()
{
    RakNet::BitStream s;
    s.Write(mta::kNewIncomingId);
    WriteAddress(s, remote_.binaryAddress, remote_.port);
    for (const auto& a : localAddresses_) WriteAddress(s, a.binaryAddress, a.port);
    return Send(s, SYSTEM_PRIORITY, RELIABLE_ORDERED);
}

bool MtaTransport::ParseConnectionAccepted(const std::vector<std::uint8_t>& packet)
{
    if (packet.size() < 69) return false;
    RakNet::BitStream s(const_cast<unsigned char*>(packet.data()), static_cast<unsigned int>(packet.size()), false);
    s.IgnoreBits(8);
    auto readAddr = [&s](SystemAddress& a) {
        std::uint32_t h{}; std::uint16_t np{};
        if (!s.ReadBits(reinterpret_cast<unsigned char*>(&h), 32, true) ||
            !s.ReadBits(reinterpret_cast<unsigned char*>(&np), 16, true)) return false;
        a.binaryAddress = ~h; a.port = ntohs(np); return true;
    };
    if (!readAddr(externalAddress_) || !s.Read(systemIndex_)) return false;
    for (auto& a : serverAddresses_) if (!readAddr(a)) return false;
    return true;
}

void MtaTransport::DiscoverLocalAddresses()
{
    localAddresses_.fill(SystemAddress{0xFFFFFFFFu, 0});
    std::array<char, 80> name{};
    if (gethostname(name.data(), static_cast<int>(name.size())) == SOCKET_ERROR) return;
    const auto* host = gethostbyname(name.data());
    if (!host || host->h_addrtype != AF_INET) return;
    for (std::size_t i = 0; i < localAddresses_.size() && host->h_addr_list[i]; ++i)
    {
        std::memcpy(&localAddresses_[i].binaryAddress, host->h_addr_list[i], sizeof(std::uint32_t));
        localAddresses_[i].port = localPort_;
    }
}

bool MtaTransport::SendConnectedPing()
{
    RakNet::BitStream s;
    s.Write(mta::kInternalPingId);
    s.Write(static_cast<std::uint64_t>(GetTickCount64()));
    lastPing_ = std::chrono::steady_clock::now();
    return Send(s, SYSTEM_PRIORITY, UNRELIABLE);
}

bool MtaTransport::SendRawControl(const std::uint8_t* data, std::size_t size)
{
    if (!data || !size || (state_ != State::AwaitConnectionAccepted && state_ != State::Connected)) return false;
    std::array<std::uint8_t, 2048> enc{};
    const auto n = cipher_.Encode(data, size, enc.data(), enc.size());
    return n > 0 && SendRawUdp(enc.data(), static_cast<std::size_t>(n));
}

bool MtaTransport::HandleRawControl(const std::vector<std::uint8_t>& packet)
{
    if (packet.size() < 18 || packet[0] != mta::kRawControlId || !std::equal(mta::kOfflineMagic.begin(), mta::kOfflineMagic.end(), packet.begin() + 1)) return false;
    const auto type = packet[17];
    if (type == mta::kRawPingType && packet.size() >= 22)
    {
        std::uint32_t echo{};
        std::memcpy(&echo, packet.data() + 18, 4);
        std::vector<std::uint8_t> reply;
        reply.reserve(26);
        reply.push_back(mta::kRawControlId);
        reply.insert(reply.end(), mta::kOfflineMagic.begin(), mta::kOfflineMagic.end());
        reply.push_back(mta::kRawPongType);
        AppendU32(reply, echo);
        AppendU32(reply, static_cast<std::uint32_t>(GetTickCount64()));
        SendRawControl(reply.data(), reply.size());
    }
    return true;
}

void MtaTransport::PollRawControls()
{
    const auto now = ClockUs();
    const auto pingInterval = cipher_.PingIntervalUs();
    if (pingInterval && now - lastControlPingUs_ >= pingInterval)
    {
        lastControlPingUs_ = now;
        std::vector<std::uint8_t> p;
        p.reserve(22);
        p.push_back(mta::kRawControlId);
        p.insert(p.end(), mta::kOfflineMagic.begin(), mta::kOfflineMagic.end());
        p.push_back(mta::kRawPingType);
        AppendU32(p, static_cast<std::uint32_t>(GetTickCount64()));
        SendRawControl(p.data(), p.size());
    }
    const auto idle = cipher_.IdleIntervalUs();
    const auto lastSend = cipher_.LastEncryptedSendUs();
    if (idle && lastSend && now - lastSend >= idle)
    {
        std::array<std::uint8_t, 18> p{};
        p[0] = mta::kRawControlId;
        std::copy(mta::kOfflineMagic.begin(), mta::kOfflineMagic.end(), p.begin() + 1);
        SendRawControl(p.data(), p.size());
    }
}

void MtaTransport::HandleConnectedDatagram(const std::uint8_t* data, std::size_t size)
{
    std::vector<std::uint8_t> plain; std::string err;
    if (!cipher_.Decode(data, size, plain, err)) return;
    if (!plain.empty() && plain[0] == mta::kRawControlId) { HandleRawControl(plain); return; }
    const auto* special = mta::kSpecial.data();
    const auto specialCount = mta::kSpecial.size();
    if (plain.size() >= 17 && std::find(special, special + specialCount, plain[0]) != special + specialCount &&
        std::equal(mta::kOfflineMagic.begin(), mta::kOfflineMagic.end(), plain.begin() + 1))
    {
        incoming_.push_back(std::move(plain)); return;
    }
    if (!reliability_.HandleSocketReceiveFromConnectedPlayer(reinterpret_cast<const char*>(plain.data()),
        static_cast<unsigned int>(plain.size()), remote_, plugins_, mta::kMtu))
    {
        incoming_.push_back(std::vector<std::uint8_t>{24}); return;
    }
    DrainReliability();
}

void MtaTransport::DrainReliability()
{
    for (;;)
    {
        unsigned char* data = nullptr;
        const auto bits = reliability_.Receive(&data);
        if (!bits || !data) break;
        const auto bytes = static_cast<std::size_t>((bits + 7) / 8);
        std::vector<std::uint8_t> packet(data, data + bytes);
        RakNet::RakMemoryOverride::RakFree(data);
        HandlePayload(std::move(packet));
    }
}

void MtaTransport::HandlePayload(std::vector<std::uint8_t> packet)
{
    if (packet.empty()) return;
    if (packet[0] == mta::kAcceptedId && state_ == State::AwaitConnectionAccepted)
    {
        if (!ParseConnectionAccepted(packet)) { Fail("malformed CONNECTION_REQUEST_ACCEPTED"); return; }
        state_ = State::Connected;
        if (!SendNewIncomingConnection()) { Fail("failed to send NEW_INCOMING_CONNECTION"); return; }
        SendConnectedPing();
        incoming_.push_back(std::move(packet));
        return;
    }
    if (packet[0] == mta::kInternalPingId && packet.size() >= 9)
    {
        RakNet::BitStream pong;
        pong.Write(mta::kPongId);
        pong.WriteAlignedBytes(packet.data() + 1, 8);
        pong.Write(static_cast<std::uint64_t>(GetTickCount64()));
        Send(pong, SYSTEM_PRIORITY, UNRELIABLE);
        return;
    }
    if (packet[0] == mta::kPongId && packet.size() >= 9)
    {
        std::uint64_t sent = 0;
        std::memcpy(&sent, packet.data() + 1, 8);
        const auto now = GetTickCount64();
        if (now >= sent) pingMs_ = static_cast<std::uint32_t>(now - sent);
        return;
    }
    incoming_.push_back(std::move(packet));
}

void MtaTransport::Fail(std::string reason)
{
    if (state_ != State::Failed) error_ = std::move(reason);
    state_ = State::Failed;
}

void MtaTransport::WriteAddress(RakNet::BitStream& s, std::uint32_t addr, std::uint16_t port)
{
    const auto h = ~addr;
    const auto np = htons(port);
    s.WriteBits(reinterpret_cast<const unsigned char*>(&h), 32, true);
    s.WriteBits(reinterpret_cast<const unsigned char*>(&np), 16, true);
}