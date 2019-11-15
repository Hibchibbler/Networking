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

