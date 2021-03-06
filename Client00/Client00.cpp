/////////////////////////////////////////////////////////////////////////////////
//// Daniel J Ferguson
//// 2020
/////////////////////////////////////////////////////////////////////////////////
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
#include <conio.h>

#include "Networking\ConnMan.h"

#include <vector>
#include <chrono>
#include <string>
#include <algorithm>
#include <random>
#include <chrono>
#include <windows.h>
using namespace bali;

struct SharedContext
{
    ConnMan* pConnMan;
};

bool gAbort = false;
Thread gInputThread;
Thread gNetworkThread;
ConnMan gConnMan;
Connection::ConnectionPtr gLocalConn;

uint32_t requiredNumberOfPlayers = 3;
std::string playerName = "JackA";
std::string gameName = "Bali";
std::string gamePass = "Bear";
uint32_t thisport = 0;
uint32_t serverport = 51001;
std::string serveripv4 = "10.0.0.93";
uint32_t gMyUniqueId = 32; // I Sure hope no one else picks this! :)! lolz
bool gConnected = false;
Address gToServer;
SharedContext gSharedContext;


BOOL
WINAPI
SignalFunction(
    _In_ DWORD dwCtrlType
);

void
InputFunction(
    void* p
);

void
NetworkFunction(
    void* p
);

void
OnEvent(
    void* oecontext,
    ConnManState::OnEventType t,
    uint32_t uid,
    Packet* packet
);

int main(int argc, char** argv)
{
    std::random_device rd;     // only used once to initialise (seed) engine
    std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)
    std::uniform_int_distribution<uint32_t> uni(1, 65536); // guaranteed unbiased

    gMyUniqueId = uni(rng);
    //
    // Handle Ctrl-C 
    // Ctrl-C sets Abort = true
    //
    SetConsoleCtrlHandler(SignalFunction, TRUE);

    //
    // Argument handling
    //
    if (argc > 4)
    {
        thisport = 0;// std::atol(argv[1]);
        serverport = std::atol(argv[2]);
        serveripv4 = std::string(argv[3], strlen(argv[3]));
        playerName = std::string(argv[4], strlen(argv[4]));
    }
    else
    {
        std::cout << "Usage:\n\t" << argv[0] << "<thisport> <serverport> <serveripv4> <playername>\n";
        return 0;
    }

    std::cout << "Selecting Player: " << playerName << "\n";
    std::cout << "This port: " << thisport << "\n";
    std::cout << "Server port: " << serverport << "\n";
    std::cout << "Server ipv4: " << serveripv4 << "\n";

    //
    // Initialize ConnMan Client
    //
    gSharedContext.pConnMan = &gConnMan;
    std::cout << "Initializing ConnMan Client...\n";

    NetworkConfig netcfg = LoadNetworkConfig("network.config.txt");

    gConnMan.Initialize(netcfg,
                        ConnManState::ConnManType::CLIENT,
                        0,
                        1,
                        gameName,
                        gamePass,
                        OnEvent,
                        &gSharedContext);

    Sleep(60);

    //
    // Create an Address to Server
    //
    gToServer = ConnMan::CreateAddress(serverport, serveripv4.c_str());

    //
    // Start service threads
    //
    gInputThread.handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)InputFunction, &gSharedContext, 0, &gInputThread.id);
    gNetworkThread.handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)NetworkFunction, &gSharedContext, 0, &gNetworkThread.id);
    //
    // Pump the pump
    //
    while (!gAbort)
    {
        //gConnMan.Update(1);
        Sleep(0);
    }

    //
    // Wait for all service threads to stop
    //
    WaitForSingleObject(gInputThread.handle, INFINITE);

    gConnMan.Cleanup();
    return 0;
}

void
NetworkFunction(
    void* p
)
{
    while (!gAbort)
    {
        gConnMan.Update(1);
        Sleep(0);
    }
}

