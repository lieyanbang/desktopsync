#define BUILD_SYNC_DLL
#include <winsock2.h>
#include <ws2tcpip.h>
#include "shared.hpp"
#include "common.hpp"
#include "sync_api.hpp"
#include <udt.h>
#include "md5.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>

#pragma comment(lib, "Ws2_32.lib")

namespace
{
    constexpr int SYNC_PORT = 60123;
    constexpr uint16_t OPCODE_HELLO = 1;
    constexpr uint16_t OPCODE_FILE = 2;
    constexpr uint16_t OPCODE_END = 3;

    struct PacketHeader
    {
        char magic[4]{ 'D','S','Y','C' };
        uint16_t version{ 1 };
        uint16_t opcode{ 0 };
        uint16_t type{ 0 }; // ChangeType
        uint32_t pathLen{ 0 };
        uint64_t fileSize{ 0 };
        unsigned char md5[16]{};
    };

    std::atomic<bool> g_serverRunning{ false };
    std::thread g_serverThread;
    TrayCallback g_trayCb = nullptr;
    void* g_trayCtx = nullptr;
    IniState g_cfg{};
    std::mutex g_cfgMutex;

    void Report(TrayState st, uint64_t cur, uint64_t total, const std::wstring& err = L"")
    {
        if (g_trayCb)
        {
            g_trayCb(st, cur, total, err.c_str(), g_trayCtx);
        }
    }

    bool RecvAll(UDTSOCKET s, char* buf, size_t len)
    {
        size_t got = 0;
        while (got < len)
        {
            int n = UDT::recv(s, buf + got, static_cast<int>(len - got), 0);
            if (n == UDT::ERROR || n == 0) return false;
            got += static_cast<size_t>(n);
        }
        return true;
    }

    bool SendAll(UDTSOCKET s, const char* buf, size_t len)
    {
        size_t sent = 0;
        while (sent < len)
        {
            int n = UDT::send(s, buf + sent, static_cast<int>(len - sent), 0);
            if (n == UDT::ERROR || n == 0) return false;
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    bool ComputeMd5(const std::wstring& path, unsigned char out[16])
    {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) return false;
        md5_state_t st{};
        md5_init(&st);
        char buf[4096];
        while (ifs.good())
        {
            ifs.read(buf, sizeof(buf));
            std::streamsize n = ifs.gcount();
            if (n > 0)
            {
                md5_append(&st, reinterpret_cast<const md5_byte_t*>(buf), static_cast<int>(n));
            }
        }
        md5_finish(&st, out);
        return true;
    }

    std::vector<std::filesystem::path> CollectFiles(const std::wstring& root, uint64_t lastSync)
    {
        std::vector<std::filesystem::path> files;
        if (root.empty()) return files;
        std::filesystem::path p(root);
        for (auto& entry : std::filesystem::directory_iterator(p))
        {
            if (!entry.is_regular_file()) continue;
            auto ftime = entry.last_write_time();
            auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(ftime).time_since_epoch().count();
            if (lastSync == 0 || static_cast<uint64_t>(sctp) > lastSync)
            {
                files.push_back(entry.path());
            }
        }
        return files;
    }

    bool SendHello(UDTSOCKET sock, uint64_t ts)
    {
        PacketHeader hdr{};
        hdr.opcode = OPCODE_HELLO;
        hdr.fileSize = ts;
        return SendAll(sock, reinterpret_cast<char*>(&hdr), sizeof(hdr));
    }

    bool SendFilePacket(UDTSOCKET sock, const std::filesystem::path& path, ChangeType type, uint64_t& sentBytes, uint64_t totalBytes)
    {
        std::wstring wpath = path.filename().wstring();
        std::string utf8 = WideToUtf8(wpath);
        if (utf8.size() > 1024 * 64) return false;

        PacketHeader hdr{};
        hdr.opcode = OPCODE_FILE;
        hdr.type = static_cast<uint16_t>(type);
        hdr.pathLen = static_cast<uint32_t>(utf8.size());
        hdr.fileSize = std::filesystem::file_size(path);
        if (!ComputeMd5(wpath.empty() ? L"" : path.wstring(), hdr.md5))
        {
            memset(hdr.md5, 0, sizeof(hdr.md5));
        }

        if (!SendAll(sock, reinterpret_cast<char*>(&hdr), sizeof(hdr))) return false;
        if (!SendAll(sock, utf8.data(), utf8.size())) return false;

        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) return false;
        char buf[4096];
        while (ifs.good())
        {
            ifs.read(buf, sizeof(buf));
            std::streamsize n = ifs.gcount();
            if (n > 0)
            {
                if (!SendAll(sock, buf, static_cast<size_t>(n))) return false;
                sentBytes += static_cast<uint64_t>(n);
                Report(TrayState::Uploading, sentBytes, totalBytes);
            }
        }
        return true;
    }

