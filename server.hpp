#ifndef __SERVER_H__
#define __SERVER_H__

#include "client.hpp"
#include "packets.hpp"
#include "types.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <condition_variable>
#include <list>
#include <optional>
#include <queue>
#include <set>
#include <sys/epoll.h>
#include <thread>

constexpr char SERVER_ADDR[] = "0.0.0.0";
constexpr static int SERVER_PORT = 9091;

class cServer
{
public:
    static constexpr int MAX_PLAYERS = 100;

private:
    using cClientIterator = std::list<cClientPtr>::iterator;
    using cConstClientIterator = std::list<cClientPtr>::const_iterator;

    int players_cnt_ = 0;
    std::set<ClientId> free_players_id_;
    std::atomic<bool> is_running_ = false;
    std::string addr_str_;
    sockaddr_in saddr_;
    int socket_;
    std::list<cClientPtr> cClientPtrs_;
    int epfd_;
    struct epoll_event ev_, events_[cServer::MAX_PLAYERS];

    std::queue<cInMessagePtr> inMessageQueue_;
    std::mutex inMessageQueue_mutex_;
    std::condition_variable inMessageQueue_cv_;
    std::thread networkThread_;
    std::thread gameThread_;

    std::optional<cClientIterator> FindClient(int fd);

    std::optional<cClientIterator> FindClient(ClientId client_id);

    std::optional<cConstClientIterator> FindClient(ClientId client_id) const;

    bool DeleteClient(std::optional<cClientIterator> client_it);

public:
    cServer(const char *addr, int port);

    ~cServer();

    void Start();

    int GetSocket();

    int GetMaxPlayers();

    bool IsRunning();

    cClient *ServerAcceptNewClient();

    bool DeleteClient(const cClient *client);

    bool DeleteClient(ClientId client_id);

    bool DeleteClient(int fd);

    std::optional<eConnState> GetClientConnState(ClientId client_id) const;

    void GameLoop();

    void NetworkLoop();

    void Run();

    void Shut();
};

extern cServer Server;

void HandleStopSignal(int);

#endif // __SERVER_H__
