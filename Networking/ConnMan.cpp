#include "ConnManUtils.h"
#include <sstream>
#include <random>
#include <future>
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
ConnMan::Initialize(
    NetworkConfig & netcfg,
    ConnManState::ConnManType cmtype,
    uint32_t thisport,
    uint32_t numplayers,
    std::string gamename,
    std::string gamepass,
    ConnManState::OnEvent onevent,
    void* oneventcontext
)
{
    srand(1523791);

    cmstate.cmtype = cmtype;
    cmstate.thisport = thisport;
    cmstate.numplayers = numplayers;
    cmstate.gamename = gamename;
    cmstate.gamepass = gamepass;

    cmstate.timeout_warning_ms = netcfg.TIMEOUT_WARNING_MS;
    cmstate.timeout_ms = netcfg.TIMEOUT_MS;
    cmstate.ack_timeout_ms = netcfg.ACK_TIMEOUT_MS;
    cmstate.heart_beat_ms = netcfg.HEART_BEAT_MS;
    cmstate.retry_count = netcfg.RETRY_COUNT;

    cmstate.done = 0;
    cmstate.connectionsmutex.create();

    cmstate.onevent = onevent;
    cmstate.oneventcontext = oneventcontext;

    std::random_device rd;     // only used once to initialise (seed) engine
    std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)

    if (cmstate.cmtype == ConnManState::ConnManType::SERVER)
    {
        Network::initialize(cmstate.netstate, 8, thisport, ConnMan::ConnManServerIOHandler, this);
    }
    else if (cmstate.cmtype == ConnManState::ConnManType::CLIENT)
    {
        Network::initialize(cmstate.netstate, 1, thisport, ConnMan::ConnManClientIOHandler, this);
    }
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

bool
ConnMan::SendTryReliable(
    Connection & connection,
    uint8_t* buffer,
    uint32_t buffersize,
    uint32_t& request_index
)
{

    bool sent = false;

    RequestFuture barrier_future;
    std::future_status wait_status;

    barrier_future =
        SendBuffer(connection,
            ConnMan::SendType::WITHRECEIPT,
            (uint8_t*)buffer,
            buffersize,
            request_index);

    bool deferred = false;
    bool timeout = false;
    bool ready = false;
    bool waiting = true;

    std::cout << "Waiting For Receipt...";
    do
    {
        wait_status = barrier_future.wait_for(std::chrono::milliseconds(1));

        if (wait_status == std::future_status::deferred)
        {
            deferred = true;
            waiting = true;
            // TODO: what is this?
        }
        else if (wait_status == std::future_status::timeout)
        {
            timeout = true;
            waiting = true;
            //std::cout << ".";
        }
        else if (wait_status == std::future_status::ready)
        {
            ready = true;
            RequestStatus::RequestResult state = barrier_future.get();
            if (state == RequestStatus::RequestResult::ACKNOWLEDGED)
            {
                sent = true;
            }
            else if (state == RequestStatus::RequestResult::TIMEDOUT)
            {
                sent = false;
                //curRetries++;
            }
            waiting = false;
        }
    } while (waiting);

    return sent;
}

