#include "packets.hpp"
#include "server.hpp"
#include "util.hpp"
#include <cassert>
#include <cstring>
#include <netinet/in.h>

extern cServer g_server;

cParser::cParser(size_t n, uint8_t *buf) : pos_(0), n(n), buf(buf) {}

cPacketHandlePtr
MakeHandshakingPacket(cInMessagePtr msg)
{
    INIT_PARSER(parser);

    int proto_version = parser.ReadVarInt();
    std::string server_addr = parser.ReadString();
    uint16_t server_port = parser.ReadUnsignedShort();
    eIntent intention = static_cast<eIntent>(parser.ReadVarInt());

    return std::make_shared<cPacketHandshake>(
        msg->client_id, proto_version, server_addr, server_port, intention);
}

cPacketHandlePtr
MakeLoginStartPacket(cInMessagePtr msg)
{
    INIT_PARSER(parser);

    debug("start read string");
    std::string name = parser.ReadString();
    debug("Read strign %s", name.c_str());
    UUID uuid = parser.ReadUUID();

    return std::make_shared<cPacketLoginStart>(msg->client_id, name, uuid);
}

std::unordered_map<cPacketKey, PacketHandleFactoryFn, cPacketKeyHash>
    packet_factory = {
        { { 0x00, eConnState::Handshaking, ePacketBound::Serverbound },
         { &MakeHandshakingPacket } },
        { { 0x00, eConnState::Login, ePacketBound::Serverbound },
         { &MakeLoginStartPacket }  }
};

cInMessage::cInMessage(ClientId client_id, cPacketPtr pkt)
    : client_id(client_id), pkt(std::move(pkt))
{
}

int8_t
cParser::ReadByte()
{
    assert(n > 0);
    assert(pos_ < n);
    return buf[pos_++];
}

int
cParser::ReadVarInt()
{
    int value = 0;
    int position = 0;
    uint8_t currentByte;

    while (true)
    {
        currentByte = ReadByte();
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

std::string
cParser::ReadString()
{
    int len = ReadVarInt();
    assert(len >= 0);
    assert(len <= 255);
    std::string res(len, 0);
    for (size_t i = 0; i < len; ++i)
    {
        int byte = ReadByte();
        res[i] = byte;
    }

    return res;
}

uint16_t
cParser::ReadUnsignedShort()
{
    int msb = ReadByte();
    int lsb = ReadByte();
    return (msb >> 8) | lsb;
}

int32_t
cParser::ReadInt()
{
    int32_t res = 0;
    for (size_t i = 0; i < 4; ++i)
    {
        res |= (ReadByte() << (3 - i));
    }

    return res;
}

uint32_t
cParser::ReadUInt()
{
    uint32_t res = 0;
    for (size_t i = 0; i < 4; ++i)
    {
        res |= (ReadByte() << (3 - i));
    }

    return res;
}

int64_t
cParser::ReadLong()
{
    int64_t res = 0;
    for (size_t i = 0; i < 8; ++i)
    {
        res |= (ReadByte() << (7 - i));
    }

    return res;
}

uint64_t
cParser::ReadULong()
{
    uint64_t res = 0;
    for (size_t i = 0; i < 8; ++i)
    {
        res |= (ReadByte() << (7 - i));
    }

    return res;
}

UUID
cParser::ReadUUID()
{
    uint64_t ms = ReadULong();
    uint64_t ls = ReadULong();
    return { ms, ls };
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

cPacketHandlePtr
cParser::DispatchMsg(cInMessagePtr msg)
{
    auto pkt = msg->pkt;
    int proto_id = ReadVarInt();

    std::optional<eConnState> state
        = g_server.GetClientConnState(msg->client_id);
    if (!state)
    {
        throw "Failed to get client state";
    }

    ePacketBound bound = ePacketBound::Serverbound;

    const cPacketKey key{ proto_id, state.value(), ePacketBound::Serverbound };

    const auto it = packet_factory.find(key);
    if (it == packet_factory.end())
    {
        return nullptr;
    }

    return it->second(msg);
}

ePacketMetaState
cPacketBuilder::GetState()
{
    return state_;
}

void
cPacketBuilder::InitData(size_t length)
{
    packetPtr_ = std::make_shared<cPacket>(length);
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

    if (this->GetState() == ePacketMetaState::End)
    {
        this->Reset();
    }

    if (this->GetState() == ePacketMetaState::Init)
    {
        cParser parser(n, buf);

        int length = parser.ReadVarInt();
        if (!length)
        {
            return std::nullopt;
        }

        buf += parser.GetPos();
        n -= parser.GetPos();

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

cPacketHandshake::cPacketHandshake(ClientId client_id, int proto_version,
                                   std::string server_addr, int server_port,
                                   eIntent intention)
    : cPacketHandle(client_id), proto_version(proto_version),
      server_addr(server_addr), server_port(server_port), intention(intention)
{
}

int
cPacketHandshake::Handle()
{
    g_server.SetClientConnState(client_id_, eConnState::Login);
    Debug();
    return 0;
}

void
cPacketHandshake::Debug()
{
    debug("cPacketHandshake debug");
    debug("proto_version: %d", proto_version);
    debug("server_addr: %s", server_addr.c_str());
    debug("server_port: %d", server_port);
    debug("intention: %d", intention);
}

std::size_t
cPacketKeyHash::operator ()(const cPacketKey &key) const
{
    std::size_t h1 = std::hash<int>{}(key.proto_id);
    std::size_t h2 = std::hash<int>{}(static_cast<int>(key.state_));
    std::size_t h3 = std::hash<int>{}(static_cast<int>(key.bound_));

    return h1 ^ (h2 << 1) ^ (h3 << 2);
}

bool
cPacketKey::operator ==(const cPacketKey &value) const
{
    return proto_id == value.proto_id && state_ == value.state_
           && bound_ == value.bound_;
}

cPacketHandle::cPacketHandle(ClientId client_id) : client_id_(client_id) {}

cPacketLoginStart::cPacketLoginStart(ClientId client_id, std::string name,
                                     UUID uuid)
    : cPacketHandle(client_id), name(name), uuid(uuid)
{
}

void
cPacketLoginStart::Debug()
{
    debug("cPacketLoginStart debug");
    debug("name: %s", name.c_str());
}

int
cPacketLoginStart::Handle()
{
    Debug();
    return 0;
}
