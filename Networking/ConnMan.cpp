#include "ConnMan.h"
#include <sstream>
#include <random>
#include <future>
namespace bali
{

#define BAD_NUMBER_A 1000       // client read
#define BAD_NUMBER_B 1000       // server read
#define BAD_NUMBER_C 1000       // all write

#define BAD_NUMBER_D 20000      // read general & ack
#define BAD_NUMBER_E 20000       // write General & Ack

//#define SHOW_PING_PRINTS

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
    cmstate.globalrxmutex.create();

    cmstate.onevent = onevent;
    cmstate.oneventcontext = oneventcontext;

    if (cmstate.cmtype == ConnManState::ConnManType::SERVER ||
        cmstate.cmtype == ConnManState::ConnManType::PASS_THROUGH)
    {
        Network::initialize(cmstate.netstate, 8, thisport, ConnMan::ConnManServerIOHandler, this);
    }
    else if (cmstate.cmtype == ConnManState::ConnManType::CLIENT)
    {
        Network::initialize(cmstate.netstate, 1, thisport, ConnMan::ConnManClientIOHandler, this);
    }
    Network::start(cmstate.netstate);
}

void
ConnMan::Initialize(
    NetworkConfig & netcfg,
    ConnManState::ConnManType cmtype,
    uint32_t thisport,
    ConnManState::OnEvent onevent,
    void* oneventcontext
)
{
    srand(1523791);

    cmstate.cmtype = cmtype;
    cmstate.thisport = thisport;

    cmstate.numplayers = 0;
    cmstate.gamename = "";
    cmstate.gamepass = "";

    cmstate.timeout_warning_ms = netcfg.TIMEOUT_WARNING_MS;
    cmstate.timeout_ms = netcfg.TIMEOUT_MS;
    cmstate.ack_timeout_ms = netcfg.ACK_TIMEOUT_MS;
    cmstate.heart_beat_ms = netcfg.HEART_BEAT_MS;
    cmstate.retry_count = netcfg.RETRY_COUNT;

    cmstate.done = 0;
    cmstate.connectionsmutex.create();
    cmstate.globalrxmutex.create();

    cmstate.onevent = onevent;
    cmstate.oneventcontext = oneventcontext;

    Network::initialize(cmstate.netstate, 8, thisport, ConnMan::ConnManServerIOHandler, this);

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
ConnMan::Write(
    Packet& packet
)
{
    MESG* m = (MESG*)packet.buffer;
    Network::write(cmstate.netstate, packet);
}

void
ConnMan::Cleanup(
)
{
    Network::stop(cmstate.netstate);
    cmstate.connectionsmutex.destroy();
}

RequestFuture
ConnMan::SendBuffer(
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
        pMsg->header.ack = pConn->curack;
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
    Write(packet);
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
    Packet packet = ConnMan::CreateIdentifyPacket(who, playername, curuseq, curack, randomcode, gamename, gamepass);
    Connection::ConnectingResult state = cmfuture.get();
    return state;
}


uint32_t
ConnMan::ExtractSequenceFromPacket(
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

    //
    // Service incoming packets
    //
    while (!pConn->rxpackets.empty())
    {
        Packet packet;
        packet = pConn->rxpackets.front();
        pConn->rxpackets.pop();

        // TODO
        // Basically -- Process General....
        // on.event(MESSAGE_RECEIVED);
    }
    //
    // Send Pings
    //
    if (pConn->state == Connection::State::ALIVE)
    {
        duration d = clock::now() - pConn->heartbeat;
        if (d.count() > cmstate.heart_beat_ms)
        {
            pConn->heartbeat = clock::now();
            Packet packet = ConnMan::CreatePingPacket(pConn->who, pConn->id, pConn->curuseq, pConn->curack);

            // When we recieve a Pong
            // we will only be interested
            // in ack's that match curpingseq/
            // This implies one ping at a time,
            // or heart_beat_ms > ack_timeout_ms
            pConn->pingstart = clock::now();
            pConn->curpingseq = ConnMan::ExtractSequenceFromPacket(packet);
            pConn->totalpings++;
            RequestStatus status;
            status.promise = std::make_shared<RequestPromise>();
            status.seq = ConnMan::ExtractSequenceFromPacket(packet);
            status.starttime = clock::now();
            status.endtime = clock::now();
            status.state = RequestStatus::State::PENDING;
            //future = status.promise->get_future();
            status.packet = packet;
            //request_index = status.seq;
            pConn->AddRequestStatus(status, status.seq);

            Write(packet);
        }
    } // If Alive

    //
    // Process this connections pending Requests
    //
    if (pConn->state == Connection::State::ALIVE)
    {
        //
        // For each request, if is pending too long
        // time it out.
        // for General comms
        //
        pConn->reqstatusmutex.lock();
        auto pReq = pConn->reqstatus.begin();
        for (; pReq != pConn->reqstatus.end(); pReq++)
        {
            if (pReq->second.state == RequestStatus::State::PENDING)
            {
                duration dur = clock::now() - pReq->second.starttime;
                if (dur.count() > cmstate.ack_timeout_ms)
                {
                    MESG* pMsg = (MESG*)pReq->second.packet.buffer;
                    RequestStatus::State  stateToBe = RequestStatus::State::DEAD;

                    if (IsCode(pReq->second.packet, MESG::HEADER::Codes::Ping))
                    {// Pings are not reclimaed elsewhere.
                            stateToBe = RequestStatus::State::DEAD;
                    }
                    else
                    {// This is For General packets. they are reclaimed elsewhere.
                        stateToBe = RequestStatus::State::DYING;
                    }

                    pReq->second.state = stateToBe;
                    pReq->second.endtime = clock::now();
                    cmstate.onevent(cmstate.oneventcontext,
                        ConnManState::OnEventType::MESSAGE_ACK_TIMEOUT,
                        nullptr,
                        &pReq->second.packet);
                    pReq->second.promise->set_value(RequestStatus::RequestResult::TIMEDOUT);
                }
            }
            else if (pReq->second.state == RequestStatus::State::ACKNOWLEDGED)// Ping will never be this; as it will be removed on succeed.
            {
                pReq->second.state = RequestStatus::State::DYING;
                cmstate.onevent(cmstate.oneventcontext,
                                ConnManState::OnEventType::MESSAGE_ACK_RECEIVED,
                                nullptr,
                                &pReq->second.packet);
                pReq->second.promise->set_value(RequestStatus::RequestResult::ACKNOWLEDGED);
            }
        }
        //
        // Remove requests that are Dead
        // 
        pConn->ReapDeadRequests();
        pConn->reqstatusmutex.unlock();
    }
    //
    // Kill this connection if it times out
    //
    duration d = clock::now() - pConn->lastrxtime;
    if (pConn->state == Connection::State::ALIVE)
    {
        if (d.count() > cmstate.timeout_ms)
        {
            pConn->state = Connection::State::DEAD;
            cmstate.onevent(cmstate.oneventcontext,
                            ConnManState::OnEventType::CONNECTION_TIMEOUT,
                            nullptr,
                            nullptr);
        }
    }
}

void
ConnMan::UpdateRelayMachine(
)
{
    //
    // Service incoming packets
    //
    while (!cmstate.globalrxqueue.empty())
    {
        Packet packet;
        packet = cmstate.globalrxqueue.front();
        cmstate.globalrxqueue.pop();

        // Basically -- Process General....
        // on.event(MESSAGE_RECEIVED);
        cmstate.onevent(cmstate.oneventcontext,
                        ConnManState::OnEventType::MESSAGE_RECEIVED,
                        nullptr,
                        &packet);
    }
}

void
ConnMan::UpdateClientConnection(
    Connection::ConnectionPtr pConn
)
{
    if (pConn->state == Connection::State::IDENTIFIED)
    {
        duration d2 = clock::now() - pConn->lasttxtime;
        if (d2.count() > cmstate.timeout_ms)
        {
            pConn->state = Connection::State::DYING;
            cmstate.onevent(cmstate.oneventcontext,
                            ConnManState::OnEventType::CONNECTION_HANDSHAKE_TIMEOUT,
                            nullptr,
                            nullptr);

            Connection::ConnectingResult cr;
            cr.code = Connection::ConnectingResultCode::TIMEDOUT;
            pConn->connectingresultpromise->set_value(cr);
        }
    }

    if (pConn->state == Connection::State::GRACKING)
    {
        pConn->state = Connection::State::ALIVE;
        //
        // Send Grack
        //
        Packet packet = ConnMan::CreateGrackPacket(pConn->who, pConn->id, pConn->curuseq, pConn->curack);
        Write(packet);

        cmstate.onevent(cmstate.oneventcontext,
                        ConnManState::OnEventType::CONNECTION_HANDSHAKE_GRANTED,
                        nullptr,
                        nullptr);
        Connection::ConnectingResult cr;
        cr.code = Connection::ConnectingResultCode::GRANTED;
        cr.id = pConn->id;
        pConn->connectingresultpromise->set_value(cr);
    }


    if (pConn->state == Connection::State::DENIED)
    {
        pConn->state = Connection::State::DYING;
        cmstate.onevent(cmstate.oneventcontext,
                        ConnManState::OnEventType::CONNECTION_HANDSHAKE_DENIED,
                        nullptr,
                        nullptr);

        Connection::ConnectingResult cr;
        cr.code = Connection::ConnectingResultCode::DENIED;
        pConn->connectingresultpromise->set_value(cr);
    }
}

void
ConnMan::UpdateServerConnections(
    Connection::ConnectionPtr  pConn
)
{
    if (pConn->state == Connection::State::IDENTIFIED)
    {
        pConn->state = Connection::State::GRANTED;
        Packet packet =
            ConnMan::CreateGrantPacket(pConn->who,
                pConn->id,
                pConn->curuseq,
                pConn->curack,
                pConn->playername);
        Write(packet);
    }

    if (pConn->state == Connection::State::GRANTED)
    {
        pConn->state = Connection::State::GRACKING;
        // Now we are left to wait for the Grack from Client
        cmstate.onevent(cmstate.oneventcontext,
                        ConnManState::OnEventType::CONNECTION_HANDSHAKE_GRANTED,
                        nullptr,
                        nullptr);
    }

    if (pConn->state == Connection::State::GRACKING)
    {// We transition out of this state if
     // A) we don't see a grack for timeout_ms (transitioned here)
     // B) or, we receive Pings and/or General packets. (transistioned in ProcessPing() and ProcessGeneral())
        duration d2 = clock::now() - pConn->lastrxtime;
        if (d2.count() > cmstate.timeout_ms)
        {
            pConn->state = Connection::State::DEAD;
            cmstate.onevent(cmstate.oneventcontext,
                            ConnManState::OnEventType::CONNECTION_HANDSHAKE_TIMEOUT_NOGRACK,
                            nullptr,
                            nullptr);
        }
    }
}

void
ConnMan::Update(
    uint32_t ms_elapsed
)
{
    cmstate.timeticks += ms_elapsed;

    if (cmstate.cmtype != ConnManState::ConnManType::PASS_THROUGH)
    {
        cmstate.globalrxmutex.lock();
        while (!cmstate.globalrxqueue.empty())
        {
            Packet packet = cmstate.globalrxqueue.front();
            cmstate.globalrxqueue.pop();

            uint32_t cid = ExtractConnectionIdFromPacket(packet);
            cmstate.connectionsmutex.lock();
            Connection::ConnectionPtr pConn = ConnMan::GetConnectionById(cmstate.connections, cid);
            cmstate.connectionsmutex.unlock();


            if (pConn)
            {
                if (ConnMan::IsCode(packet, MESG::HEADER::Codes::Grant)) {
                    // C Identifying -> Gracking
                    ConnMan::ProcessGrant(pConn, packet);
                }
                if (ConnMan::IsCode(packet, MESG::HEADER::Codes::Deny)) {
                    // C --> Identifying -> Denied
                    ConnMan::ProcessDeny(pConn, packet);
                }
                if (ConnMan::IsCode(packet, MESG::HEADER::Codes::Grack)) {
                    // S Gracking -> Alive
                    ConnMan::ProcessGrack(pConn, packet);
                }
                if (ConnMan::IsCode(packet, MESG::HEADER::Codes::Disconnect)) {
                    ConnMan::ProcessDisconnect(pConn);
                }
                if (ConnMan::IsCode(packet, MESG::HEADER::Codes::Ping)) {
                    // Sends Pong
                    ConnMan::ProcessPing(pConn, packet);
                }
                if (ConnMan::IsCode(packet, MESG::HEADER::Codes::Pong)) {
                    // FInd request with seq that matches pong's ack
                    // do some easy math, get rtt
                    // remove pings' request from list.
                    ProcessPong(pConn, packet);
                }
                if (ConnMan::IsCode(packet, MESG::HEADER::Codes::General)) {
                    //TODO -- the goods. oh..
                    ConnMan::ProcessGeneral(pConn, packet);
                }
                if (ConnMan::IsCode(packet, MESG::HEADER::Codes::Ack)) {
                    // Request: PENDING -> SUCCEEDED
                    ConnMan::ProcessAck(pConn, packet);
                }
            }
            else
            {
                std::cout << "Weird Packet From Weird Origin. Ignore.\n";
            }
        }
        cmstate.globalrxmutex.unlock();

        //////////////////////////////////////////
        // Basically, we update our understanding of the connnections
        // given the new (if any) packets processed above.
        //
        cmstate.connectionsmutex.lock();
        for (auto& pConn : cmstate.connections)
        {
            //
            // Common update path
            //
            UpdateConnection(pConn);

            if (cmstate.cmtype == ConnManState::ConnManType::SERVER)
            {
                UpdateServerConnections(pConn);
            }
            else if(cmstate.cmtype == ConnManState::ConnManType::CLIENT)
            {
                UpdateClientConnection(pConn);
            }
        }

        //
        // Reap Dead Connections
        //
        ReapDeadConnections();
        cmstate.connectionsmutex.unlock();
    }
    else // Relay
    {
        UpdateRelayMachine();
        //cmstate.globalrxmutex.lock();
        //while (!cmstate.globalrxqueue.empty())
        //{
        //    Packet packet = cmstate.globalrxqueue.front();
        //    cmstate.globalrxqueue.pop();


        //}
        //cmstate.globalrxmutex.unlock();
    }
}

void
ConnManState::RemoveConnection(
    uint32_t uid
)
{
    connectionsmutex.lock();
    
    for (auto pConn = connections.begin();
         pConn != connections.end();
         pConn++)
    {
        if ((*pConn)->id == uid)
        {
            std::cout << "Removing Connection: "<< (uint32_t)(*pConn)->state << "\n";
            connections.erase(pConn);
            break;
        }
    }
    connectionsmutex.unlock();
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
        Connection::ConnectionPtr Love = *pConn;
        if (Love->state == Connection::State::DEAD)
        {
            std::cout << "Reap Dead Connection\n";
            pConn = cmstate.connections.erase(pConn);
        }
        else
        {
            pConn++;
        }
    }
}

