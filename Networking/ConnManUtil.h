#ifndef CONNMANUTIL_H_
#define CONNMANUTIL_H_

#include "Network.h"
#include <queue>

namespace bali
{
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

struct READY
{
};

struct START
{
};

struct UPDATE
{
};

struct LEAVE
{
};

struct ACK
{
};

struct PING
{
};

struct PONG
{
};

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
            I = 1, G, D, R, S, U, L, A, LU, PING,PONG
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
        IDENTIFY    identify;
        GRANT       grant;
        DENY        deny;
        READY       ready;
        START       start;
        UPDATE      update;
        LEAVE       leave;
        ACK         ack;
        LOBBYUPDATE lobbyupdate;
        PING        ping;
        PONG        poing;
    }payload;
};

class Connection
{
public:
    /*
    Transitions requiring a Packet:
    Unknown -> SendGrant | SendDeny [I]
    WaitForReady -> Ready           [R]
    Started -> Started              [U]

    */
    enum class State {
        INIT,
        WAITFORIDENTIFY,
        WAITFORREADY,
        WAITFORSTART,
        WAITFORGRANTDENY,
        GENERAL,
        SENDACK,
        WAITFORACK
    };

    std::string         playername;
    std::string         gamename;
    std::string         gamepass;
    State               state;
    uint32_t            id;
    Address             who;

    std::queue<Packet> packets;
};

struct ConnManState
{
    uint64_t                CurrentConnectionId;
    uint64_t                timeticks;
    uint32_t                done;

    uint32_t                port;
    uint32_t                numplayers;
    std::string             gamename;
    std::string             gamepass;

    NetworkState            netstate;
    Mutex                   cmmutex;
    Thread                  threadConnMan;
    std::vector<Connection> connections;
    std::vector<Packet>     packets;

};

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
    uint32_t traits
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
GetConnection(
    std::vector<Connection> & connections,
    uint32_t id,
    Connection** connection
);


bool
GetConnection(
    std::vector<Connection> & connections,
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


} // end namespace bali


#endif // CONNMANUTIL_H_
