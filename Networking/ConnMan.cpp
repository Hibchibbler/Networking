#include "ConnMan.h"
#include "ConnManUtil.h"
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


uint64_t
ConnMan::sendStart(
    ConnManState & cmstate
)
{
    uint64_t cnt = 0;
    Packet startPacket;
    uint32_t traits = 0;
    cmstate.cmmutex.lock();
    for (auto & c : cmstate.connections)
    {
        if (c.state == Connection::State::WAITFORSTART)
        {
            InitializePacket(startPacket,
                             c.who,
                             MESG::HEADER::Codes::S,
                             c.id,
                             traits);

            Network::write(cmstate.netstate, startPacket);
            c.state = Connection::State::GENERAL;
        }
    }
    cmstate.cmmutex.unlock();
    return 0;
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
            Connection* connection = nullptr;
            MESG* pMsg = (MESG*)packet.buffer;
            if (GetConnection(cmstate.connections, pMsg->header.id, &connection))
            {
                std::cout << "Update: " << connection->playername << "\n";
                
            }
            else
            {
                // Problem: Packet contains an unknown ID
                std::cout << "Problem: Packet contains an unknown ID\n";
            }
        }
        cmstate.packets.clear();/// ok...scary

        //
        // Update Connection States
        //
        for (auto & c : cmstate.connections)
        {
            if (c.state == Connection::State::WAITFORSTART)
            {
                // We need to send a Lobby Update to everyone in the lobby
            }
        }

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

uint64_t
ConnMan::sendIdentifyTo(
    ConnManState & cmstate,
    Address to,
    std::string playername,
    std::string gamename,
    std::string gamepass
)
{
    Packet ipacket;
    uint32_t traits = 0;

    InitializePacket(ipacket,
                        to,
                        MESG::HEADER::Codes::I,
                        0,
                        traits);

    MESG* pMsg = (MESG*)ipacket.buffer;
    memcpy(pMsg->payload.identify.playername,
            playername.c_str(),
            playername.size());

    memcpy(pMsg->payload.identify.gamename,
            gamename.c_str(),
            gamename.size());

    memcpy(pMsg->payload.identify.gamepass,
            gamepass.c_str(),
            gamepass.size());

    Network::write(cmstate.netstate, ipacket);

    //
// Grant
// Create and insert a new connection, 
// randomly generate id, then send a Grant.
//
    Connection connection;
    connection.playername = playername;
    connection.gamename = gamename;
    connection.gamepass = gamepass;

    connection.id = 0;

    connection.state = Connection::State::WAITFORGRANTDENY;
    cmstate.connections.push_back(connection);
    return 0;
}

uint64_t
ConnMan::sendGrantTo(
    ConnManState & cmstate,
    Address to,
    uint32_t id,
    std::string playername
)
{
    Packet ipacket;
    uint32_t traits = 0;

    InitializePacket(ipacket,
                     to,
                     MESG::HEADER::Codes::G,
                     id,
                     traits);

    MESG* pMsg = (MESG*)ipacket.buffer;

    memcpy(pMsg->payload.identify.playername,
           playername.c_str(),
           playername.size());

    Network::write(cmstate.netstate, ipacket);

    return 0;
}

