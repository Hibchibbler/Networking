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

using namespace bali;

volatile bool Abort = false;

BOOL WINAPI HandlerRoutine(_In_ DWORD dwCtrlType) {
    switch (dwCtrlType)
    {
    case CTRL_C_EVENT:
        std::cout << "Aborting...\n";
        Abort = true;
        // Signal is handled - don't pass it on to the next handler
        return TRUE;
    default:
        // Pass signal on to the next handler
        return FALSE;
    }
}


int main()
{
    ConnManState cmstate;
    uint32_t elapsed = 0;
    uint32_t requiredNumberOfPlayers = 2;
    std::string gameName = "Bali";
    std::string gamePass = "Bear";

    //
    // Handle Ctrl-C 
    // Ctrl-C sets Abort = true
    //
    SetConsoleCtrlHandler(HandlerRoutine, TRUE);

    std::cout << "Starting up...\n";
    ConnMan::initialize(cmstate,
                        51002,
                        requiredNumberOfPlayers,
                        gameName,
                        gamePass);

    Sleep(5000);
    Address to;
    to.addr.ss_family = AF_INET;
    ((sockaddr_in*)&to.addr)->sin_port = htons(51001);
    ((sockaddr_in*)&to.addr)->sin_addr.S_un.S_addr = inet_addr("10.54.60.102");
    
    ConnMan::sendIdentifyTo(cmstate,
                            to,
                            "Jack",
                            "Bali",
                            "Bear");
    std::cout << "Sent Identify\n";

    while (!Abort && cmstate.connections[0].state == Connection::State::WAITFORGRANTDENY){}
    std::cout << "Recieved Grant/Deny\n";

    ConnMan::sendReadyTo(cmstate,
                         to,
                         cmstate.connections[0].id);
    std::cout << "Sent Ready\n";
    while (!Abort && cmstate.connections[0].state == Connection::State::WAITFORSTART) {}
    std::cout << "Received Start\n";
    while (!Abort) {}
    std::cout << "Cleaning up...\n";
    ConnMan::cleanup(cmstate);
    return 0;
}

