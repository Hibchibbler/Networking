#include "ConnMan.h"
#include <sstream>
#include <random>

namespace bali
{

void
InitializeConnection(
    Connection* pConn,
    Address to,
    std::string playername
)
{
    // But we don't know it's ID yet
    // because we have not yet been GRANTed
    pConn->id = 0;
    pConn->state = Connection::State::DEAD;
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
    pConn->rxpacketmutex.create();

    pConn->who = to;
    pConn->playername = playername;
}

void
ConnMan::InitializeServer(
    NetworkConfig & netcfg,
    uint32_t thisport,
    uint32_t numplayers,
    std::string gamename,
    std::string gamepass,
    ConnManState::OnEvent onevent,
    void* oneventcontext
)
{
    srand(1523791);

    cmstate.thisport = thisport;
    cmstate.numplayers = numplayers;
    cmstate.gamename = gamename;
    cmstate.gamepass = gamepass;

    cmstate.timeout_warning_ms = netcfg.TIMEOUT_WARNING_MS;
    cmstate.timeout_ms = netcfg.TIMEOUT_MS;
    cmstate.ack_timeout_ms = netcfg.ACK_TIMEOUT_MS;
    cmstate.heart_beat_ms = netcfg.HEART_BEAT_MS;

    cmstate.done = 0;
    cmstate.connectionsmutex.create();

    cmstate.onevent = onevent;
    cmstate.oneventcontext = oneventcontext;

    std::random_device rd;     // only used once to initialise (seed) engine
    std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)

    Network::initialize(cmstate.netstate, 8, thisport, ConnMan::ConnManServerIOHandler, this);
    Network::start(cmstate.netstate);
}

void
ConnMan::InitializeClient(
    NetworkConfig & netcfg,
    uint32_t thisport,
    uint32_t numplayers,
    std::string gamename,
    std::string gamepass,
    ConnManState::OnEvent onevent,
    void* oneventcontext
)
{
    srand(1523791);

    cmstate.thisport = thisport;
    cmstate.numplayers = numplayers;
    cmstate.gamename = gamename;
    cmstate.gamepass = gamepass;

    cmstate.timeout_warning_ms = netcfg.TIMEOUT_WARNING_MS;
    cmstate.timeout_ms = netcfg.TIMEOUT_MS;
    cmstate.ack_timeout_ms = netcfg.ACK_TIMEOUT_MS;
    cmstate.heart_beat_ms = netcfg.HEART_BEAT_MS;

    cmstate.done = 0;
    cmstate.connectionsmutex.create();

    cmstate.onevent = onevent;
    cmstate.oneventcontext = oneventcontext;

    std::random_device rd;     // only used once to initialise (seed) engine
    std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)

    Network::initialize(cmstate.netstate, 1, thisport, ConnMan::ConnManClientIOHandler, this);
    Network::start(cmstate.netstate);
}


uint64_t
ConnMan::ReadyCount(
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
)
{
    Network::stop(cmstate.netstate);
    cmstate.connectionsmutex.destroy();
}

uint32_t
ConnMan::SendReliable(
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


    Network::write(cmstate.netstate, status.packet);
    return status.seq;
}