RequestFuture
ConnMan::SendBuffer(
    Connection & connection,
    ConnMan::SendType sendType,
    uint8_t* buffer,
    uint32_t buffersize,
    uint32_t& token
)
{
    Packet packet;
    
    RequestFuture barrier_future;

    memset(packet.buffer, 0, MAX_PACKET_SIZE);
    packet.buffersize = sizeof(MESG::HEADER) + sizeof(GENERAL);
    packet.address = connection.who;

    MESG* pMsg = (MESG*)packet.buffer;
    AddMagic(pMsg);
    pMsg->header.code = (uint8_t)MESG::HEADER::Codes::General;
    pMsg->header.id = connection.id;
    
    

    packet.ackhandler = nullptr;
    if (sendType == ConnMan::SendType::WITHRECEIPT)
    {
        pMsg->header.mode = (uint8_t)MESG::HEADER::Mode::Reliable;
        pMsg->header.seq = connection.curseq; // Incremented on receipt of ACK
        token = pMsg->header.seq;

        RequestStatus status;
        status.promise = std::make_shared<RequestPromise>();
        status.seq = pMsg->header.seq;
        status.starttime = clock::now();
        status.endtime = clock::now();
        status.state = RequestStatus::State::PENDING;
        barrier_future = status.promise->get_future(); 
        status.packet = packet;
        AddRequestStatus(connection, status, status.seq);
    }
    else
    {
        pMsg->header.mode = (uint8_t)MESG::HEADER::Mode::Unreliable;
        pMsg->header.seq = InterlockedIncrement(&connection.curuseq);
        token = pMsg->header.seq;
    }
    Network::write(cmstate.netstate, packet);
    return barrier_future;
}
Connection::ConnectingResult
ConnMan::Connect(
    Connection& connection,
    uint32_t randomcode,
    std::string gamename,
    std::string gamepass
)
{
    connection.connectingresultpromise = std::make_shared<ConnectingResultPromise>();
    ConnectingResultFuture cmfuture = connection.connectingresultpromise->get_future();
    SendIdentify(connection, randomcode, gamename, gamepass);
    
    Connection::ConnectingResult state = cmfuture.get();
    return state;
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
void
ConnMan::SendPongTo(
    Connection & connection,
    uint32_t ack
)
{
    Packet packet;
    memset(packet.buffer, 0, MAX_PACKET_SIZE);
    packet.buffersize = sizeof(MESG::HEADER) + sizeof(PINGPONG);
    packet.address = connection.who;

    MESG* pMsg = (MESG*)packet.buffer;
    AddMagic(pMsg);
    pMsg->header.mode = (uint8_t)MESG::HEADER::Mode::Unreliable;
    pMsg->header.code = (uint8_t)MESG::HEADER::Codes::Pong;
    pMsg->header.id = connection.id;
    pMsg->header.seq = ack;

    Network::write(cmstate.netstate, packet);
    return;
}
//RequestFuture
void
ConnMan::SendPingTo(
    Connection & connection//,
    //uint32_t & request_index
)
{
    Packet packet;
    //RequestFuture future;
    memset(packet.buffer, 0, MAX_PACKET_SIZE);
    packet.buffersize = sizeof(MESG::HEADER) + sizeof(PINGPONG);
    packet.address = connection.who;

    MESG* pMsg = (MESG*)packet.buffer;
    AddMagic(pMsg);
    pMsg->header.mode = (uint8_t)MESG::HEADER::Mode::Unreliable;
    pMsg->header.code = (uint8_t)MESG::HEADER::Codes::Ping;
    pMsg->header.id = connection.id;
    pMsg->header.seq = InterlockedIncrement(&connection.curuseq);

    RequestStatus status;
    status.promise = std::make_shared<RequestPromise>();
    status.seq = pMsg->header.seq;
    status.starttime = clock::now();
    status.endtime = clock::now();
    status.state = RequestStatus::State::PENDING;
    //future = status.promise->get_future();
    status.packet = packet;
    //request_index = status.seq;
    AddRequestStatus(connection, status, status.seq);
    //connection.reqpingsmutex.lock();
    //connection.reqpings.emplace(status.seq, status);
    //connection.reqpingsmutex.unlock();

    Network::write(cmstate.netstate, packet);
    return;
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
ConnMan::UpdateConnection(
    Connection* pConn
)
{
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
        // For each request, if is pending too long
        // time it out.
        // for General comms
        //
        pConn->reqstatusmutex.lock();
        auto pReq = pConn->reqstatus.begin();
        //while (pReq != pConn->reqstatus.end())
        for (;pReq != pConn->reqstatus.end(); pReq++)
        {
            if (pReq->second.state == RequestStatus::State::PENDING)
            {
                duration dur = clock::now() - pReq->second.starttime;
                if (dur.count() > cmstate.ack_timeout_ms)
                {
                    pReq->second.endtime = clock::now();
                    pReq->second.state = RequestStatus::State::DYING;
                    pReq->second.retries = 0;
                    cmstate.onevent(cmstate.oneventcontext,
                                    ConnManState::OnEventType::MESSAGE_ACK_TIMEOUT,
                                    pConn,
                                    &pReq->second.packet);
                    pReq->second.promise->set_value(RequestStatus::RequestResult::TIMEDOUT);
                }
            }
            else if (pReq->second.state == RequestStatus::State::SUCCEEDED)
            {
                pReq->second.state = RequestStatus::State::DYING;
                cmstate.onevent(cmstate.oneventcontext,
                                ConnManState::OnEventType::MESSAGE_ACK_RECEIVED,
                                pConn,
                                &pReq->second.packet);
                pReq->second.promise->set_value(RequestStatus::RequestResult::ACKNOWLEDGED);
            }
        }
        pConn->reqstatusmutex.unlock();

        //
        // Send pings, that we use to track connection
        // health.
        //
        duration d = clock::now() - pConn->heartbeat;
        if (d.count() > cmstate.heart_beat_ms)
        {
            pConn->heartbeat = clock::now();
            SendPingTo(*pConn);
        }

        //
        // Remove requests that are Dead
        // 
        ReapDeadRequests(pConn);

        d = clock::now() - pConn->lastrxtime;
        if (d.count() > cmstate.timeout_ms)
        {
            if (pConn->locality == Connection::Locality::REMOTE)
            {
                pConn->state = Connection::State::DEAD;
            }
            else
            {// We're treating it as a Warning
                pConn->state = Connection::State::ALIVE;
                pConn->lastrxtime = clock::now();
            }

            //pConn->state = Connection::State::DYING;
            cmstate.onevent(cmstate.oneventcontext,
                            ConnManState::OnEventType::CONNECTION_TIMEOUT,
                            pConn,
                            nullptr);
        }
    } // If Alive


}
void
ConnMan::UpdateClientConnection(
    Connection* pConn
)
{

    //cmstate.connectionsmutex.lock();


    // Service outgoing reliable packets.
    //
    // We are only going to send a packet
    // if we are not currently waiting for an ack
    // from last packet. 
    if (pConn->state == Connection::State::GRANTED)
    {
        pConn->state = Connection::State::ALIVE;
        cmstate.onevent(cmstate.oneventcontext,
            ConnManState::OnEventType::CONNECTION_HANDSHAKE_GRANTED,
            pConn,
            nullptr);
        pConn->connectingresultpromise->set_value(Connection::ConnectingResult::GRANTED);
    }

    UpdateConnection(pConn);

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
            pConn->connectingresultpromise->set_value(Connection::ConnectingResult::TIMEDOUT);
        }
    }

    if (pConn->state == Connection::State::DENIED)
    {
        pConn->state = Connection::State::DEAD;
        cmstate.onevent(cmstate.oneventcontext,
                        ConnManState::OnEventType::CONNECTION_HANDSHAKE_DENIED,
                        pConn,
                        nullptr);
        pConn->connectingresultpromise->set_value(Connection::ConnectingResult::DENIED);
    }


    //cmstate.connectionsmutex.unlock();
}

