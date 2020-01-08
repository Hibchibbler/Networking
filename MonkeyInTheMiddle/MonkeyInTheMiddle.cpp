/////////////////////////////////////////////////////////////////////////////////
//// Daniel J Ferguson
//// 2019
/////////////////////////////////////////////////////////////////////////////////
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
#include <conio.h>

#include "Networking\ConnMan.h"
#include "Networking\ConnManUtils.h"
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
                        ConnManState::ConnManType::RELAY,
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
        std::cout << "MESSAGE_RECIEVED" << std::endl;
        break;
    case ConnManState::OnEventType::CONNECTION_TIMEOUT:
        std::cout << "CONNECTION_TIMEOUT: " << std::endl;
        break;
    case ConnManState::OnEventType::MESSAGE_ACK_TIMEOUT: {
        MESG * m = (MESG*)packet->buffer;
        std::cout << "MESSAGE_ACK_TIMEOUT: " << CodeName[m->header.code] << std::endl;
        break;
    }case ConnManState::OnEventType::MESSAGE_ACK_RECEIVED: {
        MESG* m = (MESG*)packet->buffer;
        std::cout << "MESSAGE_ACK_RECEIVED: " << CodeName[m->header.code] << std::endl;
        break;
    }case ConnManState::OnEventType::CONNECTION_HANDSHAKE_GRANTED:
        std::cout << "CONNECTION_HANDSHAKE_GRANTED: " << std::endl;
        break;
    case ConnManState::OnEventType::CONNECTION_HANDSHAKE_DENIED:
        std::cout << "CONNECTION_HANDSHAKE_DENIED: " << std::endl;
        break;
    case ConnManState::OnEventType::CONNECTION_HANDSHAKE_TIMEOUT:
        std::cout << "CONNECTION_HANDSHAKE_TIMEOUT: " << std::endl;
        break;
    case ConnManState::OnEventType::CONNECTION_HANDSHAKE_TIMEOUT_NOGRACK:
        std::cout << "CONNECTION_HANDSHAKE_TIMEOUT_NOGRACK: " << std::endl;
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
            Network::read(cm.cmstate.netstate);
        }
        else if (strinput == "rtt")
        {
            //Connection::ConnectionPtr pConn = ConnMan::GetConnectionById(cmstate.connectionsmutex, cmstate.connections, gMyUniqueId);

            cm.cmstate.connectionsmutex.lock();
            for (auto & c : cm.cmstate.connections)
            {
                std::cout << "Connection: " << c->id << std::endl;
                std::cout << "     State: " << (uint32_t)c->state << std::endl;
                std::cout << "  Fidelity: " << 100.f*((float)c->totalpongs / (float)c->totalpings) << std::endl;
                std::cout << "     Pings: " << c->totalpings << std::endl;
                std::cout << "  Cur Ping: " << c->curping << std::endl;
                std::cout << "       Avg: " << c->avgping << std::endl;
            }
            cm.cmstate.connectionsmutex.unlock();
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
