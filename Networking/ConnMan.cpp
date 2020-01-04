#include "ConnManUtils.h"
#include <sstream>
#include <random>
#include <future>
namespace bali
{



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
    NetworkState& netstate,
    Connection::ConnectionPtr pConn,
    uint8_t* buffer,
    uint32_t buffersize,
    uint32_t& request_index
)
{
    bool sent = false;

    std::future_status wait_status;
    RequestFuture barrier_future;

    barrier_future =
        SendBuffer(netstate,
                   pConn,
                   ConnMan::SendType::WITHRECEIPT,
                   (uint8_t*)buffer,
                   buffersize,
                   request_index);

    bool deferred = false;
    bool timeout = false;
    bool ready = false;
    bool waiting = true;

    //std::cout << "Waiting For Receipt...";
    do
    {
        wait_status = barrier_future.wait_for(std::chrono::milliseconds(1));

        if (wait_status == std::future_status::timeout)
        {
            timeout = true;
            waiting = true;
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

    pConn->RemoveRequestStatus(request_index);

    return sent;
}

RequestFuture
ConnMan::SendBuffer(
    NetworkState& netstate,
    Connection::ConnectionPtr pConn,
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
    packet.address = pConn->who;

    MESG* pMsg = (MESG*)packet.buffer;
    AddMagic(pMsg);
    pMsg->header.code = (uint8_t)MESG::HEADER::Codes::General;
    pMsg->header.id = pConn->id;
    
    

    packet.ackhandler = nullptr;
    if (sendType == ConnMan::SendType::WITHRECEIPT)
    {
        pMsg->header.mode = (uint8_t)MESG::HEADER::Mode::Reliable;
        pMsg->header.seq = pConn->curseq; // Incremented on receipt of ACK
        token = pMsg->header.seq;

        RequestStatus status;
        status.promise = std::make_shared<RequestPromise>();
        status.seq = pMsg->header.seq;
        status.starttime = clock::now();
        status.endtime = clock::now();
        status.state = RequestStatus::State::PENDING;
        barrier_future = status.promise->get_future(); 
        status.packet = packet;
        pConn->AddRequestStatus(status, status.seq);
    }
    else
    {
        pMsg->header.mode = (uint8_t)MESG::HEADER::Mode::Unreliable;
        pMsg->header.seq = InterlockedIncrement(&pConn->curuseq);
        token = pMsg->header.seq;
    }
    Network::write(netstate, packet);
    return barrier_future;
}
Connection::ConnectingResult
Connection::Connect(
    uint32_t randomcode,
    std::string gamename,
    std::string gamepass
)
{
    connectingresultpromise = std::make_shared<ConnectingResultPromise>();
    ConnectingResultFuture cmfuture = connectingresultpromise->get_future();
    Packet packet = CreateIdentifyPacket(who, playername, curuseq, randomcode, gamename, gamepass);
    Connection::ConnectingResult state = cmfuture.get();
    return state;
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
    Connection::ConnectionPtr pConn
)
{
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
                            nullptr,
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
        for (;pReq != pConn->reqstatus.end(); pReq++)
        {
            if (pReq->second.state == RequestStatus::State::PENDING)
            {
                duration dur = clock::now() - pReq->second.starttime;
                if (dur.count() > cmstate.ack_timeout_ms)
                {
                    uint32_t lastState = InterlockedCompareExchange((uint32_t*)&pReq->second.state, (uint32_t)RequestStatus::State::DYING, (uint32_t)RequestStatus::State::PENDING);
                    if (lastState == (uint32_t)RequestStatus::State::PENDING)
                    {
                        pReq->second.endtime = clock::now();
                        cmstate.onevent(cmstate.oneventcontext,
                                        ConnManState::OnEventType::MESSAGE_ACK_TIMEOUT,
                                        nullptr,
                                        &pReq->second.packet);
                        pReq->second.promise->set_value(RequestStatus::RequestResult::TIMEDOUT);
                    }
                }
            }
            else if (pReq->second.state == RequestStatus::State::SUCCEEDED)
            {
                uint32_t lastState = InterlockedCompareExchange((uint32_t*)&pReq->second.state, (uint32_t)RequestStatus::State::DYING, (uint32_t)RequestStatus::State::SUCCEEDED);
                if (lastState == (uint32_t)RequestStatus::State::SUCCEEDED)
                {
                    //pReq->second.state = RequestStatus::State::DYING;
                    cmstate.onevent(cmstate.oneventcontext,
                                    ConnManState::OnEventType::MESSAGE_ACK_RECEIVED,
                                    nullptr,
                                    &pReq->second.packet);
                    pReq->second.promise->set_value(RequestStatus::RequestResult::ACKNOWLEDGED);
                }
            }
        }
        //
        // Remove requests that are Dead
        // 
        ReapDeadRequests(pConn);
        pConn->reqstatusmutex.unlock();


        //
        // Kill this connection if it times out
        //
        duration d = clock::now() - pConn->lastrxtime;
        if (d.count() > cmstate.timeout_ms)
        {
            //pConn->state = Connection::State::DEAD;
            uint32_t lastState = InterlockedCompareExchange((uint32_t*)&pConn->state, (uint32_t)Connection::State::DEAD, (uint32_t)Connection::State::ALIVE);
            if (lastState == (uint32_t)Connection::State::ALIVE)
            {
                //pConn->state = Connection::State::DYING;
                cmstate.onevent(cmstate.oneventcontext,
                                ConnManState::OnEventType::CONNECTION_TIMEOUT,
                                nullptr,
                                nullptr);
            }
        }
    } // If Alive
}
void
ConnMan::UpdateClientConnection(
    Connection::ConnectionPtr pConn
)
{
    // Service outgoing reliable packets.
    //
    // We are only going to send a packet
    // if we are not currently waiting for an ack
    // from last packet. 
    if (pConn->state == Connection::State::IDENTIFYING)
    {
        duration d2 = clock::now() - pConn->lasttxtime;
        if (d2.count() > cmstate.timeout_ms)
        {
            uint32_t lastState = InterlockedCompareExchange((uint32_t*)&pConn->state, (uint32_t)Connection::State::DYING, (uint32_t)Connection::State::IDENTIFYING);
            if (lastState == (uint32_t)Connection::State::IDENTIFYING)
            {
                //pConn->state = Connection::State::DEAD;
                cmstate.onevent(cmstate.oneventcontext,
                    ConnManState::OnEventType::CONNECTION_HANDSHAKE_TIMEOUT,
                    nullptr,
                    nullptr);
                pConn->connectingresultpromise->set_value(Connection::ConnectingResult::TIMEDOUT);
            }
        }
    }

    uint32_t lastState = InterlockedCompareExchange((uint32_t*)&pConn->state, (uint32_t)Connection::State::ALIVE, (uint32_t)Connection::State::GRANTED);
    if (lastState == (uint32_t)Connection::State::GRANTED)
    {
        //pConn->state = Connection::State::ALIVE;
        cmstate.onevent(cmstate.oneventcontext,
            ConnManState::OnEventType::CONNECTION_HANDSHAKE_GRANTED,
            nullptr,
            nullptr);
        pConn->connectingresultpromise->set_value(Connection::ConnectingResult::GRANTED);
    }
    
    //if (pConn->state == Connection::State::DENIED)

    lastState = InterlockedCompareExchange((uint32_t*)&pConn->state, (uint32_t)Connection::State::DEAD, (uint32_t)Connection::State::DENIED);

    if (lastState == (uint32_t)Connection::State::DENIED)
    {
        ///pConn->state = Connection::State::DEAD;
        cmstate.onevent(cmstate.oneventcontext,
            ConnManState::OnEventType::CONNECTION_HANDSHAKE_DENIED,
            nullptr,
            nullptr);
        pConn->connectingresultpromise->set_value(Connection::ConnectingResult::DENIED);
    }

    UpdateConnection(pConn);
}

void
ConnMan::UpdateServerConnections(
    Connection::ConnectionPtr  pConn
)
{
    uint32_t lastState = InterlockedCompareExchange((uint32_t*)&pConn->state, (uint32_t)Connection::State::ALIVE, (uint32_t)Connection::State::GRANTED);
    if (lastState == (uint32_t)Connection::State::GRANTED)
    //if (pConn->state == Connection::State::GRANTED)
    {
        //pConn->state = Connection::State::ALIVE;
        cmstate.onevent(cmstate.oneventcontext,
            ConnManState::OnEventType::CONNECTION_HANDSHAKE_GRANTED,
            nullptr,
            nullptr);
    }
    

    if (pConn->state == Connection::State::ALIVE)
    {
        duration d = clock::now() - pConn->heartbeat;
        if (d.count() > cmstate.heart_beat_ms)
        {
            pConn->heartbeat = clock::now();
            Packet packet = CreatePingPacket(pConn->who, pConn->id, pConn->curuseq);


            RequestStatus status;
            status.promise = std::make_shared<RequestPromise>();
            status.seq = GetPacketSequence(packet);
            status.starttime = clock::now();
            status.endtime = clock::now();
            status.state = RequestStatus::State::PENDING;
            //future = status.promise->get_future();
            status.packet = packet;
            //request_index = status.seq;

            pConn->AddRequestStatus(status, status.seq);

            Network::write(cmstate.netstate, packet);
        }
    }
    UpdateConnection(pConn);
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
            UpdateServerConnections(pConn);
        }
        
    }
    else if (cmstate.cmtype == ConnManState::ConnManType::CLIENT)
    {
        for (auto& pConn : cmstate.connections)
        {
            UpdateClientConnection(pConn);
        }
    }

    ReapDeadConnections();

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
        if ((*pConn)->state == Connection::State::DEAD)
        {
            //if ((*pConn)->reqstatus.size() > 0)
            //{
            //    pConn++;
            //    continue;
            //}
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
    Connection::ConnectionPtr pConn
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
    Connection::ConnectionPtr pConn
)
{
    //pConn->reqstatusmutex.lock();
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
    //pConn->reqstatusmutex.unlock();
}

