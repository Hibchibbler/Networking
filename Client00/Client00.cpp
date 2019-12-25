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
Thread gStateMonitorThread;
ConnMan gConnMan;

uint32_t requiredNumberOfPlayers = 3;
std::string playerName = "JackA";
std::string gameName = "Bali";
std::string gamePass = "Bear";
uint32_t thisport = 51002;
uint32_t serverport = 51001;
std::string serveripv4 = "10.0.0.93";
uint32_t gRandomNumber= 32;
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
    std::random_device rd;     // only used once to initialise (seed) engine
    std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)
    std::uniform_int_distribution<uint32_t> uni(1, 65536); // guaranteed unbiased

    gRandomNumber = uni(rng);
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

    gConnMan.InitializeClient(netcfg,
                              thisport,
                              1,
                              gameName,
                              gamePass,
                              OnEvent,
                              &gSharedContext);

    Sleep(60);
    
    //
    // Create an Address to Server
    //
    gToServer = CreateAddress(serverport, serveripv4.c_str());

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
        gConnMan.Update(1, true);
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
        break;
    case ConnManState::OnEventType::CONNECTION_TIMEOUT:
        std::cout << "CONNECTION_TIMEOUT: " << pConn->playername << " : " << pConn->id << std::endl;
        break;
    case ConnManState::OnEventType::MESSAGE_ACK_TIMEOUT:
        std::cout << "MESSAGE_ACK_TIMEOUT: " 
                  << pConn->playername << " : " << pConn->id << ": "
                  << ((MESG*)packet->buffer)->header.seq << std::endl;
        break;
    case ConnManState::OnEventType::MESSAGE_ACK_RECEIVED:{
        MESG* p = (MESG*)packet->buffer;
        std::cout << "MESSAGE_ACK_RECEIVED: " 
                  << pConn->playername << " : " << pConn->id << ": "
                  << p->header.seq<< std::endl;
        break;
    }case ConnManState::OnEventType::CONNECTION_HANDSHAKE_GRANTED:
        std::cout << "CONNECTION_HANDSHAKE_GRANTED: " << pConn->playername << " : " << pConn->id << std::endl;
        break;
    case ConnManState::OnEventType::CONNECTION_HANDSHAKE_DENIED:
        std::cout << "CONNECTION_HANDSHAKE_DENIED: " << pConn->playername << " : " << pConn->id << std::endl;
        break;
    case ConnManState::OnEventType::CONNECTION_HANDSHAKE_TIMEOUT:
        std::cout << "CONNECTION_HANDSHAKE_TIMEOUT: " << pConn->playername << " : " << pConn->id << std::endl;
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
        if (strinput == "q")
        {
            std::random_device rd;     // only used once to initialise (seed) engine
            std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)
            std::uniform_int_distribution<uint32_t> uni(1, 65536); // guaranteed unbiased

            gRandomNumber = uni(rng);
        }
        if (strinput == "i")
        {


            InitializeConnection(&cm.cmstate.localconn,
                                 gToServer,
                                 playerName);
//BammerWeed:
            cm.SendIdentify(cm.cmstate.localconn,
                            gRandomNumber,
                            gameName,
                            gamePass);
            //if (cm.cmstate.localconn.state == Connection::State::DENIED)
            //{
            //    goto BammerWeed;
            //}
        }
        else if (strinput == "r")
        {
            Connection& connection = cm.cmstate.localconn;

            if (connection.state != Connection::State::ALIVE)
            {
                std::cout << "Bad Idea, Connection is Dead, or still Identifying\n";
                //continue;
            }
            uint32_t index = 
            cm.SendReliable(connection,
                            (uint8_t*)"I Love Bali Fern Sprout",
                            24,
                            nullptr);

        }
        else if (strinput == "u")
        {
            cm.SendUnreliable(cm.cmstate.localconn,
                              (uint8_t*)"I Love All My Dogs",
                              18);
        }
        else if (strinput == "p")
        {
            std::cout << "Avg. Ping: " << cm.cmstate.localconn.avgping << std::endl;
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
