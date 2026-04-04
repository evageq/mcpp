#include "util.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <list>
#include <memory>
#include <netinet/in.h>
#include <optional>
#include <set>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

constexpr char SERVER_ADDR[] = "0.0.0.0";
constexpr static int SERVER_PORT = 9090;

struct Packet
{
    struct meta_s
    {
        int read;
        int packet_id_length;
    } meta;

    int length;
    int packet_id;
    unsigned char data[];

    bool
    IsValid() const
    {
        return (meta.read > 0)
               && (meta.read + meta.packet_id_length == length);
    }

    int
    GetDataLen() const
    {
        return meta.read;
    }
};

class cClient;
class cServer;
using cClientPtr = std::unique_ptr<cClient>;
using ClientId = uint64_t;

enum class eReadStatus
{
    Ok,
    WouldBlock,
    Disconnected,
    Error,
    BufferFull,
};

static void
SetNonBlocking(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    assert(flags != -1);
    const int ret = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    assert(ret != -1);
}

class cParser
{
private:
    constexpr static int SEGMENT_BITS = 0x7F;
    constexpr static int CONTINUE_BIT = 0x80;

public:
    static int8_t
    ReadByte(size_t n, unsigned char *buf, size_t &pos)
    {
        assert(n > 0);
        assert(pos < n);
        return buf[pos++];
    }

    static std::optional<int>
    readVarInt(size_t n, unsigned char *buf, size_t &pos)
    {
        int value = 0;
        int position = 0;
        uint8_t currentByte;
        size_t tmp_pos = pos;

        while (true)
        {
            currentByte = ReadByte(n, buf, tmp_pos);
            value |= (currentByte & SEGMENT_BITS) << position;

            if ((currentByte & CONTINUE_BIT) == 0)
                break;

            position += 7;

            if (position >= 32)
            {
                throw "Var int is too big";
            }

            if (pos + 1 >= n)
            {
                return std::nullopt;
            }
        }

        pos = tmp_pos;

        return value;
    }
};

class cClient
{
public:
    static constexpr int INPUT_BUF_SIZE = 4096;
    unsigned char buf[INPUT_BUF_SIZE];
    size_t n = 0;

private:
    sockaddr_in saddr;
    int socket;
    ClientId id;

    std::shared_ptr<struct Packet> packetPtr;

public:
    cClient(int socket, const sockaddr_in &addr, ClientId id)
        : saddr(addr), socket(socket), id(id)
    {
    }

    cClient(const cClient &) = delete;
    cClient &operator =(const cClient &) = delete;

    ~cClient() { close(socket); }

    int
    GetSocket() const
    {
        return socket;
    }

    ClientId
    GetId() const
    {
        return id;
    }

    eReadStatus
    ReadData()
    {
        while (true)
        {
            if (n == sizeof(buf))
            {
                return eReadStatus::BufferFull;
            }

            const ssize_t bytes_read
                = recv(socket, buf + n, sizeof(buf) - n, 0);
            if (bytes_read > 0)
            {
                n += static_cast<size_t>(bytes_read);
                continue;
            }

            if (bytes_read == 0)
            {
                return eReadStatus::Disconnected;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return eReadStatus::WouldBlock;
            }

            return eReadStatus::Error;
        }
    }

    std::shared_ptr<struct Packet>
    ExtractPacket()
    {
        size_t buf_pos = 0;

        if (packetPtr == nullptr)
        {

            int length = cParser::readVarInt(n, buf, buf_pos).value_or(-1);
            if (length <= 0)
            {
                return nullptr;
            }

            int tmp = buf_pos;

            int packet_id = cParser::readVarInt(n, buf, buf_pos).value_or(-1);
            if (packet_id < 0)
            {
                return nullptr;
            }

            int packet_id_length = buf_pos - tmp;

            packetPtr = std::shared_ptr<struct Packet>(
                static_cast<struct Packet *>(std::malloc(
                    sizeof(struct Packet) + length - packet_id_length)),
                free);

            packetPtr->length = length;
            packetPtr->packet_id = packet_id;
            packetPtr->meta.packet_id_length = packet_id_length;
            packetPtr->meta.read = 0;
        }

        assert(packetPtr->GetDataLen() + packetPtr->meta.packet_id_length + n
                   - buf_pos
               <= packetPtr->length);
        std::memcpy(packetPtr->data + packetPtr->meta.read, buf, n - buf_pos);
        packetPtr->meta.read += n - buf_pos;
        n = 0;

        if (packetPtr->IsValid() == true)
        {
            return std::move(packetPtr);
        }

        return nullptr;
    }
};

