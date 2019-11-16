#include "ConnManUtil.h"

namespace bali
{

void
AddMagic(
    MESG* pMsg
)
{
    memcpy(pMsg->header.magic, "ABCD", 4);
}

uint32_t
SizeofPayload(
    MESG::HEADER::Codes code
)
{
    uint32_t payloadSize = 0;
    if (code == MESG::HEADER::Codes::I) { payloadSize += sizeof(IDENTIFY); }
    if (code == MESG::HEADER::Codes::G) { payloadSize += sizeof(GRANT); }
    if (code == MESG::HEADER::Codes::D) { payloadSize += sizeof(DENY); }
    if (code == MESG::HEADER::Codes::R) { payloadSize += sizeof(READY); }
    if (code == MESG::HEADER::Codes::S) { payloadSize += sizeof(START); }
    if (code == MESG::HEADER::Codes::U) { payloadSize += sizeof(UPDATE); }
    if (code == MESG::HEADER::Codes::L) { payloadSize += sizeof(LEAVE); }
    return payloadSize;
}

uint64_t
InitializePacket(
    Packet & packet,
    Address & who,
    MESG::HEADER::Codes code,
    uint32_t id,
    uint32_t traits
)
{
    MESG* pMsg = (MESG*)packet.buffer;
    packet.buffersize = sizeof(MESG::HEADER) + SizeofPayload(code);
    packet.address = who;

    memcpy(pMsg->header.magic, "ABCD", 4);
    pMsg->header.code = (uint32_t)code;
    pMsg->header.id = id;
    pMsg->header.traits = traits;
    pMsg->header.seq = 0;
    pMsg->header.crc = 0;
    return 0;
}


bool
IsCode(
    Packet & packet,
    MESG::HEADER::Codes code
)
{
    bool ret = false;
    MESG* RxMsg = (MESG*)packet.buffer;
    if (RxMsg->header.code == (uint32_t)code)
    {
        ret = true;
    }
    return ret;
}

bool
IsMagicGood(
    Packet & packet
)
{
    bool ret = false;
    if (packet.buffer[0] == 65 &&
        packet.buffer[1] == 66 &&
        packet.buffer[2] == 67 &&
        packet.buffer[3] == 68)
    {
        ret = true;
    }
    return ret;
}

bool
IsSizeValid(
    Packet & packet
)
{
    bool ret = false;
    MESG* RxMsg = (MESG*)packet.buffer;
    if (RxMsg->header.code == (uint64_t)(MESG::HEADER::Codes::I))
    {
        if (packet.buffersize >= sizeof(IDENTIFY))
        {
            ret = true;
        }
    }
    else if (RxMsg->header.code == (uint64_t)(MESG::HEADER::Codes::G))
    {
        if (packet.buffersize >= sizeof(GRANT))
        {
            ret = true;
        }
    }
    else  if (RxMsg->header.code == (uint64_t)(MESG::HEADER::Codes::D))
    {
        if (packet.buffersize >= sizeof(DENY))
        {
            ret = true;
        }
    }
    else  if (RxMsg->header.code == (uint64_t)(MESG::HEADER::Codes::R))
    {
        if (packet.buffersize >= sizeof(READY))
        {
            ret = true;
        }
    }
    else if (RxMsg->header.code == (uint64_t)(MESG::HEADER::Codes::S))
    {
        if (packet.buffersize >= sizeof(START))
        {
            ret = true;
        }
    }
    else if (RxMsg->header.code == (uint64_t)(MESG::HEADER::Codes::U))
    {
        if (packet.buffersize >= sizeof(UPDATE))
        {
            ret = true;
        }
    }
    else if (RxMsg->header.code == (uint64_t)(MESG::HEADER::Codes::L))
    {
        if (packet.buffersize >= sizeof(LEAVE))
        {
            ret = true;
        }
    }
    else if (RxMsg->header.code == (uint64_t)(MESG::HEADER::Codes::PING))
    {
        if (packet.buffersize >= sizeof(PING))
        {
            ret = true;
        }
    }
    else if (RxMsg->header.code == (uint64_t)(MESG::HEADER::Codes::PONG))
    {
        if (packet.buffersize >= sizeof(PING))
        {
            ret = true;
        }
    }
    return ret;
}


bool
IsGood2(
    MESG::HEADER::Codes code,
    Packet & packet
)
{
    bool ret = false;

    {
        if (IsMagicGood(packet))
        {
            if (IsSizeValid(packet))
            {
                if (IsCode(packet, code))
                {
                    //ConnMan::processWaitForIdentify(cmstate, connection, packet);
                    //std::cout << "Rx Identify.\n";
                    ret = true;
                }
                else
                {
                    // Not the Code we're looking for
                    std::cout << "Rx: Weird, packet not expected\n";
                }
            }
            else
            {
                // Invalid - Size does not match code
                std::cout << "Rx: Packet Bad Size\n";
            }
        }
        else
        {
            // Invalid - No Magic
            std::cout << "Rx: Packet Bad Magic\n";
        }
    }
    return ret;
}

bool
GetConnection(
    std::vector<Connection> & connections,
    uint32_t id,
    Connection** connection
)
{
    bool found = false;
    //for (auto & c : connections)
    for (auto i = 0; i < connections.size(); i++)
    {
        if (id == connections[i].id)
        {
            *connection = &connections[i];
            found = true;
            break;
        }
    }
    return found;
}


uint32_t
GetConnectionId(
    Packet & packet
)
{
    MESG* RxMsg = (MESG*)packet.buffer;
    return RxMsg->header.id;
}

bool
IsExpectsAck(
    Packet & packet
)
{
    bool expectation = false;
    MESG* RxMsg = (MESG*)packet.buffer;
    if (RxMsg->header.traits & (1ul << (uint32_t)MESG::HEADER::Traits::ACK))
    {
        expectation = true;
    }
    return expectation;
}

bool
IsPlayerNameAvailable(
    ConnManState & cmstate,
    std::string name
)
{
    bool available = true;
    for (auto c : cmstate.connections)
    {
        if (c.playername == name)
        {
            available = false;
            break;
        }
    }
    return available;
}

void
PrintMsgHeader(
    Packet & packet,
    bool rx
)
{
    MESG* pDbgMsg = (MESG*)packet.buffer;
    if (rx){std::cout << "[Rx]\n";}
    else{std::cout << "[Tx]\n";}

    std::cout << "  Magic:  " << std::hex << (uint8_t)pDbgMsg->header.magic[0]
                            << std::hex << (uint8_t)pDbgMsg->header.magic[1]
                            << std::hex << (uint8_t)pDbgMsg->header.magic[2]
                            << std::hex << (uint8_t)pDbgMsg->header.magic[3] << "\n";

    switch (pDbgMsg->header.code)
    {
        case (uint32_t)MESG::HEADER::Codes::I:
            std::cout << "  Code: Identify\n";
            break;
        case (uint32_t)MESG::HEADER::Codes::G:
            std::cout << "  Code: Grant\n";
            break;
        case (uint32_t)MESG::HEADER::Codes::D:
            std::cout << "  Code: Deny\n";
            break;
        case (uint32_t)MESG::HEADER::Codes::R:
            std::cout << "  Code: Ready\n";
            break;
        case (uint32_t)MESG::HEADER::Codes::S:
            std::cout << "  Code: Start\n";
            break;
        case (uint32_t)MESG::HEADER::Codes::U:
            std::cout << "  Code: Update\n";
            break;
    }
    //std::cout << "\tCode:   " << std::hex << pDbgMsg->header.code << "\n";
    std::cout << "  Id:     " << std::hex << pDbgMsg->header.id << "\n";
    std::cout << "  Traits: " << std::hex << pDbgMsg->header.traits << "\n";
    std::cout << "  Seq:    " << std::hex << pDbgMsg->header.seq << "\n";
    std::cout << "  Crc:    " << std::hex << pDbgMsg->header.crc << "\n";
}


bool
GetConnection(
    std::vector<Connection> & connections,
    std::string playername,
    Connection** connection
)
{
    bool found = false;
    //for (auto & c : connections)
    for (auto i = 0; i < connections.size(); i++)
    {
        if (playername == connections[i].playername)
        {
            *connection = &connections[i];
            found = true;
            break;
        }
    }
    return found;
}

std::string
GetPlayerName(
    Packet & packet
)
{
    MESG* pMsg = (MESG*)packet.buffer;
    return std::string(pMsg->payload.grant.playername,
                       strlen(pMsg->payload.grant.playername));
}

} // end namespace bali