void
ConnMan::UpdateServerConnections(
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
    UpdateConnection(pConn);

    if (pConn->state == Connection::State::ALIVE)
    {
        duration d = clock::now() - pConn->heartbeat;
        if (d.count() > cmstate.heart_beat_ms)
        {
            pConn->heartbeat = clock::now();
            SendPingTo(*pConn);
        }
    }
}

void
ConnMan::Update(
    uint32_t ms_elapsed
)
{
    cmstate.timeticks += ms_elapsed;
    if (cmstate.cmtype == ConnManState::ConnManType::SERVER)
    {
        cmstate.connectionsmutex.lock();
        for (auto& pConn : cmstate.connections)
        {
            UpdateServerConnections(&pConn);
        }
        ReapDeadConnections();
    }
    else if (cmstate.cmtype == ConnManState::ConnManType::CLIENT)
    {
        UpdateClientConnection(&cmstate.localconn);
    }

    if (cmstate.cmtype == ConnManState::ConnManType::SERVER)
    {
        cmstate.connectionsmutex.unlock();
    }
}

void
ConnMan::ReapDeadConnections(
)
{
    // Reap Dead Connections
    //
    auto pConn = cmstate.connections.begin();
    while (pConn != cmstate.connections.end())
    {
        if (pConn->state == Connection::State::DEAD)
        {
            pConn = cmstate.connections.erase(pConn);
        }
        else
        {
            pConn++;
        }
    }
}

void
ConnMan::KillDyingRequests(
    Connection* pConn
)
{
    pConn->reqstatusmutex.lock();
    for (auto & pReq : pConn->reqstatus)
    {
        if (pReq.second.state == RequestStatus::State::DYING)
        {
            pReq.second.state = RequestStatus::State::DEAD;
            std::cout << "Killing\n";
        }
    }
    pConn->reqstatusmutex.unlock();
}