void
OnEvent(
    void* context,
    ConnManState::OnEventType t,
    uint32_t uid,
    Packet* packet
)
{
    static bool timeout_seen = false;

    SharedContext* pSharedContext = (SharedContext*)context;
    ConnMan& cm = *pSharedContext->pConnMan;
    switch (t)
    {
    case ConnManState::OnEventType::MESSAGE_RECEIVED: {
        Connection::ConnectionPtr pConn = ConnMan::GetConnectionById(cm.cmstate.connections, uid);
        std::cout << "MESSAGE_RECIEVED: " << pConn->playername << std::endl;
        break;
    }case ConnManState::OnEventType::CONNECTION_TIMEOUT:
        std::cout << "CONNECTION_TIMEOUT: " << std::endl;
        break;
    case ConnManState::OnEventType::MESSAGE_ACK_TIMEOUT: {
        MESG* m = (MESG*)packet->buffer;
        std::cout << "MESSAGE_ACK_TIMEOUT: " << CodeName[m->header.code] << std::endl;
        break;
    }case ConnManState::OnEventType::MESSAGE_ACK_RECEIVED: {
        MESG* m = (MESG*)packet->buffer;
        std::cout << "MESSAGE_ACK_RECEIVED: " << CodeName[m->header.code] << std::endl;
        break;
    }case ConnManState::OnEventType::CONNECTION_HANDSHAKE_GRANTED: {
        Connection::ConnectionPtr pConn = ConnMan::GetConnectionById(cm.cmstate.connections, uid);
        gConnected = true;
        gMyUniqueId = uid;
        std::cout << "CONNECTION_HANDSHAKE_GRANTED: " << pConn->playername << std::endl;
        break;
    }case ConnManState::OnEventType::CONNECTION_HANDSHAKE_DENIED:
        std::cout << "CONNECTION_HANDSHAKE_DENIED: " << std::endl;
        break;
    case ConnManState::OnEventType::CONNECTION_HANDSHAKE_TIMEOUT:
        std::cout << "CONNECTION_HANDSHAKE_TIMEOUT: " << std::endl;
        break;
    }
}

void
InputFunction(
    void* context
)
{
    SharedContext* pSharedContext = (SharedContext*)context;
    ConnMan& cm = *pSharedContext->pConnMan;
    std::string strinput;
    char input[64];

    while (!gAbort)
    {
        std::cin >> input;
        strinput = std::string(input, strlen(input));

        // Disconnect current Connection
        if (strinput == "d")
        {
            ConnMan::Disconnect(cm, gToServer, gMyUniqueId);
            gConnected = false;
        }
        // Connect to Server, 
        else if (strinput == "i")
        {
            //
            // Connecting is important, so
            // we'll perform this operation
            // synchronously.
            ConnectingResultFuture future;
            if (ConnMan::Connect(cm, gToServer, playerName, "Bali", "Bear", gMyUniqueId, future))
            {
                //Connection::ConnectingResult cresult = future.get();
                //if (cresult.code == Connection::ConnectingResultCode::GRANTED)
                //{
                //    gMyUniqueId = cresult.id;
                //    //std::cout << "Connection: Granted: " << gMyUniqueId << "\n";
                //}
                //else if (cresult.code == Connection::ConnectingResultCode::DENIED)
                //{
                //    std::cout << "Connection: Denied\n";
                //}
                //else if (cresult.code == Connection::ConnectingResultCode::TIMEDOUT)
                //{
                //    std::cout << "Connection: Timeout\n";
                //}
            }
        }
        // Send a Reliable Packet.
        else if (strinput == "r")
        {
            if (gConnected)
            {
                ConnMan::SendReliable(cm, gMyUniqueId, "I Love Bali Fern and Sprout",
                                      strlen("I Love Bali Fern and Sprout"));
            }
        }
        else if (strinput == "u")
        {
            if (gConnected)
            {
                ConnMan::SendUnreliable(cm, gMyUniqueId, "Fern is beautiful, Sprout is strong.", strlen("Fern is beautiful, Sprout is strong."));
            }
        }
        else if (strinput == "rtt")
        {
            Connection::ConnectionPtr pConn = ConnMan::GetConnectionById(cm.cmstate.connectionsmutex, cm.cmstate.connections, gMyUniqueId);
            if (pConn != nullptr)
            {
                std::cout << "Connection: " << pConn->id << std::endl;
                std::cout << "     State: " << (uint32_t)pConn->state << std::endl;
                std::cout << "  Fidelity: " << 100.f*((float)pConn->totalpongs / (float)pConn->totalpings) << std::endl;
                std::cout << "     Pings: " << pConn->totalpings << std::endl;
                std::cout << "  Cur Ping: " << pConn->curping << std::endl;
                std::cout << "   Avg(us): " << pConn->avgping << std::endl;
            }
        }
        Sleep(0);
    }
}

BOOL
WINAPI
SignalFunction(
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
