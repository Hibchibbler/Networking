#include "ConnMan.h"
#include <sstream>
#include <random>

namespace bali
{
void
ConnMan::InitializeServer(
    ConnManState & cmstate,
    NetworkConfig & netcfg,
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

    cmstate.stale_ms = netcfg.STALE_MS;
    cmstate.remove_ms = netcfg.REMOVE_MS;
    cmstate.acktimeout_ms = netcfg.ACKTIMEOUT_MS;
    cmstate.heartbeat_ms = netcfg.HEARTBEAT_MS;

    cmstate.done = 0;
    cmstate.connectionsmutex.create();

    cmstate.onevent = onevent;
    cmstate.oneventcontext = oneventcontext;

    Network::initialize(cmstate.netstate, 8, port, ConnMan::ConnManServerIOHandler, &cmstate);
    Network::start(cmstate.netstate);
}
void
InitializeConnection(
    Connection* pConn
)
{
    // But we don't know it's ID yet
    // because we have not yet been GRANTed
    pConn->id = 0;
    pConn->state = Connection::State::NEW;
    pConn->curseq = 0;
    pConn->curack = 0;
    pConn->highseq = 0;
    pConn->curuseq = 0;
    pConn->curuack = 0;
    pConn->highuseq = 0;
    pConn->lastrxtime = clock::now();
    pConn->lasttxtime = clock::now();

    pConn->heartbeat = clock::now();
    pConn->reqstatusmutex.create();
}

void
ConnMan::InitializeClient(
    ConnManState & cmstate,
    NetworkConfig & netcfg,
    std::string ipv4server,
    uint32_t port,
    std::string gamename,
    std::string gamepass,
    ConnManState::OnEvent onevent,
    void* oneventcontext
)
{
    srand(1523791);

    cmstate.port = port;
    cmstate.numplayers = 0;
    cmstate.ipv4server = ipv4server;
    cmstate.gamename = gamename;
    cmstate.gamepass = gamepass;

    cmstate.stale_ms = netcfg.STALE_MS;
    cmstate.remove_ms = netcfg.REMOVE_MS;
    cmstate.acktimeout_ms = netcfg.ACKTIMEOUT_MS;
    cmstate.heartbeat_ms = netcfg.HEARTBEAT_MS;

    cmstate.done = 0;
    cmstate.connectionsmutex.create();

    cmstate.onevent = onevent;
    cmstate.oneventcontext = oneventcontext;

    Network::initialize(cmstate.netstate, 8, 0, ConnMan::ConnManClientIOHandler, &cmstate);
    Network::start(cmstate.netstate);
}


uint64_t
ConnMan::ReadyCount(
    ConnManState & cmstate
)
{
    uint64_t cnt = 0;
    cmstate.connectionsmutex.lock();
    cnt = cmstate.connections.size();
    cmstate.connectionsmutex.unlock();
    return cnt;
}

void
ConnMan::Cleanup(
    ConnManState & state
)
{
    Network::stop(state.netstate);
    state.connectionsmutex.destroy();
}

uint32_t
ConnMan::Query(
    Connection& who,
    uint32_t index,
    QueryType qt,
    uint32_t param1,
    uint32_t param2
)
{
    uint32_t ret = 0;
    who.reqstatusmutex.lock();
    if (qt == ConnMan::QueryType::IS_STATE_EQUAL)
    {
        for (auto &rs : who.reqstatus)
        {
            if (rs.first == index)
            {
                if (rs.second.state == (Connection::RequestStatus::State)param1)
                {
                    ret = 1;
                    break;
                }
            }
        }
    }
    else if (qt == ConnMan::QueryType::GET_PING)
    {
        duration ping = who.reqstatus[index].endtime - who.reqstatus[index].starttime;
        ret = ping.count();
    }
    who.reqstatusmutex.unlock();
    return ret;
}

uint32_t
ConnMan::SendReliable(
    ConnManState & cmstate,
    Connection & connection,
    uint8_t* buffer,
    uint32_t buffersize,
    AcknowledgeHandler ackhandler
)
{
    Packet packet;
    Connection::RequestStatus status;

    memset(packet.buffer, 0, MAX_PACKET_SIZE);
    packet.buffersize = sizeof(MESG::HEADER) + sizeof(GENERAL);
    packet.address = connection.who;

    MESG* pMsg = (MESG*)packet.buffer;
    AddMagic(pMsg);
    pMsg->header.code = (uint8_t)MESG::HEADER::Codes::General;
    pMsg->header.id = connection.id;
    pMsg->header.seq = InterlockedIncrement(&connection.curseq);
    pMsg->header.mode = (uint8_t)MESG::HEADER::Mode::Reliable;

    packet.ackhandler = ackhandler;

    status.seq = pMsg->header.seq;
    status.starttime = clock::now();
    status.endtime = clock::now();

    status.state = Connection::RequestStatus::State::PENDING;
    status.packet = packet;
    AddRequestStatus(connection, status, status.seq);

    //std::cout << "Push Reliable: " << CodeName[pMsg->header.code] << std::endl;
    //connection.txpacketsreliable.push(packet);
    Network::write(cmstate.netstate, status.packet);
    return status.seq;
}


void
ConnMan::SendUnreliable(
    ConnManState & cmstate,
    Connection & connection,
    uint8_t* buffer,
    uint32_t buffersize
)
{
    Packet packet;

    memset(packet.buffer, 0, MAX_PACKET_SIZE);
    packet.buffersize = sizeof(MESG::HEADER) + sizeof(GENERAL);
    packet.address = connection.who;
    MESG* pMsg = (MESG*)packet.buffer;
    AddMagic(pMsg);
    pMsg->header.code = (uint8_t)MESG::HEADER::Codes::General;
    pMsg->header.id = connection.id;
    pMsg->header.seq = InterlockedIncrement(&connection.curuseq);
    ///
    pMsg->header.mode = (uint8_t)MESG::HEADER::Mode::Unreliable;

    packet.ackhandler = nullptr;

    Network::write(cmstate.netstate, packet);
}

uint64_t
ConnMan::SendIdentifyTo(
    ConnManState & cmstate,
    Address to,
    std::string playername,
    std::string gamename,
    std::string gamepass
)
{
    Packet packet;
    uint32_t traits = 0;

    //
    // Let's put a new connection into the list
    //

    // But we don't know it's ID yet
    // because we have not yet been GRANTed
    InitializeConnection(&cmstate.localconn);
    cmstate.localconn.who = to;
    cmstate.localconn.playername = playername;
    cmstate.localconn.state = Connection::State::NEW;


    memset(packet.buffer, 0, MAX_PACKET_SIZE);
    packet.buffersize = sizeof(MESG::HEADER) + sizeof(IDENTIFY);
    packet.address = to;

    MESG* pMsg = (MESG*)packet.buffer;
    AddMagic(pMsg);
    pMsg->header.code = (uint8_t)MESG::HEADER::Codes::Identify;
    pMsg->header.id = 0;
    pMsg->header.seq = 7;

    memcpy(pMsg->payload.identify.playername,
           playername.c_str(),
           playername.size());

    memcpy(pMsg->payload.identify.gamename,
           gamename.c_str(),
           gamename.size());

    memcpy(pMsg->payload.identify.gamepass,
           gamepass.c_str(),
           gamepass.size());

    Network::write(cmstate.netstate, packet);

    return 0;
}

uint64_t
ConnMan::SendGrantTo(
    ConnManState & cmstate,
    Address to,
    uint32_t id,
    std::string playername
)
{
    Packet packet;
    memset(packet.buffer, 0, MAX_PACKET_SIZE);
    packet.buffersize = sizeof(MESG::HEADER) + sizeof(GRANT);
    packet.address = to;

    MESG* pMsg = (MESG*)packet.buffer;
    AddMagic(pMsg);
    pMsg->header.code = (uint8_t)MESG::HEADER::Codes::Grant;
    pMsg->header.id = id;
    pMsg->header.seq = 13;

    memcpy(pMsg->payload.identify.playername,
           playername.c_str(),
           playername.size());

    Network::write(cmstate.netstate, packet);
    return 0;
}

uint64_t
ConnMan::SendDenyTo(
    ConnManState & cmstate,
    Address to,
    std::string playername
)
{
    Packet packet;
    memset(packet.buffer, 0, MAX_PACKET_SIZE);
    packet.buffersize = sizeof(MESG::HEADER) + sizeof(DENY);
    packet.address = to;

    MESG* pMsg = (MESG*)packet.buffer;
    AddMagic(pMsg);
    pMsg->header.code = (uint8_t)MESG::HEADER::Codes::Deny;
    pMsg->header.id = 0;
    pMsg->header.seq = 13;

    memcpy(pMsg->payload.identify.playername,
           playername.c_str(),
           playername.size());

    Network::write(cmstate.netstate, packet);
    return 0;
}

void
ConnMan::SendAckTo(
    ConnManState & cmstate,
    Address to,
    uint32_t id,
    uint32_t ack
)
{
    Packet packet;
    memset(packet.buffer, 0, MAX_PACKET_SIZE);
    packet.buffersize = sizeof(MESG::HEADER) + sizeof(ACK);
    packet.address = to;

    MESG* pMsg = (MESG*)packet.buffer;
    AddMagic(pMsg);
    pMsg->header.code = (uint8_t)MESG::HEADER::Codes::Ack;
    pMsg->header.id = id;
    pMsg->header.seq = 13;
    pMsg->header.ack = ack;

    Network::write(cmstate.netstate, packet);
}

uint32_t
GetPacketSequence(
    Packet & packet
)
{
    MESG* m = (MESG*)packet.buffer;
    return m->header.seq;
}

void
ConnMan::UpdateClient(
    ConnManState & cmstate,
    uint32_t ms_elapsed
)
{

    cmstate.timeticks += ms_elapsed;
    cmstate.connectionsmutex.lock();


    // Service outgoing reliable packets.
    //
    // We are only going to send a packet
    // if we are not currently waiting for an ack
    // from last packet.

    auto * pConn = &cmstate.localconn;
    if (pConn->state != Connection::State::NEW)
    {
        //
        // Service incoming packets
        //
        while (!pConn->rxpackets.empty())
        {
            Packet packet;
            packet = pConn->rxpackets.front();
            pConn->rxpackets.pop();
            cmstate.onevent(cmstate.oneventcontext,
                            ConnManState::OnEventType::MESSAGE,
                            pConn,
                            &packet);
        }

        //
        // Update the connection states, and raise interesting facts.
        //

        //
        // Time out those waiting for an ACK
        //
        pConn->reqstatusmutex.lock();
        for (auto & pair : pConn->reqstatus)
        {
            if (pair.second.state == Connection::RequestStatus::State::PENDING)
            {
                duration dur = clock::now() - pair.second.starttime;
                if (dur.count() > cmstate.acktimeout_ms)
                {
                    pair.second.endtime = clock::now();
                    SetRequestStatusState(*pConn, pair.first, Connection::RequestStatus::State::FAILED, false);
                    cmstate.onevent(cmstate.oneventcontext,
                                    ConnManState::OnEventType::ACK_TIMEOUT,
                                    pConn,
                                    &pair.second.packet);
                }
            }
            else if (pair.second.state == Connection::RequestStatus::State::SUCCEEDED)
            {

            }
        }
        pConn->reqstatusmutex.unlock();
        //
        // Connection hasn't seen traffic in over 10 seconds
        // Notify user, and drop Connection
        //

        //duration hb = clock::now() - c.heartbeat;
        //if (hb.count() > cmstate.heartbeat_ms)
        //{
        //    c.heartbeat = clock::now();
        //    ConnMan::SendPing(cmstate, c, true);
        //}

        duration d = clock::now() - pConn->lastrxtime;
        if (d.count() > cmstate.stale_ms)
        {
            cmstate.onevent(cmstate.oneventcontext,
                            ConnManState::OnEventType::CONNECTION_STALE,
                            pConn,
                            nullptr);
        }
    }// state != UNINIT
    cmstate.connectionsmutex.unlock();
   
}
void
ConnMan::ProcessGrant(
    ConnManState& cmstate,
    Connection* pConn,
    Request* request
)
{
    std::string playername = GetPlayerName(request->packet);
    if (playername == pConn->playername)
    {
        pConn->lastrxtime = clock::now();
        pConn->id = GetConnectionId(request->packet);
        pConn->state = Connection::State::READY;
        cmstate.onevent(cmstate.oneventcontext,
                        ConnManState::OnEventType::GRANTED,
                        pConn,
                        &request->packet);
    }
    else
    {
        std::cout << "Weird: Client Rx Grant contains unknown Player Name\n";
    }
}
void
ConnMan::ProcessDeny(
    ConnManState& cmstate,
    Connection* pConn,
    Request* request
)
{
    std::string playername = GetPlayerName(request->packet);
    if (playername == pConn->playername)
    {
        cmstate.onevent(cmstate.oneventcontext,
            ConnManState::OnEventType::DENIED,
            pConn,
            &request->packet);
    }
    else
    {
        std::cout << "Weird: Client Rx Deny contains unknown Player Name\n";
    }
}

void
ConnMan::ProcessGeneral(
    ConnManState& cmstate,
    Connection* pConn,
    Request* request
)
{
    if (pConn)
    {
        pConn->rxpackets.push(request->packet);
        pConn->lastrxtime = clock::now();
        /*
        On Packet Receive, If packet is reliable, Then send Ack packet unreliably.
        */
        MESG* msg = (MESG*)request->packet.buffer;
        if (msg->header.mode == (uint8_t)MESG::HEADER::Mode::Reliable)
        {
            ConnMan::SendAckTo(cmstate, pConn->who, pConn->id, msg->header.seq);
        }
    }
    else
    {
        std::cout << "Client Rx Packet from unknown origin: tossing." << std::endl;
    }
}

void
ConnMan::ProcessAck(
    ConnManState& cmstate,
    Connection* pConn,
    Request* request
)
{
    if (pConn)
    {
        MESG* pMsg = (MESG*)request->packet.buffer;

        uint32_t ack = pMsg->header.ack;
        pConn->reqstatusmutex.lock();

        if (pConn->reqstatus.count(ack) == 1)
        {
            if (pConn->reqstatus[ack].state == Connection::RequestStatus::State::PENDING)
            {
                pConn->lastrxtime = clock::now();
                pConn->reqstatus[ack].endtime = clock::now();
                pConn->reqstatus[ack].state = Connection::RequestStatus::State::SUCCEEDED;
                cmstate.onevent(cmstate.oneventcontext,
                    ConnManState::OnEventType::ACK_RECEIVED,
                    pConn,
                    &pConn->reqstatus[ack].packet);
            }
            else
            {
                // Request has already been FAILED, but not yet removed
                // from the request map
                //
                std::cout << "Fact: Rx'd Ack for FAILED request: " << ack << std::endl;
            }
        }
        else
        {
            // Request has alreadt been FAILED, and removed
            // from the request map
            //
            std::cout << "Fact: Rx'd Ack for old FAILED request: " << ack << std::endl;
        }
        pConn->reqstatusmutex.unlock();
    }
    else
    {
        std::cout << "Problem: Rx'd ACK Packet contains unknown ID!\n";
    }
}
void
ConnMan::ConnManClientIOHandler(
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
    cmstate.connectionsmutex.lock();
    if (request->ioType == Request::IOType::READ)
    {
        MESG* m = (MESG*)request->packet.buffer;
        std::cout << "Client Rx: " << CodeName[m->header.code] << "[" <<m->header.seq << "]" << "[" << m->header.ack << "]" << std::endl;


        if (IsMagicGood(request->packet))
        {
            if (IsSizeValid(request->packet))
            {
                if (IsCode(request->packet, MESG::HEADER::Codes::Grant))
                {
                    //
                    // If we're receiving a Grant,
                    // then the associated connection has not yet been assigned an Id.
                    // Therefore, we find the associated connection
                    // by name
                    //
                    Connection* pConn = &cmstate.localconn;
                    ConnMan::ProcessGrant(cmstate, pConn, request);
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::Deny))
                {
                    Connection* pConn = &cmstate.localconn;
                    ProcessDeny(cmstate, pConn, request);
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::Ack))
                {
                    // Rx'd an ACk. 
                    // Something is eagerly awaiting this i'm sure.
                    Connection* pConn = &cmstate.localconn;
                    ConnMan::ProcessAck(cmstate, pConn, request);
                }
                else
                {
                    //
                    // Rx'd something other than Identify, or Ack
                    //
                    Connection* pConn = &cmstate.localconn;
                    ConnMan::ProcessGeneral(cmstate, pConn, request);
                }
            }
            else
            {
                // Size does not meet expectations, given "request->header.code"
                std::cout << "Client Rx Packet with incoherent Code or Size." << std::endl;
            }
        }
        else
        {
            // Magic MisMatch
            std::cout << "Client Rx Packet with incoherent Magic." << std::endl;
        }
        Network::read(cmstate.netstate);
    }
    else if (request->ioType == Request::IOType::WRITE)
    {
        MESG* m = (MESG*)request->packet.buffer;
        std::cout << "Client Tx: " << CodeName[m->header.code] << "[" << m->header.seq << "]" << "[" << m->header.ack << "]" << std::endl;
    }
    cmstate.connectionsmutex.unlock();
}

