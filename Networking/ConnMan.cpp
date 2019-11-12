#include "ConnMan.h"
#include <sstream>

namespace bali
{
void
ConnMan::initialize(
    ConnManState & cmstate,
    ConnManState::OnClientEnter oce_, void* entercontext,
    ConnManState::OnClientLeave ocl_, void* leavecontext,
    ConnManState::OnClientUpdate ocu_, void* cupdatecontext,
    ConnManState::OnServerUpdate osu_, void* supdatecontext,
    uint32_t numplayers,
    std::string gamename,
    std::string gamepass
)
{
    cmstate.oce = oce_;
    cmstate.ocl = ocl_;
    cmstate.ocu = ocu_;
    cmstate.osu = osu_;

    cmstate.numplayers = numplayers;
    cmstate.gamename = gamename;
    cmstate.gamepass = gamepass;

    cmstate.CurrentConnectionId = 13;
    cmstate.done = 0;
    cmstate.cmmutex.create();

    Network::initialize(cmstate.netstate, 8, 8967, ConnMan::ConnManIOHandler, &cmstate);
    Network::start(cmstate.netstate);
}


uint64_t
readyCount(
    ConnManState & cmstate
)
{
    cmstate.cmmutex.lock();
    uint64_t cnt =0;
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
sendStart(
    ConnManState & cmstate
)
{

}

bool
ConnMan::NameExists(
    ConnManState & state,
    std::string name
)
{
    bool res = false;
    for (auto & c : state.connections)
    {
        if (c.name == name)
        {
            res = true;
            break;
        }
    }
    return res;
}

bool
isCode(
    Packet & packet,
    MESG::HEADER::Codes code
)
{
    bool ret = false;
    MESG* RxMsg = (MESG*)packet.buffer;
    if (RxMsg->header.code == (uint64_t)code)
    {
        ret = true;
    }
    return ret;
}

bool
magicMatch(
    Packet & packet
)
{
    bool ret = false;
    if (packet.buffer[0] == 65 &&
        packet.buffer[1] == 66 &&
        packet.buffer[2] == 67 &&
        packet.buffer[3] == 68 &&
        packet.buffer[4] == 69 &&
        packet.buffer[5] == 70 &&
        packet.buffer[6] == 71 &&
        packet.buffer[7] == 72)
    {
        ret = true;
    }
    return ret;
}

bool
sizeValid(
    Packet & packet
)
{
    bool ret = false;
    MESG* RxMsg = (MESG*)packet.buffer;
    if (RxMsg->header.code == (uint64_t)(MESG::HEADER::Codes::I))
    {
        if (packet.buffersize >= sizeof(IDENTIFY))
        {
            ret = true;
        }
    }
    else
    if (RxMsg->header.code == (uint64_t)(MESG::HEADER::Codes::G))
    {
        if (packet.buffersize >= sizeof(GRANT))
        {
            ret = true;
        }
    }
    else
    if (RxMsg->header.code == (uint64_t)(MESG::HEADER::Codes::D))
    {
        if (packet.buffersize >= sizeof(DENY))
        {
            ret = true;
        }
    }
    else
    if (RxMsg->header.code == (uint64_t)(MESG::HEADER::Codes::R))
    {
        if (packet.buffersize >= sizeof(READY))
        {
            ret = true;
        }
    }
    else
    if (RxMsg->header.code == (uint64_t)(MESG::HEADER::Codes::S))
    {
        if (packet.buffersize >= sizeof(START))
        {
            ret = true;
        }
    }
    else
    if (RxMsg->header.code == (uint64_t)(MESG::HEADER::Codes::U))
    {
        if (packet.buffersize >= sizeof(UPDATE))
        {
            ret = true;
        }
    }
    else
    if (RxMsg->header.code == (uint64_t)(MESG::HEADER::Codes::L))
    {
        if (packet.buffersize >= sizeof(LEAVE))
        {
            ret = true;
        }
    }
    return ret;
}

bool
ConnMan::processWaitForIdentify(
    ConnManState & state,
    Connection & connection,
    Packet & packet
)
{
    bool ret = false;
    MESG* RxMsg = (MESG*)packet.buffer;
    if (RxMsg->header.code == (uint64_t)(MESG::HEADER::Codes::I))
    {

        // Store Credentials
        connection.name = std::string(RxMsg->payload.identify.name, 16);
        connection.pass = std::string(RxMsg->payload.identify.pass, 16);
        if (state.gamepass == "" || state.gamepass == connection.pass)
        {
            connection.state = Connection::State::SENDGRANT;
        }
        else
        {
            connection.state = Connection::State::SENDDENY;
        }
        ret = true;
    }
    return ret;
}
bool
ConnMan::processSendGrant(
    ConnManState & state,
    Connection & connection
)
{
    bool ret = false;
    // Send a Grant
    Packet packet;
    MESG* TxMsg = (MESG*)packet.buffer;
    packet.address = connection.who;
    packet.buffersize = sizeof(MESG);
    TxMsg->header.code = (uint8_t)(MESG::HEADER::Codes::G);
    TxMsg->header.traits = 0;
    TxMsg->payload.grant.id = connection.id;
    Network::write(state.netstate, packet);
    connection.state = Connection::State::WAITFORREADY;
    return ret;
}

bool
ConnMan::processSendDeny(
    ConnManState & state,
    Connection & connection
)
{
    bool ret = false;
    return ret;
}

bool
ConnMan::processWaitForReady(
    ConnManState & state,
    Connection & connection,
    Packet & packet
)
{
    bool ret = false;
    MESG* RxMsg = (MESG*)packet.buffer;
    if (RxMsg->header.code == (uint8_t)(MESG::HEADER::Codes::R))
    {
        connection.state = Connection::State::WAITFORSTART;
        ret = true;
    }
    return ret;
}

bool
ConnMan::processWaitForStart(
    ConnManState & state,
    Connection & connection
)
{
    bool ret = false;
    return ret;
}

bool
ConnMan::processSendStart(
    ConnManState & state,
    Connection & connection
)
{
    bool ret = false;
    return ret;
}

bool
ConnMan::processGaming(
    ConnManState & state,
    Connection & connection,
    Packet & packet
)
{
    return false;
}

bool
isGood(
    ConnManState & cmstate,
    Connection & connection,
    MESG::HEADER::Codes code,
    Packet & packet
)
{
    bool ret = false;
    if (connection.packets.size() > 0)
    {
        packet = connection.packets.front();

        if (magicMatch(packet))
        {
            if (sizeValid(packet))
            {
                if (isCode(packet, code))
                {
                    //ConnMan::processWaitForIdentify(cmstate, connection, packet);
                    //std::cout << "Rx Identify.\n";
                    ret = true;
                }
                else
                {
                    // Not the Code we're looking for
                    std::cout << "Rx: Weird, packet not expected\n";
                }
            }
            else
            {
                // Invalid - Size does not match code
                std::cout << "Rx: Packet Bad Size\n";
            }
        }
        else
        {
            // Invalid - No Magic
            std::cout << "Rx: Packet Bad Magic\n";
        }
    }
    return ret;
}

void
updateServerConnection(
    ConnManState & cmstate,
    Connection & connection
)
{
    Packet packet;
    bool magicmatch = false;

    /*
    Grab the first packet from this connections' queue.
    sanity check the packet. 
    */

    //if (connection.packets.size() > 0)
    //{
    //    packet = &connection.packets.front();
    //    connection.packets.pop();
    //    assert(packet->buffersize > 0);

    //    if (!magicMatch(*packet) || !sizeValid(*packet))
    //    {
    //        packet = nullptr;
    //    }
    //    //else
    //    //{
    //    //    if (wantsAck(*packet))
    //    //    {

    //    //    }
    //    //}
    //}

    switch (connection.state)
    {
    case Connection::State::WAITFORIDENTIFY: {
        // We are expecting Identify 
        if (isGood(cmstate,connection, MESG::HEADER::Codes::I, packet))
        {
            ConnMan::processWaitForIdentify(cmstate, connection, packet);
            std::cout << "Rx Identify.\n";
        }


        break;
    }case Connection::State::SENDGRANT: {
        // We need to send a Grant
        ConnMan::processSendGrant(cmstate, connection);
        std::cout << "Tx Grant.\n";
        break;
    }case Connection::State::SENDDENY: {
        // We need to send a Deny
        ConnMan::processSendDeny(cmstate, connection);
        std::cout << "Tx Deny.\n";
        break;
    }case Connection::State::WAITFORREADY: {
        // We are waiting for a Ready
        if (isGood(cmstate, connection, MESG::HEADER::Codes::R, packet))
        {
            ConnMan::processWaitForReady(cmstate, connection, packet);
            std::cout << "Rx Ready.\n";
        }
        break;
    }case Connection::State::WAITFORSTART: {
        // This is looking for an internal event
        ConnMan::processWaitForStart(cmstate, connection);
        break;
    }case Connection::State::SENDSTART: {
        // We need to send a Start
        ConnMan::processSendStart(cmstate, connection);
        std::cout << "Tx Start.\n";
        break;
    }case Connection::State::GENERAL: {
        // We are waiting for an Update
        if (isGood(cmstate, connection, MESG::HEADER::Codes::U, packet))
        {
            ConnMan::processGaming(cmstate, connection, packet);
            std::cout << "Rx Update.\n";
        }

        break;
    }case Connection::State::SENDACK: {
        break;
    }case Connection::State::WAITFORACK: {
        break;
    }default:
        break;
    }
}


void
ConnMan::updateServer(
    ConnManState & state,
    uint32_t ms_elapsed
)
{

    state.timeticks += ms_elapsed;

    if (state.timeticks > 90)
    {
        state.timeticks = 0;
        state.cmmutex.lock();
        for (int p = 0; p < state.connections.size(); p++)
        {
            updateServerConnection(state, state.connections[p]);
        }
        state.cmmutex.unlock();
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

bool
ConnMan::AddressKnown(
    ConnManState & state,
    Address* address,
    size_t* index
)
{
    for (*index = 0; *index < state.connections.size();*index++)
    {
        if (memcmp(&address->addr, &state.connections[*index].who.addr, sizeof(SOCKADDR_STORAGE)) == 0)
        {
            return true;
        }
    }
    return false;
}

bool
ConnMan::AddressAuthorized(
    ConnManState & state,
    Address* address
)
{
    return false;
}

void
ConnMan::ConnManIOHandler(
    void* state_,
    Request* request,
    uint64_t id
)
{

    bali::Network::Result result(bali::Network::ResultType::SUCCESS);
    //IoHandlerContext* c = (IoHandlerContext*)cm->handlercontext;
    ConnManState* state = (ConnManState*)state_;
    if (request->ioType == Request::IOType::READ)
    {
        // Debug Print buffer
        std::string payloadAscii((PCHAR)request->packet.buffer, request->packet.buffersize);
        std::cout << "[IN][" << id << "][" << request->packet.buffersize << "]" << payloadAscii.c_str() << std::endl;

        /*
            If we haven't seen this address before
            create a connection, and set state to unknown.

            enqueue packet.
        */

        size_t index=0;

        state->cmmutex.lock();
        if (!ConnMan::AddressKnown(*state, &request->packet.address, &index))
        {
            Connection connection;
            connection.id = InterlockedIncrement(&state->CurrentConnectionId);
            connection.who = request->packet.address;
            connection.state = Connection::State::WAITFORIDENTIFY;

            state->connections.push_back(connection);
            index = state->connections.size() - 1;

            std::cout << "New Connection\n";
        }

        state->connections[index].packets.push(request->packet);
        state->cmmutex.unlock();

        // Prepare read to perpetuate.
        // TODO: what happens when no more free requests?
        Network::read(state->netstate);
    }
    else if (request->ioType == Request::IOType::WRITE)
    {
        // Debug Print buffer
        std::string payloadAscii((PCHAR)request->packet.buffer, request->packet.buffersize);
        std::cout << "[OUT][" << id << "][" << request->packet.buffersize << "]" << payloadAscii.c_str() << std::endl;
    }
    return;
}
} // namespace bali