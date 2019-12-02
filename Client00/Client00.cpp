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
    
    ConnMan::initialize(gConnManState,
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
    OnEventContext* oec = (OnEventContext*)oecontext;
    ConnManState& cmstate = *oec->pConnManState;
    std::string playername;
    switch (t)
    {
    case ConnManState::OnEventType::MESSAGE:
        //std::cout << "Message Received." << std::endl;
        break;
    case ConnManState::OnEventType::CONNECTION_ADD:
        std::cout << "Add Connection: " << pConn->playername << std::endl;
        break;
    case ConnManState::OnEventType::CONNECTION_REMOVE:
        std::cout << "Remove Connection: " << pConn->playername << std::endl;
        break;
    case ConnManState::OnEventType::STALECONNECTION:
        std::cout << "Stale Connection: " << pConn->playername << std::endl;
        pConn->checkintime = clock::now();//hack
        ConnMan::SendReliable(cmstate, pConn->id, (uint8_t*)"BLAh", 4, nullptr);
        break;
    case ConnManState::OnEventType::ACK_TIMEOUT:
        // i'm going to need the packet that timedout.
        std::cout << "Ack Timeout" << std::endl;
        pConn->state = Connection::State::IDLE; // This is to let the next reliable message in the reliable queue to go through.
        break;
    case ConnManState::OnEventType::ACK_RECEIVED:
        std::cout << "Ack Received" << std::endl;
        pConn->state = Connection::State::IDLE; // This is to let the next reliable message in the reliable queue to go through.
        break;
    case ConnManState::OnEventType::GRANTED:
        //
        // Client-Side Event
        // Our attempt to connect to a game server was successful
        //
        std::cout << "Authorization Granted: " << ((MESG*)packet->buffer)->header.id << "\n";
        break;
    case ConnManState::OnEventType::DENIED:
        //
        // Client-Side Event
        // Our attempt to connect to a game server was unsuccessful
        //
        std::cout << "Authorization Denied\n";
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
            std::cout << "Pushing Identify\n";
        }
        else if (strinput == "r")
        {

        }
        else if (strinput == "u")
        {

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

//using namespace bali;
//
//uint32_t requiredNumberOfPlayers = 3;
//std::string playerName = "JackA";
//std::string gameName = "Bali";
//std::string gamePass = "Bear";
//uint32_t thisport = 51002;
//uint32_t serverport = 51001;
//std::string serveripv4 = "10.0.0.93";
//
//volatile bool gAbort = false;
//Thread gInputThread;
//Thread gStateMonitorThread;
//
//Address gToServer;
//
//struct OnEventContext
//{
//    ConnManState* pConnManState;
//    uint32_t      currentReady;
//    uint32_t      requiredReady;
//};
//
//
//BOOL
//WINAPI
//SignalFunction(
//    _In_ DWORD dwCtrlType
//);
//
//void
//InputFunction(
//    void* p
//);
//
//void
//StateMonitorFunction(
//    void* p
//);
//
//void
//OnEvent(
//    void* oecontext,
//    ConnManState::OnEventType t,
//    Connection* conn,
//    Packet* packet
//);
//
//
//
//
//int main(int argc, char** argv)
//{
//    ConnManState cmstate;
//    OnEventContext oec;
//    uint32_t elapsed = 0;
//
//    //
//    // Handle Ctrl-C 
//    // Ctrl-C sets Abort = true
//    //
//    SetConsoleCtrlHandler(SignalFunction, TRUE);
//
//
//    //
//    // Argument handling
//    //
//    if (argc > 4)
//    {
//        thisport = std::atol(argv[1]);
//        serverport = std::atol(argv[2]);
//        serveripv4 = std::string(argv[3], strlen(argv[3]));
//        playerName = std::string(argv[4], strlen(argv[4]));
//    }
//    else
//    {
//        std::cout << "Usage:\n\t" << argv[0] << " <thisport> <serverport> <serveripv4> <playername>\n";
//        return 0;
//    }
//
//    std::cout << "Selecting Player: " << playerName << "\n";
//    std::cout << "Selecting port: " << thisport << "\n";
//    std::cout << "Server port: " << serverport << "\n";
//    std::cout << "Server ipv4: " << serveripv4 << "\n";
//
//    //
//    // Initialize ConnMan
//    //
//    oec.pConnManState = &cmstate;
//    oec.currentReady = 0;
//    oec.requiredReady = requiredNumberOfPlayers;
//    std::cout << "Initializing ConnMan...\n";
//
//    ConnMan::initialize(cmstate,
//                        thisport,
//                        requiredNumberOfPlayers,
//                        gameName,
//                        gamePass,
//                        OnEvent,
//                        &oec);
//
//    Sleep(60);
//
//    //
//    // Create an Address to Server
//    //
//    gToServer = CreateAddress(serverport, serveripv4.c_str());
//
//    //
//    // Start service threads
//    //
//    gInputThread.handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)InputFunction, &cmstate, 0, &gInputThread.id);
//    gStateMonitorThread.handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)StateMonitorFunction, &cmstate, 0, &gStateMonitorThread.id);
//
//    //
//    // Pump the pump
//    //
//    while (!gAbort)
//    {
//        ConnMan::updateServer(cmstate, 1);
//        Sleep(0);
//    }
//
//    //
//    // Wait for all service threads to stop
//    //
//    WaitForSingleObject(gInputThread.handle, INFINITE);
//    WaitForSingleObject(gStateMonitorThread.handle, INFINITE);
//
//    //
//    // Clean up ConnMan
//    //
//    ConnMan::cleanup(cmstate);
//    std::cout << "Exiting...\n";
//    return 0;
//}
//
//void
//InputFunction(
//    void* p
//)
//{
//    std::string strinput;
//    char input[64];
//    ConnManState& cmstate = *((ConnManState*)p);
//
//    while (!gAbort)
//    {
//        Sleep(0);
//
//        std::cin >> input;
//        strinput = std::string(input, strlen(input));
//        if (strinput == "i")
//        {
//            ConnMan::sendIdentifyTo(cmstate,
//                                    gToServer,
//                                    playerName,
//                                    gameName,
//                                    gamePass);
//            std::cout << "Tx Identify\n";
//        }
//        else if (strinput == "r")
//        {
//            SendReadyMesg(cmstate,
//                          gToServer,
//                          cmstate.localconn.id);
//            std::cout << "Tx ready\n";
//        }
//        else if (strinput == "u")
//        {
//            SendUpdateMesg(cmstate,
//                           gToServer,
//                           cmstate.localconn.id,
//                           0,
//                           0);
//            std::cout << "Tx Update\n";
//        }
//        else if (strinput == "p")
//        {
//            SendPingMesg(cmstate,
//                         gToServer,
//                         cmstate.localconn.id);
//            std::cout << "Tx Ping\n";
//        }
//        else
//        {
//            std::cout << "What?\n";
//        }
//    }
//
//}
//
//void
//StateMonitorFunction(
//    void* p
//)
//{
//    ConnManState& cmstate = *((ConnManState*)p);
//
//    while (!gAbort)
//    {
//
//        Sleep(0);
//    }
//}
//
//void
//OnEvent(
//    void* oecontext,
//    ConnManState::OnEventType t,
//    Connection* pConn,
//    Packet* packet
//)
//{
//    OnEventContext* oec = (OnEventContext*)oecontext;
//    ConnManState& cmstate = *oec->pConnManState;
//    std::string playername;
//    switch (t)
//    {
//    case ConnManState::OnEventType::CLIENTENTER:
//        //
//        // Server-Side Event
//        // This is where we'd "add" a new player to the game.
//        //
//        std::cout << "Client Arrived: " << ((MESG*)packet->buffer)->header.id << "\n";
//        break;
//    case ConnManState::OnEventType::CLIENTLEAVE:
//        //
//        // Server-Side Event
//        // This is where we'd "remove" a player from the game.
//        //
//        std::cout << "Client Left\n";
//        break;
//    case ConnManState::OnEventType::GRANTED:
//        //
//        // Client-Side Event
//        // Our attempt to connect to a game server was successful
//        //
//        std::cout << "Authorization Granted: " << ((MESG*)packet->buffer)->header.id << "\n";
//        break;
//    case ConnManState::OnEventType::DENIED:
//        //
//        // Client-Side Event
//        // Our attempt to connect to a game server was unsuccessful
//        //
//        std::cout << "Authorization Denied\n";
//        break;
//    case ConnManState::OnEventType::STALECONNECTION:
//        std::cout << "haven't seen traffic from " << pConn->playername << " in a while.\n";
//        break;
//    case ConnManState::OnEventType::MESSAGE:
//        MESG* Mesg = GetMesg(*packet);
//        GameMesg* gameMsg = GetGameMesg(Mesg);
//
//        if (gameMsg->code == (uint32_t)GameMesg::Codes::READY)
//        {
//            // Server-Side Event
//            std::cout << "Rx Ready\n";
//            oec->currentReady++;
//            if (oec->currentReady >= oec->requiredReady)
//            {
//                std::cout << "Tx Start\n";
//                SendStartMesgToAll(cmstate);
//            }
//        }
//        else if (gameMsg->code == (uint32_t)GameMesg::Codes::START)
//        {
//            // Client-Side Event
//            std::cout << "Rx Start\n";
//        }
//        else if (gameMsg->code == (uint32_t)GameMesg::Codes::PING)
//        {
//            std::cout << "Rx Ping\n";
//            SendPongMesg(cmstate,
//                         packet->address,
//                         GetConnectionId(*packet));
//        }
//        else if (gameMsg->code == (uint32_t)GameMesg::Codes::PONG)
//        {
//            //
//            // If we are recieving a PONG, then we already sent a PING.
//            // Therefore, calculate the time delta, and print it to console.
//            //
//            std::cout << "Rx Pong\n";
//            cmstate.localconn.endtime = clock::now();
//            duration dur = cmstate.localconn.endtime - cmstate.localconn.starttime;
//            std::cout << "PingPong: " << dur.count() << " ms\n";
//        }
//        else if (gameMsg->code == (uint32_t)GameMesg::Codes::UPDATE)
//        {
//            // Server and Client Event
//            std::cout << "Rx Update\n";
//        }
//
//        break;
//    }
//}
//
//BOOL
//WINAPI
//SignalFunction(
//    _In_ DWORD dwCtrlType
//)
//{
//    switch (dwCtrlType)
//    {
//    case CTRL_C_EVENT:
//        std::cout << "Aborting...\n";
//        gAbort = true;
//        // Signal is handled - don't pass it on to the next handler
//        return TRUE;
//    default:
//        // Pass signal on to the next handler
//        return FALSE;
//    }
//}