#ifndef __PACKETS_H__
#define __PACKETS_H__

#include "types.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

constexpr static int SEGMENT_BITS = 0x7F;
constexpr static int CONTINUE_BIT = 0x80;
constexpr static bool PACKETS_DEBUG = true;

#define INIT_PARSER(parser)                                 \
    cParser parser(msg->pkt->length, msg->pkt->data.get()); \
    parser.ReadVarInt();

enum class eReadStatus
{
    Ok,
    WouldBlock,
    Disconnected,
    Error,
    BufferFull,
};

enum class eConnState
{
    Handshaking,
    Status,
    Login,
    Play
};

enum class eIntent
{
    Error = 0,
    Status = 1,
    Login = 2,
    Transfer = 3
};

enum class ePacketBound
{
    Serverbound,
    Clientbound
};

struct cPacketKey
{
    int proto_id;
    eConnState state_;
    ePacketBound bound_;
    bool operator ==(const cPacketKey &value) const;
};

struct cPacketKeyHash
{
    std::size_t operator ()(const cPacketKey &key) const;
};

struct cPacket
{
    int length;
    std::unique_ptr<uint8_t[]> data;
    cPacket(int length);
};
using cPacketPtr = std::shared_ptr<cPacket>;

enum class ePacketMetaState
{
    Init,
    Partial,
    Full,
    End,
};

class cPacketBuilder
{
    cPacketPtr packetPtr_;
    int bytes_read_ = 0;
    ePacketMetaState state_ = ePacketMetaState::Init;

public:
    cPacketBuilder() = default;
    bool IsValid();
    ePacketMetaState GetState();
    void InitData(size_t length);
    int PushData(size_t n, uint8_t *buf);
    std::optional<cPacketPtr> IncBuildPacket(size_t n, uint8_t *buf);
    void Reset();
};

struct cInMessage
{
    ClientId client_id;
    cPacketPtr pkt;

    cInMessage(ClientId client_id, cPacketPtr pkt);
};
using cInMessagePtr = std::shared_ptr<cInMessage>;

class cPacketHandle
{
protected:
    ClientId client_id_;

public:
    explicit cPacketHandle(ClientId client_id);
    virtual int Handle() = 0;
    virtual void Debug() = 0;
    virtual ~cPacketHandle() = default;
};
using cPacketHandlePtr = std::shared_ptr<cPacketHandle>;
using PacketHandleFactoryFn = std::function<cPacketHandlePtr(cInMessagePtr)>;

struct cPacketHandshake : public cPacketHandle
{
    int proto_version;
    std::string server_addr;
    int server_port;
    eIntent intention;

    cPacketHandshake(ClientId client_id, int proto_version,
                     std::string server_addr, int server_port,
                     eIntent intention);
    virtual int Handle() override;
    virtual void Debug() override;
};

struct cPacketLoginStart : cPacketHandle
{
    std::string name;
    UUID uuid;

    cPacketLoginStart(ClientId client_id, std::string name, UUID uuid);
    virtual int Handle() override;
    virtual void Debug() override;
};

class cParser
{
    size_t pos_ = 0;
    size_t n = 0;
    uint8_t *buf = nullptr;

public:
    cParser() = delete;
    explicit cParser(size_t n, uint8_t *buf);
    int8_t ReadByte();
    int ReadVarInt();
    std::string ReadString();
    uint16_t ReadUnsignedShort();
    UUID ReadUUID();
    int32_t ReadInt();
    uint32_t ReadUInt();
    int64_t ReadLong();
    uint64_t ReadULong();

    cPacketHandlePtr DispatchMsg(cInMessagePtr msg);

    void SetPos(size_t pos);
    size_t GetPos() const;
    void Reset();
};

#endif // __PACKETS_H__
