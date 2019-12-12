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
    if (argc > 3)
    {
        serverport = std::atol(argv[1]);
        serveripv4 = std::string(argv[2], strlen(argv[2]));
        playerName = std::string(argv[3], strlen(argv[3]));
    }
    else
    {
        std::cout << "Usage:\n\t" << argv[0] << " <serverport> <serveripv4> <playername>\n";
        return 0;
    }
    
    std::cout << "Selecting Player: " << playerName << "\n";
    std::cout << "Server port: " << serverport << "\n";
    std::cout << "Server ipv4: " << serveripv4 << "\n";

    //
    // Initialize ConnMan Client
    //
    gOnEventContext.pConnManState = &gConnManState;
    std::cout << "Initializing ConnMan Client...\n";
    
    NetworkConfig netcfg = LoadNetworkConfig("network.config.txt");

    ConnMan::InitializeClient(gConnManState,
                              netcfg,
                              serveripv4,
                              serverport,
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
        ConnMan::UpdateClient(gConnManState, 1);
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
    case ConnManState::OnEventType::CONNECTION_STALE:
        // Connection has been idle for a while
        //
        if (!stale_seen){
            std::cout << "CONNECTION_STALE: " << pConn->playername << " : " << pConn->id << std::endl;
            stale_seen = true;
        }
        break;
    case ConnManState::OnEventType::ACK_TIMEOUT:
        // A reliable packet was not acknowledged.
        // pConn and packet are valid. (retry is possible)
        //
        std::cout << "ACK_TIMEOUT: " << pConn->playername << " : " << pConn->id << std::endl;
        break;
    case ConnManState::OnEventType::ACK_RECEIVED:
        // A reliable packet was acknowledged.
        //
        std::cout << "ACK_RECEIVED: " << pConn->playername << " : " << pConn->id << std::endl;
        break;
    case ConnManState::OnEventType::GRANTED:
        // Client-Side Event
        // Our attempt to connect to a game server was successful
        //
        std::cout << "GRANTED: " << pConn->playername << " : " << pConn->id << std::endl;
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
            InitializeConnection(&cmstate.localconn,
                                 gToServer,
                                 playerName);

            ConnMan::Connect(cmstate,
                             cmstate.localconn,
                             gameName,
                             gamePass);
        }
        else if (strinput == "r")
        {
            Connection& connection = cmstate.localconn;

            uint32_t index = 
            ConnMan::SendReliable(cmstate,
                                  connection,
                                  (uint8_t*)"I Love Bali Fern Sprout",
                                  24,
                                  nullptr);
            std::cout << "Pending\n";

            auto findresult = GetRequestStatus(connection, index);
            if (findresult != connection.reqstatus.end())
            {
                Connection::RequestStatus* rs = &findresult->second;
                while (rs->state != Connection::RequestStatus::State::SUCCEEDED &&
                       rs->state != Connection::RequestStatus::State::FAILED)
                {
                    Sleep(0);
                }

                if (rs->state == Connection::RequestStatus::State::SUCCEEDED)
                {
                    duration ping = rs->endtime - rs->starttime;
                    std::cout << "Ack RTT: " << ping.count() << " ms\n";
                    RemoveRequestStatus(connection, index);
                }
                else if (rs->state == Connection::RequestStatus::State::FAILED)
                {
                    std::cout << "Ack Timed Out\n";
                    RemoveRequestStatus(connection, index);
                }
            }
            


        }
        else if (strinput == "u")
        {
            ConnMan::SendUnreliable(cmstate,
                                    cmstate.localconn,
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
