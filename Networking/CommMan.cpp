#include "CommMan.h"

namespace bali
{

uint32_t
SendGameMesg(
    ConnManState& cmstate,
    Address to,
    uint32_t id,
    GameMesg::Codes GameMesgCode,
    void* payload,
    uint32_t payloadsize
)
{
    Packet packet;
    MESG* pMsg = (MESG*)packet.buffer;
    GameMesg* pGameMsg = (GameMesg*)pMsg->payload.buffer;

    packet.buffersize = sizeof(MESG::HEADER) + SizeofPayload(MESG::HEADER::Codes::General);
    packet.address = to;

    memcpy(pMsg->header.magic, "ABCD", 4);
    pMsg->header.code = (uint32_t)MESG::HEADER::Codes::General;
    pMsg->header.id = id;
    pMsg->header.traits = 0;
    pMsg->header.seq = 0;
    pMsg->header.crc = 0;

    pGameMsg->code = (uint32_t)GameMesgCode;
    memcpy(pGameMsg->payload.buffer, payload, payloadsize);

    Network::write(cmstate.netstate, packet);
    return 0;
}

uint32_t
SendStartMesgToAll(
    ConnManState& cmstate
)
{
    for (auto & c : cmstate.connections)
    {
        SendGameMesg(cmstate,
                     c.who,
                     c.id,
                     GameMesg::Codes::START,
                     0, 0);
    }
    return 0;
}

uint32_t
SendReadyMesg(
    ConnManState& cmstate,
    Address to,
    uint32_t id
)
{
    SendGameMesg(cmstate,
                 to,
                 id,
                 GameMesg::Codes::READY,
                 0,
                 0);
    return 0;
}

uint32_t
SendUpdateMesg(
    ConnManState& cmstate,
    Address to,
    uint32_t id,
    void* payload,
    uint32_t payloadsize
)
{
    SendGameMesg(cmstate,
                 to,
                 id,
                 GameMesg::Codes::UPDATE,
                 payload,
                 payloadsize);
    return 0;
}

GameMesg*
GetGameMesg(
    MESG* pMsg
)
{
    GameMesg* pGsg = (GameMesg*)&pMsg->payload.general;
    return pGsg;
}

uint32_t
GetGameMesgCode(
    GameMesg* gameMsg
)
{
    uint32_t code = gameMsg->code;
    return code;
}

} // namespace bali
