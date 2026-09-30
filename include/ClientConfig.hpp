#pragma once
#include <cstdint>
#include <string>

struct Socks5Config
{
    bool enabled{false};
    std::string ip;
    std::uint16_t port{1080};
    std::string username;
    std::string password;
};

struct ClientConfig
{
    std::string   host             { "127.0.0.1" };
    std::uint16_t port             { 22003 };
    std::string   nick             { "bot" };
    std::string   serial;
    std::string   privateSerial;
    std::string   password;
    std::string   playerVersion;
    std::uint16_t bitStreamVersion { 0 };
    std::uint8_t  versionMajor     { 1 };
    std::uint8_t  versionMinor     { 6 };
    std::uint8_t  versionPatch     { 0 };
    std::uint8_t  releaseChannel   { 9 };
    std::uint32_t build            { 0 };
    std::uint16_t revision         { 0 };
    std::uint16_t netcodeVersion   { 0 };
    std::uint16_t mtaVersion       { 0 };
    std::uint16_t gameplayBitStreamVersion { 134 };
    std::uint8_t  gameVersion      { 11 };
    std::uint32_t connectTimeoutMs { 15000 };
    std::uint32_t holdSeconds      { 0 };
    bool          randomIdentities { true };
    bool          randomNick       { false };
    bool          moveBots         { false };
    bool          noPureSync       { false };
    bool          transportOnly    { false };
    float         spawnX           { 0.0f };
    float         spawnY           { 0.0f };
    float         spawnZ           { 3.0f };
    Socks5Config  proxy;
};

void RandomizeClientInstance(ClientConfig& config, std::uint16_t instanceIndex);