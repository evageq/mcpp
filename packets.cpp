#include "packets.hpp"
#include "server.hpp"
#include "util.hpp"
#include <cassert>
#include <cstring>
#include <netinet/in.h>

extern cServer Server;

cInMessage::cInMessage(ClientId client_id, cPacketPtr pkt)
    : client_id_(client_id), pkt_(std::move(pkt))
{
}

int8_t
cParser::ReadByte(size_t n, const unsigned char *buf)
{
    assert(n > 0);
    assert(pos_ < n);
    return buf[pos_++];
}

int
cParser::ReadVarInt(size_t n, const unsigned char *buf)
{
    int value = 0;
    int position = 0;
    uint8_t currentByte;

    while (true)
    {
        currentByte = ReadByte(n, buf);
        value |= (currentByte & SEGMENT_BITS) << position;

        if ((currentByte & CONTINUE_BIT) == 0)
            break;

        position += 7;

        if (position >= 32)
        {
            throw "Var int is too big";
        }

        if (pos_ + 1 >= n)
        {
            throw "Var int cross boundaries";
        }
    }

    return value;
}

void
cParser::Reset()
{
    pos_ = 0;
}

void
cParser::SetPos(size_t pos)
{
    this->pos_ = pos;
}

size_t
cParser::GetPos() const
{
    return pos_;
}

cPacketProto
cParser::DispatchMsg(cInMessage *msg)
{
    auto pkt = msg->pkt_;
    int proto_id = ReadVarInt(pkt->length, pkt->data.get());

    std::optional<eConnState> state
        = Server.GetClientConnState(msg->client_id_);
    if (!state)
    {
        throw "Failed to get client state";
    }

    ePacketBound bound = ePacketBound::Serverbound;

    cPacketProto packetProto(proto_id, state.value(), bound);
}

ePacketMetaState
cPacketBuilder::GetState()
{
    return state_;
}

void
cPacketBuilder::InitData(size_t length)
{
    cPacketPtr packetPtr = std::make_shared<cPacket>(length);
    state_ = ePacketMetaState::Partial;
}

int
cPacketBuilder::PushData(size_t n, uint8_t *buf)
{
    if (state_ != ePacketMetaState::Partial)
    {
        return -1;
    }

    if (n + bytes_read_ > packetPtr_->length)
    {
        return -1;
    }

    size_t end = bytes_read_;
    std::memcpy(packetPtr_->data.get() + end, buf, n);
    bytes_read_ += n;

    if (bytes_read_ == packetPtr_->length)
    {
        state_ = ePacketMetaState::Full;
    }

    return 0;
}

bool
cPacketBuilder::IsValid()
{
    return state_ == ePacketMetaState::Full;
}

std::optional<cPacketPtr>
cPacketBuilder::IncBuildPacket(size_t n, uint8_t *buf)
{
    cParser parser;

    if (this->GetState() == ePacketMetaState::End)
    {
        this->Reset();
    }

    size_t buf_pos = 0;
    if (this->GetState() == ePacketMetaState::Init)
    {
        int length = parser.ReadVarInt(n, buf);
        if (!length)
        {
            return std::nullopt;
        }

        buf += buf_pos;
        n -= buf_pos;

        this->InitData(length);
    }

    this->PushData(n, buf);

    if (this->IsValid() == true)
    {
        state_ = ePacketMetaState::End;
        return packetPtr_;
    }

    return std::nullopt;
}

cPacket::cPacket(int length) : length(length)
{
    data = std::make_unique<uint8_t[]>(length);
}

void
cPacketBuilder::Reset()
{
    packetPtr_ = {};
    bytes_read_ = 0;
    state_ = ePacketMetaState::Init;
}

cPacketProto::cPacketProto(int protocol, eConnState state, ePacketBound bound)
{
    key = { protocol, state, bound };
}

cPacketProto::cPacketProto(int protocol, eConnState state, ePacketBound bound,
                           std::string resource)
    : cPacketProto(protocol, state, bound)
{
    resource_ = resource;
}

cPacketProto &
cPacketGen::CreatePacketProto(cProtocolPacketKey key)
{
}
