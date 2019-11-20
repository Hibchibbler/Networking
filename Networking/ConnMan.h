#ifndef CONNMAN_H_
#define CONNMAN_H_

#include "ConnManUtil.h"

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
    uint64_t
    readyCount(
        ConnManState & cmstate
    );

    //static
    //uint64_t
    //sendStart(
    //    ConnManState & cmstate
    //);

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

    //static
    //uint64_t
    //sendReadyTo(
    //    ConnManState & cmstate,
    //    Address to,
    //    uint32_t id
    //);

    //static
    //uint64_t
    //sendUpdateTo(
    //    ConnManState & cmstate,
    //    Address to,
    //    uint32_t id,
    //    GENERAL & update
    //);

    //static
    //uint64_t
    //sendPingTo(
    //    ConnManState & cmstate,
    //    Address to,
    //    uint32_t id
    //);

    //static
    //uint64_t
    //sendPongTo(
    //    ConnManState & cmstate,
    //    Address to,
    //    uint32_t id
    //);

};
}


#endif

