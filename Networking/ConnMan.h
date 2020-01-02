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
        FAILED,
        SUCCEEDED,
        DYING,
        DEAD
    };

    //enum class SendStyle {
    //    AUTO_REMOVE_REQUEST,
    //    MANUAL_REMOVE_REQUEST
    //};

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
        GRANTED,
        DENIED,
        ALIVE,
        IDENTIFYING
    };

    //Connection()
    //    : cmpromise(std::make_shared<std::promise<ConnectionStatus>>())
    //    {}

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

    std::list<uint32_t> pingtimes;
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
        SERVER
    };
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
    ConnManType             cmtype;
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
    uint32_t                retry_count;

    NetworkState            netstate;
    Mutex                   connectionsmutex;
    std::list<Connection>   connections;
    Connection              localconn;

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

    enum class SendType
    {
        WITHOUTRECEIPT,
        WITHRECEIPT
    };
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
    UpdateServerConnections(
        Connection* pConn
    );

    void
    UpdateClientConnection(
        Connection* pConn
    );
    void
    UpdateConnection(
        Connection* pConn
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

    RequestFuture
    SendBuffer(
        Connection & connection,
        SendType sendType,
        uint8_t* buffer,
        uint32_t buffersize,
        uint32_t& token
    );

    bool
    SendTryReliable(
        Connection & connection,
        uint8_t* buffer,
        uint32_t buffersize,
        uint32_t& request_index
    );

    void
    SendAckTo(
        Address to,
        uint32_t id,
        uint32_t ack
    );

    //RequestFuture
    void
    SendPingTo(
        Connection & connection//,
        //uint32_t & request_index
    );

    void
    SendPongTo(
        Connection & connection,
        uint32_t ack
    );

    uint64_t
    SendIdentify(
        Connection& connection,
        uint32_t randomcode,
        std::string gamename,
        std::string gamepass
    );

    Connection::ConnectingResult
    Connect(
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

    void
    ProcessPing(
        Connection* pConn,
        Request* request
    );

    void
    ProcessPong(
        Connection* pConn,
        Request* request
    );

    uint64_t
    SendDenyTo(
        Address to,
        std::string playername
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

    void
    KillDyingRequests(
        Connection* pConn
    );

    void
    ReapDeadRequests(
        Connection* pConn
    );

    ConnManState cmstate;
};


}


#endif

