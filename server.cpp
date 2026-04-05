#include "server.hpp"
#include "client.hpp"
#include "util.hpp"
#include <algorithm>
#include <cassert>
#include <unistd.h>

std::atomic<bool> g_ShouldStop{ false };

void
HandleStopSignal(int)
{
    g_ShouldStop.store(true);
}

cServer::cServer(const char *addr, int port) : is_running_(false)
{
    addr_str_ = std::string(addr);
    int ret = 0;
    saddr_.sin_family = AF_INET;
    saddr_.sin_port = htons(port);
    ret = inet_pton(AF_INET, addr, &saddr_.sin_addr);
    assert(ret == 1);
    socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(socket_ != -1);
    SetNonBlocking(socket_);

    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        free_players_id_.insert(ClientId(i));
    }
}

cServer::~cServer() { cServer::Shut(); }

void
cServer::Start()
{
    int ret = 0;

    ret = bind(socket_, (struct sockaddr *)&saddr_, sizeof(saddr_));
    assert(ret == 0);
    ret = listen(socket_, MAX_PLAYERS);
    assert(ret == 0);

    epfd_ = epoll_create(cServer::MAX_PLAYERS);
    if (epfd_ < 0)
    {
        error("Failed to epoll_create");
        exit(EXIT_FAILURE);
    }

    ev_.events = EPOLLIN;
    ev_.data.fd = socket_;
    if (epoll_ctl(epfd_, EPOLL_CTL_ADD, socket_, &ev_) == -1)
    {
        error("Failed to add server to epfd");
        exit(EXIT_FAILURE);
    }
}

int
cServer::GetSocket()
{
    return socket_;
}

int
cServer::GetMaxPlayers()
{
    return MAX_PLAYERS;
}

bool
cServer::IsRunning()
{
    return is_running_;
}

cClient *
cServer::ServerAcceptNewClient()
{
    sockaddr_in client_addr{};
    socklen_t client_addr_len = sizeof(client_addr);
    int client_socket = accept4(this->socket_, (struct sockaddr *)&client_addr,
                                &client_addr_len, SOCK_NONBLOCK);
    if (client_socket == -1)
    {
        debug("Failed to accept client");
        return nullptr;
    }

    if (free_players_id_.empty())
    {
        debug("No free player slots");
        close(client_socket);
        return nullptr;
    }

    const ClientId client_id = *free_players_id_.begin();
    free_players_id_.erase(client_id);

    cClientPtr Client
        = std::make_unique<cClient>(client_socket, client_addr, client_id);
    debug("Accepted new Client %d", Client->GetId());
    cClient *ClientRaw = Client.get();
    cClientPtrs_.push_back(std::move(Client));

    return ClientRaw;
}

std::optional<cServer::cClientIterator>
cServer::FindClient(int fd)
{
    auto client_it
        = std::find_if(cClientPtrs_.begin(), cClientPtrs_.end(),
                       [fd](const cClientPtr &CurrentClient)
                       { return CurrentClient.get()->GetSocket() == fd; });
    if (client_it == cClientPtrs_.end())
    {
        return std::nullopt;
    }

    return client_it;
}

std::optional<cServer::cClientIterator>
cServer::FindClient(ClientId client_id)
{
    auto client_it
        = std::find_if(cClientPtrs_.begin(), cClientPtrs_.end(),
                       [client_id](const cClientPtr &currentClient)
                       { return client_id == currentClient->GetId(); });
    if (client_it == cClientPtrs_.end())
    {
        return std::nullopt;
    }

    return client_it;
}

bool
cServer::DeleteClient(std::optional<cClientIterator> client_it)
{
    if (!client_it.has_value())
    {
        return false;
    }

    debug("Delete client %llu", static_cast<unsigned long long>(
                                    (*client_it.value())->GetId().GetId()));
    epoll_ctl(epfd_, EPOLL_CTL_DEL, (*client_it.value())->GetSocket(),
              nullptr);
    free_players_id_.insert((*client_it.value())->GetId());
    cClientPtrs_.erase(client_it.value());
    return true;
}

bool
cServer::DeleteClient(const cClient *client)
{
    if (client == nullptr)
    {
        return false;
    }

    return DeleteClient(FindClient(client->GetId()));
}

bool
cServer::DeleteClient(ClientId client_id)
{
    return DeleteClient(FindClient(client_id));
}

