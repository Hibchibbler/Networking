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
    if (code == MESG::HEADER::Codes::Identify) { payloadSize += sizeof(IDENTIFY); }
    if (code == MESG::HEADER::Codes::Grant) { payloadSize += sizeof(GRANT); }
    if (code == MESG::HEADER::Codes::Deny) { payloadSize += sizeof(DENY); }
    if (code == MESG::HEADER::Codes::Ready) { payloadSize += sizeof(READY); }
    if (code == MESG::HEADER::Codes::Start) { payloadSize += sizeof(START); }
    if (code == MESG::HEADER::Codes::General) { payloadSize += sizeof(UPDATE); }
    if (code == MESG::HEADER::Codes::Leave) { payloadSize += sizeof(LEAVE); }
    if (code == MESG::HEADER::Codes::LobbyUpdate) { payloadSize += sizeof(LOBBYUPDATE); }
    return payloadSize;
}

uint64_t
InitializePacket(
    Packet & packet,
    Address & who,
    MESG::HEADER::Codes code,
    uint32_t id,
    uint32_t traits,
    uint32_t seq
)
{
    MESG* pMsg = (MESG*)packet.buffer;
    packet.buffersize = sizeof(MESG::HEADER) + SizeofPayload(code);
    packet.address = who;

    memcpy(pMsg->header.magic, "ABCD", 4);
    pMsg->header.code = (uint32_t)code;
    pMsg->header.id = id;
    pMsg->header.traits = traits;
    pMsg->header.seq = seq;
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

    if (packet.buffersize >= sizeof(MESG::HEADER) + SizeofPayload((MESG::HEADER::Codes)RxMsg->header.code))
    {
        ret = true;
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
GetConnectionById(
    std::list<Connection> & connections,
    uint32_t id,
    Connection** connection
)
{
    bool found = false;
    //for (auto & c : connections)
    //for (auto i = 0; i < connections.size(); i++)
    for (auto & c : connections)
    {
        if (id == c.id)
        {
            *connection = &c;
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
        case (uint32_t)MESG::HEADER::Codes::Identify:
            std::cout << "  Code: Identify\n";
            break;
        case (uint32_t)MESG::HEADER::Codes::Grant:
            std::cout << "  Code: Grant\n";
            break;
        case (uint32_t)MESG::HEADER::Codes::Deny:
            std::cout << "  Code: Deny\n";
            break;
        case (uint32_t)MESG::HEADER::Codes::Ready:
            std::cout << "  Code: Ready\n";
            break;
        case (uint32_t)MESG::HEADER::Codes::Start:
            std::cout << "  Code: Start\n";
            break;
        case (uint32_t)MESG::HEADER::Codes::General:
            std::cout << "  Code: Update\n";
            break;
        default:
            std::cout << "  Code: Something or nother\n";
            break;
    }
    //std::cout << "\tCode:   " << std::hex << pDbgMsg->header.code << "\n";
    std::cout << "  Id:     " << std::hex << pDbgMsg->header.id << "\n";
    std::cout << "  Traits: " << std::hex << pDbgMsg->header.traits << "\n";
    std::cout << "  Seq:    " << std::hex << pDbgMsg->header.seq << "\n";
    std::cout << "  Crc:    " << std::hex << pDbgMsg->header.crc << "\n";
}

bool
RemoveConnectionByName(
    std::list<Connection> & connections,
    std::string playername
)
{
    bool found = false;
    for (auto c = connections.begin(); c != connections.end(); c++)
    {
        if (playername == c->playername)
        {
            c = connections.erase(c);
            found = true;
            break;
        }
    }
    return found;
}
bool
GetConnectionByName(
    std::list<Connection> & connections,
    std::string playername,
    Connection** connection
)
{
    bool found = false;
    //for (auto i = 0; i < connections.size(); i++)
    for (auto & c : connections)
    {
        //if (playername == connections[i].playername)
        if (playername == c.playername)
        {
            *connection = &c;
            found = true;
            break;
        }
    }
    return found;
}

MESG*
GetMesg(
    Packet & packet
)
{
    MESG* mesg = (MESG*)packet.buffer;
    return mesg;
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

void
PrintfMsg(
    char* szFormat,
    ...
)
{
    char buffer[1024];
    va_list pArgList;
    va_start (pArgList, szFormat);
    vsprintf(buffer, szFormat, pArgList);
    va_end(pArgList);

    std::cout << buffer << std::endl;
}

Address
CreateAddress(
    uint32_t port,
    const char* szIpv4
)
{
    Address address;
    address.addr.ss_family = AF_INET;
    ((sockaddr_in*)&address.addr)->sin_port = htons(port);
    ((sockaddr_in*)&address.addr)->sin_addr.S_un.S_addr = inet_addr(szIpv4);
    return address;
}

std::string
GetGameName(
    Packet & packet
)
{
    MESG* pMsg = (MESG*)packet.buffer;
    return std::string(pMsg->payload.identify.gamename,
                       strlen(pMsg->payload.identify.gamename));
}

std::string
GetGamePass(
    Packet & packet
)
{
    MESG* pMsg = (MESG*)packet.buffer;
    return std::string(pMsg->payload.identify.gamepass,
                       strlen(pMsg->payload.identify.gamepass));
}


} // end namespace bali