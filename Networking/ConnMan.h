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
            Ack
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
    }payload;
};

static const char* CodeName[] = {
    "Identify",
    "Grant",
    "Deny",
    "General",
    "Ack"
};

class Connection
{
public:

    struct RequestStatus
    {
        enum class State {
            PENDING,
            FAILED,
            SUCCEEDED
        };
        clock::time_point starttime;
        clock::time_point endtime;

        uint32_t seq;
        State    state;
        Packet   packet;
    };
    enum class Locality {
        REMOTE,
        LOCAL
    };
    enum class State {
        NEW,
        READY
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

    Locality            locality;   // TODO: was thinking this could
                                    // be used to merge client and server code
                                    // in a sane manner.

    clock::time_point   lastrxtime;
    clock::time_point   lasttxtime;

    clock::time_point   heartbeat;
    clock::time_point   pingstart;
    clock::time_point   pingend;

    std::list<duration> pingtimes;
    float               avgping;

    clock::time_point   acktime;

    Mutex               reqstatusmutex;
    std::map<uint32_t, RequestStatus> reqstatus;
    std::queue<Packet>  rxpackets;
};

void
SetRequestStatusState(
    Connection & connection,
    uint32_t sid,
    Connection::RequestStatus::State state,
    bool lock
);

void
AddRequestStatus(
    Connection & connection,
    Connection::RequestStatus & rs,
    uint32_t seq
);

std::map<uint32_t, Connection::RequestStatus>::iterator
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
    Mutex                   connectionsmutex;
    std::list<Connection>   connections;
    Connection              localconn;

    //std::queue<Packet>      rxpackets;
    //std::queue<Packet>      txpacketsunreliable;

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
    InitializeServer(
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
    InitializeClient(
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
    UpdateServer(
        ConnManState & cmstate,
        uint32_t ms_elapsed
    );

    static
    void
    UpdateClient(
        ConnManState & cmstate,
        uint32_t ms_elapsed
    );

    static
    void
    Cleanup(
        ConnManState & cmstate
    );

    typedef void (*AcknowledgeHandler)(uint32_t code);

    enum class QueryPredicate
    {
        IS_EQUAL
    };

    enum class QueryType
    {
        IS_STATE_EQUAL,
        GET_PING
    };

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
    ReadyCount(
        ConnManState & cmstate
    );

    static
    uint64_t
    Connect(
        ConnManState & cmstate,
        Connection& connection,
        std::string gamename,
        std::string gamepass
    );

    static
    uint64_t
    SendGrantTo(
        ConnManState & cmstate,
        Address to,
        uint32_t id,
        std::string playername
    );

    static
    uint64_t
    SendDenyTo(
        ConnManState & cmstate,
        Address to,
        std::string playername
    );

    static
    void
    ProcessIdentify(
        ConnManState& cmstate,
        Request* request
    );

    static
    void
    ProcessGeneral(
        ConnManState& cmstate,
        Connection* pConn,
        Request* request
    );

    static
    void
    ProcessAck(
        ConnManState& cmstate,
        Connection* pConn,
        Request* request
    );

    static
    void
    ProcessDeny(
        ConnManState& cmstate,
        Connection* pConn,
        Request* request
    );

    static
    void
    ProcessGrant(
        ConnManState& cmstate,
        Connection* pConn,
        Request* request
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