void
ConnMan::UpdateServer(
    ConnManState & cmstate,
    uint32_t ms_elapsed
)
{

    cmstate.timeticks += ms_elapsed;
    cmstate.connectionsmutex.lock();

    //
    // Update the connection states, and raise interesting facts.
    //
    auto pConn = cmstate.connections.begin();
    while (pConn != cmstate.connections.end())
    {
        //
        // Service incoming packets
        //
        while (!pConn->rxpackets.empty())
        {
            Packet packet;
            packet = pConn->rxpackets.front();
            pConn->rxpackets.pop();
            cmstate.onevent(cmstate.oneventcontext,
                            ConnManState::OnEventType::MESSAGE,
                            &(*pConn),
                            &packet);
        }

        pConn->reqstatusmutex.lock();
        for (auto & pair : pConn->reqstatus)
        {
            if (pair.second.state == Connection::RequestStatus::State::PENDING)
            {
                duration dur = clock::now() - pair.second.starttime;
                if (dur.count() > cmstate.acktimeout_ms)
                {
                    SetRequestStatusState(*pConn, pair.first, Connection::RequestStatus::State::FAILED, false);
                    cmstate.onevent(cmstate.oneventcontext,
                                    ConnManState::OnEventType::ACK_TIMEOUT,
                                    &(*pConn),
                                    &pair.second.packet);
                }
            }
            else if (pair.second.state == Connection::RequestStatus::State::SUCCEEDED)
            {

            }
        }
        pConn->reqstatusmutex.unlock();

        //// Server sent pings to client occasionally.
        ////
        //duration hb = clock::now() - pConn->heartbeat;
        //if (hb.count() > cmstate.heartbeat_ms)
        //{
        //    pConn->heartbeat = clock::now();
        //    ConnMan::SendPing(cmstate, *pConn, true);
        //}

        //
        // We need to drop connections that we haven't heard from in a while
        //
        duration d = clock::now() - pConn->lastrxtime;
        if (d.count() > cmstate.stale_ms)
        {
            cmstate.onevent(cmstate.oneventcontext,
                            ConnManState::OnEventType::CONNECTION_STALE,
                            &(*pConn),
                            nullptr);
        }

        if (d.count() > cmstate.remove_ms)
        {
            cmstate.onevent(cmstate.oneventcontext,
                            ConnManState::OnEventType::CONNECTION_REMOVE,
                            &(*pConn),
                            nullptr);
            pConn = cmstate.connections.erase(pConn);
            break;// continue;
        }
        pConn++;

    }
    cmstate.connectionsmutex.unlock();
}
void
ConnMan::ProcessIdentify(
    ConnManState& cmstate,
    Request* request
)
{
    if (cmstate.connections.size() < cmstate.numplayers)
    {
        if (cmstate.gamename == GetGameName(request->packet))
        {
            if (cmstate.gamepass == GetGamePass(request->packet))
            {
                std::random_device rd;     // only used once to initialise (seed) engine
                std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)
                std::uniform_int_distribution<uint32_t> uni(1, 65536); // guaranteed unbiased

                auto random_integer = uni(rng);

                std::string pn = GetPlayerName(request->packet);
                if (ConnMan::IsPlayerNameAvailable(cmstate.connections, pn))
                {
                    //
                    // Grant
                    // Create and insert a new connection, 
                    // randomly generate id, then send a Grant.
                    //
                    Connection connection;
                    InitializeConnection(&connection);
                    connection.playername = pn;
                    connection.id = random_integer;
                    connection.who = request->packet.address;
                    connection.state = Connection::State::READY;

                    cmstate.connections.push_back(connection);
                    cmstate.onevent(cmstate.oneventcontext,
                                    ConnManState::OnEventType::CONNECTION_ADD,
                                    &connection,
                                    &request->packet);

                    ConnMan::SendGrantTo(cmstate,
                                         connection.who,
                                         connection.id,
                                         connection.playername);
                }
                else
                {
                    // Deny - player name already exists
                    ConnMan::SendDenyTo(cmstate,
                                        request->packet.address,
                                        GetPlayerName(request->packet));
                    std::cout << "Tx Deny: Player Name already exists\n";
                }
            }
            else
            {
                // Deny - password doesn't match
                ConnMan::SendDenyTo(cmstate,
                                    request->packet.address,
                                    GetPlayerName(request->packet));
                std::cout << "Tx Deny: Password is bad\n";
            }
        }
        else
        {
            // Deny - Game name doesn't match
            ConnMan::SendDenyTo(cmstate,
                                request->packet.address,
                                GetPlayerName(request->packet));
            std::cout << "Tx Deny: Game name is unknown\n";
        }
    }
}
void
ConnMan::ConnManServerIOHandler(
    void* cmstate_,
    Request* request,
    uint64_t tid
)
{
    bali::Network::Result result(bali::Network::ResultType::SUCCESS);
    ConnManState& cmstate = *((ConnManState*)cmstate_);

    cmstate.connectionsmutex.lock();
    if (request->ioType == Request::IOType::READ)
    {
        MESG* m = (MESG*)request->packet.buffer;
        std::cout << "Server Rx: " << CodeName[m->header.code] << "[" << m->header.seq << "]" << "[" << m->header.ack << "]" << std::endl;

        /*
            If we recieve an Identify packet,
            and we are accepting more players,
            and the packet makes sense, Grant.
            Otherwise Deny.

            Packets not involved in the initial handshake,
            and that are from an identified client,
            will be enqueued to the packet queue.
        */
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
                    ConnMan::ProcessIdentify(cmstate, request);
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::Ack))
                {
                    // Rx'd an ACk. 
                    // Something is eagerly awaiting this i'm sure.
                    uint32_t cid = GetConnectionId(request->packet);
                    Connection* pConn = ConnMan::GetConnectionById(cmstate.connections, cid);
                    ConnMan::ProcessAck(cmstate, pConn, request);
                }
                else
                {
                    //
                    // Rx'd something other than Identify, or Ack
                    //
                    Connection* pConn;
                    uint32_t cid;
                    cid = GetConnectionId(request->packet);
                    pConn = ConnMan::GetConnectionById(cmstate.connections, cid);
                    ConnMan::ProcessGeneral(cmstate, pConn, request);
                }
            }
            else
            {
                // Size does not meet expectations, given "request->header.code"
                std::cout << "Server Rx Packet with incoherent Code or Size." << std::endl;
            }
        }
        else
        {
            // Magic MisMatch
            std::cout << "Server Rx Packet with incoherent Magic." << std::endl;
        }

        // Prepare read to perpetuate.
        // TODO: what happens when no more free requests?
        Network::read(cmstate.netstate);
    }
    else if (request->ioType == Request::IOType::WRITE)
    {
        MESG* m = (MESG*)request->packet.buffer;
        std::cout << "Server Tx: " << CodeName[m->header.code] << "[" << m->header.seq << "]" << "[" << m->header.ack << "]" << std::endl;
    }

    cmstate.connectionsmutex.unlock();

    return;
}


