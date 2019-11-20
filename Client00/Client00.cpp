/////////////////////////////////////////////////////////////////////////////////
//// Daniel J Ferguson
//// 2019
/////////////////////////////////////////////////////////////////////////////////
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
#include <conio.h>

//
//#ifndef UNICODE
//#define UNICODE
//#endif


#include "Networking\ConnMan.h"
#include <vector>
#include <chrono>
#include <string>
using namespace bali;

uint32_t requiredNumberOfPlayers = 1;
std::string playerName = "JackA";
std::string gameName = "Bali";
std::string gamePass = "Bear";
uint32_t thisport = 51002;
uint32_t serverport = 51001;
std::string serveripv4 = "10.0.0.93";

volatile bool gNetworkInitialized = false;
volatile bool gAbort = false;
Thread gKeyboardThread;
Thread gStateMonitorThread;
Thread gNetworkThread;

Address gToServer;

struct OnEventContext
{
    ConnManState* pConnManState;
    uint32_t      currentReady;
    uint32_t      requiredReady;
};

struct GameMesg
{
    enum class Codes {
        START,
        READY,
        PING,
        PONG,
        UPDATE
    };

    uint32_t code;
    union {
        uint8_t  buffer[960]; // MESG expects GameMesg to be <= 1024

    }payload;
};

BOOL
WINAPI
HandlerRoutine(
    _In_ DWORD dwCtrlType
);

void
NetworkFunction(
    void* p
);

void
KeyboardFunction(
    void* p
);

void
StateMonitorFunction(
    void* p
);

void
OnEvent(
    void* oecontext,
    ConnManState::OnEventType t,
    Connection* conn,
    Packet* packet
);


GameMesg*
GetGameMesg(
    MESG* pMsg
);

uint32_t
GetGameMesgCode(
    GameMesg* gameMsg
);

uint32_t
SendGameMesg(
    ConnManState& cmstate,
    Address to,
    uint32_t id,
    GameMesg::Codes GameMesgCode,
    void* payload,
    uint32_t payloadsize
);

uint32_t
SendStartMesg(
    ConnManState& cmstate
);

uint32_t
SendReadyMesg(
    ConnManState& cmstate,
    Address to,
    uint32_t id
);

uint32_t
SendUpdateMesg(
    ConnManState& cmstate,
    Address to,
    uint32_t id,
    void* payload,
    uint32_t payloadsize
);


int main(int argc, char** argv)
{
    ConnManState cmstate;
    uint32_t elapsed = 0;

    //
    // Handle Ctrl-C 
    // Ctrl-C sets Abort = true
    //
    SetConsoleCtrlHandler(HandlerRoutine, TRUE);


    //
    // Argument handling
    //
    if (argc > 4)
    {
        thisport = std::atol(argv[1]);
        serverport = std::atol(argv[2]);
        serveripv4 = std::string(argv[3], strlen(argv[3]));
        playerName = std::string(argv[4], strlen(argv[4]));
    }
    else
    {
        std::cout << "Usage:\n\t" << argv[0] << " <thisport> <serverport> <serveripv4> <playername>\n";
        return 0;
    }

    std::cout << "Selecting Player: " << playerName << "\n";
    std::cout << "Selecting port: " << thisport << "\n";
    std::cout << "Server port: " << serverport << "\n";
    std::cout << "Server ipv4: " << serveripv4 << "\n";

    //
    // Create an Address to Server
    //
    gToServer = CreateAddress(serverport, serveripv4.c_str());

    gNetworkThread.handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)NetworkFunction, &cmstate, 0, &gNetworkThread.id);
    Sleep(100);
    gKeyboardThread.handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)KeyboardFunction, &cmstate, 0, &gKeyboardThread.id);
    gStateMonitorThread.handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)StateMonitorFunction, &cmstate, 0, &gStateMonitorThread.id);

    while (!gAbort) { Sleep(0); }

    WaitForSingleObject(gKeyboardThread.handle, INFINITE);
    WaitForSingleObject(gStateMonitorThread.handle, INFINITE);
    WaitForSingleObject(gNetworkThread.handle, INFINITE);

    std::cout << "Exiting...\n";
    return 0;
}


GameMesg*
GetGameMesg(
    MESG* pMsg
)
{
    GameMesg* pGsg = (GameMesg*)&pMsg->payload.general;
    return pGsg;
}

uint32_t
GetGameMesgCode(
    GameMesg* gameMsg
)
{
    uint32_t code = gameMsg->code;
    return code;
}

uint32_t
SendGameMesg(
    ConnManState& cmstate,
    Address to,
    uint32_t id,
    GameMesg::Codes GameMesgCode,
    void* payload,
    uint32_t payloadsize
)
{
    Packet packet;
    MESG* pMsg = (MESG*)packet.buffer;
    GameMesg* pGameMsg = (GameMesg*)pMsg->payload.buffer;

    packet.buffersize = sizeof(MESG::HEADER) + SizeofPayload(MESG::HEADER::Codes::General);
    packet.address = to;

    memcpy(pMsg->header.magic, "ABCD", 4);
    pMsg->header.code = (uint32_t)MESG::HEADER::Codes::General;
    pMsg->header.id = id;
    pMsg->header.traits = 0;
    pMsg->header.seq = 0;
    pMsg->header.crc = 0;

    pGameMsg->code = (uint32_t)GameMesgCode;
    memcpy(pGameMsg->payload.buffer, payload, payloadsize);

    Network::write(cmstate.netstate, packet);
    return 0;
}

