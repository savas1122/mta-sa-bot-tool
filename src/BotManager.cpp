#include "BotManager.hpp"
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

BotManager::~BotManager() { StopAll(); }

void BotManager::Start(const ClientConfig& cfg, std::uint16_t count)
{
    StopAll();
    running_ = true;
    std::lock_guard lock(mutex_);
    bots_.reserve(count);

    for (std::uint16_t i = 1; i <= count; ++i)
    {
        auto botCfg = cfg;
        botCfg.randomIdentities = true;
        RandomizeClientInstance(botCfg, i);
        if (!cfg.randomNick)
            botCfg.nick = cfg.nick + std::to_string(i);

        auto entry     = std::make_shared<BotEntry>();
        entry->id      = i;
        entry->nick    = botCfg.nick;
        entry->client  = std::make_shared<MtaClient>(std::move(botCfg), i);

        auto client = entry->client;
        entry->worker = std::thread([client]() { try { client->Run(); } catch (...) {} });

        bots_.push_back(entry);
        if (i < count) std::this_thread::sleep_for(600ms);
    }
}

void BotManager::StopAll()
{
    if (!running_.exchange(false)) return;

    std::vector<std::shared_ptr<BotEntry>> snap;
    { std::lock_guard lock(mutex_); snap = bots_; }

    for (auto& b : snap) if (b->client) b->client->RequestStop();
    for (auto& b : snap) if (b->worker.joinable()) b->worker.join();

    std::lock_guard lock(mutex_);
    bots_.clear();
}

bool BotManager::RenameBot(std::uint16_t id, const std::string& newName)
{
    if (newName.empty() || newName.size() > 22) return false;
    std::lock_guard lock(mutex_);
    for (auto& b : bots_)
    {
        if (b->id == id)
        {
            b->nick = newName;
            if (b->client) b->client->SetNick(newName);
            return true;
        }
    }
    return false;
}

std::size_t BotManager::GetOnlineCount() const
{
    std::lock_guard lock(mutex_);
    std::size_t n = 0;
    for (auto& b : bots_) if (b->client && b->client->IsOnline()) ++n;
    return n;
}

std::vector<BotManager::BotStatusInfo> BotManager::GetBotStatuses() const
{
    std::vector<BotStatusInfo> list;
    std::lock_guard lock(mutex_);
    list.reserve(bots_.size());
    for (auto& b : bots_)
    {
        if (!b->client) continue;
        BotStatusInfo info;
        info.id       = b->id;
        info.nick     = b->client->GetNick();
        info.status   = b->client->GetStateString();
        info.pingMs   = b->client->GetPing();
        info.isOnline = b->client->IsOnline();
        list.push_back(std::move(info));
    }
    return list;
}