uint64_t
ConnMan::sendDenyTo(
    ConnManState & cmstate,
    Address to,
    std::string playername
)
{
    Packet ipacket;
    uint32_t traits = 0;

    InitializePacket(ipacket,
                     to,
                     MESG::HEADER::Codes::D,
                     0,
                     traits);

    MESG* pMsg = (MESG*)ipacket.buffer;

    memcpy(pMsg->payload.identify.playername,
           playername.c_str(),
           playername.size());

    Network::write(cmstate.netstate, ipacket);

    return 0;
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
        PrintMsgHeader(request->packet, true);
        /*
            If we recieve an Identify packet,
            and we are accepting more players,
            and the packet makes sense, Grant.
            Otherwise Deny.

            Packets not involved in the initial handshake,
            and that are from an identified client,
            will be enqueued to the packet queue.
        */

        cmstate->cmmutex.lock();
        if (IsMagicGood(request->packet))
        {
            if (IsSizeValid(request->packet))
            {
                if (IsCode(request->packet, MESG::HEADER::Codes::I))
                {
                    // Rx'd an IDENTIFY packet.
                    // Therefore, an ID hasn't been
                    // generated yet - "Connection" not
                    // established.
                    if (cmstate->connections.size() < cmstate->numplayers)
                    {
                        MESG* RxMsg = (MESG*)request->packet.buffer;
                        if (strncmp(RxMsg->payload.identify.gamename,
                                    cmstate->gamename.c_str(),
                                    strlen(RxMsg->payload.identify.gamename)) == 0)
                        {
                            if (strncmp(RxMsg->payload.identify.gamepass,
                                        cmstate->gamepass.c_str(),
                                        strlen(RxMsg->payload.identify.gamepass)) == 0)
                            {
                                Connection connection;
                                connection.playername = std::string(RxMsg->payload.identify.playername,
                                                                    strlen(RxMsg->payload.identify.playername));

                                if (IsPlayerNameAvailable(*cmstate, connection.playername))
                                {
                                    //
                                    // Grant
                                    // Create and insert a new connection, 
                                    // randomly generate id, then send a Grant.
                                    //

                                    connection.gamename = std::string(RxMsg->payload.identify.gamename,
                                                                      strlen(RxMsg->payload.identify.gamename));
                                    connection.gamepass = std::string(RxMsg->payload.identify.gamepass,
                                                                      strlen(RxMsg->payload.identify.gamepass));
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
                                    MESG* pMsg = (MESG*)packet.buffer;
                                    memcpy(pMsg->payload.grant.playername, connection.playername.c_str(), connection.playername.size());

                                    Network::write(cmstate->netstate, packet);
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
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::G))
                {
                    bool ret;
                    std::string playername;
                    Connection * pConn = nullptr;

                    playername = GetPlayerName(request->packet);
                    ret = GetConnection(cmstate->connections, playername, &pConn);
                    if (ret)
                    {
                        if (pConn->state == Connection::State::WAITFORGRANTDENY)
                        {
                            pConn->state = Connection::State::WAITFORREADY;
                            pConn->id = GetConnectionId(request->packet);
                        }
                        else
                        {
                            // Weird: We're not supposed to recieve U right now.
                            std::cout << "Weird: We're not in WAITFORGRANTDENY, but received G.\n";
                        }
                    }
                    else
                    {
                        // Problem: request->packet id is not known
                        std::cout << "Problem: Grant packet id is not known\n";
                    }
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::D))
                {

                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::R))
                {
                    bool ret;
                    uint32_t c;
                    Connection * pConn = nullptr;

                    c = GetConnectionId(request->packet);
                    ret = GetConnection(cmstate->connections, c, &pConn);
                    if (ret)
                    {
                        if (pConn->state == Connection::State::WAITFORREADY)
                        {
                            pConn->state = Connection::State::WAITFORSTART;
                        }
                        else
                        {
                            // Weird: We're not supposed to recieve U right now.
                            std::cout << "Weird: We're not in WAITFORREADY, but received R.\n";
                        }
                    }
                    else
                    {
                        // Problem: request->packet id is not known
                        std::cout << "Problem: Ready packet id is not known\n";
                    }
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::S))
                {
                    bool ret;
                    uint32_t c;
                    Connection * pConn = nullptr;

                    c = GetConnectionId(request->packet);
                    ret = GetConnection(cmstate->connections, c, &pConn);
                    if (ret)
                    {
                        if (pConn->state == Connection::State::WAITFORSTART)
                        {
                            pConn->state = Connection::State::GENERAL;
                        }
                        else
                        {
                            // Weird: We're not supposed to recieve U right now.
                            std::cout << "Weird: We're not in WAITFORSTART, but received S.\n";
                        }
                    }
                    else
                    {
                        // Problem: request->packet id is not known
                        std::cout << "Problem: Start packet id is not known\n";
                    }
                }
                else if(IsCode(request->packet, MESG::HEADER::Codes::U))
                {
                    // Not an IDENTIFY packet.
                    // Therefore, it should contain 
                    // a valid ID -- "Connection" should
                    // already be established.
                    bool ret;
                    uint32_t c;
                    Connection * pConn = nullptr;

                    c = GetConnectionId(request->packet);
                    ret = GetConnection(cmstate->connections, c, &pConn);
                    if (ret)
                    {
                        if (pConn->state == Connection::State::GENERAL)
                        {
                            cmstate->packets.push_back(request->packet);
                        }
                        else
                        {
                            // Weird: We're not supposed to recieve U right now.
                            std::cout << "Weird: We're not in GENERAL, but recieved G.\n";
                        }
                    }
                    else
                    {
                        // Problem: request->packet id is not known
                        std::cout << "Problem: Update packet id is not known\n";
                    }
                }

                if (IsExpectsAck(request->packet))
                {
                    Packet packet;
                    InitializePacket(packet,
                        request->packet.address,
                        MESG::HEADER::Codes::A,
                        ((MESG*)request->packet.buffer)->header.id,
                        0);
                    Network::write(cmstate->netstate, packet);
                    std::cout << "Acking\n";
                }
            }
            else
            {
                // Size does not meet expectations, given "request->header.code"
                std::cout << "Rx Packet with incoherent Code or Size." << std::endl;
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
        PrintMsgHeader(request->packet, false);
    }
    return;
}

uint64_t
ConnMan::sendReadyTo(
    ConnManState & cmstate,
    Address to,
    uint32_t id
)
{
    Packet ipacket;
    uint32_t traits = 0;

    InitializePacket(ipacket,
                     to,
                     MESG::HEADER::Codes::R,
                     id,
                     traits);

    Network::write(cmstate.netstate, ipacket);

    return 0;
}

} // namespace bali