void
Connection::ReapDeadRequests(
)
{
    //pConn->reqstatusmutex.lock();
    auto pReq = reqstatus.begin();
    while (pReq != reqstatus.end())
    {
        if (pReq->second.state == RequestStatus::State::DEAD)
        {
            //std::cout << "Reap Dead Request\n";
            pReq = reqstatus.erase(pReq);
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
    Packet& packet
)
{
    uint32_t cid = ConnMan::ExtractConnectionIdFromPacket(packet);
    
    if (cid == pConn->id)
    {
        if (pConn->state == Connection::State::IDENTIFIED)
        {
            pConn->state = Connection::State::GRACKING;
            pConn->lastrxtime = clock::now();
            pConn->id = cid;
            pConn->curack = ConnMan::ExtractSequenceFromPacket(packet);
        }
    }
    else
    {
        std::cout << "Weird: Client Rx Grant packet with unknown id\n";
    }
}

void
ConnMan::ProcessGrack(
    Connection::ConnectionPtr pConn,
    Packet& packet
)
{
    if (pConn->state == Connection::State::GRACKING)
    {
        pConn->state = Connection::State::ALIVE;
        pConn->lastrxtime = clock::now();
        pConn->curack = ConnMan::ExtractSequenceFromPacket(packet);
    }
}

void
ConnMan::ProcessDeny(
    Connection::ConnectionPtr pConn,
    Packet& packet
)
{
    std::string playername = ConnMan::ExtractPlayerNameFromPacket(packet);
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
    Packet& packet
)
{
    if (pConn)
    {
        MESG* msg = (MESG*)packet.buffer;
        // Send Pong to whoever sent this to us
        Packet packet = CreatePongPacket(pConn->who, pConn->id, pConn->curuseq, msg->header.seq);
        pConn->lastrxtime = clock::now();
        pConn->curack = ConnMan::ExtractSequenceFromPacket(packet);
        Write(packet);
    }
}
#include <chrono>
void
ConnMan::ProcessPong(
    Connection::ConnectionPtr pConn,
    Packet& packet
)
{
    if (pConn)
    {
        MESG* msg = (MESG*)packet.buffer;

        pConn->reqstatusmutex.lock();
        auto pReq = pConn->reqstatus.find(msg->header.ack);
        pConn->reqstatusmutex.unlock();

        if (pReq != pConn->reqstatus.end())
        {
            if (pReq->second.state == RequestStatus::State::PENDING)
            {
                pReq->second.state = RequestStatus::State::DYING;
                clock::time_point startTime = pReq->second.starttime;
                clock::time_point endTime = clock::now();
                pConn->lastrxtime = endTime;
                pConn->totalpongs++;
                pConn->curack = ConnMan::ExtractSequenceFromPacket(packet);
                pConn->RemoveRequestStatus(msg->header.ack);

                pConn->curping = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
                pConn->pingtimes.push_back(pConn->curping);
                if (pConn->pingtimes.size() > 15)
                    pConn->pingtimes.pop_front();

                uint32_t cnt = 0;
                float avg = 0;
                for (auto p : pConn->pingtimes)
                {
                    avg += p;
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
                std::cout << "     Rx'd a late pong " << msg->header.seq << std::endl;
            }
        }
        else
        {
            // Request has already been FAILED, and removed
            // from the request map
            // ??
            std::cout << "     Rx'd a super late pong " << msg->header.seq << std::endl;
        }
    }
}

void
ConnMan::ProcessRxPacket(
    Packet& packet
)
{
    cmstate.globalrxmutex.lock();
    cmstate.globalrxqueue.push(packet);
    cmstate.globalrxmutex.unlock();

}

void
ConnMan::ProcessGeneral(
    Connection::ConnectionPtr pConn,
    Packet& packet
)
{
    if (pConn)
    {
        //pConn->rxpacketmutex.lock();
        pConn->rxpackets.push(packet);
        //pConn->rxpacketmutex.unlock();
        pConn->lastrxtime = clock::now();
        pConn->curack = ConnMan::ExtractSequenceFromPacket(packet);
        /*
        On Packet Receive, If packet is reliable, Then send Ack packet unreliably.
        */
        MESG* msg = (MESG*)packet.buffer;
        if (msg->header.mode == (uint8_t)MESG::HEADER::Mode::Reliable)
        {
            Packet packet = ConnMan::CreateAckPacket(pConn->who, pConn->id, pConn->curuseq, msg->header.seq);
            Write(packet);
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
    Packet& packet
)
{
    if (pConn)
    {
        pConn->curack = ConnMan::ExtractSequenceFromPacket(packet);

        MESG* pMsg = (MESG*)packet.buffer;
        uint32_t ack = pMsg->header.ack;
        auto pReq = pConn->GetRequestStatus(ack); // Retrieves RequestStatus associated
                                                  // with an ACK
        if (pReq != pConn->reqstatus.end())
        {
            if (pReq->second.state == RequestStatus::State::PENDING)
            {
                pReq->second.state = RequestStatus::State::ACKNOWLEDGED;
                InterlockedIncrement(&pConn->curseq);
                pConn->lastrxtime = clock::now();
                pReq->second.endtime = clock::now();
            }
            else
            {
                // Request has already been FAILED, but not yet removed
                // from the request map
                //
                std::cout << "     Rx'd a late Ack " << pMsg->header.seq << std::endl;
            }
        }
        else
        {
            // Request has alreadt been FAILED, and removed
            // from the request map
            //
            std::cout << "     Rx'd a super late Ack " << pMsg->header.seq << std::endl;
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

    if (request->ioType == Request::IOType::READ)
    {
        Network::read(cm.cmstate.netstate);
        if (ConnMan::IsMagicGood(request->packet))
        {
            if (ConnMan::IsSizeValid(request->packet))
            {
#ifndef SHOW_PING_PRINTS
                MESG* m = (MESG*)request->packet.buffer;
                if (m->header.code != (uint32_t)MESG::HEADER::Codes::Ping &&
                    m->header.code != (uint32_t)MESG::HEADER::Codes::Pong)
#endif
                {
                    std::cout << "     Rx: "
                        << CodeName[m->header.code]
                        << "[" << m->header.id << "]"
                        << "[" << m->header.seq << "]"
                        << "[" << m->header.ack << "]" << std::endl;
                }

                cm.ProcessRxPacket(request->packet);
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
    }
    else if (request->ioType == Request::IOType::WRITE)
    {
        MESG* m = (MESG*)request->packet.buffer;
#ifndef SHOW_PING_PRINTS
        if (m->header.code != (uint32_t)MESG::HEADER::Codes::Ping &&
            m->header.code != (uint32_t)MESG::HEADER::Codes::Pong)
#endif
        {
            std::cout << "     Tx: "
                      << CodeName[m->header.code]
                      << "[" << m->header.id << "]"
                      << "[" << m->header.seq << "]"
                      << "[" << m->header.ack << "]" << std::endl;
        }
    }
}
void
ConnMan::ProcessDisconnect(
    Connection::ConnectionPtr pConn
)
{
    if (pConn->state == Connection::State::ALIVE)
    {
        pConn->state = Connection::State::DEAD;
        std::cout << "ProcessDisconnect(): Connection Alive -> Dead\n";
    }
}
void
ConnMan::ProcessIdentify(
    Request* request
)
{
    //if (cmstate.connections.size() < cmstate.numplayers)
    {
        if (cmstate.gamename == ConnMan::ExtractGameNameFromPacket(request->packet))
        {
            if (cmstate.gamepass == ConnMan::ExtractGamePassFromPacket(request->packet))
            {
                std::string pn = ConnMan::ExtractPlayerNameFromPacket(request->packet);
                uint16_t id = ConnMan::ExtractConnectionIdFromPacket(request->packet);
                Connection::ConnectionPtr pConn = nullptr;
                //
                cmstate.connectionsmutex.lock();
                //
                bool isnameavail = ConnMan::IsPlayerNameAndIdAvailable(cmstate.connections, pn, id);
                if (!isnameavail)
                {
                    pConn = ConnMan::GetConnectionById(cmstate.connections, id);
                }

                if (isnameavail)
                {
                    if (cmstate.connections.size() < cmstate.numplayers)
                    {
                        std::random_device rd;     // only used once to initialise (seed) engine
                        std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)
                        std::uniform_int_distribution<uint32_t> uni(1, 65536); // guaranteed unbiased

                        uint32_t randomNumber = uni(rng);
                        Connection::ConnectionPtr pConn = 
                        cmstate.CreateConnection(request->packet.address,
                                                 pn,
                                                 id,
                                                 Connection::Locality::REMOTE,
                                                 Connection::State::IDENTIFIED
                        );

                        pConn->curseq = randomNumber;
                        pConn->curuseq = randomNumber;
                        pConn->curack = ConnMan::ExtractSequenceFromPacket(request->packet);
                        cmstate.AddConnection(pConn);
                    }
                    else
                    {
                        // Deny - password doesn't match
                        Packet packet = ConnMan::CreateDenyPacket(request->packet.address,
                                                         ExtractPlayerNameFromPacket(request->packet));
                        Write(packet);
                        std::cout << "Tx Deny: Lobby Full\n";
                    }
                }
                else
                {
                    // This is a rebroadcast. Same Id, same Grant.
                    Packet packet = ConnMan::CreateGrantPacket(pConn->who,
                                                      pConn->id,
                                                      pConn->curuseq,
                                                      pConn->curack,
                                                      pConn->playername);
                    Write(packet);
                    std::cout << "Tx Grant: Reconnect\n";
                }

                //
                cmstate.connectionsmutex.unlock();
                //
            }
            else
            {
                // Deny - password doesn't match
                Packet packet = ConnMan::CreateDenyPacket(request->packet.address,
                                                 ExtractPlayerNameFromPacket(request->packet));
                Write(packet);
                std::cout << "Tx Deny: Password is bad\n";
            }
        }
        else
        {
            // Deny - Game name doesn't match
            Packet packet = ConnMan::CreateDenyPacket(request->packet.address,
                                             ExtractPlayerNameFromPacket(request->packet));
            //Network::write(cmstate.netstate, packet);
            Write(packet);
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
        Network::read(cm.cmstate.netstate);
        /*
            If we recieve an Identify packet,
            and we are accepting more players,
            and the packet makes sense, Grant.
            Otherwise Deny.

            Packets not involved in the initial handshake,
            and that are from an identified client,
            will be enqueued to the packet queue.
        */
        if (ConnMan::IsMagicGood(request->packet))
        {
            if (ConnMan::IsSizeValid(request->packet))
            {
#ifndef SHOW_PING_PRINTS
                MESG* m = (MESG*)request->packet.buffer;
                if (m->header.code != (uint32_t)MESG::HEADER::Codes::Ping &&
                    m->header.code != (uint32_t)MESG::HEADER::Codes::Pong)
#endif
                {
                    std::cout << "     Rx: "
                              << CodeName[m->header.code]
                              << "[" << m->header.id << "]"
                              << "[" << m->header.seq << "]"
                              << "[" << m->header.ack << "]" << std::endl;
                }
                if (ConnMan::IsCode(request->packet, MESG::HEADER::Codes::Identify) &&
                    cm.cmstate.cmtype != ConnManState::ConnManType::PASS_THROUGH)
                {
                    // Rx'd an IDENTIFY packet.
                    // Therefore, an ID hasn't been
                    // generated yet - "Connection" not
                    // established.
                    cm.ProcessIdentify(request);
                }
                else
                {
                    cm.ProcessRxPacket(request->packet);
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
    }
    else if (request->ioType == Request::IOType::WRITE)
    {
        MESG* m = (MESG*)request->packet.buffer;
#ifndef SHOW_PING_PRINTS
        if (m->header.code != (uint32_t)MESG::HEADER::Codes::Ping &&
            m->header.code != (uint32_t)MESG::HEADER::Codes::Pong)
#endif
        {
            std::cout << "     Tx: "
                      << CodeName[m->header.code]
                      << "[" << m->header.id << "]"
                      << "[" << m->header.seq << "]"
                      << "[" << m->header.ack << "]"
                      << std::endl;
        }
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
        if (c->state != Connection::State::DYING &&
            c->state != Connection::State::DEAD)
        {
            if (id == c->id)
            {
                mutex.unlock();
                return c;
            }
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

Connection::ConnectionPtr
ConnMan::GetConnectionById(
    std::list<Connection::ConnectionPtr> & connections,
    uint32_t id
)
{
    for ( auto & c : connections )
    {
        if ( c->state != Connection::State::DYING &&
            c->state != Connection::State::DEAD )
        {
            if ( id == c->id )
            {
                return c;
            }
        }
    }
    return nullptr;
}

bool
ConnMan::IsPlayerNameAndIdAvailable(
    std::list<Connection::ConnectionPtr> & connections,
    std::string name,
    uint16_t id
)
{
    bool available = true;
    for ( auto & c : connections )
    {
        if ( c->playername == name && c->id == id )
        {
            available = false;
            break;
        }
    }
    return available;
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
    //std::cout << "ReqStatus.size() ==" << reqstatus.size() << std::endl;
    reqstatusmutex.unlock();
}

void
Connection::RemoveRequestStatus(
    uint32_t sid
)
{
    //std::cout << "Removing Dead Request...\n";
    reqstatusmutex.lock();
    reqstatus.erase(sid);
    reqstatusmutex.unlock();
}


bool
ConnMan::SendUnreliable(
    ConnMan & cm,
    uint32_t uid,
    const char* szString
)
{
    bool ret = false;
    Connection::ConnectionPtr pConn = ConnMan::GetConnectionById(cm.cmstate.connectionsmutex, cm.cmstate.connections, uid);
    if (pConn != nullptr)
    {
        uint32_t request_index; // not used for unreliable
        cm.SendBuffer(pConn,
            ConnMan::SendType::WITHOUTRECEIPT,
            (uint8_t*)szString,
            strlen(szString),
            request_index);
        ret = true;
    }
    return ret;
}

bool
ConnMan::SendReliable(
    ConnMan & cm,
    uint32_t uid,
    const char* szString
)
{
    bool ret = false;
    Connection::ConnectionPtr pConn = ConnMan::GetConnectionById(cm.cmstate.connectionsmutex, cm.cmstate.connections, uid);
    if (pConn != nullptr)
    {
        uint32_t curRetries = 0;
        bool sent = false;
        do
        {
            uint32_t request_index;
            RequestFuture barrier_future;

            barrier_future =
                cm.SendBuffer(pConn,
                    ConnMan::SendType::WITHRECEIPT,
                    (uint8_t*)szString,
                    strlen(szString),
                    request_index);

            RequestStatus::RequestResult result = barrier_future.get();
            if (result == RequestStatus::RequestResult::ACKNOWLEDGED)
            {
                sent = true;
            }
            else if (result == RequestStatus::RequestResult::TIMEDOUT)
            {
                // Retry
            }
            pConn->RemoveRequestStatus(request_index);
            curRetries++;
        } while (!sent && (curRetries < cm.cmstate.retry_count));

        if (sent) {
            std::cout << "SendReliable Success. Tries: " << curRetries << "\n";
            ret = true;
        }
        else
        {
            std::cout << "SendReliable Failed. Tries: " << curRetries << "\n";
        }
    }
    else
    {
        std::cout << "SendReliable(): Unknown UID: " << uid << "\n";
    }
    return ret;
}

bool
ConnMan::Connect(
    ConnMan & cm,
    Address to,
    std::string playername,
    std::string gamename,
    std::string gamepass,
    ConnectingResultFuture & result
)
{
    bool ret = false;
    std::random_device rd;     // only used once to initialise (seed) engine
    std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)
    std::uniform_int_distribution<uint32_t> uni(1, 65536); // guaranteed unbiased

    auto rando = uni(rng);
    Connection::ConnectionPtr pConn =
        cm.cmstate.CreateConnection(
            to,
            playername,
            rando,
            Connection::Locality::LOCAL,
            Connection::State::IDENTIFIED);
    if (pConn != nullptr)
    {
        pConn->connectingresultpromise = std::make_shared<ConnectingResultPromise>();
        cm.cmstate.AddConnection(pConn);

        ConnectingResultFuture connectingResultFuture =
            pConn->connectingresultpromise->get_future();

        Packet packet = ConnMan::CreateIdentifyPacket(pConn->who,
            pConn->playername,
            pConn->curuseq,
            pConn->curack,
            rando,
            gamename, gamepass);
        cm.Write(packet);
        result = std::move(connectingResultFuture);
        ret = true;
    }
    return ret;
}

void
ConnMan::Disconnect(
    ConnMan & cm,
    Address to,
    uint32_t uid
)
{
    uint32_t cs = 13;
    Packet packet =
        ConnMan::CreateDisconnectPacket(to,
            uid,
            cs,
            13);
    cm.Write(packet);
    cm.cmstate.RemoveConnection(uid);
}

Packet
ConnMan::CreateIdentifyPacket(
    Address& to,
    std::string playername,
    uint32_t& curseq,
    uint32_t curack,
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
    packet.address = to;

    MESG* pMsg = (MESG*)packet.buffer;
    AddMagic(pMsg);
    pMsg->header.code = (uint8_t)MESG::HEADER::Codes::Identify;
    pMsg->header.id = randomcode;
    curseq = randomcode;
    pMsg->header.seq = InterlockedIncrement(&curseq);
    pMsg->header.ack = curack;
    memcpy(pMsg->payload.identify.playername,
        playername.c_str(),
        playername.size());

    memcpy(pMsg->payload.identify.gamename,
        gamename.c_str(),
        gamename.size());

    memcpy(pMsg->payload.identify.gamepass,
        gamepass.c_str(),
        gamepass.size());

    return packet;
}
Packet
ConnMan::CreatePongPacket(
    Address& to,
    uint32_t id,
    uint32_t& curseq,
    uint32_t ack
)
{
    Packet packet;
    memset(packet.buffer, 0, MAX_PACKET_SIZE);
    packet.buffersize = sizeof(MESG::HEADER) + sizeof(PINGPONG);
    packet.address = to;

    MESG* pMsg = (MESG*)packet.buffer;
    AddMagic(pMsg);
    pMsg->header.mode = (uint8_t)MESG::HEADER::Mode::Unreliable;
    pMsg->header.code = (uint8_t)MESG::HEADER::Codes::Pong;
    pMsg->header.id = id;
    pMsg->header.seq = InterlockedIncrement(&curseq);
    pMsg->header.ack = ack;

    return packet;
}

Packet
ConnMan::CreatePingPacket(
    Address& to,
    uint32_t id,
    uint32_t& curseq,
    uint32_t curack
)
{
    Packet packet;
    //RequestFuture future;
    memset(packet.buffer, 0, MAX_PACKET_SIZE);
    packet.buffersize = sizeof(MESG::HEADER) + sizeof(PINGPONG);
    packet.address = to;

    MESG* pMsg = (MESG*)packet.buffer;
    AddMagic(pMsg);
    pMsg->header.mode = (uint8_t)MESG::HEADER::Mode::Unreliable;
    pMsg->header.code = (uint8_t)MESG::HEADER::Codes::Ping;
    pMsg->header.id = id;
    pMsg->header.seq = InterlockedIncrement(&curseq);
    pMsg->header.ack = curack;
    return packet;
}

Packet
ConnMan::CreateGrantPacket(
    Address to,
    uint32_t id,
    uint32_t& curseq,
    uint32_t curack,
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
    pMsg->header.seq = InterlockedIncrement(&curseq);
    pMsg->header.ack = curack;
    memcpy(pMsg->payload.identify.playername,
        playername.c_str(),
        playername.size());

    return packet;
}

Packet
ConnMan::CreateGrackPacket(
    Address to,
    uint32_t id,
    uint32_t& curseq,
    uint32_t curack
)
{
    Packet packet;
    memset(packet.buffer, 0, MAX_PACKET_SIZE);
    packet.buffersize = sizeof(MESG::HEADER) + sizeof(GRACK);
    packet.address = to;

    MESG* pMsg = (MESG*)packet.buffer;
    AddMagic(pMsg);
    pMsg->header.code = (uint8_t)MESG::HEADER::Codes::Grack;
    pMsg->header.id = id;
    pMsg->header.seq = InterlockedIncrement(&curseq);
    pMsg->header.ack = curack;

    return packet;
}
Packet
ConnMan::CreateDisconnectPacket(
    Address to,
    uint32_t id,
    uint32_t& curseq,
    uint32_t curack
)
{
    Packet packet;
    memset(packet.buffer, 0, MAX_PACKET_SIZE);
    packet.buffersize = sizeof(MESG::HEADER) + sizeof(DISCONNECT);
    packet.address = to;

    MESG* pMsg = (MESG*)packet.buffer;
    AddMagic(pMsg);
    pMsg->header.code = (uint8_t)MESG::HEADER::Codes::Disconnect;
    pMsg->header.id = id;
    pMsg->header.seq = InterlockedIncrement(&curseq);
    pMsg->header.ack = curack;

    return packet;
}

Packet
ConnMan::CreateDenyPacket(
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

    return packet;
}

Packet
ConnMan::CreateAckPacket(
    Address& to,
    uint32_t id,
    uint32_t& curseq,
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
    pMsg->header.seq = InterlockedIncrement(&curseq);
    pMsg->header.ack = ack;

    return packet;
}

void
ConnMan::AddMagic(
    MESG* pMsg
)
{
    memcpy(pMsg->header.magic, "AB", 2);
}

uint32_t
ConnMan::SizeofPayload(
    MESG::HEADER::Codes code
)
{
    uint32_t payloadSize = 0;
    if (code == MESG::HEADER::Codes::Identify) { payloadSize += sizeof(IDENTIFY); }
    if (code == MESG::HEADER::Codes::Grant) { payloadSize += sizeof(GRANT); }
    if (code == MESG::HEADER::Codes::Deny) { payloadSize += sizeof(DENY); }
    if (code == MESG::HEADER::Codes::General) { payloadSize += sizeof(GENERAL); }
    if (code == MESG::HEADER::Codes::Ack) { payloadSize += sizeof(ACK); }
    if (code == MESG::HEADER::Codes::Ping ||
        code == MESG::HEADER::Codes::Pong) {
        payloadSize += sizeof(PINGPONG);
    }

    return payloadSize;
}

bool
ConnMan::IsCode(
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
ConnMan::IsMagicGood(
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
ConnMan::IsSizeValid(
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

uint32_t
ConnMan::ExtractConnectionIdFromPacket(
    Packet & packet
)
{
    MESG* RxMsg = (MESG*)packet.buffer;
    return RxMsg->header.id;
}

void
ConnMan::PrintMsgHeader(
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
ConnMan::ExtractPlayerNameFromPacket(
    Packet & packet
)
{
    MESG* pMsg = (MESG*)packet.buffer;
    return std::string(pMsg->payload.grant.playername,
        strlen(pMsg->payload.grant.playername));
}

std::string
ConnMan::ExtractGameNameFromPacket(
    Packet & packet
)
{
    MESG* pMsg = (MESG*)packet.buffer;
    return std::string(pMsg->payload.identify.gamename,
        strlen(pMsg->payload.identify.gamename));
}

std::string
ConnMan::ExtractGamePassFromPacket(
    Packet & packet
)
{
    MESG* pMsg = (MESG*)packet.buffer;
    return std::string(pMsg->payload.identify.gamepass,
        strlen(pMsg->payload.identify.gamepass));
}

void
ConnMan::PrintfMsg(
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
ConnMan::CreateAddress(
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



} // namespace bali