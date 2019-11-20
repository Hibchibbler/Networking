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
    //for (auto c : cmstate.connections)
    //{
    //    if (c.state == Connection::State::WAITFORSTART)
    //    {
    //        cnt++;
    //    }
    //}
    cnt = cmstate.connections.size();
    cmstate.cmmutex.unlock();
    return cnt;
}

//uint64_t
//sendPacket(
//    ConnManState & cmstate,
//    Packet & packet,
//    void* payload,
//    uint32_t payloadsize
//)
//{
//    uint64_t cnt = 0;
//    uint32_t traits = 0;
//    cmstate.cmmutex.lock();
//    for (auto & c : cmstate.connections)
//    {
//        InitializePacket(packet,
//                         c.who,
//                         MESG::HEADER::Codes::General,
//                         c.id,
//                         traits, 0);
//        MESG* pMsg = (MESG*)packet.buffer;
//        memcpy(&pMsg->payload.general, payload, payloadsize);
//
//        Network::write(cmstate.netstate, packet);
//    }
//    cmstate.cmmutex.unlock();
//    return 0;
//}



//uint64_t
//ConnMan::sendStart(
//    ConnManState & cmstate
//)
//{
//    //uint64_t cnt = 0;
//    //Packet startPacket;
//    //uint32_t traits = 0;
//    //cmstate.cmmutex.lock();
//    //for (auto & c : cmstate.connections)
//    //{
//    //    //if (c.state == Connection::State::WAITFORSTART)
//    //    {
//    //        InitializePacket(startPacket,
//    //                         c.who,
//    //                         MESG::HEADER::Codes::Start,
//    //                         c.id,
//    //                         traits, 0);
//
//    //        Network::write(cmstate.netstate, startPacket);
//    //        //c.state = Connection::State::GENERAL;
//    //    }
//    //}
//    //cmstate.cmmutex.unlock();
//    return 0;
//}



void
ConnMan::cleanup(
    ConnManState & state
)
{
    Network::stop(state.netstate);
    state.cmmutex.destroy();
}
//uint64_t
//ConnMan::sendPingTo(
//    ConnManState & cmstate,
//    Address to,
//    uint32_t id
//)
//{
//    //Packet packet;
//    //uint32_t traits = 0;
//
//    //InitializePacket(packet,
//    //    to,
//    //    MESG::HEADER::Codes::PING,
//    //    id,
//    //    traits, 0);
//
//    //Connection* pConn;
//    //if (GetConnectionById(cmstate.connections, id, &pConn))
//    //{
//    //    pConn->starttime = clock::now();
//    //}
//    //Network::write(cmstate.netstate, packet);
//
//    return 0;
//}

//uint64_t
//ConnMan::sendPongTo(
//    ConnManState & cmstate,
//    Address to,
//    uint32_t id
//)
//{
//    //Packet packet;
//    //uint32_t traits = 0;
//
//    //InitializePacket(packet,
//    //    to,
//    //    MESG::HEADER::Codes::PONG,
//    //    id,
//    //    traits, 0);
//
//    //Network::write(cmstate.netstate, packet);
//
//    return 0;
//}

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

    ////
    //// Let's put a new connection into the list
    ////
    //Connection connection;
    //connection.playername = playername;

    //// But we don't know it's ID yet
    //// because we have not yet been GRANTed
    //connection.id = 0;
    //connection.state = Connection::State::IDLE;
    //connection.conntype = Connection::ConnectionType::LOCAL;

    //cmstate.connections.push_back(connection);

    return 0;
}
//uint64_t
//ConnMan::sendUpdateTo(
//    ConnManState & cmstate,
//    Address to,
//    uint32_t id,
//    GENERAL & update
//)
//{
//    Packet packet;
//    uint32_t traits = 0;
//
//    InitializePacket(packet,
//        to,
//        MESG::HEADER::Codes::General,
//        id,
//        traits, 0);
//
//    MESG* pMsg = (MESG*)packet.buffer;
//    memcpy(&pMsg->payload.general, &update,sizeof(GENERAL));
//    Network::write(cmstate.netstate, packet);
//    return 0;
//}

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
        if (GetConnectionById(cmstate.connections, cid, &pConn))
        {
            cmstate.onevent(cmstate.oneventcontext,
                            ConnManState::OnEventType::MESSAGE,
                            pConn,
                            &packet);
        }
    }

    //
    // Update the connections
    //
    for (auto & c : cmstate.connections)
    {
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
                                                    nullptr);
                                    std::cout << "Tx Grant\n";
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
                    if (GetConnectionById(cmstate.connections, cid, &pConn))
                    {
                        pConn->state = Connection::State::ACKRECEIVED;
                        std::cout << "Rx ACK\n";
                    }
                    else
                    {
                        std::cout << "Weird: Outgoing Packet contains unknown ID!\n";
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
                    ret = GetConnectionByName(cmstate.connections, playername, &pConn);
                    if (ret)
                    {
                        if (pConn->level == Connection::AuthLevel::UNAUTH)
                        {
                            pConn->level = Connection::AuthLevel::AUTH;
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
                    ret = GetConnectionByName(cmstate.connections, playername, &pConn);
                    if (ret)
                    {
                        RemoveConnectionByName(cmstate.connections, playername);
                    }
                    else
                    {
                        // Problem: request->packet id is not known
                        std::cout << "Problem: Grant packet id is not known\n";
                    }
                }
                else
                {
                    //
                    // Rx'd something other than Identify, or Ack
                    //
                    Connection* pConn;
                    uint32_t cid = GetConnectionId(request->packet);
                    if (GetConnectionById(cmstate.connections, cid, &pConn))
                    {
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
                    else
                    {
                        std::cout << "Weird: Packet contains unknown ID\n";
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
        uint32_t cid = GetConnectionId(request->packet);
        if (GetConnectionById(cmstate.connections, cid, &pConn))
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
    return;
}

//uint64_t
//ConnMan::sendReadyTo(
//    ConnManState & cmstate,
//    Address to,
//    uint32_t id
//)
//{
//    //Packet packet;
//
//    //uint32_t traits = 0;
//
//    //InitializePacket(packet,
//    //                 to,
//    //                 MESG::HEADER::Codes::General,
//    //                 id,
//    //                 traits, 0);
//
//    //MESG* msg = (MESG*)packet.buffer;
//    //GameMesg* gmsg = (GameMesg*)msg->payload;
//
//    //Network::write(cmstate.netstate, packet);
//    ////cmstate.connections.front().state = Connection::State::WAITFORSTART;
//    return 0;
//}

} // namespace bali