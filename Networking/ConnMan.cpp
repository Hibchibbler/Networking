#include "ConnMan.h"
#include <sstream>
#include <random>

namespace bali
{
void
ConnMan::initialize(
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


void
ConnMan::SendReliable(
    ConnManState & cmstate,
    Address to,
    uint32_t id,
    uint8_t* buffer,
    uint32_t buffersize,
    AcknowledgeHandler ackhandler
)
{
    Packet packet;
    Connection* pConn = GetConnectionById(cmstate.connections, id);
    if (pConn)
    {
        memset(packet.buffer, 0, MAX_PACKET_SIZE);
        packet.buffersize = sizeof(MESG::HEADER) + sizeof(GENERAL);
        packet.address = to;

        MESG* pMsg = (MESG*)packet.buffer;
        AddMagic(pMsg);
        pMsg->header.code = (uint8_t)MESG::HEADER::Codes::General;
        pMsg->header.id = id;
        pMsg->header.seq = InterlockedIncrement(&pConn->curseq);
        pMsg->header.mode = (uint8_t)MESG::HEADER::Mode::Reliable;

        packet.ackhandler = ackhandler;
        std::cout << "Push Reliable: " << CodeName[pMsg->header.code] << std::endl;
        pConn->txpacketsreliable.push(packet);
    }
}


void
ConnMan::SendUnreliable(
    ConnManState & cmstate,
    Address to,
    uint32_t id,
    uint8_t* buffer,
    uint32_t buffersize
)
{
    Packet packet;
    Connection* pConn = GetConnectionById(cmstate.connections, id);
    if (pConn)
    {
        memset(packet.buffer, 0, MAX_PACKET_SIZE);
        packet.buffersize = sizeof(MESG::HEADER) + sizeof(GENERAL);
        packet.address = to;

        MESG* pMsg = (MESG*)packet.buffer;
        AddMagic(pMsg);
        pMsg->header.code = (uint8_t)MESG::HEADER::Codes::General;
        pMsg->header.id = id;
        pMsg->header.seq = InterlockedIncrement(&pConn->curuseq);
        ///
        pMsg->header.mode = (uint8_t)MESG::HEADER::Mode::Unreliable;

        packet.ackhandler = nullptr;
        std::cout << "Push Unreliable: " << CodeName[pMsg->header.code] << std::endl;
        cmstate.txpacketsunreliable.push(packet);
    }
}

void
ConnMan::SendPing(
    ConnManState & cmstate,
    Address to,
    uint32_t id,
    bool ping
)
{
    Packet packet;
    Connection* pConn = GetConnectionById(cmstate.connections, id);
    if (pConn)
    {
        memset(packet.buffer, 0, MAX_PACKET_SIZE);
        packet.buffersize = sizeof(MESG::HEADER) + sizeof(PINGPONG);
        packet.address = to;

        MESG* pMsg = (MESG*)packet.buffer;
        AddMagic(pMsg);
        if (ping) {
            pMsg->header.code = (uint8_t)MESG::HEADER::Codes::Ping;
        }
        else
        {
            pMsg->header.code = (uint8_t)MESG::HEADER::Codes::Pong;
        }
        pMsg->header.id = id;
        pMsg->header.seq = InterlockedIncrement(&pConn->curuseq);
        ///
        pMsg->header.mode = (uint8_t)MESG::HEADER::Mode::Unreliable;

        packet.ackhandler = nullptr;

        pConn->pingstart = clock::now();
        std::cout << "Push Unreliable: " << CodeName[pMsg->header.code] << std::endl;
        cmstate.txpacketsunreliable.push(packet);
    }
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
    Packet packet;
    uint32_t traits = 0;

    //
    // Let's put a new connection into the list
    //
    Connection connection;
    connection.playername = playername;

    // But we don't know it's ID yet
    // because we have not yet been GRANTed
    connection.id = 0;
    connection.state = Connection::State::IDLE;
    connection.curseq = 0;
    connection.curack = 0;
    connection.highseq = 0;
    connection.curuseq = 0;
    connection.curuack = 0;
    connection.highuseq = 0;

    cmstate.localconn = connection;

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
    std::cout << "Push Unreliable: " << CodeName[pMsg->header.code] << std::endl;
    cmstate.txpacketsunreliable.push(packet);

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
    std::cout << "Push Unreliable: " << CodeName[pMsg->header.code] << std::endl;
    cmstate.txpacketsunreliable.push(packet);
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
    std::cout << "Push Unreliable: " << CodeName[pMsg->header.code] << std::endl;
    cmstate.txpacketsunreliable.push(packet);
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
    pMsg->payload.ack.ack = ack;
    std::cout << "Push Unreliable: " << CodeName[pMsg->header.code] << std::endl;
    cmstate.txpacketsunreliable.push(packet);
}

Connection*
ConnMan::GetConnectionBaseByPacket(
    ConnManState& cmstate,
    Packet& packet
)
{
    Connection* pConn = nullptr;
    uint32_t cid = 0;
    cid = GetConnectionId(packet);
    pConn = GetConnectionById(cmstate.connections, cid);
    if (!pConn)
    {
        pConn = &cmstate.localconn;
    }
    return pConn;
}

Connection*
ConnMan::GetConnectionBaseById(
    ConnManState& cmstate,
    uint32_t id
)
{
    Connection* pConn = nullptr;
    pConn = GetConnectionById(cmstate.connections, id);
    if (!pConn)
    {
        pConn = &cmstate.localconn;
    }
    return pConn;
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
    // Service outgoing unreliable packets
    //
    while (!cmstate.txpacketsunreliable.empty())
    {
        MESG* m = (MESG*)cmstate.txpacketsunreliable.front().buffer;

        std::cout << "Popped Unreliable: " << CodeName[m->header.code] << std::endl;
        Network::write(cmstate.netstate, cmstate.txpacketsunreliable.front());
        cmstate.txpacketsunreliable.pop();
    }

    // Service outgoing reliable packets.
    //
    // We are only going to send a packet
    // if we are not currently waiting for an ack
    // from last packet.
    for (auto & c : cmstate.connections)
    {
        if (c.state == Connection::State::IDLE)
        {
            if (!c.txpacketsreliable.empty())
            {
                c.txpacketpending = c.txpacketsreliable.front();
                MESG* m = (MESG*)c.txpacketpending.buffer;
                c.state = Connection::State::WAITONACK;
                c.acktime = clock::now();

                std::cout << "Popped Reliable: " << CodeName[m->header.code] << std::endl;
                Network::write(cmstate.netstate, c.txpacketpending);
                c.txpacketsreliable.pop();
            }
        }
    }

    //
    // Service incoming packets
    //
    while (!cmstate.rxpackets.empty())
    {
        Connection* pConn = nullptr;
        Packet packet;

        packet = cmstate.rxpackets.front();
        cmstate.rxpackets.pop();

        pConn = GetConnectionBaseByPacket(cmstate, packet);
        cmstate.onevent(cmstate.oneventcontext,
                        ConnManState::OnEventType::MESSAGE,
                        pConn,
                        &packet);
        
    }

    //
    // Update the connection states, and raise interesting facts.
    //
    //for (auto c = cmstate.connections.begin(); c != cmstate.connections.end(); c++)
    auto conniter = cmstate.connections.begin();
    while (conniter != cmstate.connections.end())
    {
        //
        // Time out those waiting for an ACK
        //
        if (conniter->state == Connection::State::WAITONACK)
        {
            duration dur = clock::now() - conniter->acktime;
            if (dur.count() > cmstate.acktimeout_ms)
            {
                cmstate.onevent(cmstate.oneventcontext,
                    ConnManState::OnEventType::ACK_TIMEOUT,
                    &(*conniter),
                    &conniter->txpacketpending);
            }
        }

        //
        // Connection hasn't seen traffic in over 10 seconds
        // Notify user, and drop Connection
        //
        duration hb = clock::now() - conniter->heartbeat;
        if (hb.count() > cmstate.heartbeat_ms)
        {
            conniter->heartbeat = clock::now();
            ConnMan::SendPing(cmstate, conniter->who, conniter->id, true);
        }

        duration d = clock::now() - conniter->checkintime;
        if (d.count() > cmstate.stale_ms)
        {
            cmstate.onevent(cmstate.oneventcontext,
                            ConnManState::OnEventType::CONNECTION_STALE,
                            &(*conniter),
                            nullptr);
        }

        if (d.count() > cmstate.remove_ms)
        {
            cmstate.onevent(cmstate.oneventcontext,
                            ConnManState::OnEventType::CONNECTION_REMOVE,
                            &(*conniter),
                            nullptr);
            conniter = cmstate.connections.erase(conniter);
            continue;
        }
        conniter++;

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
        MESG* m = (MESG*)request->packet.buffer;
        std::cout << "Rx: " << CodeName[m->header.code] << std::endl;

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
                                    //std::cout << "Rx Identify: " << pn << std::endl;
                                    Connection connection;
                                    connection.playername = pn;
                                    connection.id = random_integer;
                                    connection.who = request->packet.address;
                                    connection.state = Connection::State::IDLE;
                                    connection.checkintime = clock::now();
                                    connection.heartbeat = clock::now();
                                    connection.curseq = 0;
                                    connection.curack = 0;
                                    connection.highseq = 0;
                                    connection.curuseq = 0;
                                    connection.curuack = 0;
                                    connection.highuseq = 0;
                                    cmstate.connections.push_back(connection);
                                    cmstate.onevent(cmstate.oneventcontext,
                                                    ConnManState::OnEventType::CONNECTION_ADD,
                                                    &connection,
                                                    &request->packet);

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
                        cmstate.localconn.checkintime = clock::now();
                        cmstate.localconn.id = GetConnectionId(request->packet);
                        cmstate.onevent(cmstate.oneventcontext, ConnManState::OnEventType::GRANTED, &cmstate.localconn, &request->packet);
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
                else if (IsCode(request->packet, MESG::HEADER::Codes::Ack))
                {
                    // Rx'd an ACk. 
                    // Something is eagerly awaiting this i'm sure.
                    Connection* pConn;
                    pConn = GetConnectionBaseByPacket(cmstate, request->packet);
                    if (pConn)
                    {
                        if (pConn->state == Connection::State::WAITONACK)
                        {
                            MESG* pMsg = (MESG*)request->packet.buffer;

                            uint32_t ack = pMsg->payload.ack.ack;
                            uint32_t hope = ((MESG*)pConn->txpacketpending.buffer)->header.seq;

                            if (ack == hope)
                            {
                                pConn->state = Connection::State::ACKRECEIVED;
                                //std::cout << "Rx Ack: " << ack << std::endl;
                                cmstate.onevent(cmstate.oneventcontext,
                                                ConnManState::OnEventType::ACK_RECEIVED,
                                                pConn,
                                                &pConn->txpacketpending);
                            }
                            else
                            {
                                std::cout << "Weird: received an Ack for wrong sequence. " << ack << ",  " << hope << std::endl;
                            }
                        }
                        else
                        {
                            std::cout << "Weird: received an Ack but wasn't expecting it." << std::endl;
                        }
                    }
                    else
                    {
                        std::cout << "Problem: Rx'd ACK Packet contains unknown ID!\n";
                    }
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::Ping))
                {
                    Connection* pConn;
                    pConn = GetConnectionBaseByPacket(cmstate, request->packet);
                    if (pConn)
                    {
                        ConnMan::SendPing(cmstate, pConn->who, pConn->id, false); // Pong
                    }
                }
                else if (IsCode(request->packet, MESG::HEADER::Codes::Pong))
                {
                    Connection* pConn;
                    pConn = GetConnectionBaseByPacket(cmstate, request->packet);
                    if (pConn)
                    {
                        pConn->pingend = clock::now();
                        pConn->checkintime = pConn->pingend;
                        duration d = pConn->pingend - pConn->pingstart;

                        pConn->pingtimes.push_back(d);
                        if (pConn->pingtimes.size() > 15)
                            pConn->pingtimes.pop_front();

                        pConn->avgping = 0;
                        for (auto d : pConn->pingtimes)
                        {
                            pConn->avgping += d.count();
                        }
                        pConn->avgping /= pConn->pingtimes.size();
                        std::cout << "AVGPING: " << pConn->avgping <<std::endl;
                    }
                }
                else
                {
                    //
                    // Rx'd something other than Identify, or Ack
                    //
                    Connection* pConn = nullptr;
                    pConn = GetConnectionBaseByPacket(cmstate, request->packet);
                    if (pConn)
                    {
                        pConn->checkintime = clock::now();
                        /*
                            On Packet Receive, If packet is reliable, Then send Ack packet unreliably.
                        */
                        MESG* msg = (MESG*)request->packet.buffer;
                        if (msg->header.mode == (uint8_t)MESG::HEADER::Mode::Reliable)
                        {
                            if (msg->header.seq != 40)
                            {
                                SendAckTo(cmstate, pConn->who, pConn->id, msg->header.seq);
                            }
                            else
                            {
                                std::cout << "Debug: Not Tx Ack: " << msg->header.seq << std::endl;
                            }
                        }
                        cmstate.rxpackets.push(request->packet);
                    }
                    else
                    {
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
        MESG* m = (MESG*)request->packet.buffer;
        std::cout << "Tx: " << CodeName[m->header.code] << std::endl;
    }
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

uint64_t
InitializePacket(
    Packet & packet,
    void* ackhandler,
    Address & who,
    MESG::HEADER::Codes code,
    uint32_t id,
    uint32_t seq
)
{
    //MESG* pMsg = (MESG*)packet.buffer;
    //packet.ackhandler = ackhandler;
    //packet.buffersize = sizeof(MESG::HEADER) + SizeofPayload(code);
    //packet.address = who;

    //memcpy(pMsg->header.magic, "AB", 2);
    //pMsg->header.code = (uint32_t)code;
    //pMsg->header.id = id;
    //pMsg->header.seq = seq;

    return 0;
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
GetConnectionById(
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

uint32_t
GetConnectionId(
    Packet & packet
)
{
    MESG* RxMsg = (MESG*)packet.buffer;
    return RxMsg->header.id;
}

bool
IsExpectsAck(
    Packet & packet
)
{
    bool expectation = false;
    MESG* RxMsg = (MESG*)packet.buffer;
    //if (RxMsg->header.traits & (1ul << (uint32_t)MESG::HEADER::Traits::ACK))
    {
        expectation = true;
    }
    return expectation;
}

bool
IsPlayerNameAvailable(
    ConnManState & cmstate,
    std::string name
)
{
    bool available = true;
    for (auto c : cmstate.connections)
    {
        if (c.playername == name)
        {
            available = false;
            break;
        }
    }
    return available;
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

bool
RemoveConnectionByName(
    std::list<Connection> & connections,
    std::string playername
)
{
    bool found = false;
    for (auto c = connections.begin(); c != connections.end(); c++)
    {
        if (playername == c->playername)
        {
            c = connections.erase(c);
            found = true;
            break;
        }
    }
    return found;
}
Connection*
GetConnectionByName(
    std::list<Connection> & connections,
    std::string playername
)
{
    for (auto & c : connections)
    {
        if (playername == c.playername)
        {
            return &c;

        }
    }
    return nullptr;
}

MESG*
GetMesg(
    Packet & packet
)
{
    MESG* mesg = (MESG*)packet.buffer;
    return mesg;
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

} // namespace bali