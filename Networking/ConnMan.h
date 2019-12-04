#ifndef CONNMAN_H_
#define CONNMAN_H_

#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "Network.h"
#include "SlabPool.h"
#include "NetworkConfig.h"
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

struct GENERAL
{
    uint8_t buffer[MAX_PACKET_SIZE-128];
};

struct ACK
{
    uint32_t ack;
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

class Connection
{
public:

    enum class State {
        UNINIT,
        IDLE,
        WAITONACK,
        ACKRECEIVED,
        ACKNOTRECEIVED
    };

    std::string         playername;
    State               state;
    uint32_t            id;
    Address             who;

    uint32_t            curseq; // Reliable
    uint32_t            curack; // Reliable
    uint32_t            highseq;

    uint32_t            curuseq; // Unreliable
    uint32_t            curuack; // Unreliable
    uint32_t            highuseq;

    clock::time_point   checkintime;
    clock::time_point   starttime;
    clock::time_point   endtime;

    clock::time_point   heartbeat;
    clock::time_point   pingstart;
    clock::time_point   pingend;

    std::list<duration> pingtimes;
    float               avgping;

    clock::time_point   acktime;
    std::queue<Packet>  txpacketsreliable; // Reliability is managed per-connection
    Packet              txpacketpending; //we only send 1 reliable message at a time, so,,
};

void
InitializeConnection(
    Connection* pConn
);

struct ConnManState
{
    enum class OnEventType {
        CONNECTION_ADD,    // Server Side Event
        CONNECTION_REMOVE, // Server Side Event
        ACK_TIMEOUT,
        ACK_RECEIVED,
        GRANTED,     // Client Side Event
        DENIED,      // Client Side Event
        MESSAGE,
        CONNECTION_STALE
    };
    typedef void(*OnEvent)(void* oecontext, OnEventType t, Connection* conn, Packet* packet);

    uint64_t                timeticks;
    uint32_t                done;

    std::string             ipv4server;
    uint32_t                port;

    uint32_t                numplayers;
    uint32_t                numreadyplayers;

    std::string             gamename;
    std::string             gamepass;

    uint32_t                heartbeat_ms;
    uint32_t                stale_ms;
    uint32_t                remove_ms;
    uint32_t                acktimeout_ms;

    NetworkState            netstate;
    Mutex                   cmmutex;

    std::list<Connection>   connections;
    Connection              localconn;

    std::queue<Packet>      rxpackets;
    std::queue<Packet>      txpacketsunreliable;

    struct RequestState
    {
        uint32_t id;
        uint32_t state;
    };
    SlabPool<RequestState>  slabpool;

    OnEvent                 onevent;
    void*                   oneventcontext;


};

/*
"ConnMan" is composed of "Network".
ConnMan tracks incoming
UDP packets and their origin. It is responsible
for knowing if an origin is known or unknown.
*/
class ConnMan
{
private:
    ConnMan(){}
public:
    ~ConnMan(){}

    static
    void
    initializeServer(
        ConnManState & cmstate,
        NetworkConfig & netcfg,
        uint32_t port,
        uint32_t numplayers,
        std::string gamename,
        std::string gamepass,
        ConnManState::OnEvent onevent,
        void* oneventcontext
    );

    static
    void
    initializeClient(
        ConnManState & cmstate,
        NetworkConfig & netcfg,
        std::string ipv4,
        uint32_t port,
        std::string gamename,
        std::string gamepass,
        ConnManState::OnEvent onevent,
        void* oneventcontext
    );

    static
    void
    updateServer(
        ConnManState & cmstate,
        uint32_t ms_elapsed
    );

    static
    void
    updateClient(
        ConnManState & cmstate,
        uint32_t ms_elapsed
    );

    static
    void
    cleanup(
        ConnManState & cmstate
    );

    typedef void (*AcknowledgeHandler)(uint32_t code);

    static
    uint32_t
    Query(
        uint32_t who,
        uint32_t what
    ){}

    static
    uint32_t
    SendReliable(
        ConnManState & cmstate,
        Connection & connection,
        uint8_t* buffer,
        uint32_t buffersize,
        AcknowledgeHandler ackhandler
    );

    static
    void
    SendUnreliable(
        ConnManState & cmstate,
        Connection & connection,
        uint8_t* buffer,
        uint32_t buffersize
    );

    static
    void
    SendAckTo(
        ConnManState & cmstate,
        Address to,
        uint32_t id,
        uint32_t ack
    );

    static 
    void
    SendPing(
        ConnManState & cmstate,
        Connection & connection,
        bool ping
    );

    static
    void
    ConnManServerIOHandler(
        void* state,
        Request* request,
        uint64_t id
    );

    static
    void
    ConnManClientIOHandler(
        void* state,
        Request* request,
        uint64_t id
    );

    static
    Connection*
    GetConnectionById(
        std::list<Connection> & connections,
        uint32_t id
    );

    static
    bool
    RemoveConnectionByName(
        std::list<Connection> & connections,
        std::string playername
    );

    static
    bool
    IsPlayerNameAvailable(
        std::list<Connection> & connections,
        std::string name
    );

    static
    uint64_t
    readyCount(
        ConnManState & cmstate
    );

    static
    uint64_t
    sendIdentifyTo(
        ConnManState & cmstate,
        Address to,
        std::string playername,
        std::string gamename,
        std::string gamepass
    );

    static
    uint64_t
    sendGrantTo(
        ConnManState & cmstate,
        Address to,
        uint32_t id,
        std::string playername
    );

    static
    uint64_t
    sendDenyTo(
        ConnManState & cmstate,
        Address to,
        std::string playername
    );
};

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