void
AddMagic(
    MESG* pMsg
)
{
    memcpy(pMsg->header.magic, "AB", 2);
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
    if (code == MESG::HEADER::Codes::General) { payloadSize += sizeof(GENERAL); }
    if (code == MESG::HEADER::Codes::Ack) { payloadSize += sizeof(ACK); }

    return payloadSize;
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
        packet.buffer[1] == 66)
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

Connection*
ConnMan::GetConnectionById(
    std::list<Connection> & connections,
    uint32_t id
)
{
    for (auto & c : connections)
    {
        if (id == c.id)
        {
            return &c;
        }
    }
    return nullptr;
}

bool
ConnMan::RemoveConnectionByName(
    std::list<Connection> & connections,
    std::string playername
)
{
    bool found = false;
    for (auto c = connections.begin(); c != connections.end(); c++)
    {
        if (playername == c->playername)
        {
            connections.erase(c);
            found = true;
            break;
        }
    }
    return found;
}

bool
ConnMan::IsPlayerNameAvailable(
    std::list<Connection> & connections,
    std::string name
)
{
    bool available = true;
    for (auto c : connections)
    {
        if (c.playername == name)
        {
            available = false;
            break;
        }
    }
    return available;
}

uint32_t
GetConnectionId(
    Packet & packet
)
{
    MESG* RxMsg = (MESG*)packet.buffer;
    return RxMsg->header.id;
}

