#ifndef CONNMANUTILS_H_
#define CONNMANUTILS_H_

#include "ConnMan.h"
#include <stdint.h>
#include <string>
#include <map>
#include <future>
#include <chrono>
#include <queue>

namespace bali
{


struct IDENTIFY
{
    char playername[16];
    char gamename[16];
    char gamepass[16];
};

struct GRANT
{
    char playername[16];
};

struct DENY
{
    char playername[16];
};

struct GENERAL
{
    uint8_t buffer[MAX_PACKET_SIZE - 128];
};

struct ACK
{
};

struct PINGPONG
{

};

struct DISCONNECT
{

};

struct GRACK
{

};

struct MESG
{
    struct HEADER
    {
        enum class Codes {
            Identify,
            Grant,
            Grack,
            Deny,
            Disconnect,
            General,
            Ack,
            Ping,
            Pong
        };

        enum class Mode {
            Reliable,
            Unreliable
        };
        uint8_t magic[2];
        uint8_t code;
        uint8_t mode;
        uint16_t id;
        uint32_t seq;
        uint32_t ack;

    }header;

    union {
        IDENTIFY    identify;   // Client Rx
        GRANT       grant;      // Server Rx
        DENY        deny;       // Server Rx
        GENERAL     general;    // Client Rx & Server Rx
        ACK         ack;
        PINGPONG    pingpong;
        DISCONNECT  disconnect;
        GRACK       grack;
    }payload;
};

static const char* CodeName[] = {
    "Identify",
    "Grant",
    "Grack",
    "Deny",
    "Disconnect",
    "General",
    "Ack",
    "Ping",
    "Pong"
};
void
Disconnect(
    ConnMan & cm,
    Address to,
    uint32_t uid
)
{
    uint32_t cs = 13;
    Packet packet = 
        CreateDisconnectPacket(to,
                               uid,
                               cs,
                               13);
    cm.Write(packet);
    cm.cmstate.RemoveConnection(uid);
}

bool
Connect(
    ConnMan & cm,
    Address to,
    std::string playername,
    std::string gamename,
    std::string gamepass,
    ConnectingResultFuture & result
);

bool SendUnreliable(
    ConnMan & cm,
    uint32_t uid,
    const char* szString
);

bool SendReliable(
    ConnMan & cm,
    uint32_t uid,
    const char* szString
);

Packet
CreateDisconnectPacket(
    Address to,
    uint32_t id,
    uint32_t& curseq,
    uint32_t curack
);

Packet
CreateGrackPacket(
    Address to,
    uint32_t id,
    uint32_t& curseq,
    uint32_t curack
);

Packet
CreateGrantPacket(
    Address to,
    uint32_t id,
    uint32_t& curseq,
    uint32_t curack,
    std::string playername
);

Packet
CreateDenyPacket(
    Address to,
    std::string playername
);

Packet
CreateAckPacket(
    Address& to,
    uint32_t id,
    uint32_t& curseq,
    uint32_t ack
);

//RequestFuture
Packet
CreatePingPacket(
    Address& to,
    uint32_t id,
    uint32_t& curseq,
    uint32_t curack
);

Packet
CreatePongPacket(
    Address& to,
    uint32_t id,
    uint32_t& curseq,
    uint32_t ack
);

Packet
CreateIdentifyPacket(
    Address& to,
    std::string playername,
    uint32_t& curseq,
    uint32_t curack,
    uint32_t randomcode,
    std::string gamename,
    std::string gamepass
);

void
AddMagic(
    MESG* pMsg
);

uint32_t
SizeofPayload(
    MESG::HEADER::Codes code
);

bool
IsCode(
    Packet & packet,
    MESG::HEADER::Codes code
);

bool
IsMagicGood(
    Packet & packet
);

bool
IsSizeValid(
    Packet & packet
);

std::string
ExtractPlayerNameFromPacket(
    Packet & packet
);

uint32_t
ExtractConnectionIdFromPacket(
    Packet & packet
);

std::string
ExtractGameNameFromPacket(
    Packet & packet
);

std::string
ExtractGamePassFromPacket(
    Packet & packet
);

uint32_t
ExtractSequenceFromPacket(
    Packet & packet
);

void
PrintMsgHeader(
    Packet & packet,
    bool rx
);

void
PrintfMsg(
    char* format,
    ...
);

Address
CreateAddress(
    uint32_t port,
    const char* szIpv4
);

}



#endif


