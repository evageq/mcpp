#ifndef __CLIENT_H__
#define __CLIENT_H__

#include "packets.hpp"
#include "types.hpp"
#include <arpa/inet.h>
#include <memory>

class cClient;
using cClientPtr = std::shared_ptr<cClient>;

class cClient
{
public:
    static constexpr int INPUT_BUF_SIZE = 4096;
    unsigned char buf[INPUT_BUF_SIZE];
    size_t n_ = 0;

private:
    sockaddr_in saddr_;
    int socket_;
    ClientId id_;

    cPacketBuilder packetBuilder_;
    eConnState conn_state_ = eConnState::Handshaking;

public:
    cClient(int socket, const sockaddr_in &addr, ClientId id);

    cClient(const cClient &) = delete;
    cClient &operator =(const cClient &) = delete;

    ~cClient();

    int GetSocket() const;

    ClientId GetId() const;

    eReadStatus ReadData();

    cPacketPtr ExtractPacket();

    eConnState GetConnState() const;

    void SetConnState(eConnState conn_state);
};

#endif // __CLIENT_H__