void
PrintMsgHeader(
    Packet & packet,
    bool rx
)
{
    MESG* pDbgMsg = (MESG*)packet.buffer;
    if (rx) { std::cout << "[Rx]\n"; }
    else { std::cout << "[Tx]\n"; }

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
    case (uint32_t)MESG::HEADER::Codes::General:
        std::cout << "  Code: General\n";
        break;
    default:
        std::cout << "  Code: Something or nother\n";
        break;
    }
    //std::cout << "\tCode:   " << std::hex << pDbgMsg->header.code << "\n";
    std::cout << "  Id:     " << std::hex << pDbgMsg->header.id << "\n";
    //std::cout << "  Traits: " << std::hex << pDbgMsg->header.traits << "\n";
    std::cout << "  Seq:    " << std::hex << pDbgMsg->header.seq << "\n";
    //std::cout << "  Crc:    " << std::hex << pDbgMsg->header.crc << "\n";
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
    va_start(pArgList, szFormat);
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

void
SetRequestStatusState(
    Connection & connection,
    uint32_t sid,
    Connection::RequestStatus::State state,
    bool lock
)
{
    if (lock)
        connection.reqstatusmutex.lock();
    connection.reqstatus[sid].state = state;
    if (lock)
        connection.reqstatusmutex.unlock();
}

void
AddRequestStatus(
    Connection & connection,
    Connection::RequestStatus & rs,
    uint32_t sid
)
{
    connection.reqstatusmutex.lock();
    connection.reqstatus[sid] = rs;
    connection.reqstatusmutex.unlock();
}

void
RemoveRequestStatus(
    Connection & connection,
    uint32_t sid
)
{
    connection.reqstatusmutex.lock();
    connection.reqstatus.erase(sid);
    connection.reqstatusmutex.unlock();
}

} // namespace bali