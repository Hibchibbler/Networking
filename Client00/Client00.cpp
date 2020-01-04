/////////////////////////////////////////////////////////////////////////////////
//// Daniel J Ferguson
//// 2019
/////////////////////////////////////////////////////////////////////////////////
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
#include <conio.h>

#include "Networking\ConnMan.h"
#include "Networking\ConnManUtils.h"
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
Connection::ConnectionPtr gLocalConn;

uint32_t requiredNumberOfPlayers = 3;
std::string playerName = "JackA";
std::string gameName = "Bali";
std::string gamePass = "Bear";
uint32_t thisport = 51002;
uint32_t serverport = 51001;
std::string serveripv4 = "10.0.0.93";
uint32_t gMyUniqueId= 32; // I Sure hope no one else picks this! :)! lolz
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

    gMyUniqueId = uni(rng);
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

    gConnMan.Initialize(netcfg,
                        ConnManState::ConnManType::CLIENT,
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
        gConnMan.Update(1);
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
        std::cout << "CONNECTION_TIMEOUT: " << std::endl; //<< pConn->playername << " : " << pConn->id << std::endl;
        break;
    case ConnManState::OnEventType::MESSAGE_ACK_TIMEOUT:
        std::cout << "MESSAGE_ACK_TIMEOUT: " << std::endl;
                  //<< pConn->playername << " : " << pConn->id << ": "
                  //<< ((MESG*)packet->buffer)->header.seq << std::endl;
        break;
    case ConnManState::OnEventType::MESSAGE_ACK_RECEIVED:{
        //MESG* p = (MESG*)packet->buffer;
        std::cout << "MESSAGE_ACK_RECEIVED: " << std::endl; 
                  //<< pConn->playername << " : " << pConn->id << ": "
                  //<< p->header.seq<< std::endl;
        break;
    }case ConnManState::OnEventType::CONNECTION_HANDSHAKE_GRANTED:
        std::cout << "CONNECTION_HANDSHAKE_GRANTED: " << std::endl; //<< pConn->playername << " : " << pConn->id << std::endl;
        break;
    case ConnManState::OnEventType::CONNECTION_HANDSHAKE_DENIED:
        std::cout << "CONNECTION_HANDSHAKE_DENIED: " << std::endl; //<< pConn->playername << " : " << pConn->id << std::endl;
        break;
    case ConnManState::OnEventType::CONNECTION_HANDSHAKE_TIMEOUT:
        std::cout << "CONNECTION_HANDSHAKE_TIMEOUT: " << std::endl; //<< pConn->playername << " : " << pConn->id << std::endl;
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

            //
            // Each connection we make to a server must present 
            // a unique Id. we won't know if it is unique.
            // but, hopefully we don't collide.
            //
            gMyUniqueId = uni(rng);
        }
        if (strinput == "i")
        {
            //
            // Connecting is important, so
            // we'll perform this operation
            // synchronously.
            gLocalConn.reset();
            gLocalConn =
                cm.cmstate.CreateConnection(
                                gToServer,
                                playerName,
                                gMyUniqueId,
                                Connection::Locality::LOCAL,
                                Connection::State::IDENTIFYING);
            cm.cmstate.AddConnection(gLocalConn);
            gLocalConn->connectingresultpromise = std::make_shared<ConnectingResultPromise>();
            ConnectingResultFuture cmfuture = gLocalConn->connectingresultpromise->get_future();

            Packet packet = CreateIdentifyPacket(gLocalConn->who, gLocalConn->playername, gLocalConn->curuseq, gMyUniqueId, gameName, gamePass);
            Network::write(cm.cmstate.netstate,packet);

            Connection::ConnectingResult cr = cmfuture.get();
            
            if (cr == Connection::ConnectingResult::GRANTED)
            {
                std::cout << "Connection: Granted\n";
            }
            else if (cr == Connection::ConnectingResult::DENIED)
            {
                std::cout << "Connection: Denied\n";
            }
            else if (cr == Connection::ConnectingResult::TIMEDOUT)
            {
                std::cout << "Connection: Timeout\n";
            }
        }
        else if (strinput == "r")
        {
            Connection::ConnectionPtr pConn = ConnMan::GetConnectionById(cm.cmstate.connectionsmutex, cm.cmstate.connections, gMyUniqueId);
            if (cm.cmstate.connections.size() == 0 || pConn->state == Connection::State::IDENTIFYING)
            {
                std::cout << "Bad Idea, there ain't any registered connections, or still Identifying\n";
                continue;
            }
            uint32_t curRetries = 0;
            bool sent = false;
            do
            {
                uint32_t request_index;
                RequestFuture barrier_future;

                barrier_future =
                    cm.SendBuffer(cm.cmstate.netstate,
                        pConn,
                        ConnMan::SendType::WITHRECEIPT,
                        (uint8_t*)"I Love All My Dogs",
                        18,
                        request_index);
                RequestStatus::RequestResult result = 
                        barrier_future.get();
                if (result == RequestStatus::RequestResult::ACKNOWLEDGED)
                {
                    sent = true;
                }
                else if (result == RequestStatus::RequestResult::TIMEDOUT)
                {
                    std::cout << "Retrying...\n";
                }
                pConn->RemoveRequestStatus(request_index);
                curRetries++;
            } while (!sent && (curRetries < cm.cmstate.retry_count));

            if (sent)
                std::cout << "SendReliable Success\n";
            else
                std::cout << "SendReliable Failed\n";
        } 
        else if (strinput == "u")
        {
            Connection::ConnectionPtr pConn = ConnMan::GetConnectionById(cm.cmstate.connectionsmutex, cm.cmstate.connections, gMyUniqueId);
            uint32_t request_index; // not used for unreliable
            cm.SendBuffer(cm.cmstate.netstate,
                          pConn,
                          ConnMan::SendType::WITHOUTRECEIPT,
                          (uint8_t*)"I Love All My Dogs",
                          18,
                          request_index);
        }
        else if (strinput == "p")
        {// Just sends a ping.
         // when a pong is eventually receieved
         // the rtt counters, etc, will be updated
         // and we can query them. we don't want to
         // wait around for the pong.
            Connection::ConnectionPtr pConn = ConnMan::GetConnectionById(cm.cmstate.connectionsmutex, cm.cmstate.connections, gMyUniqueId);
            Packet packet = CreatePingPacket(pConn->who, pConn->id, pConn->curuseq);
            Network::write(cm.cmstate.netstate, packet);
        }
        else if (strinput == "rtt")
        {
            Connection::ConnectionPtr pConn = ConnMan::GetConnectionById(cm.cmstate.connectionsmutex, cm.cmstate.connections, gMyUniqueId);
            std::cout << "Ping: " << pConn->curping << std::endl;
            std::cout << "Avg: " << pConn->avgping << std::endl;
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
