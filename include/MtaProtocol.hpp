#pragma once
#include <array>
#include <cstdint>

namespace mta
{
    constexpr std::uint8_t kWireUserPacketBase = 99;
    constexpr std::uint8_t kSwapA = 4;
    constexpr std::uint8_t kSwapB = 19;

    enum class PacketId : std::uint8_t
    {
        PlayerJoin          = 3,
        PlayerJoinData      = 4,
        ModName             = 7,
        ServerJoinComplete  = 2,
        ServerJoinedGame    = 22,
        ServerDisconnected  = 23,
        Rpc                 = 24,
        PlayerPureSync      = 32,
        Command             = 42,
        PlayerNetworkStatus = 103,
    };

    constexpr std::uint8_t  kRpcIngame            = 0;
    constexpr std::uint8_t  kRpcInitial           = 1;
    constexpr std::uint8_t  kNetStatusType        = 1;
    constexpr std::uint8_t  kPureSyncType         = 0;
    constexpr std::uint16_t kPureSyncFlags        = 1u << 1;
    constexpr std::uint16_t kDisconnectTypeBits   = 5;
    constexpr std::uint32_t kStatusIntervalMs     = 2000;
    constexpr std::uint32_t kPureSyncIntervalMs   = 100;
    constexpr std::uint32_t kElementIdBits        = 17;

    constexpr std::uint16_t kNetcodeVersion = 0x01DA;
    constexpr std::uint16_t kMtaVersion     = 0x0160;
    constexpr std::uint32_t kBuild          = 24139;
    constexpr std::uint32_t kBuildXor       = 0x54ABCD;
    constexpr char          kPlayerVersion[] = "1.6.0-9.24139.0";
    constexpr std::uint8_t  kVersionBits    = 7;
    constexpr std::uint8_t  kVersionMajor   = 1;
    constexpr std::uint8_t  kVersionMinor   = 6;
    constexpr std::uint8_t  kVersionPatch   = 0;
    constexpr std::uint8_t  kReleaseChannel = 9;
    constexpr std::uint8_t  kBuildBits      = 23;
    constexpr std::uint8_t  kGameVersion    = 11;
    constexpr std::uint8_t  kWrapperLen     = 61;
    constexpr std::uint8_t  kPrimaryLen     = 64;
    constexpr std::uint8_t  kSerialXor      = 0xD1;
    constexpr std::uint8_t  kPrimaryXor     = 0xD4;
    constexpr std::uint8_t  kNickLen        = 22;
    constexpr std::uint8_t  kPasswordLen    = 16;
    constexpr std::uint8_t  kSerialUserLen  = 32;

    constexpr std::array<std::uint8_t, 16> kOfflineMagic{
        0x00, 0xFF, 0x00, 0xFF, 0xFE, 0xFE, 0xFE, 0xFE,
        0xFD, 0xFD, 0xFD, 0xFD, 0x78, 0x56, 0x34, 0x17 };

    constexpr std::uint8_t kOpenReplyId      = 10;
    constexpr std::uint8_t kHelloMark        = 0xED;
    constexpr std::uint8_t kHelloEnd         = 0xDE;
    constexpr std::uint8_t kTlvMode          = 0x80;
    constexpr std::uint8_t kTlvKey           = 0x6E;
    constexpr std::uint8_t kTlvTag           = 0x1A;
    constexpr std::uint8_t kTlvIdle          = 0xA8;
    constexpr std::uint8_t kTlvPing          = 0xC8;
    constexpr std::uint16_t kCrcPoly         = 0x1021;
    constexpr std::uint32_t kLcgMul          = 214013u;
    constexpr std::uint32_t kLcgAdd          = 2531011u;
    constexpr std::uint32_t kSboxDrops       = 1000;
    constexpr std::uint8_t  kModeCipher      = 1;
    constexpr std::uint8_t  kTagLimit        = 5;
    constexpr std::uint8_t  kHeaderExtra     = 3;

    constexpr std::uint8_t kOpenRequestId    = 9;
    constexpr std::uint8_t kOpenRequestProto = 4;
    constexpr std::array<std::uint8_t, 17> kClientHello{
        0xED, 0xCA, 0x01, 0x01, 0xA8, 0x04, 0xE8, 0x03, 0x00, 0x00,
        0xC8, 0x04, 0xE8, 0x03, 0x00, 0x00, 0xDE };
    constexpr std::uint8_t kConnectionRequestId = 4;
    constexpr std::uint8_t kNewIncomingId       = 17;
    constexpr std::uint8_t kDisconnectId        = 19;
    constexpr std::uint8_t kInternalPingId      = 0;
    constexpr std::uint8_t kPongId              = 3;
    constexpr std::uint8_t kAcceptedId          = 14;
    constexpr std::uint8_t kRawControlId        = 38;
    constexpr std::uint8_t kRawPingType         = 1;
    constexpr std::uint8_t kRawPongType         = 2;
    constexpr std::array<std::uint8_t, 11> kSpecial{ 1, 2, 4, 9, 10, 13, 15, 16, 18, 22, 95 };
    constexpr std::uint32_t kMtu                  = 1188;
    constexpr std::uint32_t kOpenRequestCount     = 20;
    constexpr std::uint32_t kReliabilityTimeoutMs = 15000;
    constexpr std::uint32_t kConnectedPingMs      = 2500;

    constexpr const char* kSerials[] = {
        "8609939AE18F48AEC72A091501E298E3",
        "EE896A30CAE3924244578F0DEFCB3792",
        "50D41669DD0345AD1A1021F03637B414",
        "799D31D8741E821DE31509C423F0ADF2",
        "1F5522155F86E734BF34EF72DE7755A1",
        "7732482A7A7BA5F3AD5CA30F07595224",
        "0BEA0223AC9D05D0F56BDE35C26543F3",
        "4B362AFA6AA7795EB995F26EA31FC5E4",
        "65457B0F4BE7F0D291CFCA5B89146BB4",
        "8F8B0204C418A29C15D7C23FC0855B93",
    };
    constexpr int kSerialCount = 10;
}
