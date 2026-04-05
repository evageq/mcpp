#include "client.hpp"
#include "packets.hpp"
#include <cassert>
#include <cstring>
#include <unistd.h>

cClient::cClient(int socket, const sockaddr_in &addr, ClientId id)
    : saddr_(addr), socket_(socket), id_(id)
{
}

cClient::~cClient() { close(socket_); }

int
cClient::GetSocket() const
{
    return socket_;
}

ClientId
cClient::GetId() const
{
    return id_;
}

eReadStatus
cClient::ReadData()
{
    while (true)
    {
        if (n_ == sizeof(buf))
        {
            return eReadStatus::BufferFull;
        }

        const ssize_t bytes_read = recv(socket_, buf + n_, sizeof(buf) - n_, 0);
        if (bytes_read > 0)
        {
            n_ += static_cast<size_t>(bytes_read);
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

cPacketPtr
cClient::ExtractPacket()
{
    auto packet = packetBuilder_.IncBuildPacket(n_, buf);
    if (packet)
    {
        return packet.value();
    }

    return nullptr;
}

eConnState
cClient::GetConnState() const
{
    return conn_state_;
}
