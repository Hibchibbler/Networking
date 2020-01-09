/////////////////////////////////////////////////////////////////////////////////
//// Daniel J Ferguson
//// 2020
/////////////////////////////////////////////////////////////////////////////////
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
#include <conio.h>

#include "Networking\ConnMan.h"
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
struct PacketInfo
{
    PacketInfo()
        :start(clock::now()), expiry(clock::now())
    {
    }

    PacketInfo(clock::time_point futurerelative)
        : start(clock::now()), expiry(futurerelative)
    {
    }

    Packet packet;
    clock::time_point start;
    clock::time_point expiry;

};
std::queue<Packet> gAPackets; // from this to that
Address gAAddress;
Address gBAddress;
std::queue<Packet> gBPackets; // from that to this

std::list<PacketInfo> gADetoured;
std::list<PacketInfo> gBDetoured;


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

void
ProcessShenanigans(Mutex & mutex, std::queue<Packet> & packets, std::list<PacketInfo> & paused, ConnMan& cm, Address & address)
{
    std::random_device rd;     // only used once to initialise (seed) engine
    std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)
    std::uniform_int_distribution<uint32_t> uni(1, 1000); // guaranteed unbiased
    // From That to This
    mutex.lock();
    if (!packets.empty())
    {
        Packet p = packets.front();
        packets.pop();

        auto random_integer = uni(rng);
        if (random_integer >= 0 && random_integer < 850)
        {
            p.address = address;//gAAddress;
            cm.Write(p);
            std::cout << ".";
        }
        else if (random_integer >= 850 && random_integer < 950)
        {
            std::cout << "Pause\n";

            // TODO: randomize expiry
            PacketInfo newpi(clock::now() + std::chrono::milliseconds(uni(rng) % 1000));
            newpi.packet = p;

            paused.push_back(newpi);
        }
        else
        {
            std::cout << "Dropped\n";
        }
    }
    if (paused.size() > 0)
    {
        auto iter = paused.begin();
        while (iter != paused.end())
        {
            if (clock::now() >= iter->expiry)
            {
                std::cout << "Resume\n";
                PacketInfo pi = *iter;

                pi.packet.address = address;//gAAddress;
                gThisConnMan.Write(pi.packet);
                iter = paused.erase(iter);
            }
            else
            {
                iter++;
            }
        }


    }
    mutex.unlock();
}

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
    gThisAddress = ConnMan::CreateAddress(thisport, thatipv4.c_str());//we're just pretending it's all on same ip
    gThatAddress = ConnMan::CreateAddress(thatport, thatipv4.c_str());
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
        ProcessShenanigans(amutex,
                           gAPackets,
                           gADetoured,
                           gThatConnMan,
                           gThatAddress);

        // From That to This
        ProcessShenanigans(bmutex,
                           gBPackets,
                           gBDetoured,
                           gThisConnMan,
                           gAAddress);

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