bool
cServer::DeleteClient(int fd)
{
    return DeleteClient(FindClient(fd));
}

void
cServer::GameLoop()
{
    while (true)
    {
        std::unique_lock<std::mutex> lk(inMessageQueue_mutex_);
        inMessageQueue_cv_.wait(
            lk, [this]
            { return !inMessageQueue_.empty() || !is_running_.load(); });
        if (!is_running_.load() && inMessageQueue_.empty())
        {
            break;
        }

        auto msg = std::move(inMessageQueue_.front());
        inMessageQueue_.pop();
        lk.unlock();

        cParser parser;
        auto packetProto = parser.DispatchMsg(msg.get());
    }
}

void
cServer::NetworkLoop()
{
    while (is_running_.load())
    {
        int nfds = epoll_wait(epfd_, events_, cServer::MAX_PLAYERS, 100);
        for (int i = 0; i < nfds; ++i)
        {
            if (events_[i].data.fd == socket_)
            {
                cClient *Client = this->ServerAcceptNewClient();
                if (Client == nullptr)
                {
                    continue;
                }

                ev_.events = EPOLLIN | EPOLLRDHUP;
                ev_.data.fd = Client->GetSocket();
                if (epoll_ctl(epfd_, EPOLL_CTL_ADD, Client->GetSocket(), &ev_)
                    == -1)
                {
                    error("Failed to add client to epfd");
                    this->DeleteClient(Client);
                }
            }
            else
            {
                auto client_it = this->FindClient(events_[i].data.fd);
                if (!client_it.has_value())
                {
                    continue;
                }
                cClient *Client = client_it.value()->get();
                if (events_[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR))
                {
                    this->DeleteClient(Client);
                }
                else
                {
                    const eReadStatus read_status = Client->ReadData();
                    if (read_status == eReadStatus::Disconnected
                        || read_status == eReadStatus::Error
                        || read_status == eReadStatus::BufferFull)
                    {
                        this->DeleteClient(Client);
                        continue;
                    }
                    auto packet = Client->ExtractPacket();
                    if (packet)
                    {
                        {
                            std::lock_guard<std::mutex> guard(
                                inMessageQueue_mutex_);

                            inMessageQueue_.push(std::make_shared<cInMessage>(
                                Client->GetId(), packet));
                        }

                        inMessageQueue_cv_.notify_one();

                        print_hex_packet(packet->length, packet->data.get(),
                                         packet->length);
                    }
                }
            }
        }
    }
}

void
cServer::Run()
{
    debug("Run server at %s %d", addr_str_.c_str(), ntohs(saddr_.sin_port));
    if (is_running_.exchange(true))
    {
        return;
    }

    networkThread_ = std::thread([this] { this->NetworkLoop(); });
    gameThread_ = std::thread([this] { this->GameLoop(); });

    while (!g_ShouldStop.load() && is_running_.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    Shut();
}

void
cServer::Shut()
{
    is_running_.store(false);
    inMessageQueue_cv_.notify_all();

    if (networkThread_.joinable())
    {
        networkThread_.join();
    }
    if (gameThread_.joinable())
    {
        gameThread_.join();
    }

    {
        std::lock_guard<std::mutex> guard(inMessageQueue_mutex_);
        std::queue<cInMessagePtr> empty_queue;
        inMessageQueue_.swap(empty_queue);
    }

    cClientPtrs_.clear();
    if (epfd_ != -1)
    {
        close(epfd_);
        epfd_ = -1;
    }
    if (socket_ != -1)
    {
        close(socket_);
        socket_ = -1;
    }
}

std::optional<cServer::cConstClientIterator>
cServer::FindClient(ClientId client_id) const
{
    auto client_it
        = std::find_if(cClientPtrs_.cbegin(), cClientPtrs_.cend(),
                       [client_id](const cClientPtr &currentClient)
                       { return client_id == currentClient->GetId(); });
    if (client_it == cClientPtrs_.cend())
    {
        return std::nullopt;
    }

    return client_it;
}

std::optional<eConnState>
cServer::GetClientConnState(ClientId client_id) const
{
    const auto client_it = this->FindClient(client_id);
    if (!client_it.has_value())
    {
        return std::nullopt;
    }

    return client_it.value()->get()->GetConnState();
}
