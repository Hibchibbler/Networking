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
    void* p
);

void
OnUpdate(
    void* p
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
            Connection::State & state = cmstate.connections[0].state;
            if (state == Connection::State::WAITFORREADY)
            {
                ConnMan::sendReadyTo(cmstate,
                                     gToServer,
                                     cmstate.connections[0].id);
                std::cout << "Sent ready\n";
            }
        }
        else if (strinput == "u")
        {
            UPDATE update;
            update.blargh = rand() % 65536;
            ConnMan::sendUpdateTo(cmstate,
                                  gToServer,
                                  cmstate.connections[0].id,
                                  update);
            std::cout << "Sent Update\n";
        }
        else if (strinput == "p")
        {
            ConnMan::sendPingTo(cmstate,
                                gToServer,
                                cmstate.connections[0].id);
            std::cout << "Sent Ping\n";
        }
        else if (strinput == "sp")
        {
            float acc = 0;
            float cnt = 0;
            for (auto v : cmstate.connections[0].pingtimes)
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
    
    while (!gAbort)
    {
        for (auto & c : cmstate.connections)
        {
            if (c.state == Connection::State::GRANTED)
            {
                // Heck yea
                c.state = Connection::State::WAITFORREADY;
                std::cout << "GRANTED!\n";
            }
            else if (c.state == Connection::State::DENIED)
            {
                // Yikes
                std::cout << "DENIED!\n";
                cmstate.connections.clear();
            }
        }
        Sleep(0);
    }
}

void
NetworkFunction(
    void* p
)
{
    ConnManState& cmstate = *((ConnManState*)p);
    std::cout << "Initializing ConnMan...\n";
    ConnMan::initialize(cmstate,
        thisport,
        requiredNumberOfPlayers,
        gameName,
        gamePass,
        OnEvent,
        OnUpdate);

    gNetworkInitialized = true;

    while (!gAbort)
    {
        ConnMan::updateServer(cmstate, 1);
        Sleep(0);
    }

    ConnMan::cleanup(cmstate);
}

void
OnEvent(
    void* p
)
{
    std::cout << "Event Noted\n";
}

void
OnUpdate(
    void* p
)
{
    std::cout << "Update Noted\n";
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