class cServer
{
public:
    static constexpr int MAX_PLAYERS = 100;

private:
    int players_cnt = 0;
    std::set<ClientId> free_players_id;
    bool is_running = false;
    sockaddr_in saddr;
    int socket;
    std::list<cClientPtr> cClientPtrs;
    int epfd;
    struct epoll_event ev, events[cServer::MAX_PLAYERS];

public:
    cServer(const char *addr, int port) : is_running(false)
    {
        int ret = 0;
        saddr.sin_family = AF_INET;
        saddr.sin_port = htons(port);
        ret = inet_pton(AF_INET, addr, &saddr.sin_addr);
        assert(ret == 1);
        socket = ::socket(AF_INET, SOCK_STREAM, 0);
        assert(socket != -1);
        SetNonBlocking(socket);

        for (int i = 0; i < MAX_PLAYERS; ++i)
        {
            free_players_id.insert(i);
        }
    }

    void
    Start()
    {
        int ret = 0;

        ret = bind(socket, (struct sockaddr *)&saddr, sizeof(saddr));
        assert(ret == 0);
        ret = listen(socket, MAX_PLAYERS);
        assert(ret == 0);

        epfd = epoll_create(cServer::MAX_PLAYERS);
        if (epfd < 0)
        {
            error("Failed to epoll_create");
            exit(EXIT_FAILURE);
        }

        ev.events = EPOLLIN;
        ev.data.fd = socket;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, socket, &ev) == -1)
        {
            error("Failed to add server to epfd");
            exit(EXIT_FAILURE);
        }
    }

    int
    GetSocket()
    {
        return socket;
    }

    int
    GetMaxPlayers()
    {
        return MAX_PLAYERS;
    }

    bool
    IsRunning()
    {
        return is_running;
    }

    cClient *
    ServerAcceptNewClient()
    {
        sockaddr_in client_addr{};
        socklen_t client_addr_len = sizeof(client_addr);
        int client_socket
            = accept4(this->socket, (struct sockaddr *)&client_addr,
                      &client_addr_len, SOCK_NONBLOCK);
        if (client_socket == -1)
        {
            debug("Failed to accept client");
            return nullptr;
        }

        if (free_players_id.empty())
        {
            debug("No free player slots");
            close(client_socket);
            return nullptr;
        }

        const ClientId client_id = *free_players_id.begin();
        free_players_id.erase(client_id);

        cClientPtr Client
            = std::make_unique<cClient>(client_socket, client_addr, client_id);
        debug("Accepted new Client %d", Client->GetId());
        cClient *ClientRaw = Client.get();
        cClientPtrs.push_back(std::move(Client));

        return ClientRaw;
    }

    auto
    FindClient(int fd)
    {
        auto client_it
            = std::find_if(cClientPtrs.begin(), cClientPtrs.end(),
                           [fd](const cClientPtr &CurrentClient)
                           { return CurrentClient.get()->GetSocket() == fd; });
        return client_it;
    }

    bool
    DeleteClient(const cClient *client)
    {
        if (client == nullptr)
        {
            return false;
        }

        auto client_it = this->FindClient(client->GetSocket());

        if (client_it == cClientPtrs.end())
        {
            return false;
        }
        debug("Delete client %d", (*client_it)->GetId());

        epoll_ctl(epfd, EPOLL_CTL_DEL, (*client_it)->GetSocket(), nullptr);
        free_players_id.insert((*client_it)->GetId());
        cClientPtrs.erase(client_it);
        return true;
    }

    bool
    DeleteClient(int fd)
    {
        auto client_it
            = std::find_if(cClientPtrs.begin(), cClientPtrs.end(),
                           [fd](const cClientPtr &CurrentClient)
                           { return CurrentClient.get()->GetSocket() == fd; });
        if (client_it == cClientPtrs.end())
        {
            return false;
        }
        return this->DeleteClient(client_it->get());
    }

    void
    Run()
    {
        is_running = true;
        while (is_running)
        {
            int nfds = epoll_wait(epfd, events, cServer::MAX_PLAYERS, -1);
            for (int i = 0; i < nfds; ++i)
            {
                if (events[i].data.fd == socket)
                {
                    cClient *Client = this->ServerAcceptNewClient();
                    if (Client == nullptr)
                    {
                        continue;
                    }

                    ev.events = EPOLLIN | EPOLLRDHUP;
                    ev.data.fd = Client->GetSocket();
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, Client->GetSocket(),
                                  &ev)
                        == -1)
                    {
                        error("Failed to add client to epfd");
                        this->DeleteClient(Client);
                    }
                }
                else
                {
                    auto client_it = this->FindClient(events[i].data.fd);
                    if (client_it == cClientPtrs.end())
                    {
                        continue;
                    }
                    cClient *Client = client_it->get();
                    if (events[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR))
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
                            print_hex_packet(packet->GetDataLen(),
                                             packet->data,
                                             packet->GetDataLen());
                        }

                        // while (true)
                        // {
                        //     auto packet = Client->TryExtractPacket();
                        //     if (!packet.has_value())
                        //     {
                        //         break;
                        //     }
                        //
                        //     debug("Client %d sent packet payload of %zu
                        //     bytes",
                        //           static_cast<int>(Client->GetId()),
                        //           packet->size());
                        // }
                    }
                }
            }
        }
    }
};

cServer Server(SERVER_ADDR, SERVER_PORT);

int
main()
{

    Server.Start();
    Server.Run();

    return 0;
}
