/////////////////////////////////////////////////////////////////////////////////
//// Daniel J Ferguson
//// 2019
/////////////////////////////////////////////////////////////////////////////////

#include <iostream>
#include <conio.h>

#ifndef UNICODE
#define UNICODE
#endif


#include "ConnMan.h"
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
                        51001,
                        requiredNumberOfPlayers,
                        gameName,
                        gamePass);

    //
    // Wait for required number of players to Ready Up
    //
    while (!Abort && ConnMan::readyCount(cmstate) < requiredNumberOfPlayers)
    {
        Sleep(30);
        ConnMan::updateServer(cmstate, 30);
    } 

    //
    // Since all players are ready, broadcast a Start
    //
    if (!Abort)
    {
        ConnMan::sendStart(cmstate);

    }
    //
    // Ok, everyone should be Started.
    // Normal Game Packets should be all we see
    //
    while (!Abort)
    {
        Sleep(30);
        ConnMan::updateServer(cmstate, 30);

        for (auto & c : cmstate.connections)
        {
            
        }
    }

    std::cout << "Cleaning up...\n";
    ConnMan::cleanup(cmstate);
    return 0;
}

