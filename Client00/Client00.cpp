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
std::string playerName = "Jack";
std::string gameName = "Bali";
std::string gamePass = "Bear";
uint32_t port = 51002;

volatile bool gAbort = false;
Thread gKeyboardThread;
Thread gStateMonitorThread;
Address gToServer;

BOOL WINAPI HandlerRoutine(_In_ DWORD dwCtrlType) {
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
        std::cout << ">";

        std::cin >> input;
        strinput = std::string(input, strlen(input));
        if (strinput == "identify")
        {
            ConnMan::sendIdentifyTo(cmstate,
                                    gToServer,
                                    playerName,
                                    gameName,
                                    gamePass);
            std::cout << "Sent Identify\n";
        }
        else if (strinput == "ready")
        {
            ConnMan::sendReadyTo(cmstate,
                                 gToServer,
                                 cmstate.connections[0].id);
            std::cout << "Sent ready\n";
        }
        else if (strinput == "update")
        {
            UPDATE update;
            update.blargh = rand() % 65536;
            ConnMan::sendUpdateTo(cmstate,
                                 gToServer,
                                 cmstate.connections[0].id,
                                 update);
            std::cout << "Sent Update\n";
        }
        else if (strinput == "ping")
        {
            ConnMan::sendPingTo(cmstate,
                                gToServer,
                                cmstate.connections[0].id);
            std::cout << "Sent Ping\n";
        }
        else if (strinput == "showping")
        {
            float acc= 0;
            float cnt = 0;
            for (auto v : cmstate.connections[0].pingtimes)
            {
                acc += v.count();
                cnt++;
            }
            std::cout << "Avg Ping: " << acc / cnt << "\n";
            /*for (size_t i = 0; i < cmstate.connections[0].pingtimes.size(); i++)
            {
                std::cout << "Ping: " << cmstate.connections[0].pingtimes[i].count() << "\n";
            }*/
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
    
    while (!gAbort)
    {

    }
}

int main(int argc, char** argv)
{
    ConnManState cmstate;
    uint32_t elapsed = 0;

    //
    //
    //
    if (argc > 2)
    {
        port = std::atol(argv[1]);
        playerName = std::string(argv[2], strlen(argv[2]));
    }

    std::cout << "Selecting Player: " << playerName << "\n";
    std::cout << "Selecting port: " << port << "\n";

    std::cout << "Initializing ConnMan...\n";
    ConnMan::initialize(cmstate,
                        port,
                        requiredNumberOfPlayers,
                        gameName,
                        gamePass);

    //
    // Handle Ctrl-C 
    // Ctrl-C sets Abort = true
    //
    SetConsoleCtrlHandler(HandlerRoutine, TRUE);

    //
    // Create an Address to Server
    //
    gToServer.addr.ss_family = AF_INET;
    ((sockaddr_in*)&gToServer.addr)->sin_port = htons(51001);
    ((sockaddr_in*)&gToServer.addr)->sin_addr.S_un.S_addr = inet_addr("10.0.0.93");


    gKeyboardThread.handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)KeyboardFunction, &cmstate, 0, &gKeyboardThread.id);
    gStateMonitorThread.handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)StateMonitorFunction, &cmstate, 0, &gStateMonitorThread.id);

    while (!gAbort)
    {
        ConnMan::updateServer(cmstate, 120);
        //Sleep(10);
    }

    WaitForSingleObject(gKeyboardThread.handle, INFINITE);
    WaitForSingleObject(gStateMonitorThread.handle, INFINITE);

    while (!gAbort) {}
    std::cout << "Cleaning up...\n";
    ConnMan::cleanup(cmstate);
    return 0;
}

