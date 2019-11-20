#ifndef COMMMAN_H_
#define COMMMAN_H_

#include "ConnMan.h"
#include <inttypes.h>

namespace bali
{

struct GameMesg
{
    enum class Codes {
        START,
        READY,
        PING,
        PONG,
        UPDATE
    };

    uint32_t code;
    union {
        uint8_t  buffer[960]; // MESG expects GameMesg to be <= 1024

    }payload;
};

GameMesg*
GetGameMesg(
    MESG* pMsg
);

uint32_t
GetGameMesgCode(
    GameMesg* gameMsg
);

uint32_t
SendGameMesg(
    ConnManState& cmstate,
    Address to,
    uint32_t id,
    GameMesg::Codes GameMesgCode,
    void* payload,
    uint32_t payloadsize
);

uint32_t
SendStartMesgToAll(
    ConnManState& cmstate
);

uint32_t
SendReadyMesg(
    ConnManState& cmstate,
    Address to,
    uint32_t id
);

uint32_t
SendUpdateMesg(
    ConnManState& cmstate,
    Address to,
    uint32_t id,
    void* payload,
    uint32_t payloadsize
);

class CommMan
{
public:

    void initialize();
    void update();
    void cleanup();

};

}

#endif // COMMMAN_H_

