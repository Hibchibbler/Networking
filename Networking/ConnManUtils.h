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


struct MESG
{
    struct HEADER
    {
        enum class Codes {
            Identify,
            Grant,
            Deny,
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
    }payload;
};

static const char* CodeName[] = {
    "Identify",
    "Grant",
    "Deny",
    "General",
    "Ack",
    "Ping",
    "Pong"
};

void
AddRequestStatus(
    Connection & connection,
    RequestStatus & rs,
    uint32_t seq
);

std::map<uint32_t, RequestStatus>::iterator
GetRequestStatus(
    Connection & connection,
    uint32_t index
);

void
RemoveRequestStatus(
    Connection & connection,
    uint32_t sid
);

void
InitializeConnection(
    Connection* pConn,
    Address to,
    std::string playername
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
GetPlayerName(
    Packet & packet
);

uint32_t
GetConnectionId(
    Packet & packet
);

std::string
GetGameName(
    Packet & packet
);

std::string
GetGamePass(
    Packet & packet
);

uint32_t
GetPacketSequence(
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