void
ConnMan::ProcessGrant(
    Connection::ConnectionPtr pConn,
    Request* request
)
{
    uint32_t cid = GetConnectionId(request->packet);
    
    if (cid == pConn->id)
    {
        pConn->lastrxtime = clock::now();
        pConn->id = cid;
        pConn->state = Connection::State::GRANTED;
    }
    else
    {
        std::cout << "Weird: Client Rx Grant contains unknown Player Name\n";
    }
}
void
ConnMan::ProcessDeny(
    Connection::ConnectionPtr pConn,
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
    Connection::ConnectionPtr pConn,
    Request* request
)
{
    if (pConn)
    {
        MESG* msg = (MESG*)request->packet.buffer;
        // Send Pong to whoever sent this to us
        Packet packet = CreatePongPacket(pConn->who, pConn->id, pConn->curuseq, msg->header.seq);
        Network::write(cmstate.netstate, packet);
        pConn->lastrxtime = clock::now();
    }
}

void
ConnMan::ProcessPong(
    Connection::ConnectionPtr pConn,
    Request* request
)
{
    if (pConn)
    {
        MESG* msg = (MESG*)request->packet.buffer;

        pConn->reqstatusmutex.lock();
        auto pReq = pConn->reqstatus.find(msg->header.ack);
        pConn->reqstatusmutex.unlock();

        if (pReq != pConn->reqstatus.end())
        {
            if (pReq->second.state == RequestStatus::State::PENDING)
            {
                clock::time_point startTime = pReq->second.starttime;
                clock::time_point endTime = clock::now();
                pConn->lastrxtime = endTime;

                pConn->RemoveRequestStatus(msg->header.ack);

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
                std::cout << "Fact: Rx'd Pong for FAILED request: " << msg->header.seq << std::endl;
            }
        }
        else
        {
            // Request has already been FAILED, and removed
            // from the request map
            // ??
            std::cout << "Fact: Rx'd Pong for old FAILED request: " << msg->header.seq << std::endl;
        }
    }
}

