#ifndef CONNMAN_H_
#define CONNMAN_H_

#include "Network.h"
//#include <WinSock2.h>
//#include <windows.h>
#include <queue>
#include <chrono>

namespace bali
{

struct IDENTIFY
{
    CHAR name[16];
    CHAR pass[16];
};

struct GRANT
{
    UINT8 id;
};

struct DENY
{

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



struct MESG
{
    struct HEADER
    {
        enum class Codes {
            I = 1, G, D, R, S, U, L
        };

        enum class Traits {
            ACK = 0
        };
        char magic[8];
        uint64_t code;
        uint64_t traits;
        uint64_t seq;
    }header;

    union{
        IDENTIFY identify;
        GRANT    grant;
        DENY     deny;
        READY    ready;
        START    start;
        UPDATE   update;
        LEAVE    leave;
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
        WAITFORIDENTIFY,
        SENDGRANT,
        SENDDENY,
        WAITFORREADY,
        WAITFORSTART,
        SENDSTART,
        GENERAL,
        SENDACK,
        WAITFORACK
    };

    std::string         name;
    std::string         pass;
    State               state;
    State               retstate;
    uint64_t            id;
    Address             who;

    std::queue<Packet> packets;
};

class IoHandlerContext
{
public:

};



struct ConnManState
{
    typedef void(*OnClientEnter) (void*);
    typedef void(*OnClientLeave) (void*);
    typedef void(*OnClientUpdate)(void*);
    typedef void(*OnServerUpdate)(void*);

    uint64_t                CurrentConnectionId;
    uint64_t                timeticks;
    uint32_t                done;

    uint32_t                numplayers;
    std::string             gamename;
    std::string             gamepass;

    OnClientEnter           oce;
    OnClientLeave           ocl;
    OnClientUpdate          ocu;
    OnServerUpdate          osu;

    NetworkState            netstate;
    Mutex                   cmmutex;
    Thread                  threadConnMan;
    std::vector<Connection> connections;
    std::vector<Connection> unknowns;

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
    initialize(
        ConnManState & cmstate,
        ConnManState::OnClientEnter oce, void* entercontext,
        ConnManState::OnClientLeave ocl, void* leavecontext,
        ConnManState::OnClientUpdate ocu, void* cupdatecontext,
        ConnManState::OnServerUpdate osu, void* supdatecontext,
        uint32_t numplayers,
        std::string gamename,
        std::string gamepass
    );

    static
    void
    updateServer(
        ConnManState & cmstate,
        uint32_t ms_elapsed
    );

    static
    void
    cleanup(
        ConnManState & cmstate
    );

    static
    void
    ConnManIOHandler(
        void* state,
        Request* request,
        uint64_t id
    );

    static
    bool
    AddressKnown(
        ConnManState & cmstate,
        Address* address,
        size_t* index
    );

    static
    bool
    AddressAuthorized(
        ConnManState & cmstate,
        Address* address
    );

    static
    bool
    NameExists(
        ConnManState & cmstate,
        std::string name
    );

    static
    bool
    processWaitForIdentify(
        ConnManState & cmstate,
        Connection & connection,
        Packet & packet
    );

    static
    bool
    processSendGrant(
        ConnManState & cmstate,
        Connection & connection
    );

    static
    bool
    processSendDeny(
        ConnManState & cmstate,
        Connection & connection
    );

    static
    bool
    processWaitForReady(
        ConnManState & cmstate,
        Connection & connection,
        Packet & packet
    );

    static
    bool
    processGaming(
        ConnManState & cmstate,
        Connection & connection,
        Packet & packet
    );

    static
    bool
    processSendStart(
        ConnManState & cmstate,
        Connection & connection
    );

    static
    bool
    processWaitForStart(
        ConnManState & cmstate,
        Connection & connection
    );

    static
    uint64_t
    readyCount(
        ConnManState & cmstate
    );

    static
    uint64_t
    sendStart(
        ConnManState & cmstate
    );


};
}


#endif