void
ConnMan::SendUnreliable(
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
ConnMan::SendIdentify(
    Connection& connection,
    uint32_t randomcode,
    std::string gamename,
    std::string gamepass
)
{
    Packet packet;
    uint32_t traits = 0;

    //
    // Kick off the connection sequence.
    //
    memset(packet.buffer, 0, MAX_PACKET_SIZE);
    packet.buffersize = sizeof(MESG::HEADER) + sizeof(IDENTIFY);
    packet.address = connection.who;

    MESG* pMsg = (MESG*)packet.buffer;
    AddMagic(pMsg);
    pMsg->header.code = (uint8_t)MESG::HEADER::Codes::Identify;
    pMsg->header.id = randomcode;
    pMsg->header.seq = 7;

    memcpy(pMsg->payload.identify.playername,
           connection.playername.c_str(),
           connection.playername.size());

    memcpy(pMsg->payload.identify.gamename,
           gamename.c_str(),
           gamename.size());

    memcpy(pMsg->payload.identify.gamepass,
           gamepass.c_str(),
           gamepass.size());

    connection.identtime = clock::now();
    connection.state = Connection::State::IDENTIFYING;
    connection.locality = Connection::Locality::LOCAL;
    Network::write(cmstate.netstate, packet);


    return 0;
}

uint64_t
ConnMan::SendGrantTo(
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
ConnMan::UpdateAlive(
    Connection* pConn
)
{
    if (pConn->state == Connection::State::GRANTED)
    {
        pConn->state = Connection::State::ALIVE;
        cmstate.onevent(cmstate.oneventcontext,
            ConnManState::OnEventType::CONNECTION_HANDSHAKE_GRANTED,
            pConn,
            nullptr);
    }

    if (pConn->state == Connection::State::DENIED)
    {
    }

    if (pConn->state == Connection::State::IDENTIFYING)
    {

    }

    if (pConn->state == Connection::State::ALIVE)
    {
        //
        // Service incoming packets
        //
        pConn->rxpacketmutex.lock();
        while (!pConn->rxpackets.empty())
        {
            Packet packet;
            packet = pConn->rxpackets.front();
            pConn->rxpackets.pop();
            cmstate.onevent(cmstate.oneventcontext,
                            ConnManState::OnEventType::MESSAGE_RECEIVED,
                            pConn,
                            &packet);
        }
        pConn->rxpacketmutex.unlock();
        //
        // Update the connection states, and rise interesting facts.
        //

        //
        // Time out those waiting for an ACK
        //
        pConn->reqstatusmutex.lock();
        auto pReq = pConn->reqstatus.begin();
        while (pReq != pConn->reqstatus.end())
        {
            if (pReq->second.state == Connection::RequestStatus::State::PENDING)
            {
                duration dur = clock::now() - pReq->second.starttime;
                if (dur.count() > cmstate.ack_timeout_ms)
                {
                    pReq->second.endtime = clock::now();
                    pReq->second.state = Connection::RequestStatus::State::FAILED;
                    cmstate.onevent(cmstate.oneventcontext,
                                    ConnManState::OnEventType::MESSAGE_ACK_TIMEOUT,
                                    pConn,
                                    &pReq->second.packet);
                    pReq = pConn->reqstatus.erase(pReq);
                }
                else
                    pReq++;
            }
            else if (pReq->second.state == Connection::RequestStatus::State::SUCCEEDED)
            {
                cmstate.onevent(cmstate.oneventcontext,
                                ConnManState::OnEventType::MESSAGE_ACK_RECEIVED,
                                pConn,
                                &pReq->second.packet);
                pReq = pConn->reqstatus.erase(pReq);
            }
            else
                pReq++;
        }
        pConn->reqstatusmutex.unlock();

        duration d = clock::now() - pConn->lastrxtime;
        if (d.count() > cmstate.timeout_ms)
        {
            if (pConn->locality == Connection::Locality::REMOTE)
            {
                pConn->state = Connection::State::DEAD;
            }
            else
            {//HACK
                pConn->state = Connection::State::ALIVE;
                pConn->lastrxtime = clock::now();
            }

            cmstate.onevent(cmstate.oneventcontext,
                            ConnManState::OnEventType::CONNECTION_TIMEOUT,
                            pConn,
                            nullptr);
        }
    } // If Alive
    if (pConn->state == Connection::State::DEAD)
    {
    }
}
void
ConnMan::UpdateClient(
    Connection* pConn
)
{

    //cmstate.connectionsmutex.lock();


    // Service outgoing reliable packets.
    //
    // We are only going to send a packet
    // if we are not currently waiting for an ack
    // from last packet. 
    UpdateAlive(pConn);

    if (pConn->state == Connection::State::IDENTIFYING)
    {
        duration d2 = clock::now() - pConn->lasttxtime;
        if (d2.count() > cmstate.timeout_ms)
        {
            pConn->state = Connection::State::DEAD;
            cmstate.onevent(cmstate.oneventcontext,
                            ConnManState::OnEventType::CONNECTION_HANDSHAKE_TIMEOUT,
                            pConn,
                            nullptr);
        }
    }

    if (pConn->state == Connection::State::DENIED)
    {
        pConn->state = Connection::State::DEAD;
        cmstate.onevent(cmstate.oneventcontext,
                        ConnManState::OnEventType::CONNECTION_HANDSHAKE_DENIED,
                        pConn,
                        nullptr);
    }


    //cmstate.connectionsmutex.unlock();
}
void
ConnMan::Update(
    uint32_t ms_elapsed,
    bool client
)
{
    Connection* pConn = nullptr;
    cmstate.timeticks += ms_elapsed;
    if (!client)
    {
        cmstate.connectionsmutex.lock();
        for (auto& pConn : cmstate.connections)
        {
            UpdateServer(&pConn);
        }
        ReapConnections();
    }
    else
    {
        UpdateClient(&cmstate.localconn);
    }

    if (!client)
    {
        cmstate.connectionsmutex.unlock();
    }
}
void
ConnMan::UpdateServer(
    Connection* pConn
)
{
    UpdateAlive(pConn);
    if (pConn->state == Connection::State::GRANTED)
    {
        pConn->state = Connection::State::ALIVE;
        cmstate.onevent(cmstate.oneventcontext,
            ConnManState::OnEventType::CONNECTION_HANDSHAKE_GRANTED,
            pConn,
            nullptr);
    }
}
void
ConnMan::ProcessGrant(
    Connection* pConn,
    Request* request
)
{
    std::string playername = GetPlayerName(request->packet);
    if (playername == pConn->playername)
    {
        pConn->lastrxtime = clock::now();
        pConn->id = GetConnectionId(request->packet);
        pConn->state = Connection::State::GRANTED;
    }
    else
    {
        std::cout << "Weird: Client Rx Grant contains unknown Player Name\n";
    }
}
void
ConnMan::ProcessDeny(
    Connection* pConn,
    Request* request
)
{
    std::string playername = GetPlayerName(request->packet);
    if (playername == pConn->playername)
    {
        pConn->state = Connection::State::DENIED;
    }
    else
    {
        std::cout << "Weird: Client Rx Deny contains unknown Player Name\n";
    }
}

void
ConnMan::ProcessGeneral(
    Connection* pConn,
    Request* request
)
{
    std::random_device rd;     // only used once to initialise (seed) engine
    std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)
    std::uniform_int_distribution<uint32_t> uni(1, 65536); // guaranteed unbiased

    auto random_integer = uni(rng);
    if (random_integer < 10000)
    {
        std::cout << "Simulation: Dropping General Packet" << std::endl;
        return ;
    }


    if (pConn)
    {
        pConn->rxpacketmutex.lock();
        pConn->rxpackets.push(request->packet);
        pConn->rxpacketmutex.unlock();
        pConn->lastrxtime = clock::now();
        /*
        On Packet Receive, If packet is reliable, Then send Ack packet unreliably.
        */
        MESG* msg = (MESG*)request->packet.buffer;
        if (msg->header.mode == (uint8_t)MESG::HEADER::Mode::Reliable)
        {
            SendAckTo(pConn->who, pConn->id, msg->header.seq);
        }
    }
    else
    {
        std::cout << "Client Rx Packet from unknown origin: tossing." << std::endl;
    }
}

void
ConnMan::ProcessAck(
    Connection* pConn,
    Request* request
)
{
    if (pConn)
    {
        MESG* pMsg = (MESG*)request->packet.buffer;

        uint32_t ack = pMsg->header.ack;

        auto pReq = pConn->GetRequestStatus(ack);
        //pConn->reqstatusmutex.lock();
        if (pReq != pConn->reqstatus.end())
        {
            if (pReq->second.state == Connection::RequestStatus::State::PENDING)
            {
                pConn->lastrxtime = clock::now();
                pReq->second.endtime = clock::now();
                pReq->second.state = Connection::RequestStatus::State::SUCCEEDED;
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
        //pConn->reqstatusmutex.unlock();
    }
    else
    {
        std::cout << "Problem: Rx'd ACK Packet contains unknown ID!\n";
    }
}
void
ConnMan::ConnManClientIOHandler(
    void* cm_,
    Request* request,
    uint64_t tid
)
{
    bali::Network::Result result(bali::Network::ResultType::SUCCESS);
    ConnMan& cm = *((ConnMan*)cm_);
    std::random_device rd;     // only used once to initialise (seed) engine
    std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)
    std::uniform_int_distribution<uint32_t> uni(1, 65536); // guaranteed unbiased

    auto random_integer = uni(rng);
    cm.cmstate.connectionsmutex.lock();
    if (request->ioType == Request::IOType::READ)
    {
        MESG* m = (MESG*)request->packet.buffer;
        std::cout << "Client Rx: " << CodeName[m->header.code] << "[" << m->header.id << "]" << "[" << m->header.seq << "]" << "[" << m->header.ack << "]" << std::endl;


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
                    Connection* pConn = &cm.cmstate.localconn;
                    cm.ProcessGrant(pConn, request);
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::Deny))
                {
                    Connection* pConn = &cm.cmstate.localconn;
                    cm.ProcessDeny(pConn, request);
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::Ack))
                {
                    // Rx'd an ACk. 
                    // Something is eagerly awaiting this i'm sure.
                    Connection* pConn = &cm.cmstate.localconn;
                    cm.ProcessAck(pConn, request);
                }
                else
                {
                    //
                    // Rx'd something other than Identify, or Ack
                    //
                    Connection* pConn = &cm.cmstate.localconn;
                    cm.ProcessGeneral(pConn, request);
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
        Network::read(cm.cmstate.netstate);
    }
    else if (request->ioType == Request::IOType::WRITE)
    {
        MESG* m = (MESG*)request->packet.buffer;
        std::cout << "Client Tx: " << CodeName[m->header.code] << "[" << m->header.id << "]" << "[" << m->header.seq << "]" << "[" << m->header.ack << "]" << std::endl;
    }
    cm.cmstate.connectionsmutex.unlock();
}