void
ConnMan::ReapDeadRequests(
    Connection* pConn
)
{
    pConn->reqstatusmutex.lock();
    auto pReq = pConn->reqstatus.begin();
    while (pReq != pConn->reqstatus.end())
    {
        if (pReq->second.state == RequestStatus::State::DEAD)
        {
            pReq = pConn->reqstatus.erase(pReq);
        }
        else
        {
            pReq++;
        }
    }
    pConn->reqstatusmutex.unlock();
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
ConnMan::ProcessPing(
    Connection* pConn,
    Request* request
)
{
    if (pConn)
    {
        MESG* msg = (MESG*)request->packet.buffer;
        // Send Pong to whoever sent this to us
        SendPongTo(*pConn, msg->header.seq);
        pConn->lastrxtime = clock::now();
    }
}

void
ConnMan::ProcessPong(
    Connection* pConn,
    Request* request
)
{
    if (pConn)
    {
        MESG* msg = (MESG*)request->packet.buffer;

        pConn->reqstatusmutex.lock();
        auto pReq = pConn->reqstatus.find(msg->header.seq);
        pConn->reqstatusmutex.unlock();

        if (pReq != pConn->reqstatus.end())
        {
            if (pReq->second.state == RequestStatus::State::PENDING)
            {
                clock::time_point startTime = pReq->second.starttime;
                clock::time_point endTime = clock::now();
                pConn->lastrxtime = endTime;

                RemoveRequestStatus(*pConn, msg->header.seq);

                pConn->curping = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
                pConn->pingtimes.push_back(pConn->curping);
                if (pConn->pingtimes.size() > 15)
                    pConn->pingtimes.pop_front();

                uint32_t cnt = 0;
                float avg = 0;
                for (auto p : pConn->pingtimes)
                {
                    avg+= p ;
                    cnt++;
                }
                avg /= cnt;
                pConn->avgping = avg;
                //TODO min, max ping
            }
            else
            {
                // Request has already been FAILED, but not yet removed
                // from the request map
                // ??
                std::cout << "Fact: Rx'd Ack for FAILED request: " << msg->header.seq << std::endl;
            }
        }
        else
        {
            // Request has already been FAILED, and removed
            // from the request map
            // ??
            std::cout << "Fact: Rx'd Ack for old FAILED request: " << msg->header.seq << std::endl;
        }
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
    // HACK FUZZ
    auto random_integer = uni(rng);
    if (random_integer < 20000)
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

        auto pReq = pConn->GetRequestStatus(ack); // Retrieves RequestStatus associated
                                                  // with an ACK
        if (pReq != pConn->reqstatus.end())
        {
            if (pReq->second.state == RequestStatus::State::PENDING)
            {
                InterlockedIncrement(&pConn->curseq);
                pConn->lastrxtime = clock::now();
                pReq->second.endtime = clock::now();
                pReq->second.state = RequestStatus::State::SUCCEEDED;
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
                else if (IsCode(request->packet, MESG::HEADER::Codes::Ping))
                {
                    // Rx'd an Ping. 
                    // Something is eagerly awaiting our response
                    Connection* pConn = &cm.cmstate.localconn;
                    cm.ProcessPing(pConn, request);
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::Pong))
                {
                    // Rx'd an Pong. 
                    // We are eagerly awaiting this pong
                    Connection* pConn = &cm.cmstate.localconn;
                    cm.ProcessPong(pConn, request);
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
                // HACK FUZZ
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
                else if (IsCode(request->packet, MESG::HEADER::Codes::Ping))
                {
                    // Rx'd an Ping. 
                    // Something is eagerly awaiting our response
                    uint32_t cid = GetConnectionId(request->packet);
                    cm.cmstate.connectionsmutex.lock();
                    Connection* pConn = ConnMan::GetConnectionById(cm.cmstate.connections, cid);
                    cm.cmstate.connectionsmutex.unlock();
                    cm.ProcessPing(pConn, request);
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::Pong))
                {
                    // Rx'd an Pong. 
                    // We are eagerly awaiting this pong
                    uint32_t cid = GetConnectionId(request->packet);
                    cm.cmstate.connectionsmutex.lock();
                    Connection* pConn = ConnMan::GetConnectionById(cm.cmstate.connections, cid);
                    cm.cmstate.connectionsmutex.unlock();
                    cm.ProcessPong(pConn, request);
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

    return;
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
    for (auto & c : connections)
    {
        if (c.playername == name && c.id == id)
        {
            available = false;
            break;
        }
    }
    return available;
}

std::map<uint32_t, RequestStatus>::iterator
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