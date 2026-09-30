#include "MtaJoinCodec.hpp"
#include "MtaProtocol.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstring>
#include <vector>

namespace
{
std::array<std::uint8_t, 16> Md5(const void* data, std::size_t size)
{
    std::array<std::uint8_t, 16> digest{};
    BCRYPT_ALG_HANDLE alg{}; BCRYPT_HASH_HANDLE hash{}; DWORD objLen{}, got{};
    BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM, nullptr, 0);
    BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &got, 0);
    std::vector<std::uint8_t> obj(objLen);
    BCryptCreateHash(alg, &hash, obj.data(), objLen, nullptr, 0, 0);
    BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<void*>(data)), static_cast<ULONG>(size), 0);
    BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return digest;
}
}

void mta::BuildJoinData(const ClientConfig& cfg, RakNet::BitStream& s)
{
    s.Reset();
    s.Write(static_cast<std::uint16_t>(cfg.bitStreamVersion));

    std::uint8_t fixed[32] = {};
    std::memcpy(fixed, cfg.serial.data(), std::min<std::size_t>(cfg.serial.size(), 32));
    for (std::size_t i = 0; i < 32; ++i)
        fixed[i] = static_cast<std::uint8_t>(fixed[i] ^ static_cast<std::uint8_t>(i) ^ kSerialXor ^ static_cast<std::uint8_t>(1u << (i & 7)));
    s.WriteAlignedBytes(fixed, 32);

    std::vector<std::uint8_t> prim(cfg.privateSerial.begin(), cfg.privateSerial.end());
    prim.resize(kPrimaryLen);
    for (std::size_t i = 0; i < prim.size(); ++i)
        prim[i] = static_cast<std::uint8_t>(prim[i] ^ static_cast<std::uint8_t>(i) ^ kPrimaryXor ^ static_cast<std::uint8_t>(1u << (i & 7)));
    s.Write(static_cast<std::uint16_t>(prim.size()));
    if (!prim.empty()) s.Write(reinterpret_cast<const char*>(prim.data()), static_cast<unsigned int>(prim.size()));

    std::uint8_t v = kVersionMajor;  s.WriteBits(&v, kVersionBits, true);
    v = kVersionMinor;               s.WriteBits(&v, kVersionBits, true);
    v = kVersionPatch;               s.WriteBits(&v, kVersionBits, true);
    v = kReleaseChannel;             s.WriteBits(&v, kVersionBits, true);

    auto build = kBuild;
    s.WriteBits(reinterpret_cast<unsigned char*>(&build), kBuildBits, true);
    s.WriteCompressed(static_cast<std::uint16_t>(0));
    auto buildXor = build ^ kBuildXor;
    s.WriteBits(reinterpret_cast<unsigned char*>(&buildXor), kBuildBits, true);

    std::vector<std::uint8_t> wrapper(kWrapperLen);
    BCryptGenRandom(nullptr, wrapper.data(), static_cast<ULONG>(wrapper.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    s.Write(static_cast<std::uint16_t>(wrapper.size()));
    s.WriteBits(wrapper.data(), static_cast<unsigned int>(wrapper.size() * 8), false);
    s.AlignWriteToByteBoundary();

    s.Write(kNetcodeVersion);
    s.Write(kMtaVersion);
    s.Write(static_cast<std::uint16_t>(cfg.bitStreamVersion));

    const auto pvl = static_cast<std::uint16_t>(std::strlen(kPlayerVersion));
    s.Write(pvl);
    if (pvl) s.Write(kPlayerVersion, pvl);
    s.Write0();
    s.Write(kGameVersion);

    std::vector<char> nickBuf(kNickLen, 0);
    std::memcpy(nickBuf.data(), cfg.nick.data(), std::min<std::size_t>(cfg.nick.size(), nickBuf.size() - 1));
    s.Write(nickBuf.data(), static_cast<unsigned int>(nickBuf.size()));

    std::vector<std::uint8_t> pw(kPasswordLen, 0);
    if (!cfg.password.empty())
    {
        auto d = Md5(cfg.password.data(), cfg.password.size());
        std::memcpy(pw.data(), d.data(), std::min<std::size_t>(d.size(), pw.size()));
    }
    s.Write(reinterpret_cast<const char*>(pw.data()), static_cast<unsigned int>(pw.size()));

    auto uh = Md5((cfg.serial + cfg.privateSerial).data(), (cfg.serial + cfg.privateSerial).size());
    std::vector<char> su(kSerialUserLen, 0);
    constexpr char hex[] = "0123456789ABCDEF";
    for (std::size_t i = 0; i < 16 && i * 2 + 1 < su.size(); ++i)
    {
        su[i * 2] = hex[uh[i] >> 4];
        su[i * 2 + 1] = hex[uh[i] & 0xF];
    }
    s.Write(su.data(), static_cast<unsigned int>(su.size()));
}
