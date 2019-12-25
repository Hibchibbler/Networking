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
        DEAD,
        GRANTED,
        DENIED,
        ALIVE,
        IDENTIFYING
    };

    std::map<uint32_t, RequestStatus>::iterator
    GetRequestStatus(
        uint32_t index
    );

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
    clock::time_point   identtime;
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

    Mutex               rxpacketmutex;
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
        CONNECTION_HANDSHAKE_GRANTED,    // Server Side Event
        CONNECTION_HANDSHAKE_DENIED,
        CONNECTION_HANDSHAKE_TIMEOUT,
        CONNECTION_TIMEOUT_WARNING,
        CONNECTION_TIMEOUT,
        MESSAGE_ACK_TIMEOUT,
        MESSAGE_ACK_RECEIVED,
        MESSAGE_RECEIVED
    };
    typedef void(*OnEvent)(void* oecontext, OnEventType t, Connection* conn, Packet* packet);

    uint64_t                timeticks;
    uint32_t                done;

    uint32_t                thisport;
    std::string             serveripv4;
    uint32_t                serverport;

    uint32_t                numplayers;
    uint32_t                numreadyplayers;

    std::string             gamename;
    std::string             gamepass;

    uint32_t                heart_beat_ms;
    uint32_t                timeout_warning_ms;
    uint32_t                timeout_ms;
    uint32_t                ack_timeout_ms;

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
public:
    ConnMan(){}
    ~ConnMan(){}

    void
    InitializeServer(
        NetworkConfig & netcfg,
        uint32_t thisport,
        uint32_t numplayers,
        std::string gamename,
        std::string gamepass,
        ConnManState::OnEvent onevent,
        void* oneventcontext
    );

    void
    InitializeClient(
        NetworkConfig & netcfg,
        uint32_t thisport,
        uint32_t numplayers,
        std::string gamename,
        std::string gamepass,
        ConnManState::OnEvent onevent,
        void* oneventcontext
    );

    void
    UpdateServer(
        Connection* pConn
    );

    void
    UpdateClient(
        Connection* pConn
    );
    void
    UpdateAlive(
        Connection* pConn
    );
    void
    ReapConnections(

    )
    {
        // Reap Dead Connections
        //
        auto pConn = cmstate.connections.begin();
        while (pConn != cmstate.connections.end())
        {
            if (pConn->state == Connection::State::DEAD)
            {
                pConn = cmstate.connections.erase(pConn);
            }
            else
            {
                pConn++;
            }
        }
    }

    void
    Update(
        uint32_t ms_elapsed,
        bool client
    );


    void
    Cleanup(
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

    uint32_t
    SendReliable(
        Connection & connection,
        uint8_t* buffer,
        uint32_t buffersize,
        AcknowledgeHandler ackhandler
    );

    void
    SendUnreliable(
        Connection & connection,
        uint8_t* buffer,
        uint32_t buffersize
    );

    void
    SendAckTo(
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
        IsPlayerNameAndIdAvailable(
        std::list<Connection> & connections,
        std::string name,
        uint16_t id
    );


    uint64_t
    ReadyCount(
    );

    uint64_t
    SendIdentify(
        Connection& connection,
        uint32_t randomcode,
        std::string gamename,
        std::string gamepass
    );

    uint64_t
    SendGrantTo(
        Address to,
        uint32_t id,
        std::string playername
    );

    uint64_t
    SendDenyTo(
        Address to,
        std::string playername
    );

    void
    ProcessIdentify(
        Request* request
    );

    void
    ProcessGeneral(
        Connection* pConn,
        Request* request
    );

    void
    ProcessAck(
        Connection* pConn,
        Request* request
    );

    void
    ProcessDeny(
        Connection* pConn,
        Request* request
    );

    void
    ProcessGrant(
        Connection* pConn,
        Request* request
    );

    ConnManState cmstate;
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

