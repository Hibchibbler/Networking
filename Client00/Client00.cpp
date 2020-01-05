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
        std::cout << "CONNECTION_TIMEOUT: " << std::endl;
        break;
    case ConnManState::OnEventType::MESSAGE_ACK_TIMEOUT:{
        MESG* m = (MESG*)packet->buffer;
        std::cout << "MESSAGE_ACK_TIMEOUT: " << CodeName[m->header.code]<< std::endl;
        break;
    }case ConnManState::OnEventType::MESSAGE_ACK_RECEIVED:{
        MESG* m = (MESG*)packet->buffer;
        std::cout << "MESSAGE_ACK_RECEIVED: " << CodeName[m->header.code] << std::endl;
        break;
    }case ConnManState::OnEventType::CONNECTION_HANDSHAKE_GRANTED:
        std::cout << "CONNECTION_HANDSHAKE_GRANTED: " << std::endl;
        break;
    case ConnManState::OnEventType::CONNECTION_HANDSHAKE_DENIED:
        std::cout << "CONNECTION_HANDSHAKE_DENIED: " << std::endl;
        break;
    case ConnManState::OnEventType::CONNECTION_HANDSHAKE_TIMEOUT:
        std::cout << "CONNECTION_HANDSHAKE_TIMEOUT: " << std::endl;
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
            if (gLocalConn != nullptr)
            {
                cm.RemoveConnection(gMyUniqueId);
                gLocalConn.reset();
            }
            gLocalConn =
                cm.cmstate.CreateConnection(
                                gToServer,
                                playerName,
                                gMyUniqueId,
                                Connection::Locality::LOCAL,
                                Connection::State::IDENTIFYING);
            if (gLocalConn != nullptr)
            {
                gLocalConn->connectingresultpromise = std::make_shared<ConnectingResultPromise>();
                ConnectingResultFuture cmfuture = gLocalConn->connectingresultpromise->get_future();
                gLocalConn->curseq = gMyUniqueId;
                gLocalConn->curuseq = gMyUniqueId;

                cm.cmstate.AddConnection(gLocalConn);


                Packet packet = CreateIdentifyPacket(gLocalConn->who, gLocalConn->playername, gLocalConn->curuseq, gLocalConn->curack, gMyUniqueId, gameName, gamePass);
                cm.Write(packet);

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
        }
        else if (strinput == "r")
        {
            Connection::ConnectionPtr pConn = ConnMan::GetConnectionById(cm.cmstate.connectionsmutex, cm.cmstate.connections, gMyUniqueId);
            if (pConn != nullptr)
            {
                uint32_t curRetries = 0;
                bool sent = false;
                do
                {
                    uint32_t request_index;
                    RequestFuture barrier_future;

                    barrier_future =
                        cm.SendBuffer(pConn,
                                      ConnMan::SendType::WITHRECEIPT,
                                      (uint8_t*)"I Love All My Dogs",
                                      18,
                                      request_index);

                    RequestStatus::RequestResult result = barrier_future.get();
                    if (result == RequestStatus::RequestResult::ACKNOWLEDGED)
                    {
                        sent = true;
                    }
                    else if (result == RequestStatus::RequestResult::TIMEDOUT)
                    {
                        // Retry
                    }
                    pConn->RemoveRequestStatus(request_index);
                    curRetries++;
                } while (!sent && (curRetries < cm.cmstate.retry_count));

                if (sent)
                    std::cout << "SendReliable Success\n";
                else
                    std::cout << "SendReliable Failed\n";
            }
        } 
        else if (strinput == "u")
        {
            Connection::ConnectionPtr pConn = ConnMan::GetConnectionById(cm.cmstate.connectionsmutex, cm.cmstate.connections, gMyUniqueId);
            if (pConn != nullptr)
            {
                uint32_t request_index; // not used for unreliable
                cm.SendBuffer(pConn,
                              ConnMan::SendType::WITHOUTRECEIPT,
                              (uint8_t*)"I Love All My Dogs",
                              18,
                              request_index);
            }
        }
        else if (strinput == "rtt")
        {
            Connection::ConnectionPtr pConn = ConnMan::GetConnectionById(cm.cmstate.connectionsmutex, cm.cmstate.connections, gMyUniqueId);
            if (pConn != nullptr)
            {
                std::cout << "Connection: " << pConn->id << std::endl;
                std::cout << "     State: " << (uint32_t)pConn->state << std::endl;
                std::cout << "  Fidelity: " << 100.f*((float)pConn->totalpongs / (float)pConn->totalpings) << std::endl;
                std::cout << "     Pings: " << pConn->totalpings << std::endl;
                std::cout << "  Cur Ping: " << pConn->curping << std::endl;
                std::cout << "       Avg: " << pConn->avgping << std::endl;
            }
        }
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
