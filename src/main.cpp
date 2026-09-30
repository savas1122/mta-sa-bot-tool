#include "BotManager.hpp"
#include "ClientConfig.hpp"

#include <windows.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <iomanip>

using namespace std::chrono_literals;

static const char* R  = "\033[0m";
static const char* G  = "\033[92m";
static const char* RD = "\033[91m";
static const char* Y  = "\033[93m";
static const char* C  = "\033[96m";
static const char* GR = "\033[90m";

static void cls()
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD c  = {0, 0};
    DWORD n;
    CONSOLE_SCREEN_BUFFER_INFO i;
    GetConsoleScreenBufferInfo(h, &i);
    FillConsoleOutputCharacterA(h, ' ', i.dwSize.X * i.dwSize.Y, c, &n);
    FillConsoleOutputAttribute (h, i.wAttributes, i.dwSize.X * i.dwSize.Y, c, &n);
    SetConsoleCursorPosition(h, c);
}

static std::string ask(const char* label, const char* def)
{
    std::cout << C << "  " << label << GR << "[" << def << "] " << R;
    std::string s;
    std::getline(std::cin, s);
    return s.empty() ? def : s;
}

static void show(BotManager& mgr)
{
    cls();
    std::cout << C << "\n  bot panel\n\n" << R;

    auto list   = mgr.GetBotStatuses();
    size_t online = mgr.GetOnlineCount();

    std::cout << GR << "  " << std::string(46, '-') << R << "\n";
    std::cout << "  " << std::left
              << std::setw(5)  << "id"
              << std::setw(16) << "nick"
              << std::setw(14) << "durum"
              << "ping\n";
    std::cout << GR << "  " << std::string(46, '-') << R << "\n";

    for (auto& s : list)
    {
        const char* col = s.isOnline ? G : RD;
        std::string st  = s.isOnline ? "girdi" : s.status;
        if (st.size() > 12) st = st.substr(0, 12);
        std::string ping = s.isOnline ? std::to_string(s.pingMs) + "ms" : "-";

        std::cout << col << "  "
                  << std::setw(5)  << s.id
                  << std::setw(16) << (s.nick.size() > 14 ? s.nick.substr(0, 14) : s.nick)
                  << std::setw(14) << st
                  << ping << R << "\n";
    }

    std::cout << GR << "  " << std::string(46, '-') << R << "\n";
    std::cout << "\n  " << G << online << R << " / " << list.size() << " girdi\n\n";
    std::cout << Y << "  [n] nick degistir   [0] cikis\n" << R << "\n  > ";
}

int main()
{
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(hOut, &mode);
    SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    CONSOLE_CURSOR_INFO ci{1, FALSE};
    SetConsoleCursorInfo(hOut, &ci);
    SetConsoleTitleW(L"bot panel");

    cls();
    std::cout << C << "\n  bot panel\n\n" << R;

    ClientConfig cfg;
    cfg.noPureSync    = false;
    cfg.transportOnly = false;
    cfg.randomNick    = false;

    cfg.host = ask("ip       : ", "127.0.0.1");
    std::string ps = ask("port     : ", "22003");
    try { cfg.port = (uint16_t)std::stoul(ps); } catch(...) { cfg.port = 22003; }
    std::string cs = ask("bot sayi : ", "5");
    int count = 5;
    try { count = std::stoi(cs); } catch(...) {}
    cfg.nick = ask("nick pre : ", "bot");

    BotManager mgr;
    mgr.Start(cfg, (uint16_t)count);

    bool running = true;
    while (running)
    {
        show(mgr);
        std::string in;
        std::getline(std::cin, in);
        if (in.empty()) continue;

        char ch = (char)tolower((unsigned char)in[0]);

        if (ch == '0')
        {
            running = false;
        }
        else if (ch == 'n')
        {
            std::cout << "\n  bot id   : ";
            std::string sid; std::getline(std::cin, sid);
            std::cout << "  yeni nick: ";
            std::string nn; std::getline(std::cin, nn);
            if (!sid.empty() && !nn.empty())
            {
                try {
                    uint16_t id = (uint16_t)std::stoul(sid);
                    if (mgr.RenameBot(id, nn))
                        std::cout << G << "  degistirildi.\n" << R;
                } catch(...) {}
            }
            std::this_thread::sleep_for(800ms);
        }
    }

    mgr.StopAll();
    cls();
    std::cout << GR << "  kapatildi.\n" << R;
    std::this_thread::sleep_for(500ms);
    return 0;
}