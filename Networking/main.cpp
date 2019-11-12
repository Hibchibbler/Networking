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

volatile bool isRunning = true;

BOOL WINAPI HandlerRoutine(_In_ DWORD dwCtrlType) {
    switch (dwCtrlType)
    {
    case CTRL_C_EVENT:
        std::cout << "[Ctrl]+C\n";
        isRunning = false;
        // Signal is handled - don't pass it on to the next handler
        return TRUE;
    default:
        // Pass signal on to the next handler
        return FALSE;
    }
}




struct OnClientContext
{

};

struct OnServerContext
{

};

void
OnClientEnter(
    OnClientContext* context
)
{

}

void
OnClientLeave(
    OnClientContext* context
)
{

}

void
OnClientUpdate(
    OnClientContext* context
)
{

}

void
OnServerUpdate(
    OnServerContext* context
)
{

}


int main()
{
    bool done = false;
    ConnManState cmstate;
    OnClientContext ccontext;
    OnServerContext scontext;
    //ZeroMemory(&cmstate, sizeof(cmstate)); // <---- this little bitch

    SetConsoleCtrlHandler(HandlerRoutine, TRUE);

    uint32_t elapsed = 0;
    uint32_t numPlayers = 2;
    std::string gameName = "Bali";
    std::string gamePass = "Bear";



    ConnMan::initialize(cmstate,
                        (ConnManState::OnClientEnter)OnClientEnter, &ccontext,
                        (ConnManState::OnClientLeave)OnClientLeave, &ccontext,
                        (ConnManState::OnClientUpdate)OnClientUpdate, &ccontext,
                        (ConnManState::OnServerUpdate)OnServerUpdate, &scontext,
                        numPlayers,
                        gameName,
                        gamePass);

    do
    {
        Sleep(30);
        ConnMan::updateServer(cmstate, 30);

    } while (ConnMan::readyCount(cmstate) < numPlayers);

    ConnMan::sendStart(cmstate);

    do
    {
        Sleep(30);
        ConnMan::updateServer(cmstate, 30);
    } while (!done);

    //isRunning = true; std::cout << "Sending Start\n";

    //do
    //{
    //    Sleep(30);
    //    ConnMan::updateServer(cmstate, 30);
    //} while (isRunning);

    std::cout << "Cleaning up...\n";
    ConnMan::cleanup(cmstate);


    return 0;
}

