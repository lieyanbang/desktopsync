#define BUILD_P2P_DLL
#define _WINSOCKAPI_
#include "shared.hpp"
#include "common.hpp"
#include "p2p_api.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <random>
#include <string>
#include <fstream>

#pragma comment(lib, "Ws2_32.lib")

namespace
{
    constexpr unsigned short P2P_PORT = 54545;
    constexpr const char* MULTICAST_ADDR = "239.255.255.250";
    constexpr DWORD HELLO_INTERVAL_MS = 1000;
    constexpr DWORD LOOP_SLEEP_MS = 200;
    constexpr DWORD TIMEOUT_MS = 60 * 1000; // 1 minute per run

    uint64_t gen_id()
    {
        std::mt19937_64 rng{ std::random_device{}() };
        std::uniform_int_distribution<uint64_t> dist;
        return dist(rng);
    }

    std::string ipv4_to_string(const sockaddr_in& addr)
    {
        char buf[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf));
        return std::string(buf);
    }

    void send_hello(SOCKET s, const sockaddr_in& bcast, const sockaddr_in& mcast, uint64_t selfId)
    {
        char msg[64];
        int len = std::snprintf(msg, sizeof(msg), "HELLO %016llX", static_cast<unsigned long long>(selfId));
        if (len > 0)
        {
            sendto(s, msg, len, 0, reinterpret_cast<const sockaddr*>(&bcast), sizeof(bcast));
            sendto(s, msg, len, 0, reinterpret_cast<const sockaddr*>(&mcast), sizeof(mcast));
        }
    }

    void send_ack(SOCKET s, const sockaddr_in& peerAddr, uint64_t selfId, uint64_t peerId)
    {
        char msg[80];
        int len = std::snprintf(msg, sizeof(msg), "ACK %016llX %016llX",
            static_cast<unsigned long long>(selfId),
            static_cast<unsigned long long>(peerId));
        if (len > 0)
        {
            sockaddr_in dest = peerAddr;
            dest.sin_port = htons(P2P_PORT);
            sendto(s, msg, len, 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
        }
    }
}

extern "C" __declspec(dllexport) bool RunP2PHandshake(wchar_t* outIp, int outLen, uint64_t* outPeerId, uint64_t* outSelfId)
{
    if (!outIp || outLen <= 0) return false;

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
    {
        WSACleanup();
        return false;
    }

    BOOL opt = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(P2P_PORT);
    if (bind(s, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR)
    {
        closesocket(s);
        WSACleanup();
        return false;
    }

    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_ADDR);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&mreq), sizeof(mreq));

    WSAEVENT ev = WSACreateEvent();
    if (ev == WSA_INVALID_EVENT)
    {
        closesocket(s);
        WSACleanup();
        return false;
    }
    WSAEventSelect(s, ev, FD_READ);

    sockaddr_in bcast{};
    bcast.sin_family = AF_INET;
    bcast.sin_port = htons(P2P_PORT);
    bcast.sin_addr.s_addr = INADDR_BROADCAST;

    sockaddr_in mcast{};
    mcast.sin_family = AF_INET;
    mcast.sin_port = htons(P2P_PORT);
    mcast.sin_addr.s_addr = inet_addr(MULTICAST_ADDR);

    uint64_t selfId = gen_id();
    uint64_t peerId = 0;
    std::string peerIp;
    bool sawHello = false, sawAck = false, havePeer = false;

    ULONGLONG start = GetTickCount64();
    ULONGLONG lastHello = 0;

    for (;;)
    {
        ULONGLONG now = GetTickCount64();
        if (now - lastHello >= HELLO_INTERVAL_MS)
        {
            send_hello(s, bcast, mcast, selfId);
            lastHello = now;
        }
        if ((now - start) > TIMEOUT_MS) break;
        if (sawHello && sawAck && havePeer && !peerIp.empty()) break;

        DWORD w = WSAWaitForMultipleEvents(1, &ev, FALSE, LOOP_SLEEP_MS, FALSE);
        if (w == WSA_WAIT_EVENT_0)
        {
            WSANETWORKEVENTS ne{};
            if (WSAEnumNetworkEvents(s, ev, &ne) == 0)
            {
                if (ne.lNetworkEvents & FD_READ)
                {
                    for (;;)
                    {
                        char buf[256];
                        sockaddr_in from{}; int fromLen = sizeof(from);
                        int n = recvfrom(s, buf, sizeof(buf) - 1, 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
                        if (n == SOCKET_ERROR)
                        {
                            int err = WSAGetLastError();
                            if (err == WSAEWOULDBLOCK) break;
                            break;
                        }
                        if (n <= 0) break;
                        buf[n] = '\0';
                        std::string msg(buf, n);
                        if (msg.rfind("HELLO ", 0) == 0)
                        {
                            unsigned long long id = 0;
                            if (std::sscanf(msg.c_str(), "HELLO %llx", &id) == 1)
                            {
                                if (id != selfId)
                                {
                                    if (!havePeer)
                                    {
                                        peerId = static_cast<uint64_t>(id);
                                        peerIp = ipv4_to_string(from);
                                        havePeer = true;
                                    }
                                    if (peerId == static_cast<uint64_t>(id))
                                    {
                                        sawHello = true;
                                        send_ack(s, from, selfId, peerId);
                                    }
                                }
                            }
                        }
                        else if (msg.rfind("ACK ", 0) == 0)
                        {
                            unsigned long long fromId = 0, toId = 0;
                            if (std::sscanf(msg.c_str(), "ACK %llx %llx", &fromId, &toId) == 2)
                            {
                                if (toId == selfId)
                                {
                                    if (!havePeer)
                                    {
                                        peerId = static_cast<uint64_t>(fromId);
                                        peerIp = ipv4_to_string(from);
                                        havePeer = true;
                                    }
                                    if (peerId == static_cast<uint64_t>(fromId))
                                    {
                                        sawAck = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    bool success = sawHello && sawAck && havePeer && !peerIp.empty();
    if (success)
    {
        std::wstring wip = Utf8ToWide(peerIp);
        wcsncpy_s(outIp, outLen, wip.c_str(), _TRUNCATE);
        if (outPeerId) *outPeerId = peerId;
        if (outSelfId) *outSelfId = selfId;
        std::ofstream ofs("p2pip.txt", std::ios::trunc);
        if (ofs) ofs << peerIp << "\n";
    }

    WSACloseEvent(ev);
    closesocket(s);
    WSACleanup();
    return success;
}
