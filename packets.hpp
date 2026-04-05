#ifndef __PACKETS_H__
#define __PACKETS_H__

#include "types.hpp"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

constexpr static int SEGMENT_BITS = 0x7F;
constexpr static int CONTINUE_BIT = 0x80;

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
    None,
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

struct cProtocolPacketKey
{
    int proto_id_;
    eConnState state_;
    ePacketBound bound_;
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

class cInMessage
{
public:
    ClientId client_id_;
    cPacketPtr pkt_;

    cInMessage(ClientId client_id, cPacketPtr pkt);
};
using cInMessagePtr = std::shared_ptr<cInMessage>;

class cPacketProto
{
protected:
    cProtocolPacketKey key;
    std::string resource_;

public:
    cPacketProto() = delete;
    cPacketProto(int protocol, eConnState state, ePacketBound bound);
    cPacketProto(int protocol, eConnState state, ePacketBound bound,
                 std::string resourse);

    virtual int Fill(cPacketPtr) = 0;
};

struct cProtoIntentionHandshakingToServer : cPacketProto
{
    int proto_version_;
    std::string server_addr_;
    int server_port_;
    eIntent intention_;
};

struct cProtoStatus_responseStatusToClient : cPacketProto
{
    std::string json_response_;
};

struct cProtoPong_responseStatusToClient : cPacketProto
{
    long timtestamp_;
};

class cParser
{
    size_t pos_ = 0;

public:
    int8_t ReadByte(size_t n, const unsigned char *buf);
    int ReadVarInt(size_t n, const unsigned char *buf);

    cPacketProto DispatchMsg(cInMessage *msg);

    void SetPos(size_t pos);
    size_t GetPos() const;
    void Reset();
};

class cPacketGen
{
    const static std::unordered_map<cProtocolPacketKey, cPacketProto&> packetProtoMap;
public:
    cPacketProto &CreatePacketProto(cProtocolPacketKey key);
};

#endif // __PACKETS_H__