void
ConnMan::ProcessGeneral(
    Connection::ConnectionPtr pConn,
    Request* request
)
{
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
            Packet packet = CreateAckPacket(pConn->who, pConn->id, pConn->curuseq, msg->header.seq);
            Network::write(cmstate.netstate, packet);
        }
    }
    else
    {
        std::cout << "Client Rx Packet from unknown origin: tossing." << std::endl;
    }
}

void
ConnMan::ProcessAck(
    Connection::ConnectionPtr pConn,
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

    std::random_device rd;     // only used once to initialise (seed) engine
    std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)
    std::uniform_int_distribution<uint32_t> uni(1, 65536); // guaranteed unbiased
                                                           // HACK FUZZ
    auto random_integer = uni(rng);
    if (random_integer < 1000)
    {
        std::cout << "Simulation: Dropping General Packet" << std::endl;
        return;
    }

    //cm.cmstate.connectionsmutex.lock();
    if (request->ioType == Request::IOType::READ)
    {
        MESG* m = (MESG*)request->packet.buffer;
        std::cout << "Client Rx: " << CodeName[m->header.code] << "[" << m->header.id << "]" << "[" << m->header.seq << "]" << "[" << m->header.ack << "]" << std::endl;


        if (IsMagicGood(request->packet))
        {
            if (IsSizeValid(request->packet))
            {

                Connection::ConnectionPtr pConn;
                uint32_t cid;
                cid = GetConnectionId(request->packet);
                cm.cmstate.connectionsmutex.lock();
                //pConn = ConnMan::GetConnectionById(cm.cmstate.connectionsmutex, cm.cmstate.connections, 0);
                pConn = *cm.cmstate.connections.begin();
                cm.cmstate.connectionsmutex.unlock();

                assert(pConn != nullptr);

                if (IsCode(request->packet, MESG::HEADER::Codes::Grant))
                {
                    //
                    // If we're receiving a Grant,
                    // then the associated connection has not yet been assigned an Id.
                    // Therefore, we find the associated connection
                    // by name
                    //
                    //Connection* pConn = &cm.cmstate.localconn;

                    // TODO BIG TIME
                    //pConn = ConnMan::GetConnectionById(cm.cmstate.connectionsmutex, cm.cmstate.connections, cid);
                    cm.ProcessGrant(pConn, request);
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::Deny))
                {
                    //Connection* pConn = &cm.cmstate.localconn;
                    cm.ProcessDeny(pConn, request);
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::Ack))
                {
                    // Rx'd an ACk. 
                    // Something is eagerly awaiting this i'm sure.
                    //Connection* pConn = &cm.cmstate.localconn;
                    cm.ProcessAck(pConn, request);
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::Ping))
                {
                    // Rx'd an Ping. 
                    // Something is eagerly awaiting our response
                    //Connection* pConn = &cm.cmstate.localconn;
                    cm.ProcessPing(pConn, request);
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::Pong))
                {
                    // Rx'd an Pong. 
                    // We are eagerly awaiting this pong
                    //Connection* pConn = &cm.cmstate.localconn;
                    cm.ProcessPong(pConn, request);
                }
                else
                {
                    //
                    // Rx'd something other than Identify, or Ack
                    //
                    //Connection* pConn = &cm.cmstate.localconn;
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
    //cm.cmstate.connectionsmutex.unlock();
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
                std::string pn = GetPlayerName(request->packet);
                uint16_t id = GetConnectionId(request->packet);
                Connection::ConnectionPtr pConn = nullptr;

                //cmstate.connectionsmutex.lock();
                bool isnameavail = ConnMan::IsPlayerNameAndIdAvailable(cmstate.connectionsmutex, cmstate.connections, pn, id);
                if (!isnameavail)
                {
                    pConn = GetConnectionById(cmstate.connectionsmutex, cmstate.connections, id);
                }
                //cmstate.connectionsmutex.unlock();

                if (isnameavail)
                {
                    //
                    // Grant
                    // Create and insert a new connection, 
                    // randomly generate id, then send a Grant.
                    //
                    //Connection::ConnectionPtr pConn = std::make_shared<Connection>();
                    //pConn->Initialize(request->packet.address, pn, id, Connection::Locality::REMOTE, Connection::State::GRANTED);
                    //
                    //cmstate.connectionsmutex.lock();
                    //cmstate.connections.push_back(pConn);
                    //cmstate.connectionsmutex.unlock();
                    Connection::ConnectionPtr pConn = 
                    cmstate.CreateConnection(
                        request->packet.address,
                        pn,
                        id,
                        Connection::Locality::REMOTE,
                        Connection::State::GRANTED
                    );

                    cmstate.AddConnection(pConn);

                    Packet packet = CreateGrantPacket(pConn->who,
                                                      pConn->id,
                                                      pConn->curuseq,
                                                      pConn->playername);
                    Network::write(cmstate.netstate, packet);
                }
                else
                {
                    // This is a rebroadcast. Same Id, same Grant.
                    Packet packet = CreateGrantPacket(pConn->who,
                                                      pConn->id,
                                                      pConn->curuseq,
                                                      pConn->playername);
                    Network::write(cmstate.netstate, packet);
                }
            }
            else
            {
                // Deny - password doesn't match
                Packet packet = CreateDenyPacket(request->packet.address,
                                                 GetPlayerName(request->packet));
                Network::write(cmstate.netstate, packet);
                std::cout << "Tx Deny: Password is bad\n";
            }
        }
        else
        {
            // Deny - Game name doesn't match
            Packet packet = CreateDenyPacket(request->packet.address,
                                             GetPlayerName(request->packet));
            Network::write(cmstate.netstate, packet);
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
                                                           // HACK FUZZ
    auto random_integer = uni(rng);
    if (random_integer < 1000)
    {
        std::cout << "Simulation: Dropping General Packet" << std::endl;
        return;
    }

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
                Connection::ConnectionPtr pConn;
                uint32_t cid;
                cid = GetConnectionId(request->packet);
                cm.cmstate.connectionsmutex.lock();
                pConn = ConnMan::GetConnectionById(cm.cmstate.connectionsmutex, cm.cmstate.connections, cid);
                cm.cmstate.connectionsmutex.unlock();
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
                    cm.ProcessAck(pConn, request);
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::Ping))
                {
                    // Rx'd an Ping. 
                    // Something is eagerly awaiting our response
                    cm.ProcessPing(pConn, request);
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::Pong))
                {
                    // Rx'd an Pong. 
                    // We are eagerly awaiting this pong
                    cm.ProcessPong(pConn, request);
                }
                else
                {
                    //
                    // Rx'd something other than Identify, or Ack
                    //
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



Connection::ConnectionPtr
ConnMan::GetConnectionById(
    Mutex & mutex,
    std::list<Connection::ConnectionPtr> & connections,
    uint32_t id
)
{
    mutex.lock();
    for (auto & c : connections)
    {
        if (id == c->id)
        {
            mutex.unlock();
            return c;
        }
    }
    mutex.unlock();
    return nullptr;
}

bool
ConnMan::RemoveConnectionByName(
    std::list<Connection::ConnectionPtr> & connections,
    std::string playername
)
{
    bool found = false;
    for (auto c = connections.begin(); c != connections.end(); c++)
    {
        if (playername == (*c)->playername)
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
    Mutex & mutex,
    std::list<Connection::ConnectionPtr> & connections,
    std::string name,
    uint16_t id
)
{
    bool available = true;
    mutex.lock();
    for (auto & c : connections)
    {
        if (c->playername == name && c->id == id)
        {
            available = false;
            break;
        }
    }
    mutex.unlock();
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

void
Connection::AddRequestStatus(
    RequestStatus & rs,
    uint32_t seq
)
{
    reqstatusmutex.lock();
    reqstatus.emplace(seq, rs);
    reqstatusmutex.unlock();
}

void
Connection::RemoveRequestStatus(
    uint32_t sid
)
{
    std::cout << "Removing Dead Request...\n";
    reqstatusmutex.lock();
    reqstatus.erase(sid);
    reqstatusmutex.unlock();
}

} // namespace bali