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

struct OnEventContext
{
    ConnManState* pConnManState;
};

bool gAbort = false;
Thread gInputThread;
Thread gStateMonitorThread;
ConnManState gConnManState;

uint32_t requiredNumberOfPlayers = 3;
std::string playerName = "JackA";
std::string gameName = "Bali";
std::string gamePass = "Bear";
uint32_t thisport = 51002;
uint32_t serverport = 51001;
std::string serveripv4 = "10.0.0.93";
OnEventContext gOnEventContext;

uint32_t ackstatus = 0;
uint32_t localId = 0;

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
        serverport = std::atol(argv[1]);
    }
    else
    {
        std::cout << "Usage:\n\t" << argv[0] << " <serverport>\n";
        return 0;
    }

    std::cout << "Server port: " << serverport << "\n";

    //
    // Initialize ConnMan Server
    //
    gOnEventContext.pConnManState = &gConnManState;
    std::cout << "Initializing ConnMan Server...\n";

    NetworkConfig netcfg = LoadNetworkConfig("network.config.txt");

    ConnMan::InitializeServer(gConnManState,
                              netcfg,
                              serverport,
                              requiredNumberOfPlayers,
                              gameName,
                              gamePass,
                              OnEvent,
                              &gOnEventContext);

    Sleep(60);

    //
    // Start service threads
    //
    gInputThread.handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)InputFunction, &gConnManState, 0, &gInputThread.id);
    gStateMonitorThread.handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)StateMonitorFunction, &gConnManState, 0, &gStateMonitorThread.id);

    //
    // Pump the pump
    //
    while (!gAbort)
    {
        ConnMan::UpdateServer(gConnManState, 1);
        Sleep(0);
    }

    //
    // Wait for all service threads to stop
    //
    WaitForSingleObject(gInputThread.handle, INFINITE);
    WaitForSingleObject(gStateMonitorThread.handle, INFINITE);

    ConnMan::Cleanup(gConnManState);
    return 0;
}

void
OnEvent(
    void* oecontext,
    ConnManState::OnEventType t,
    Connection* pConn,
    Packet* packet
)
{
    static bool stale_seen = false;
    OnEventContext* oec = (OnEventContext*)oecontext;
    ConnManState& cmstate = *oec->pConnManState;
    std::string playername;
    switch (t)
    {
    case ConnManState::OnEventType::MESSAGE:
        stale_seen = false;
        break;
    case ConnManState::OnEventType::CONNECTION_ADD:
        // Server has a new connection
        //
        std::cout << "CONNECTION_ADD: " << pConn->playername << " : " << pConn->id << std::endl;
        break;
    case ConnManState::OnEventType::CONNECTION_REMOVE:
        // Server lost a connection
        //
        std::cout << "CONNECTION_REMOVE: " << pConn->playername << " : " << pConn->id << std::endl;
        break;
    case ConnManState::OnEventType::CONNECTION_STALE:
        // Connection has been idle for a while
        //
        if (!stale_seen) {
            std::cout << "CONNECTION_STALE: " << pConn->playername << " : " << pConn->id << std::endl;
            stale_seen = true;
        }
        break;
    case ConnManState::OnEventType::ACK_TIMEOUT:
        // A reliable packet was not acknowledged.
        // pConn and packet are valid. (retry is possible)
        //
        std::cout << "ACK_TIMEOUT: " << pConn->playername << " : " << pConn->id << std::endl;
        ackstatus = 1;
        break;
    case ConnManState::OnEventType::ACK_RECEIVED:
        // A reliable packet was acknowledged.
        //
        std::cout << "ACK_RECEIVED: " << pConn->playername << " : " << pConn->id << std::endl;
        ackstatus = 2;
        break;
    }
}

void
InputFunction(
    void* p
)
{
    ConnManState & cmstate = *((ConnManState*)p);
    std::string strinput;
    char input[64];

    while (!gAbort)
    {
        std::cin >> input;
        strinput = std::string(input, strlen(input));
        if (strinput == "p")
        {

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
