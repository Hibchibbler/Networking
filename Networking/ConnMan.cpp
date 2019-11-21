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
    void* oneventcontext
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
    cmstate.oneventcontext = oneventcontext;

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
    cnt = cmstate.connections.size();
    cmstate.cmmutex.unlock();
    return cnt;
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
                        MESG::HEADER::Codes::Identify,
                        0,
                        traits, 0);

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
    connection.level = Connection::AuthLevel::UNAUTH;
    connection.state = Connection::State::IDLE;
    connection.conntype = Connection::ConnectionType::LOCAL;

    cmstate.localconn = connection;

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
                     traits, 0);

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
                     traits, 0);

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
ConnMan::updateServer(
    ConnManState & cmstate,
    uint32_t ms_elapsed
)
{

    cmstate.timeticks += ms_elapsed;
    cmstate.cmmutex.lock();
    //
    // Service the packets
    //
    while (!cmstate.packets.empty())
    {
        Connection* pConn = nullptr;
        Packet packet;
        uint32_t cid = 0;

        packet = cmstate.packets.front();
        cmstate.packets.pop();
        cid = GetConnectionId(packet);

        pConn = GetConnectionById(cmstate.connections, cid);
        if (!pConn)
        {
            pConn = &cmstate.localconn;
        }

        cmstate.onevent(cmstate.oneventcontext,
                        ConnManState::OnEventType::MESSAGE,
                        pConn,
                        &packet);
        
    }

    //
    // Update the connections
    //
    for (auto & c : cmstate.connections)
    {
        //
        // Connection hasn't seen traffic in over 5 seconds
        // Notify user.
        //
        duration d = clock::now() - c.checkintime;
        if (d.count() > 5000)
        {
            cmstate.onevent(cmstate.oneventcontext,
                            ConnManState::OnEventType::STALECONNECTION,
                            &c,
                            nullptr);

            c.checkintime = clock::now();
        }

        //
        // Time out those waiting for an ACK
        //
        if (c.state == Connection::State::WAITONACK)
        {
            clock::time_point cur = clock::now();
            if ((cur - c.starttime).count() > 5000)
            {
                c.state = Connection::State::ACKNOTRECEIVED;
            }
        }
    }
    cmstate.cmmutex.unlock();
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
    std::uniform_int_distribution<uint32_t> uni(1, 65536); // guaranteed unbiased

    auto random_integer = uni(rng);

    if (request->ioType == Request::IOType::READ)
    {
        // Debug Print buffer
        //PrintMsgHeader(request->packet, true);
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
                                std::string pn = GetPlayerName(request->packet);
                                if (IsPlayerNameAvailable(cmstate, pn))
                                {
                                    //
                                    // Grant
                                    // Create and insert a new connection, 
                                    // randomly generate id, then send a Grant.
                                    //
                                    std::cout << "Rx IDENTIFY\n";
                                    Connection connection;
                                    connection.playername = pn;
                                    connection.id = random_integer;
                                    connection.who = request->packet.address;
                                    connection.state = Connection::State::IDLE;
                                    connection.checkintime = clock::now();
                                    connection.conntype = Connection::ConnectionType::REMOTE;
                                    connection.level = Connection::AuthLevel::UNAUTH;
                                    cmstate.connections.push_back(connection);
                                    cmstate.onevent(cmstate.oneventcontext,
                                                    ConnManState::OnEventType::CLIENTENTER,
                                                    &connection,
                                                    &request->packet);
                                    std::cout << "Tx Grant: " << random_integer << "\n";
                                    ConnMan::sendGrantTo(cmstate, connection.who, connection.id, connection.playername);
                                }
                                else
                                {
                                    // Deny - player name already exists
                                    ConnMan::sendDenyTo(cmstate,
                                        request->packet.address,
                                        GetPlayerName(request->packet));
                                    std::cout << "Tx Deny: Player Name already exists\n";
                                }
                            }
                            else
                            {
                                // Deny - password doesn't match
                                ConnMan::sendDenyTo(cmstate,
                                    request->packet.address,
                                    GetPlayerName(request->packet));
                                std::cout << "Tx Deny: Password is bad\n";
                            }
                        }
                        else
                        {
                            // Deny - Game name doesn't match
                            ConnMan::sendDenyTo(cmstate,
                                request->packet.address,
                                GetPlayerName(request->packet));
                            std::cout << "Tx Deny: Game name is unknown\n";
                        }
                    }
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::Ack))
                {
                    // Rx'd an ACk. 
                    // Something is eagerly awaiting this i'm sure.
                    Connection* pConn;
                    uint32_t cid = GetConnectionId(request->packet);
                    pConn = GetConnectionById(cmstate.connections, cid);
                    if (pConn)
                    {
                        pConn->state = Connection::State::ACKRECEIVED;
                        std::cout << "Rx ACK\n";
                    }
                    else
                    {
                        std::cout << "Problem: Rx'd ACK Packet contains unknown ID!\n";
                    }
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::Grant))
                {
                    //
                    // If we're receiving a Grant,
                    // then the associated connection has not yet been assigned an Id.
                    // Therefore, we find the associated connection
                    // by name
                    //
                    std::string playername = GetPlayerName(request->packet);
                    if (playername == cmstate.localconn.playername)
                    {
                        if (cmstate.localconn.level == Connection::AuthLevel::UNAUTH)
                        {
                            cmstate.localconn.checkintime = clock::now();
                            cmstate.localconn.level = Connection::AuthLevel::AUTH;
                            cmstate.localconn.id = GetConnectionId(request->packet);
                            cmstate.onevent(cmstate.oneventcontext, ConnManState::OnEventType::GRANTED, &cmstate.localconn, &request->packet);
                        }
                        else
                        {
                            // Weird: We're not supposed to recieve U right now.
                            std::cout << "Weird: We're already authorized, but received a Grant.\n";
                        }
                    }
                    else
                    {
                        std::cout << "Weird: Client Rx Grant contains unknown Player Name\n";
                    }
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::Deny))
                {
                    std::string playername = GetPlayerName(request->packet);
                    if (playername == cmstate.localconn.playername)
                    {
                        cmstate.localconn.checkintime = clock::now();
                        cmstate.onevent(cmstate.oneventcontext, ConnManState::OnEventType::DENIED, &cmstate.localconn, &request->packet);
                    }
                    else
                    {
                        std::cout << "Weird: Client Rx Deny contains unknown Player Name\n";
                    }
                }
                else
                {
                    //
                    // Rx'd something other than Identify, or Ack
                    //
                    Connection* pConn = nullptr;
                    uint32_t cid;
                    
                    cid = GetConnectionId(request->packet);
                    pConn = GetConnectionById(cmstate.connections, cid);
                    if (!pConn)
                    {
                        if (cid == (uint32_t)cmstate.localconn.id)
                        {
                            pConn = &cmstate.localconn;
                        }
                        else
                        {
                            std::cout << "Weird: GENERAL Packet contains unknown ID\n";
                        }
                    }
                    pConn->checkintime = clock::now();
                    cmstate.packets.push(request->packet);

                    if (IsExpectsAck(request->packet))
                    {
                        Packet packet;
                        InitializePacket(packet,
                                            request->packet.address,
                                            MESG::HEADER::Codes::Ack,
                                            ((MESG*)request->packet.buffer)->header.id,
                                            0,
                                            ((MESG*)request->packet.buffer)->header.seq);
                        Network::write(cmstate.netstate, packet);
                        std::cout << "<<Acking>>\n";
                    }
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
        //PrintMsgHeader(request->packet, false);
        Connection* pConn;
        uint32_t code = GetMesg(request->packet)->header.code;
        if (code == (uint32_t)MESG::HEADER::Codes::Ack &&
            code == (uint32_t)MESG::HEADER::Codes::General)
        {
            uint32_t cid = GetConnectionId(request->packet);
            pConn = GetConnectionById(cmstate.connections, cid);
            if (pConn)
            {
                if (IsExpectsAck(request->packet))
                {
                    pConn->state = Connection::State::WAITONACK;
                    pConn->starttime = clock::now();
                }
            }
            else
            {
                std::cout << "Weird: Outgoing Packet contains unknown ID!\n";
            }
        }
    }
    return;
}


} // namespace bali