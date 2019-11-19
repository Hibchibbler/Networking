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

uint32_t requiredNumberOfPlayers = 2;
std::string playerName = "JackB";
std::string gameName = "Bali";
std::string gamePass = "Bear";
uint32_t thisport = 51001;
uint32_t serverport = 51002;

std::string serveripv4 = "10.0.0.93";

volatile bool gNetworkInitialized = false;
volatile bool gAbort = false;
Thread gKeyboardThread;
Thread gStateMonitorThread;
Thread gNetworkThread;

Address gToServer;

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
        if (strinput == "p")
        {
            ConnMan::sendPingTo(cmstate,
                gToServer,
                cmstate.connections.front().id);
            std::cout << "Sent Ping\n";
        }
        else if (strinput == "sp")
        {
            float acc = 0;
            float cnt = 0;
            for (auto v : cmstate.connections.front().pingtimes)
            {
                acc += v.count();
                cnt++;
            }
            std::cout << "Avg Ping: " << acc / cnt << "\n";
        }
        else
        {
            std::cout << "What?\n";
        }
    }

}

void
StateMonitorFunction(
    void* p
)
{
    ConnManState& cmstate = *((ConnManState*)p);

    //
    // Wait for required number of players to Ready Up
    //
    while (!gAbort && ConnMan::readyCount(cmstate) < requiredNumberOfPlayers)
    {Sleep(0);}

    //
    // Since all players are ready, broadcast a Start
    //
    ConnMan::sendStart(cmstate);

    //
    // Ok, everyone should be Started.
    // Normal Game Packets should be all we see
    //
    while (!gAbort)
    {
        Sleep(0);
        for (auto & c : cmstate.connections)
        {
        }
    }
}
struct OnEventContext
{

};
void
NetworkFunction(
    void* p
)
{
    OnEventContext oec;
    ConnManState& cmstate = *((ConnManState*)p);
    std::cout << "NetworkFunction engaged.\n";
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
        Sleep(0);
        ConnMan::updateServer(cmstate, 1);
    }

    ConnMan::cleanup(cmstate);
    std::cout << "NetworkFunction disengaged.\n";
}

void
OnEvent(
    void* oecontext,
    ConnManState::OnEventType t,
    Connection* conn,
    Packet* packet
)
{
    std::cout << "Event Received.\n";
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

