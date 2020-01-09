#include "ConnManUtils.h"
#include <random>
namespace bali
{

bool SendUnreliable(
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

bool SendReliable(
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

        if (sent){
            std::cout << "SendReliable Success. Tries: "<< curRetries << "\n";
            ret = true;
        }else
        {
            std::cout << "SendReliable Failed. Tries: " << curRetries << "\n";
        }
    }
    else
    {
        std::cout << "SendReliable(): Unknown UID: "<< uid <<"\n";
    }
    return ret;
}

bool
Connect(
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

        Packet packet = CreateIdentifyPacket(pConn->who,
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

Packet
CreateIdentifyPacket(
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

    //TODO TOAD
    //conn.identtime = clock::now();
    //conn.state = Connection::State::IDENTIFYING;
    //conn.locality = Connection::Locality::LOCAL;

    //TODO ZEBRA
    //cmstate.connectionsmutex.lock();
    //cmstate.connections.push_back(connection);
    //cmstate.connectionsmutex.unlock();
    //Network::write(*netstate, packet);


    return packet;
}
Packet
CreatePongPacket(
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
CreatePingPacket(
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
CreateGrantPacket(
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
CreateGrackPacket(
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
CreateDisconnectPacket(
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
CreateDenyPacket(
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
CreateAckPacket(
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
    if (code == MESG::HEADER::Codes::Ping ||
        code == MESG::HEADER::Codes::Pong) { payloadSize += sizeof(PINGPONG); }

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

uint32_t
ExtractConnectionIdFromPacket(
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
ExtractPlayerNameFromPacket(
    Packet & packet
)
{
    MESG* pMsg = (MESG*)packet.buffer;
    return std::string(pMsg->payload.grant.playername,
        strlen(pMsg->payload.grant.playername));
}

std::string
ExtractGameNameFromPacket(
    Packet & packet
)
{
    MESG* pMsg = (MESG*)packet.buffer;
    return std::string(pMsg->payload.identify.gamename,
                       strlen(pMsg->payload.identify.gamename));
}

std::string
ExtractGamePassFromPacket(
    Packet & packet
)
{
    MESG* pMsg = (MESG*)packet.buffer;
    return std::string(pMsg->payload.identify.gamepass,
                       strlen(pMsg->payload.identify.gamepass));
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




}