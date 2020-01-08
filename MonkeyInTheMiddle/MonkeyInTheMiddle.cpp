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
#include <deque>
#include <chrono>
#include <string>
#include <chrono>
#include <random>
#include <windows.h>
using namespace bali;

struct SharedContext
{
    ConnMan* pThisConnMan;
    ConnMan* pThatConnMan;
};

bool gAbort = false;
Thread gInputThread;

ConnMan gThisConnMan;
ConnMan gThatConnMan;

uint32_t requiredNumberOfPlayers = 3;
std::string playerName = "JackA";
std::string gameName = "Bali";
std::string gamePass = "Bear";
uint32_t thisport = 51002;
std::string thisipv4 = "10.0.0.93";
uint32_t thatport = 51003;
std::string thatipv4 = "10.0.0.93";

Address gThisAddress;
Address gThatAddress;
SharedContext gSharedContext;

Mutex amutex;
Mutex bmutex;
std::queue<Packet> gAPackets; // from this to that
Address gAAddress;
Address gBAddress;
std::queue<Packet> gBPackets; // from that to this

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
OnEventThis(
    void* oecontext,
    ConnManState::OnEventType t,
    Connection* pConn,
    Packet* packet
);

void
OnEventThat(
    void* oecontext,
    ConnManState::OnEventType t,
    Connection* pConn,
    Packet* packet
);

std::deque<Packet> gADetoured;
std::deque<Packet> gBDetoured;

int main(int argc, char** argv)
{

    std::random_device rd;     // only used once to initialise (seed) engine
    std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)
    std::uniform_int_distribution<uint32_t> uni(1, 1000); // guaranteed unbiased

    auto random_integer = uni(rng);
    //
    // Handle Ctrl-C 
    // Ctrl-C sets Abort = true
    //
    SetConsoleCtrlHandler(SignalFunction, TRUE);

    amutex.create();
    bmutex.create();

    //
    // Argument handling
    //
    if (argc == 4)
    {
        thisport = std::atol(argv[1]);
        thatport = std::atol(argv[2]);
        thatipv4 = std::string(argv[3]);
    }
    else
    {
        std::cout << "Usage:\n\t" << argv[0] << " <thisport> <thatport> <thatipv4>\n";
        return 0;
    }

    std::cout << "This port: " << thisport << "\n";
    std::cout << "This ipv4: " << thatipv4 << "\n";// weirdos
    std::cout << "That port: " << thatport << "\n";
    std::cout << "That ipv4: " << thatipv4 << "\n";

    //
    // Initialize ConnMan Server
    //
    gSharedContext.pThisConnMan = &gThisConnMan;
    gSharedContext.pThatConnMan = &gThatConnMan;

    std::cout << "Initializing ConnMan Relay...\n";

    NetworkConfig netcfg = LoadNetworkConfig("network.config.txt");

    gThisConnMan.Initialize(netcfg,
                        ConnManState::ConnManType::PASS_THROUGH,
                        thisport,
                        OnEventThis,
                        &gSharedContext);

    gThatConnMan.Initialize(netcfg,
                        ConnManState::ConnManType::PASS_THROUGH,
                        thisport + 1,
                        OnEventThat,
                        &gSharedContext);
    gThisAddress = CreateAddress(thisport, thatipv4.c_str());//we're just pretending it's all on same ip
    gThatAddress = CreateAddress(thatport, thatipv4.c_str());
    Sleep(60);

    //
    // Start service threads
    //
    gInputThread.handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)InputFunction, &gSharedContext, 0, &gInputThread.id);

    //
    // Pump the pump
    //
    while (!gAbort)
    {
        gThisConnMan.Update(1);
        gThatConnMan.Update(1);

        // From This to That
        amutex.lock();
        if (!gAPackets.empty())
        {
            Packet p = gAPackets.front();
            gAPackets.pop();

            random_integer = uni(rng);
            if (random_integer >= 0 && random_integer < 500)
            { 
                p.address = gThatAddress;
                gThatConnMan.Write(p);
            }
            else
            {
                //random_integer = uni(rng);
                //if (random_integer >= 0 && random_integer < 500)
                {
                    std::cout << "Detoured !%$#@%$#\n";
                    gADetoured.push_back(p);
                }
            }
        }

        random_integer = uni(rng);
        if (random_integer >= 0 && random_integer < 10)
        {
            //while(gADetoured.size() > 3)
            if (gADetoured.size() > 0)
            {
                std::cout << "Resume !%$#@%$#\n";
                //std::random_shuffle(gADetoured.begin(), gADetoured.end());
                //gADetoured.front().address = gThatAddress;
                Packet p = gADetoured.front();
                gADetoured.pop_front();
                p.address = gThatAddress;
                gThatConnMan.Write(p);

            }
        }
        amutex.unlock();

        // From That to This
        bmutex.lock();
        if (!gBPackets.empty())
        {
            Packet p = gBPackets.front();
            gBPackets.pop();

            random_integer = uni(rng);
            if (random_integer > 0)
            {
                p.address = gAAddress;
                gThisConnMan.Write(p);
            }
            else
            {
                std::cout << "That !%$#@%$#\n";
            }
        }
        bmutex.unlock();

        Sleep(10);
    }

    //
    // Wait for all service threads to stop
    //
    WaitForSingleObject(gInputThread.handle, INFINITE);

    gThisConnMan.Cleanup();
    gThatConnMan.Cleanup();
    return 0;
}

void
OnEventThis(
    void* context,
    ConnManState::OnEventType t,
    Connection* pConn,
    Packet* packet
)
{
    static bool timeout_seen = false;
    SharedContext* pSharedContext = (SharedContext*)context;
    ConnMan& cm = *pSharedContext->pThisConnMan;

    switch (t)
    {
    case ConnManState::OnEventType::MESSAGE_RECEIVED:
        std::cout << "MESSAGE_RECIEVED" << std::endl;

        gAAddress = packet->address;
        amutex.lock();
        gAPackets.push(*packet);
        amutex.unlock(); 
        break;
    }
}
void
OnEventThat(
    void* context,
    ConnManState::OnEventType t,
    Connection* pConn,
    Packet* packet
)
{
    SharedContext* pSharedContext = (SharedContext*)context;
    ConnMan& cm = *pSharedContext->pThisConnMan;

    switch (t)
    {
    case ConnManState::OnEventType::MESSAGE_RECEIVED:
        std::cout << "MESSAGE_RECIEVED" << std::endl;

        gBAddress = packet->address;
        bmutex.lock();
        gBPackets.push(*packet);
        bmutex.unlock();
        break;
    }
}
void
InputFunction(
    void* context
)
{
    SharedContext* pSharedContext = (SharedContext*)context;
    ConnMan& cm = *pSharedContext->pThatConnMan;
    std::string strinput;
    char input[64];

    while (!gAbort)
    {
        std::cin >> input;
        strinput = std::string(input, strlen(input));
        if (strinput == "p")
        {

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
