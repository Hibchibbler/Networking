#include "ConnMan.h"
#include <sstream>

namespace bali
{
void
ConnMan::initialize(
    ConnManState & cmstate,
    uint32_t port,
    uint32_t numplayers,
    std::string gamename,
    std::string gamepass
)
{
    srand(1523791);

    cmstate.port = port;
    cmstate.numplayers = numplayers;
    cmstate.gamename = gamename;
    cmstate.gamepass = gamepass;

    cmstate.CurrentConnectionId = 13;
    cmstate.done = 0;
    cmstate.cmmutex.create();

    Network::initialize(cmstate.netstate, 8, port, ConnMan::ConnManIOHandler, &cmstate);
    Network::start(cmstate.netstate);
}


uint64_t
ConnMan::readyCount(
    ConnManState & cmstate
)
{
    uint64_t cnt = 0;
    cmstate.cmmutex.lock();
    for (auto c : cmstate.connections)
    {
        if (c.state == Connection::State::WAITFORSTART)
        {
            cnt++;
        }
    }
    cmstate.cmmutex.unlock();
    return cnt;
}

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


uint64_t
ConnMan::sendStart(
    ConnManState & cmstate
)
{
    uint64_t cnt = 0;
    Packet startPacket;
    uint32_t traits = 0;
    cmstate.cmmutex.lock();
    for (auto c : cmstate.connections)
    {
        if (c.state == Connection::State::WAITFORSTART)
        {
            InitializePacket(startPacket,
                             c.who,
                             MESG::HEADER::Codes::S,
                             c.id,
                             traits);

            Network::write(cmstate.netstate, startPacket);
        }
    }
    cmstate.cmmutex.unlock();
    return 0;
}

bool
ConnMan::NameExists(
    ConnManState & state,
    std::string name
)
{
    bool res = false;
    for (auto & c : state.connections)
    {
        if (c.name == name)
        {
            res = true;
            break;
        }
    }
    return res;
}

bool
isCode(
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
magicMatch(
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
sizeValid(
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
    else
    if (RxMsg->header.code == (uint64_t)(MESG::HEADER::Codes::G))
    {
        if (packet.buffersize >= sizeof(GRANT))
        {
            ret = true;
        }
    }
    else
    if (RxMsg->header.code == (uint64_t)(MESG::HEADER::Codes::D))
    {
        if (packet.buffersize >= sizeof(DENY))
        {
            ret = true;
        }
    }
    else
    if (RxMsg->header.code == (uint64_t)(MESG::HEADER::Codes::R))
    {
        if (packet.buffersize >= sizeof(READY))
        {
            ret = true;
        }
    }
    else
    if (RxMsg->header.code == (uint64_t)(MESG::HEADER::Codes::S))
    {
        if (packet.buffersize >= sizeof(START))
        {
            ret = true;
        }
    }
    else
    if (RxMsg->header.code == (uint64_t)(MESG::HEADER::Codes::U))
    {
        if (packet.buffersize >= sizeof(UPDATE))
        {
            ret = true;
        }
    }
    else
    if (RxMsg->header.code == (uint64_t)(MESG::HEADER::Codes::L))
    {
        if (packet.buffersize >= sizeof(LEAVE))
        {
            ret = true;
        }
    }
    return ret;
}

bool
ConnMan::processWaitForIdentify(
    ConnManState & state,
    Connection & connection,
    Packet & packet
)
{
    bool ret = false;
    //MESG* RxMsg = (MESG*)packet.buffer;


    //// Store Credentials
    //connection.name = std::string(RxMsg->payload.identify.name, strlen(RxMsg->payload.identify.name));
    //connection.pass = std::string(RxMsg->payload.identify.pass, strlen(RxMsg->payload.identify.pass));
    //if (state.gamepass == connection.pass)
    //{
    //    connection.state = Connection::State::SENDGRANT;
    //}
    //else
    //{
    //    connection.state = Connection::State::SENDDENY;
    //}
    //ret = true;

    return ret;
}
bool
ConnMan::processSendGrant(
    ConnManState & state,
    Connection & connection
)
{
    bool ret = false;
    // Send a Grant
    Packet packet;
    InitializePacket(packet,
                     connection.who,
                     MESG::HEADER::Codes::G,
                     connection.id,
                     0);

    Network::write(state.netstate, packet);
    connection.state = Connection::State::WAITFORREADY;
    return ret;
}

bool
ConnMan::processSendDeny(
    ConnManState & state,
    Connection & connection
)
{
    bool ret = false;
    // Send a Deny
    Packet packet;
    InitializePacket(packet,
                     connection.who,
                     MESG::HEADER::Codes::D,
                     0,
                     0);

    Network::write(state.netstate, packet);
    connection.state = Connection::State::WAITFORIDENTIFY;
    return ret;
}

bool
ConnMan::processWaitForReady(
    ConnManState & state,
    Connection & connection,
    Packet & packet
)
{
    bool ret = false;
    if (isCode(packet, MESG::HEADER::Codes::R))
    {
        connection.state = Connection::State::WAITFORSTART;
        ret = true;
    }
    return ret;
}

bool
ConnMan::processWaitForStart(
    ConnManState & state,
    Connection & connection
)
{
    bool ret = false;
    return ret;
}

bool
ConnMan::processSendStart(
    ConnManState & state,
    Connection & connection
)
{
    bool ret = false;
    return ret;
}

bool
ConnMan::processGaming(
    ConnManState & state,
    Connection & connection,
    Packet & packet
)
{
    return false;
}

bool
isGood(
    ConnManState & cmstate,
    Connection & connection,
    MESG::HEADER::Codes code,
    Packet & packet
)
{
    bool ret = false;
    if (connection.packets.size() > 0)
    {
        packet = connection.packets.front();

        if (magicMatch(packet))
        {
            if (sizeValid(packet))
            {
                if (isCode(packet, code))
                {
                    //ConnMan::processWaitForIdentify(cmstate, connection, packet);
                    //std::cout << "Rx Identify.\n";
                    ret = true;
                }
                else
                {
                    // Not the Code we're looking for
                    std::cout << "Rx: Weird, packet not expected\n";
                    connection.packets.pop();
                }
            }
            else
            {
                // Invalid - Size does not match code
                std::cout << "Rx: Packet Bad Size\n";
                connection.packets.pop();
            }
        }
        else
        {
            // Invalid - No Magic
            std::cout << "Rx: Packet Bad Magic\n";
            connection.packets.pop();
        }
    }
    return ret;
}

bool
isGood2(
    MESG::HEADER::Codes code,
    Packet & packet
)
{
    bool ret = false;
    
    {
        if (magicMatch(packet))
        {
            if (sizeValid(packet))
            {
                if (isCode(packet, code))
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
getConnection(
    ConnManState & cmstate,
    Packet & packet,
    Connection & connection
)
{
    bool found = false;
    for (auto & c : cmstate.connections)
    {
        MESG* pMsg = (MESG*)packet.buffer;
        if (pMsg->header.id == connection.id)
        {
            connection = c;
            found = true;
            break;
        }
    }
    return found;
}

void
ConnMan::updateServer(
    ConnManState & cmstate,
    uint32_t ms_elapsed
)
{

    cmstate.timeticks += ms_elapsed;

    if (cmstate.timeticks > 90)
    {
        cmstate.timeticks = 0;
        cmstate.cmmutex.lock();

        //
        // Process new packets
        //
        for (size_t pi = 0; pi < cmstate.packets.size();pi++)
        {
            Packet & packet = cmstate.packets[pi];
            Connection connection;
            if (getConnection(cmstate, packet, connection))
            {
                
            }
            else
            {
                // Problem: Packet contains an unknown ID
            }
        }

        //
        // Update Connection States
        //

        cmstate.cmmutex.unlock();
    }
}

void
ConnMan::cleanup(
    ConnManState & state
)
{
    Network::stop(state.netstate);
    state.cmmutex.destroy();
}

bool
ConnMan::AddressKnown(
    ConnManState & state,
    Address* address,
    size_t* index
)
{
    for (*index = 0; *index < state.connections.size();*index++)
    {
        if (memcmp(&address->addr, &state.connections[*index].who.addr, sizeof(SOCKADDR_STORAGE)) == 0)
        {
            return true;
        }
    }
    return false;
}

bool
ConnMan::AddressAuthorized(
    ConnManState & state,
    Address* address
)
{
    return false;
}

uint64_t
getConnectionId(
    Packet & packet
)
{
    MESG* RxMsg = (MESG*)packet.buffer;
    return RxMsg->header.id;
}

bool
expectsAck(
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
playerNameAvailable(
    ConnManState & cmstate,
    std::string name
)
{
    bool available = false;
    for (auto c : cmstate.connections)
    {
        if (c.playername == name)
        {
            available = true;
            break;
        }
    }
    return available;
}


void
ConnMan::ConnManIOHandler(
    void* cmstate_,
    Request* request,
    uint64_t id
)
{
    bali::Network::Result result(bali::Network::ResultType::SUCCESS);
    //IoHandlerContext* c = (IoHandlerContext*)cm->handlercontext;
    ConnManState* cmstate = (ConnManState*)cmstate_;
    if (request->ioType == Request::IOType::READ)
    {
        // Debug Print buffer
        std::string payloadAscii((PCHAR)request->packet.buffer, request->packet.buffersize);
        std::cout << "[Rx][" << id << "][" << request->packet.buffersize << "]" << payloadAscii.c_str() << std::endl;

        /*
            If we recieve an Identify packet
            enqueue packet.
        */

        cmstate->cmmutex.lock();
        if (magicMatch(request->packet))
        {
            if (sizeValid(request->packet))
            {
                cmstate->packets.push_back(request->packet);
                if (isCode(request->packet, MESG::HEADER::Codes::I))
                {
                    // Rx'd an IDENTIFY packet.
                    // Therefore, an ID hasn't been
                    // generated yet - "Connection" not
                    // established.
                    MESG* RxMsg = (MESG*)request->packet.buffer;
                    if (strncmp(RxMsg->payload.identify.gamename, cmstate->gamename.c_str(), strlen(RxMsg->payload.identify.gamename)))
                    {
                        if (strncmp(RxMsg->payload.identify.gamepass, cmstate->gamepass.c_str(), strlen(RxMsg->payload.identify.gamepass)))
                        {
                            // Grant
                            Connection connection;
                            connection.playername = std::string(RxMsg->payload.identify.playername, strlen(RxMsg->payload.identify.playername));
                            if (playerNameAvailable(*cmstate, connection.playername))
                            {
                                connection.gamename = std::string(RxMsg->payload.identify.gamename, strlen(RxMsg->payload.identify.gamename));
                                connection.gamepass = std::string(RxMsg->payload.identify.gamepass, strlen(RxMsg->payload.identify.gamepass));
                                connection.id = rand() % 255;
                                connection.who = request->packet.address;
                                connection.state = Connection::State::WAITFORREADY;
                                cmstate->connections.push_back(connection);


                                Packet packet;
                                InitializePacket(packet,
                                                 request->packet.address,
                                                 MESG::HEADER::Codes::G,
                                                 connection.id,
                                                 0);

                                Network::write(cmstate->netstate, packet);
                                std::cout << "[New]";
                            }
                            else
                            {
                                // Deny - player name already exists
                                Packet packet;
                                InitializePacket(packet,
                                                 request->packet.address,
                                                 MESG::HEADER::Codes::D,
                                                 0,
                                                 0);

                                Network::write(cmstate->netstate, packet);
                            }
                        }
                        else
                        {
                            // Deny - password doesn't match
                            Packet packet;
                            InitializePacket(packet,
                                             request->packet.address,
                                             MESG::HEADER::Codes::D,
                                             0,
                                             0);

                            Network::write(cmstate->netstate, packet);
                        }
                    }
                    else
                    {
                        // Deny - Game name doesn't match
                        Packet packet;
                        InitializePacket(packet,
                                         request->packet.address,
                                         MESG::HEADER::Codes::D,
                                         0,
                                         0);

                        Network::write(cmstate->netstate, packet);
                    }
                }
                else
                {
                    // Not an IDENTIFY packet.
                    // Therefore, it should contain 
                    // a valid ID -- "Connection" should
                    // already be established.
                    cmstate->packets.push_back(request->packet);

                    if (expectsAck(request->packet))
                    {
                        Packet packet;
                        InitializePacket(packet,
                                         request->packet.address,
                                         MESG::HEADER::Codes::A,
                                         ((MESG*)request->packet.buffer)->header.id,
                                         0);
                        Network::write(cmstate->netstate, packet);
                    }
                }
            }
            else
            {
                // Size does not meet expectations, given "request->header.code"
                std::cout << "Rx Packet with incoherent Size." << std::endl;
            }
        }
        else
        {
            // Magic MisMatch
            std::cout << "Rx Packet with incoherent Magic." << std::endl;
        }
        cmstate->cmmutex.unlock();

        // Prepare read to perpetuate.
        // TODO: what happens when no more free requests?
        Network::read(cmstate->netstate);
    }
    else if (request->ioType == Request::IOType::WRITE)
    {
        // Debug Print buffer
        std::string payloadAscii((PCHAR)request->packet.buffer, request->packet.buffersize);
        std::cout << "[Tx][" << id << "][" << request->packet.buffersize << "]" << payloadAscii.c_str() << std::endl;
    }
    return;
}
} // namespace bali