#include "ClientConfig.hpp"
#include "MtaProtocol.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
template<std::size_t N>
std::string RandomUpperHex()
{
    std::array<std::uint8_t, N> bytes{};
    BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    constexpr char d[] = "0123456789ABCDEF";
    std::string s(N * 2, '0');
    for (std::size_t i = 0; i < N; ++i) { s[i * 2] = d[bytes[i] >> 4]; s[i * 2 + 1] = d[bytes[i] & 0xF]; }
    return s;
}

std::size_t RandomIndex(std::size_t count)
{
    std::uint32_t v = 0;
    BCryptGenRandom(nullptr, reinterpret_cast<unsigned char*>(&v), sizeof(v), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return v % count;
}

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

std::string RandomHumanNick()
{
    static constexpr const char* first[] = {
        "Alex","Andrey","Anton","Artem","Bogdan","Daniil","Denis","Dmitry",
        "Egor","Gleb","Ilya","Ivan","Kirill","Maksim","Mark","Matvey" };
    static constexpr const char* last[] = {
        "Bondar","Boyko","Fedorenko","Gonchar","Klymenko","Koval",
        "Kravchenko","Kuzmenko","Lysenko","Marchenko","Melnik","Moroz" };
    static std::mutex mx;
    static std::unordered_set<std::string> issued;
    for (unsigned i = 0; i < 64; ++i)
    {
        std::string n = std::string(first[RandomIndex(16)]) + "_" + std::string(last[RandomIndex(12)]);
        std::lock_guard<std::mutex> lock(mx);
        if (issued.insert(n).second) return n;
    }
    return "Player_" + RandomUpperHex<4>();
}
}

void RandomizeClientInstance(ClientConfig& cfg, std::uint16_t idx)
{
    std::string pub = mta::kSerials[(idx > 0 ? idx - 1 : 0) % mta::kSerialCount];
    const std::string seed1 = "PRIV_HWID_" + std::to_string(idx) + "_" + pub;
    const std::string seed2 = "PRIV_TAIL_" + pub + "_" + std::to_string(idx);
    auto d1 = Md5(seed1.data(), seed1.size());
    auto d2 = Md5(seed2.data(), seed2.size());
    constexpr char hex[] = "0123456789ABCDEF";
    std::string priv(64, '0');
    for (std::size_t i = 0; i < 16; ++i)
    {
        priv[i * 2] = hex[d1[i] >> 4]; priv[i * 2 + 1] = hex[d1[i] & 0xF];
        priv[32 + i * 2] = hex[d2[i] >> 4]; priv[32 + i * 2 + 1] = hex[d2[i] & 0xF];
    }
    cfg.serial = pub;
    cfg.privateSerial = priv;
    if (cfg.randomNick) cfg.nick = RandomHumanNick();
}
