/////////////////////////////////////////////////////////////////////////////////
//// Daniel J Ferguson
//// 2019
/////////////////////////////////////////////////////////////////////////////////
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
#include <conio.h>

#include "Networking\ConnMan.h"
#include <vector>
#include <chrono>
#include <string>

#include <windows.h>
using namespace bali;

struct SharedContext
{
    ConnMan* pConnMan;
};

bool gAbort = false;
Thread gInputThread;
Thread gStateMonitorThread;
ConnMan gConnMan;

uint32_t requiredNumberOfPlayers = 3;
std::string playerName = "JackA";
std::string gameName = "Bali";
std::string gamePass = "Bear";
uint32_t thisport = 51002;
uint32_t serverport = 51001;
std::string serveripv4 = "10.0.0.93";

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
StateMonitorFunction(
    void* p
);

void
OnEvent(
    void* oecontext,
    ConnManState::OnEventType t,
    Connection* pConn,
    Packet* packet
);


struct SlabDescriptor
{

};
SlabPool<SlabDescriptor> pendingwrites;

int main(int argc, char** argv)
{
    //
    // Handle Ctrl-C 
    // Ctrl-C sets Abort = true
    //
    SetConsoleCtrlHandler(SignalFunction, TRUE);

    //
    // Argument handling
    //
    if (argc > 1)
    {
        thisport = std::atol(argv[1]);
    }
    else
    {
        std::cout << "Usage:\n\t" << argv[0] << "<serverport>\n";
        return 0;
    }

    std::cout << "Server port: " << thisport << "\n";

    //
    // Initialize ConnMan Server
    //
    gSharedContext.pConnMan = &gConnMan;
    std::cout << "Initializing ConnMan Server...\n";

    NetworkConfig netcfg = LoadNetworkConfig("network.config.txt");

    gConnMan.Initialize(netcfg,
                        ConnManState::ConnManType::SERVER,
                        thisport,
                        requiredNumberOfPlayers,
                        gameName,
                        gamePass,
                        OnEvent,
                        &gSharedContext);

    Sleep(60);

    //
    // Start service threads
    //
    gInputThread.handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)InputFunction, &gSharedContext, 0, &gInputThread.id);
    gStateMonitorThread.handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)StateMonitorFunction, &gSharedContext, 0, &gStateMonitorThread.id);

    //
    // Pump the pump
    //
    while (!gAbort)
    {
        gConnMan.Update(1);
        Sleep(0);
    }

    //
    // Wait for all service threads to stop
    //
    WaitForSingleObject(gInputThread.handle, INFINITE);
    WaitForSingleObject(gStateMonitorThread.handle, INFINITE);

    gConnMan.Cleanup();
    return 0;
}

void
OnEvent(
    void* context,
    ConnManState::OnEventType t,
    Connection* pConn,
    Packet* packet
)
{
    static bool timeout_seen = false;
    SharedContext* pSharedContext = (SharedContext*)context;
    ConnMan& cm = *pSharedContext->pConnMan;

    switch (t) 
    {
    case ConnManState::OnEventType::MESSAGE_RECEIVED:
        timeout_seen = false;
        break;
    case ConnManState::OnEventType::CONNECTION_HANDSHAKE_GRANTED:
        break;
    case ConnManState::OnEventType::CONNECTION_HANDSHAKE_DENIED:
        break;
    case ConnManState::OnEventType::CONNECTION_TIMEOUT:
        // Connection has been idle for a while
        //
        if (!timeout_seen) {
            std::cout << "CONNECTION_TIMEOUT: " << std::endl;//<< pConn->playername << " : " << pConn->id << std::endl;
            timeout_seen = true;
        }
        break;  
    case ConnManState::OnEventType::MESSAGE_ACK_TIMEOUT:
        // A reliable packet was not acknowledged.
        // pConn and packet are valid. (retry is possible)
        //
        std::cout << "MESSAGE_ACK_TIMEOUT: " << std::endl;//<< pConn->playername << " : " << pConn->id << std::endl;
        break;
    case ConnManState::OnEventType::MESSAGE_ACK_RECEIVED:
        // A reliable packet was acknowledged.
        //
        std::cout << "MESSAGE_ACK_RECEIVED: " << std::endl;// << pConn->playername << " : " << pConn->id << std::endl;
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
        if (strinput == "p")
        {

        }
        else if (strinput == "rtt")
        {
            //Connection::ConnectionPtr pConn = ConnMan::GetConnectionById(cmstate.connectionsmutex, cmstate.connections, gMyUniqueId);

            cm.cmstate.connectionsmutex.lock();
            for (auto & c : cm.cmstate.connections)
            {
                std::cout << "Connection: " << c->id << std::endl;
                std::cout << "\tPing: " << c->curping << std::endl;
                std::cout << "\tAvg: " << c->avgping << std::endl;
            }
            cm.cmstate.connectionsmutex.unlock();
        }

        Sleep(0);
    }

}

void
StateMonitorFunction(
    void* p
)
{

    while (!gAbort)
    {

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
