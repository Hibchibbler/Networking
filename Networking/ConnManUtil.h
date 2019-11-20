#ifndef CONNMANUTIL_H_
#define CONNMANUTIL_H_

#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "Network.h"
#include <queue>
#include <list>
#include <map>
#include <chrono>

namespace bali
{

typedef std::chrono::high_resolution_clock clock;
typedef std::chrono::duration<float, std::milli> duration;

struct IDENTIFY
{
    CHAR playername[16];
    CHAR gamename[16];
    CHAR gamepass[16];
};

struct GRANT
{
    CHAR playername[16];
};

struct DENY
{
    CHAR playername[16];
};

//struct READY
//{
//};
//
//struct START
//{
//};

struct GENERAL
{
    uint8_t buffer[1024];
};

//struct LEAVE
//{
//};

struct ACK
{
};

//struct PING
//{
//};
//
//struct PONG
//{
//};

struct LOBBYUPDATE
{
    // Updates on who else is in the Lobby...
    // Player needs to know whether or not to 
    // Ready Up.
};


struct MESG
{
    struct HEADER
    {
        enum class Codes {
            Identify,
            Grant,
            Deny,
            Ack,
            General
        };

        enum class Traits {
            ACK = 0
        };
        char magic[4];
        uint32_t code;
        uint32_t id;
        uint32_t traits;
        uint32_t seq;
        uint32_t crc;
    }header;

    union {
        uint8_t     buffer[1024]; // Packet expects MESG to be <= 1280

        IDENTIFY    identify;   // Client Rx
        GRANT       grant;      // Server Rx
        DENY        deny;       // Server Rx
        ACK         ack;
        GENERAL     general;    // Client Rx & Server Rx

    }payload;
};

class Connection
{
public:

    enum class ConnectionType {
        LOCAL,
        REMOTE
    };
    //enum class State {
    //    WAITFORGRANTDENY,   // Client
    //    GRANTED,            // Client
    //    DENIED,             // Client
    //    WAITFORREADY,       // Client/Server
    //    WAITFORSTART,       // Client/Server
    //    GENERAL,            // Connection Established
    //    SENDACK,
    //    WAITFORACK,
    //    INVALID
    //};
    enum class State {
        IDLE,
        WAITONACK,
        ACKRECEIVED,
        ACKNOTRECEIVED
    };
    enum class AuthLevel {
        UNAUTH,
        AUTH
    };

    AuthLevel           level;
    std::string         playername;
    State               state;
    uint32_t            id;
    Address             who;
    ConnectionType      conntype;
    clock::time_point   checkintime;
    clock::time_point   starttime;
    clock::time_point   endtime;
    std::list<duration> pingtimes;
};

struct ConnManState
{
    enum class OnEventType {
        CLIENTENTER,
        CLIENTLEAVE,
        MESSAGE
    };
    typedef void(*OnEvent)(void* oecontext, OnEventType t, Connection* conn, Packet* packet);

    uint64_t                CurrentConnectionId;
    uint64_t                timeticks;
    uint32_t                done;

    uint32_t                port;
    uint32_t                numplayers;
    uint32_t                numreadyplayers;
    std::string             gamename;
    std::string             gamepass;

    NetworkState            netstate;
    Mutex                   cmmutex;
    Thread                  threadConnMan;
    std::list<Connection>   connections;
    std::queue<Packet>      packets;
    OnEvent                 onevent;
    void*                   oneventcontext;
};

bool
RemoveConnectionByName(
    std::list<Connection> & connections,
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
uint64_t
InitializePacket(
    Packet & packet,
    Address & who,
    MESG::HEADER::Codes code,
    uint32_t id,
    uint32_t traits,
    uint32_t seq
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

bool
IsGood2(
    MESG::HEADER::Codes code,
    Packet & packet
);

bool
GetConnectionById(
    std::list<Connection> & connections,
    uint32_t id,
    Connection** connection
);


bool
GetConnectionByName(
    std::list<Connection> & connections,
    std::string playername,
    Connection** connection
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

bool
IsExpectsAck(
    Packet & packet
);

bool
IsPlayerNameAvailable(
    ConnManState & cmstate,
    std::string name
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

MESG*
GetMesg(
    Packet & packet
);

} // end namespace bali


#endif // CONNMANUTIL_H_
