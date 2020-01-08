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
#include <future>
#include <memory>
using std::move;
namespace bali
{
typedef std::chrono::high_resolution_clock clock;
typedef std::chrono::duration<float, std::milli> duration;
struct RequestStatus
{
    enum class RequestResult {
        ACKNOWLEDGED,
        TIMEDOUT
    };
    enum class State {
        PENDING,
        ACKNOWLEDGED,
        DYING, // These requests will be removed manually
        DEAD   // These will be removed automatically.
    };

    RequestStatus()
    {
        promise = std::make_shared<std::promise<RequestResult>>();
        retries = 0;
    }

    ~RequestStatus()
    {

    }


    uint32_t retries;
    clock::time_point starttime;
    clock::time_point endtime;
    std::shared_ptr<std::promise<RequestResult>> promise;
    uint32_t seq;
    State    state;
    Packet   packet;
};


typedef std::promise<RequestStatus::RequestResult> RequestPromise;
typedef std::future<RequestStatus::RequestResult> RequestFuture;



class Connection
{
public:
    enum class Locality {
        REMOTE,
        LOCAL
    };

    enum class ConnectingResult {
        GRANTED,
        DENIED,
        TIMEDOUT
    };


    enum class State {
        DEAD,
        DYING,
        DISCONNECTING,
        GRANTED,
        GRACKING,
        DENIED,
        ALIVE,
        IDENTIFIED
    };

    typedef std::shared_ptr<Connection> ConnectionPtr;

    void
    Initialize(
        Address to,
        std::string playername,
        uint32_t uid,
        Connection::Locality locality,
        Connection::State initstate
    )
    {
        // But we don't know it's ID yet
        // because we have not yet been GRANTed
        id = uid;
        state = initstate;
        curseq = 0;
        curack = 0;
        highseq = 0;
        curuseq = 0;
        curuack = 0;
        highuseq = 0;
        lastrxtime = clock::now();
        lasttxtime = clock::now();

        heartbeat = clock::now();
        reqstatusmutex.create();
        rxpacketmutex.create();
        who = to;
        playername = playername;
        this->locality = locality;
    }

    Connection::ConnectingResult
    Connect(
        uint32_t randomcode,
        std::string gamename,
        std::string gamepass
    );

    void
    RemoveRequestStatus(
        uint32_t sid
    );

    void
    AddRequestStatus(
        RequestStatus & rs,
        uint32_t seq
    );

    std::map<uint32_t, RequestStatus>::iterator
    GetRequestStatus(
        uint32_t index
    );

    void
    ReapDeadRequests(
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
    uint32_t            curpingseq;
    clock::time_point   pingend;


    std::list<uint32_t> pingtimes;
    uint32_t            totalpings;
    uint32_t            totalpongs;
    float               avgping;
    uint32_t            curping;
    uint32_t            minping;
    uint32_t            maxping;

    clock::time_point   acktime;

    Mutex               reqstatusmutex;
    std::map<uint32_t, RequestStatus> reqstatus;

    Mutex               rxpacketmutex;
    std::queue<Packet>  rxpackets;


    std::shared_ptr<std::promise<ConnectingResult>> connectingresultpromise;
};

typedef std::promise<Connection::ConnectingResult> ConnectingResultPromise;
typedef std::future<Connection::ConnectingResult> ConnectingResultFuture;


struct ConnManState
{
    enum class ConnManType {
        CLIENT,
        SERVER,
        PASS_THROUGH
    };
    enum class OnEventType {
        CONNECTION_HANDSHAKE_GRANTED,    // Server Side Event
        CONNECTION_HANDSHAKE_DENIED,
        CONNECTION_HANDSHAKE_TIMEOUT,
        CONNECTION_HANDSHAKE_TIMEOUT_NOGRACK,
        CONNECTION_TIMEOUT_WARNING,
        CONNECTION_TIMEOUT,
        CONNECTION_DISCONNECT,
        MESSAGE_ACK_TIMEOUT,
        MESSAGE_ACK_RECEIVED,
        MESSAGE_RECEIVED,
        NOTIFICATION_INFO,
        NOTIFICATION_WARNING,
        NOTIFICATION_ERROR
    };
    typedef void(*OnEvent)(void* oecontext, OnEventType t, Connection* conn, Packet* packet);
    ConnManType             cmtype;
    uint64_t                timeticks;
    uint32_t                done;

