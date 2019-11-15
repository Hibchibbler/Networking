#ifndef CONNMAN_H_
#define CONNMAN_H_

#include "ConnManUtil.h"
//#include <WinSock2.h>
//#include <windows.h>

#include <chrono>

namespace bali
{

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
        uint32_t port,
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

