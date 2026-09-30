#pragma once
#include "ClientConfig.hpp"
#include "MtaClient.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class BotManager
{
public:
    BotManager() = default;
    ~BotManager();

    void Start(const ClientConfig& cfg, std::uint16_t count);
    void StopAll();
    bool RenameBot(std::uint16_t id, const std::string& newName);
    std::size_t GetOnlineCount() const;

    struct BotStatusInfo
    {
        std::uint16_t id{};
        std::string   nick;
        std::string   status;
        std::uint32_t pingMs{};
        bool          isOnline{};
    };

    std::vector<BotStatusInfo> GetBotStatuses() const;

private:
    struct BotEntry
    {
        std::uint16_t              id{};
        std::string                nick;
        std::shared_ptr<MtaClient> client;
        std::thread                worker;
    };

    mutable std::mutex                          mutex_;
    std::vector<std::shared_ptr<BotEntry>>      bots_;
    std::atomic<bool>                           running_{ false };
};