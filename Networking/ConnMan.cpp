#include "ConnMan.h"
#include "ConnManUtil.h"
#include <sstream>
#include <random>

namespace bali
{
void
ConnMan::initialize(
    ConnManState & cmstate,
    uint32_t port,
    uint32_t numplayers,
    std::string gamename,
    std::string gamepass,
    ConnManState::OnEvent onevent,
    ConnManState::OnUpdate onupdate
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

    cmstate.onevent = onevent;
    cmstate.onupdate = onupdate;

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
                             MESG::HEADER::Codes::Start,
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

    if (cmstate.timeticks > 0)
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
                cmstate.onevent((void*)&cmstate);
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
        }

        //cmstate.onupdate((void*)&cmstate);

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
ConnMan::sendPingTo(
    ConnManState & cmstate,
    Address to,
    uint32_t id
)
{
    Packet packet;
    uint32_t traits = 0;

    InitializePacket(packet,
        to,
        MESG::HEADER::Codes::PING,
        id,
        traits);

    Connection* pConn;
    if (GetConnection(cmstate.connections, id, &pConn))
    {
        pConn->starttime = clock::now();
    }
    Network::write(cmstate.netstate, packet);

    return 0;
}

uint64_t
ConnMan::sendPongTo(
    ConnManState & cmstate,
    Address to,
    uint32_t id
)
{
    Packet packet;
    uint32_t traits = 0;

    InitializePacket(packet,
        to,
        MESG::HEADER::Codes::PONG,
        id,
        traits);

    Network::write(cmstate.netstate, packet);

    return 0;
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
                        MESG::HEADER::Codes::Identify,
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
    // Let's put a new connection into the list
    //
    Connection connection;
    connection.playername = playername;

    // But we don't know it's ID yet
    // because we have not yet been GRANTed
    connection.id = 0;
    connection.state = Connection::State::WAITFORGRANTDENY;

    cmstate.connections.push_back(connection);

    return 0;
}
uint64_t
ConnMan::sendUpdateTo(
    ConnManState & cmstate,
    Address to,
    uint32_t id,
    UPDATE & update
)
{
    Packet packet;
    uint32_t traits = 0;

    InitializePacket(packet,
        to,
        MESG::HEADER::Codes::General,
        id,
        traits);

    MESG* pMsg = (MESG*)packet.buffer;
    memcpy(&pMsg->payload.update, &update,sizeof(update));
    Network::write(cmstate.netstate, packet);
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
    Packet packet;
    uint32_t traits = 0;

    InitializePacket(packet,
                     to,
                     MESG::HEADER::Codes::Grant,
                     id,
                     traits);

    MESG* pMsg = (MESG*)packet.buffer;

    memcpy(pMsg->payload.identify.playername,
           playername.c_str(),
           playername.size());

    Network::write(cmstate.netstate, packet);
    //
    // Per Connection state is not available,
    // caller must set that up.
    //
    return 0;
}

uint64_t
ConnMan::sendDenyTo(
    ConnManState & cmstate,
    Address to,
    std::string playername
)
{
    Packet packet;
    uint32_t traits = 0;

    InitializePacket(packet,
                     to,
                     MESG::HEADER::Codes::Deny,
                     0,
                     traits);

    MESG* pMsg = (MESG*)packet.buffer;

    memcpy(pMsg->payload.identify.playername,
           playername.c_str(),
           playername.size());

    Network::write(cmstate.netstate, packet);
    //
    // Per Connection state is not available
    //
    return 0;
}


void
ConnMan::ConnManIOHandler(
    void* cmstate_,
    Request* request,
    uint64_t tid
)
{
    bali::Network::Result result(bali::Network::ResultType::SUCCESS);
    ConnManState& cmstate = *((ConnManState*)cmstate_);
    std::random_device rd;     // only used once to initialise (seed) engine
    std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)
    std::uniform_int_distribution<int> uni(1, 65536); // guaranteed unbiased

    auto random_integer = uni(rng);

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