void
ConnMan::ProcessIdentify(
    Request* request
)
{
    //if (cmstate.connections.size() < cmstate.numplayers)
    {
        if (cmstate.gamename == GetGameName(request->packet))
        {
            if (cmstate.gamepass == GetGamePass(request->packet))
            {
                std::random_device rd;     // only used once to initialise (seed) engine
                std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)
                std::uniform_int_distribution<uint32_t> uni(1, 65536); // guaranteed unbiased

                auto random_integer = uni(rng);
                // DEBUG
                if (random_integer > 20000 && random_integer < 30000)
                {
                    std::cout << "Simulation: Dropping Identify Packet" << std::endl;
                    return;
                }
                if (random_integer > 30000 && random_integer < 40000)
                {
                    std::cout << "Simulation: Sending Deny Packet" << std::endl;
                    SendDenyTo(request->packet.address,
                                GetPlayerName(request->packet));
                    return;
                }

                std::string pn = GetPlayerName(request->packet);
                uint16_t id = GetConnectionId(request->packet);
                Connection* pConn = nullptr;

                cmstate.connectionsmutex.lock();
                bool isnameavail = ConnMan::IsPlayerNameAndIdAvailable(cmstate.connections, pn, id);
                if (!isnameavail)
                {
                    pConn = GetConnectionById(cmstate.connections, id);
                }
                cmstate.connectionsmutex.unlock();

                if (isnameavail)
                {
                    //
                    // Grant
                    // Create and insert a new connection, 
                    // randomly generate id, then send a Grant.
                    //
                    Connection connection;
                    InitializeConnection(&connection,
                                         request->packet.address,
                                         pn);
                    connection.playername = pn;
                    connection.id = id;
                    connection.who = request->packet.address;
                    connection.lastrxtime = clock::now();
                    connection.lasttxtime = clock::now();
                    connection.locality = Connection::Locality::REMOTE;
                    connection.state = Connection::State::GRANTED;

                    cmstate.connectionsmutex.lock();
                    cmstate.connections.push_back(connection);
                    cmstate.connectionsmutex.unlock();

                    SendGrantTo(connection.who,
                                connection.id,
                                connection.playername);
                    

                }
                else
                {
                    SendGrantTo(pConn->who,
                                pConn->id,
                                pConn->playername);
                }
                //else
                //{
                //    // Deny - player name already exists
                //    SendDenyTo(request->packet.address,
                //               GetPlayerName(request->packet));
                //    std::cout << "Tx Deny: Player Name already exists\n";
                //}
            }
            else
            {
                // Deny - password doesn't match
                SendDenyTo(request->packet.address,
                           GetPlayerName(request->packet));
                std::cout << "Tx Deny: Password is bad\n";
            }
        }
        else
        {
            // Deny - Game name doesn't match
            SendDenyTo(request->packet.address,
                       GetPlayerName(request->packet));
            std::cout << "Tx Deny: Game name is unknown\n";
        }
    }
}
void
ConnMan::ConnManServerIOHandler(
    void* cm_,
    Request* request,
    uint64_t tid
)
{
    bali::Network::Result result(bali::Network::ResultType::SUCCESS);
    ConnMan& cm = *((ConnMan*)cm_);

    std::random_device rd;     // only used once to initialise (seed) engine
    std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)
    std::uniform_int_distribution<uint32_t> uni(1, 65536); // guaranteed unbiased

    auto random_integer = uni(rng);

    
    if (request->ioType == Request::IOType::READ)
    {
        MESG* m = (MESG*)request->packet.buffer;
        std::cout << "Server Rx: " << CodeName[m->header.code] << "[" << m->header.id << "]" << "[" << m->header.seq << "]" << "[" << m->header.ack << "]" << std::endl;

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
                    cm.ProcessIdentify(request);
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::Ack))
                {
                    // Rx'd an ACk. 
                    // Something is eagerly awaiting this i'm sure.
                    uint32_t cid = GetConnectionId(request->packet);
                    cm.cmstate.connectionsmutex.lock();
                    Connection* pConn = ConnMan::GetConnectionById(cm.cmstate.connections, cid);
                    cm.cmstate.connectionsmutex.unlock();
                    cm.ProcessAck(pConn, request);
                }
                else
                {
                    //
                    // Rx'd something other than Identify, or Ack
                    //
                    Connection* pConn;
                    uint32_t cid;
                    cid = GetConnectionId(request->packet);
                    cm.cmstate.connectionsmutex.lock();
                    pConn = ConnMan::GetConnectionById(cm.cmstate.connections, cid);
                    cm.cmstate.connectionsmutex.unlock();
                    cm.ProcessGeneral(pConn, request);
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
        Network::read(cm.cmstate.netstate);
    }
    else if (request->ioType == Request::IOType::WRITE)
    {
        MESG* m = (MESG*)request->packet.buffer;
        std::cout << "Server Tx: " << CodeName[m->header.code] << "[" << m->header.id << "]" << "[" << m->header.seq << "]" << "[" << m->header.ack << "]" << std::endl;
    }

    //cm.cmstate.connectionsmutex.unlock();

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
ConnMan::IsPlayerNameAndIdAvailable(
    std::list<Connection> & connections,
    std::string name,
    uint16_t id
)
{
    bool available = true;
    for (auto c : connections)
    {
        if (c.playername == name && c.id == id)
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
    uint32_t seq
)
{
    connection.reqstatusmutex.lock();
    connection.reqstatus.insert(
        std::pair<uint32_t, Connection::RequestStatus>(seq, rs)
    );
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

std::map<uint32_t, Connection::RequestStatus>::iterator
GetRequestStatus(
    Connection & connection,
    uint32_t index
)
{
    connection.reqstatusmutex.lock();
    auto iter = connection.reqstatus.find(index);
    connection.reqstatusmutex.unlock();

    return iter;
}

std::map<uint32_t, Connection::RequestStatus>::iterator
Connection::GetRequestStatus(
    uint32_t index
)
{
    reqstatusmutex.lock();
    auto iter = reqstatus.find(index);
    reqstatusmutex.unlock();

    return iter;
}

} // namespace bali