    bool SendEnd(UDTSOCKET sock)
    {
        PacketHeader hdr{};
        hdr.opcode = OPCODE_END;
        return SendAll(sock, reinterpret_cast<char*>(&hdr), sizeof(hdr));
    }

    bool HandleIncoming(UDTSOCKET sock)
    {
        PacketHeader hdr{};
        while (true)
        {
            if (!RecvAll(sock, reinterpret_cast<char*>(&hdr), sizeof(hdr))) return false;
            if (memcmp(hdr.magic, "DSYC", 4) != 0) return false;
            if (hdr.opcode == OPCODE_END) return true;
            if (hdr.opcode == OPCODE_HELLO)
            {
                continue;
            }
            if (hdr.opcode == OPCODE_FILE)
            {
                std::string utf8(hdr.pathLen, '\0');
                if (!RecvAll(sock, utf8.data(), utf8.size())) return false;
                std::wstring fileName = Utf8ToWide(utf8);
                std::vector<char> data(static_cast<size_t>(hdr.fileSize));
                size_t offset = 0;
                while (offset < data.size())
                {
                    int chunk = UDT::recv(sock, data.data() + offset, static_cast<int>(data.size() - offset), 0);
                    if (chunk <= 0) return false;
                    offset += static_cast<size_t>(chunk);
                }

                std::filesystem::path targetDir;
                {
                    std::lock_guard<std::mutex> lock(g_cfgMutex);
                    targetDir = g_cfg.targetDir.empty() ? g_cfg.desktopPath : g_cfg.targetDir;
                }
                std::filesystem::path target = targetDir / fileName;
                std::ofstream ofs(target, std::ios::binary);
                ofs.write(data.data(), data.size());
                ofs.close();
            }
        }
    }

    void ServerRoutine()
    {
        UDT::startup();
        addrinfo hints{}; hints.ai_flags = AI_PASSIVE; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        getaddrinfo(nullptr, "60123", &hints, &res);
        UDTSOCKET serv = UDT::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        UDT::bind(serv, res->ai_addr, res->ai_addrlen);
        UDT::listen(serv, 4);
        freeaddrinfo(res);

        while (g_serverRunning.load())
        {
            sockaddr_storage clientaddr{}; int addrlen = sizeof(clientaddr);
            UDTSOCKET cli = UDT::accept(serv, (sockaddr*)&clientaddr, &addrlen);
            if (cli == UDT::INVALID_SOCK)
            {
                Sleep(100);
                continue;
            }
            Report(TrayState::Downloading, 0, 0);
            HandleIncoming(cli);
            UDT::close(cli);
            Report(TrayState::Completed, 0, 0);
        }

        UDT::close(serv);
        UDT::cleanup();
    }
}

extern "C" __declspec(dllexport) bool StartSyncService(const IniState* cfg, TrayCallback cb, void* ctx)
{
    if (!cfg) return false;
    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);
        g_cfg = *cfg;
    }
    g_trayCb = cb;
    g_trayCtx = ctx;
    if (g_serverRunning.load()) return true;
    g_serverRunning.store(true);
    g_serverThread = std::thread(ServerRoutine);
    return true;
}

extern "C" __declspec(dllexport) bool SyncUploadOnce(const IniState* cfg, const wchar_t* peerIp, TrayCallback cb, void* ctx)
{
    if (!cfg || !peerIp) return false;
    g_trayCb = cb;
    g_trayCtx = ctx;

    std::string ip = WideToUtf8(peerIp);
    addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    addrinfo* peer = nullptr;
    if (getaddrinfo(ip.c_str(), "60123", &hints, &peer) != 0) return false;

    UDT::startup();
    UDTSOCKET sock = UDT::socket(peer->ai_family, peer->ai_socktype, peer->ai_protocol);
    if (UDT::connect(sock, peer->ai_addr, peer->ai_addrlen) == UDT::ERROR)
    {
        freeaddrinfo(peer);
        UDT::close(sock);
        UDT::cleanup();
        Report(TrayState::Error, 0, 0, L"connect failed");
        return false;
    }

    IniState cfgCopy = *cfg;
    auto files = CollectFiles(cfgCopy.desktopPath, cfgCopy.lastSync);
    uint64_t totalBytes = 0;
    for (auto& f : files) totalBytes += std::filesystem::file_size(f);
    Report(TrayState::Uploading, 0, totalBytes);

    uint64_t sent = 0;
    SendHello(sock, NowSec());
    for (auto& f : files)
    {
        if (!SendFilePacket(sock, f, ChangeType::Update, sent, totalBytes))
        {
            Report(TrayState::Error, sent, totalBytes, L"send failed");
            break;
        }
    }
    SendEnd(sock);

    UDT::close(sock);
    UDT::cleanup();

    Report(TrayState::Completed, totalBytes, totalBytes);
    return true;
}