    uint32_t                thisport;
    //std::string             thisipv4;
    //uint32_t                thatport;
    //std::string             thatipv4;

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
    uint32_t                retry_count;

    NetworkState            netstate;
    Mutex                   connectionsmutex;
    

    Mutex               globalrxmutex;
    std::queue<Packet>  globalrxqueue;

    std::list<Connection::ConnectionPtr>   connections;

    void
    AddConnection(Connection::ConnectionPtr pConn)
    {
        std::cout << "AddConnection: Connections.size= " << connections.size() << std::endl;
        connectionsmutex.lock();
        connections.push_back(pConn);
        connectionsmutex.unlock();
    }

    std::shared_ptr<Connection>
    CreateConnection(
        Address to,
        std::string playername,
        uint32_t uid,
        Connection::Locality locality,
        Connection::State initstate
    )
    {
        Connection::ConnectionPtr newConn = std::make_shared<Connection>();
        newConn->Initialize(to, playername, uid, locality, initstate);
        return newConn;
    }

    void
    RemoveConnection(
        uint32_t uid
    );

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
    typedef void (*AcknowledgeHandler)(uint32_t code);


    ConnMan(){}
    ~ConnMan(){}

    void
    Initialize(
        NetworkConfig & netcfg,
        ConnManState::ConnManType cmtype,
        uint32_t thisport,
        uint32_t numplayers,
        std::string gamename,
        std::string gamepass,
        ConnManState::OnEvent onevent,
        void* oneventcontext
    );

    void
    Initialize(
        NetworkConfig & netcfg,
        ConnManState::ConnManType cmtype,
        uint32_t thisport,
        ConnManState::OnEvent onevent,
        void* oneventcontext
    );

    void
        UpdateRelayMachine(
    );

    void
    UpdateServerConnections(
        Connection::ConnectionPtr pConn
    );

    void
    UpdateClientConnection(
        Connection::ConnectionPtr pConn
    );
    void
    UpdateConnection(
        Connection::ConnectionPtr pConn
    );

    void
    ReapDeadConnections(
    );

    void
    Update(
        uint32_t ms_elapsed
    );

    void
    Cleanup(
    );

    enum class SendType
    {
        WITHOUTRECEIPT,
        WITHRECEIPT
    };

    RequestFuture
    SendBuffer(
        Connection::ConnectionPtr pConn,
        SendType sendType,
        uint8_t* buffer,
        uint32_t buffersize,
        uint32_t& token
    );

    void
    Write(
        Packet& packet
    );

    void
    ProcessDisconnect(
        Connection::ConnectionPtr pConn
    );

    void
    ProcessIdentify(
        Request* request
    );

    void
    ProcessRxPacket(
        Packet& packet
    );

    void
    ProcessGeneral(
        Connection::ConnectionPtr pConn,
        Packet& packet
    );

    void
    ProcessAck(
        Connection::ConnectionPtr pConn,
        Packet& packet
    );

    void
    ProcessDeny(
        Connection::ConnectionPtr pConn,
        Packet& packet
    );

    void
    ProcessGrant(
        Connection::ConnectionPtr pConn,
        Packet& packet
    );

    void
    ProcessGrack(
        Connection::ConnectionPtr pConn,
        Packet& packet
    );

    void
    ProcessPing(
        Connection::ConnectionPtr pConn,
        Packet& packet
    );

    void
    ProcessPong(
        Connection::ConnectionPtr pConn,
        Packet& packet
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
    Connection::ConnectionPtr
    GetConnectionById(
        std::list<Connection::ConnectionPtr> & connections,
        uint32_t id
    );

    static
    Connection::ConnectionPtr
    GetConnectionById(
        Mutex & mutex,
        std::list<Connection::ConnectionPtr> & connections,
        uint32_t id
    );

    static
    bool
    IsPlayerNameAndIdAvailable(
        std::list<Connection::ConnectionPtr> & connections,
        std::string name,
        uint16_t id
    );
    
    static
    bool
    IsPlayerNameAndIdAvailable(
        Mutex & mutex,
        std::list<Connection::ConnectionPtr> & connections,
        std::string name,
        uint16_t id
    );

    static
    bool
    RemoveConnectionByName(
        std::list<Connection::ConnectionPtr> & connections,
        std::string playername
    );

    uint64_t
    ReadyCount(
    );




    ConnManState cmstate;
};


}


#endif