uint32_t
SendStartMesg(
    ConnManState& cmstate
)
{
    for (auto & c : cmstate.connections)
    {
        SendGameMesg(cmstate,
                     c.who,
                     c.id,
                     GameMesg::Codes::START,
                     0, 0);
    }
    return 0;
}

uint32_t
SendReadyMesg(
    ConnManState& cmstate,
    Address to,
    uint32_t id
)
{
    SendGameMesg(cmstate,
                 to,
                 id,
                 GameMesg::Codes::READY,
                 0,
                 0);
    return 0;
}

uint32_t
SendUpdateMesg(
    ConnManState& cmstate,
    Address to,
    uint32_t id,
    void* payload,
    uint32_t payloadsize
)
{
    SendGameMesg(cmstate,
                 to,
                 id,
                 GameMesg::Codes::UPDATE,
                 payload,
                 payloadsize);
    return 0;
}

void
KeyboardFunction(
    void* p
)
{
    std::string strinput;
    char input[64];
    ConnManState& cmstate = *((ConnManState*)p);

    while (!gAbort)
    {
        Sleep(0);
        std::cout << ">>>";

        std::cin >> input;
        strinput = std::string(input, strlen(input));
        if (strinput == "i")
        {
            ConnMan::sendIdentifyTo(cmstate,
                                    gToServer,
                                    playerName,
                                    gameName,
                                    gamePass);
            std::cout << "Sent Identify\n";
        }
        else if (strinput == "r")
        {
            /*ConnMan::sendReadyTo(cmstate,
                                    gToServer,
                                    cmstate.connections.front().id);*/
            SendReadyMesg(cmstate,
                          gToServer,
                          cmstate.connections.front().id);
            std::cout << "Sent ready\n";
        }
        else if (strinput == "u")
        {
            SendUpdateMesg(cmstate,
                           gToServer,
                           cmstate.connections.front().id,
                           0,
                           0);
            std::cout << "Sent Update\n";
        }
        else if (strinput == "p")
        {
            //ConnMan::sendPingTo(cmstate,
            //                    gToServer,
            //                    cmstate.connections.front().id);
            //std::cout << "Sent Ping\n";
        }
        else if (strinput == "sp")
        {
            //float acc = 0;
            //float cnt = 0;
            //for (auto v : cmstate.connections.front().pingtimes)
            //{
            //    acc += v.count();
            //    cnt++;
            //}
            //std::cout << "Avg Ping: " << acc / cnt << "\n";
        }
        else
        {
            std::cout << "What?\n";
        }
    }

}

void
NetworkFunction(
    void* p
)
{
    OnEventContext oec;
    ConnManState& cmstate = *((ConnManState*)p);
    oec.pConnManState = &cmstate;
    oec.currentReady = 0;
    oec.requiredReady = requiredNumberOfPlayers;
    std::cout << "Initializing ConnMan...\n";
    ConnMan::initialize(cmstate,
                        thisport,
                        requiredNumberOfPlayers,
                        gameName,
                        gamePass,
                        OnEvent,
                        &oec);

    gNetworkInitialized = true;

    while (!gAbort)
    {
        ConnMan::updateServer(cmstate, 1);
        Sleep(0);
    }

    ConnMan::cleanup(cmstate);
}

void
StateMonitorFunction(
    void* p
)
{
    ConnManState& cmstate = *((ConnManState*)p);

    while (!gAbort)
    {

        Sleep(0);
    }
}

void
OnEvent(
    void* oecontext,
    ConnManState::OnEventType t,
    Connection* pConn,
    Packet* packet
)
{
    OnEventContext* oec = (OnEventContext*)oecontext;
    std::string playername;
    switch (t)
    {
    case ConnManState::OnEventType::NEW:

        std::cout << "New Connection\n";

        break;
    case ConnManState::OnEventType::MESSAGE:
        MESG* pMsg = GetMesg(*packet);
        GameMesg* gameMsg = GetGameMesg(pMsg);
        uint32_t gameCode = GetGameMesgCode(gameMsg);
        if (gameCode == (uint32_t)GameMesg::Codes::READY)
        {
            std::cout << "Rx Ready\n";
            oec->currentReady++;
            if (oec->currentReady >= oec->requiredReady)
            {
                std::cout << "Tx Start\n";
                SendStartMesg(*oec->pConnManState);
            }
        }
        else if (gameCode == (uint32_t)GameMesg::Codes::START)
        {
            std::cout << "Rx Start\n";
        }
        else if (gameCode == (uint32_t)GameMesg::Codes::PING)
        {
            std::cout << "Rx Ping\n";
        }
        else if (gameCode == (uint32_t)GameMesg::Codes::PONG)
        {
            std::cout << "Rx Pong\n";
        }
        else if (gameCode == (uint32_t)GameMesg::Codes::UPDATE)
        {
            std::cout << "Rx Update\n";
        }

        break;
    }
}

BOOL
WINAPI
HandlerRoutine(
    _In_ DWORD dwCtrlType
)
{
    switch (dwCtrlType)
    {
    case CTRL_C_EVENT:
        std::cout << "Aborting...\n";
        gAbort = true;
        // Signal is handled - don't pass it on to the next handler
        return TRUE;
    default:
        // Pass signal on to the next handler
        return FALSE;
    }
}