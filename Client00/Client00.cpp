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

Address gToServer;
OnEventContext gOnEventContext;

uint32_t ackstatus = 0;
uint32_t localId=0;

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
    // Initialize ConnMan
    //
    gOnEventContext.pConnManState = &gConnManState;
    std::cout << "Initializing ConnMan...\n";
    
    NetworkConfig netcfg = LoadNetworkConfig("network.config.txt");

    ConnMan::initialize(gConnManState,
                        netcfg,
                        thisport,
                        requiredNumberOfPlayers,
                        gameName,
                        gamePass,
                        OnEvent,
                        &gOnEventContext);

    Sleep(60);
    
    //
    // Create an Address to Server
    //
    gToServer = CreateAddress(serverport, serveripv4.c_str());

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
        ConnMan::updateServer(gConnManState, 1);
        Sleep(0);
    }

    //
    // Wait for all service threads to stop
    //
    WaitForSingleObject(gInputThread.handle, INFINITE);
    WaitForSingleObject(gStateMonitorThread.handle, INFINITE);

    ConnMan::cleanup(gConnManState);
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
        if (!stale_seen){
            std::cout << "CONNECTION_STALE: " << pConn->playername << " : " << pConn->id << std::endl;
            stale_seen = true;
        }
        //pConn->checkintime = clock::now();//hack
        //ConnMan::SendReliable(cmstate, pConn->id, (uint8_t*)"BLAh", 4, nullptr);
        break;
    case ConnManState::OnEventType::ACK_TIMEOUT:
        // A reliable packet was not acknowledged.
        // pConn and packet are valid. (retry is possible)
        //
        std::cout << "ACK_TIMEOUT: " << pConn->playername << " : " << pConn->id << std::endl;
        //pConn->state = Connection::State::IDLE; // This is to let the next reliable message in the reliable queue to go through.
        ackstatus = 1;
        break;
    case ConnManState::OnEventType::ACK_RECEIVED:
        // A reliable packet was acknowledged.
        //
        std::cout << "ACK_RECEIVED: " << pConn->playername << " : " << pConn->id << std::endl;
        ackstatus=2;
        break;
    case ConnManState::OnEventType::GRANTED:
        // Client-Side Event
        // Our attempt to connect to a game server was successful
        //
        //std::cout << "GRANTED: " << ((MESG*)packet->buffer)->header.id << "\n";
        std::cout << "GRANTED: " << pConn->playername << " : " << pConn->id << std::endl;
        
        localId = pConn->id;
        break;
    case ConnManState::OnEventType::DENIED:
        // Client-Side Event
        // Our attempt to connect to a game server was unsuccessful
        //
        std::cout << "DENIED: " << pConn->playername << " : " << pConn->id << std::endl;
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
        if (strinput == "i")
        {
            ConnMan::sendIdentifyTo(cmstate,
                                    gToServer,
                                    playerName,
                                    gameName,
                                    gamePass);
        }
        else if (strinput == "r")
        {
            ConnMan::SendReliable(cmstate,
                                    gToServer,
                                    localId,
                                    (uint8_t*)"I Love Bali Fern Sprout",
                                    24,
                                    nullptr);
        }
        else if (strinput == "u")
        {
            ConnMan::SendUnreliable(cmstate,
                                    gToServer,
                                    localId,
                                    (uint8_t*)"I Love All My Dogs",
                                    18);
        }
        else if (strinput == "p")
        {
            std::cout << "Avg. Ping: " << cmstate.localconn.avgping << std::endl;
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