        cmstate.cmmutex.lock();
        if (IsMagicGood(request->packet))
        {
            if (IsSizeValid(request->packet))
            {
                if (IsCode(request->packet, MESG::HEADER::Codes::Identify))
                {
                    // Rx'd an IDENTIFY packet.
                    // Therefore, an ID hasn't been
                    // generated yet - "Connection" not
                    // established.
                    if (cmstate.connections.size() < cmstate.numplayers)
                    {
                        if (cmstate.gamename == GetGameName(request->packet))
                        {
                            if (cmstate.gamepass == GetGamePass(request->packet))
                            {
                                Connection connection;
                                MESG* RxMsg = (MESG*)request->packet.buffer;
                                connection.playername = GetPlayerName(request->packet);
                                if (IsPlayerNameAvailable(cmstate, connection.playername))
                                {
                                    //
                                    // Grant
                                    // Create and insert a new connection, 
                                    // randomly generate id, then send a Grant.
                                    //
                                    connection.id = random_integer;
                                    connection.who = request->packet.address;
                                    connection.state = Connection::State::WAITFORREADY;
                                    connection.checkintime = clock::now();

                                    cmstate.connections.push_back(connection);

                                    ConnMan::sendGrantTo(cmstate, connection.who, connection.id, connection.playername);
                                }
                                else
                                {
                                    // Deny - player name already exists
                                    ConnMan::sendDenyTo(cmstate,
                                                        request->packet.address,
                                                        GetPlayerName(request->packet));
                                    std::cout << "Deny: Player Name already exists\n";
                                }
                            }
                            else
                            {
                                // Deny - password doesn't match
                                ConnMan::sendDenyTo(cmstate,
                                                    request->packet.address,
                                                    GetPlayerName(request->packet));
                                std::cout << "Deny: Password is bad\n";
                            }
                        }
                        else
                        {
                            // Deny - Game name doesn't match
                            ConnMan::sendDenyTo(cmstate,
                                                request->packet.address,
                                                GetPlayerName(request->packet));
                            std::cout << "Deny: Game name is unknown\n";
                        }
                    }
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::Grant))
                {
                    bool ret;
                    std::string playername;
                    Connection * pConn = nullptr;
                    //
                    // If we're receiving a Grant,
                    // then the associated connection has not yet been assigned an Id.
                    // Therefore, we find the associated connection
                    // by name
                    //
                    playername = GetPlayerName(request->packet);
                    ret = GetConnection(cmstate.connections, playername, &pConn);
                    if (ret)
                    {
                        if (pConn->state == Connection::State::WAITFORGRANTDENY)
                        {
                            pConn->state = Connection::State::GRANTED;
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
                else if (IsCode(request->packet, MESG::HEADER::Codes::Deny))
                {
                    bool ret;
                    std::string playername;
                    Connection * pConn = nullptr;
                    playername = GetPlayerName(request->packet);
                    ret = GetConnection(cmstate.connections, playername, &pConn);
                    if (ret)
                    {
                        if (pConn->state == Connection::State::WAITFORGRANTDENY)
                        {
                            pConn->state = Connection::State::DENIED;
                            pConn->id = GetConnectionId(request->packet);
                        }
                        else
                        {
                            // Weird: We're not supposed to recieve U right now.
                            std::cout << "Weird: We're not in WAITFORGRANTDENY, but received D.\n";
                        }
                    }
                    else
                    {
                        // Problem: request->packet id is not known
                        std::cout << "Problem: Grant packet id is not known\n";
                    }
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::Ready))
                {
                    bool ret;
                    uint32_t c;
                    Connection * pConn = nullptr;
                    //
                    // If we're receiving Ready, then
                    // the associated Connection must already be
                    // set up. Get Connection by Id
                    //
                    c = GetConnectionId(request->packet);
                    ret = GetConnection(cmstate.connections, c, &pConn);
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
                else if (IsCode(request->packet, MESG::HEADER::Codes::Start))
                {
                    bool ret;
                    uint32_t c;
                    Connection * pConn = nullptr;
                    //
                    // If we're receiving Start, then
                    // the associated Connection must already be
                    // set up. Get Connection by Id
                    //
                    c = GetConnectionId(request->packet);
                    ret = GetConnection(cmstate.connections, c, &pConn);
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
                else if(IsCode(request->packet, MESG::HEADER::Codes::General))
                {
                    bool ret;
                    uint32_t c;
                    Connection * pConn = nullptr;

                    //
                    // If we're receiving Updates, than
                    // the associated Connection must already be
                    // set up. Get Connection by Id
                    //
                    c = GetConnectionId(request->packet);
                    ret = GetConnection(cmstate.connections, c, &pConn);
                    if (ret)
                    {
                        if (pConn->state == Connection::State::GENERAL)
                        {
                            pConn->checkintime = clock::now();
                            cmstate.packets.push_back(request->packet);
                        }
                        else
                        {
                            // Weird: We're not supposed to recieve U right now.
                            std::cout << "Weird: We're not in GENERAL, but recieved U.\n";
                        }
                    }
                    else
                    {
                        // Problem: request->packet id is not known
                        std::cout << "Problem: Update packet id is not known\n";
                    }
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::PING))
                {
                    // Just being a good neighbor.
                    ConnMan::sendPongTo(cmstate,request->packet.address,GetConnectionId(request->packet));
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::PONG))
                {
                    // Calculate how long it took.
                    uint32_t id = GetConnectionId(request->packet);
                    Connection* pConn;
                    if (GetConnection(cmstate.connections, id, &pConn))
                    {
                        pConn->endtime = clock::now();
                        pConn->pingtimes.push_back(pConn->endtime - pConn->starttime);
                        if (pConn->pingtimes.size() > 20)
                            pConn->pingtimes.pop_front();
                        pConn->pingtimes.push_back(pConn->endtime - pConn->starttime);

                    }
                }

                if (IsExpectsAck(request->packet))
                {
                    Packet packet;
                    InitializePacket(packet,
                        request->packet.address,
                        MESG::HEADER::Codes::Ack,
                        ((MESG*)request->packet.buffer)->header.id,
                        0);
                    Network::write(cmstate.netstate, packet);
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
        cmstate.cmmutex.unlock();

        // Prepare read to perpetuate.
        // TODO: what happens when no more free requests?
        Network::read(cmstate.netstate);
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
    Packet packet;
    uint32_t traits = 0;

    InitializePacket(packet,
                     to,
                     MESG::HEADER::Codes::Ready,
                     id,
                     traits);

    Network::write(cmstate.netstate, packet);
    cmstate.connections[0].state = Connection::State::WAITFORSTART;
    return 0;
}

} // namespace bali