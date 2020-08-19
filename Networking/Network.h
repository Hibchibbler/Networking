///////////////////////////////////////////////////////////////////////////////
// Daniel J Ferguson
// 2019
///////////////////////////////////////////////////////////////////////////////

#ifndef NETWORK_H_
#define NETWORK_H_

#include <WinSock2.h>
#include <windows.h>
#include <list>
#include <vector>
#include <cassert>
#include <stdint.h>
#include <iostream>

//
// Network, Socket, Request, and RequestPool are used together
// to create a UDP IOCP based Server.
// 
// API to coordinate everything. The flow is as follows:
//  
// 
//

#define COMPLETION_KEY_UNKNOWN      0
#define COMPLETION_KEY_IO           1
#define COMPLETION_KEY_SHUTDOWN     2
//
#define OVERLAP_STATUS_INUSE       1
#define OVERLAP_STATUS_NOT_INUSE   0

namespace bali
{
const ULONG MAX_PACKET_SIZE = 1280;
const ULONG MAX_OVERLAPS = 16;

class Mutex
{
public:
    void create()
    {
        InitializeCriticalSection(&critSection);
        init++;
    }

    void destroy()
    {
        DeleteCriticalSection(&critSection);
        init--;
    }

    void lock()
    {
        EnterCriticalSection(&critSection);
    }

    void unlock()
    {
        LeaveCriticalSection(&critSection);
    }

    USHORT isInit()
    {
        return init;
    }
private:
    USHORT init;
    CRITICAL_SECTION critSection;
};
class Thread
{
public:
    HANDLE handle;
    DWORD  id;
    DWORD  started;
};
class Address
{
public:
    Address() 
    {
        ZeroMemory(&addr, sizeof(SOCKADDR_STORAGE));
        len = sizeof(SOCKADDR_STORAGE);
    }
    ~Address()
    {
    }
    Address(const Address & address)
    {
        // We can just use the operator= because 
        //  we are not allocating/deallocating memory.
        // Otherwise, copy constructor and assignment operator
        //  would be optimized differently.
        *this = address;
    }
    Address & operator=(const Address & address)
    {
        if (this != &address)
        {
            memcpy(&this->addr, &address.addr, sizeof(SOCKADDR_STORAGE));
            this->len = address.len;
        }
        return *this;
    }
    SOCKADDR_STORAGE    addr;
    INT                 len;
};
class Packet
{
    public:
        Packet()
            : buffersize(0)
        {
            ZeroMemory(buffer,MAX_PACKET_SIZE);
        }
        ~Packet()
        {
        }
        Packet(const Packet & packet)
        {
            // We can just use the operator= because 
            //  we are not allocating/deallocating memory
            *this = packet;

        }
        //Packet(Packet && packet)
        //{
        //    memcpy(buffer, packet.buffer, MAX_PACKET_SIZE);
        //    ackhandler = packet.ackhandler;
        //    buffersize = packet.buffersize;
        //    address = packet.address;
        //}
        Packet & operator=(const Packet & packet)
        {
            if (this != &packet)
            {
                memcpy(buffer, packet.buffer, MAX_PACKET_SIZE);
                ackhandler = packet.ackhandler;
                buffersize = packet.buffersize;
                address = packet.address;
            }
            return *this;
        }
        void*           ackhandler;
        Address         address;
        uint32_t        buffersize;
        UCHAR           buffer[MAX_PACKET_SIZE];
};
class Request
{
public:
    enum IOType
    {
        READ,
        WRITE
    };

    Request()
    {
        this->wsaoverlapped.Internal = 0;
        this->wsaoverlapped.InternalHigh = 0;
        this->wsaoverlapped.Pointer = 0;
        this->wsaoverlapped.hEvent = 0;

        wsabuf.len = 0;
        wsabuf.buf = 0;

        inuse = OVERLAP_STATUS_NOT_INUSE;
    }
    // WSAOVERLAPPED must be first member
    WSAOVERLAPPED       wsaoverlapped;
    WSABUF              wsabuf;
    IOType              ioType;
    uint32_t            index;
    uint32_t            inuse;

    Packet              packet;
};
class RequestPool
{
public:
    uint32_t index;
    Request pool[MAX_OVERLAPS];

    //
    // Initialize pool of overlaps
    //
    RequestPool()
    {
        index = 0;
        for (uint32_t i = 0; i < MAX_OVERLAPS; ++i)
        {
            pool[i].inuse = OVERLAP_STATUS_NOT_INUSE;
            pool[i].index = i;
        }
    }

    ~RequestPool()
    {
    }

    //
    // Acquire an unused overlap
    // Return false when there is not an unused overlap available.
    //
    bool acquire(Request** overlapped)
    {
        bool acquired = false;
        for (uint32_t i = 0; i <MAX_OVERLAPS; ++i)
        {
            *overlapped = &pool[i];
            if (InterlockedExchange(&(*overlapped)->inuse, OVERLAP_STATUS_INUSE) == OVERLAP_STATUS_NOT_INUSE)
            {//Wasn't already in use. So, we want it, and we have it.
                acquired = true;
                break;
            }
            else
            {//Was already in use. So we don't want it,
                // We didn't change anything that wasn't already true;
                continue;
            }
        };
        return acquired;
    }

    //
    // Release a used overlap
    //
    void release(uint32_t i)
    {
        InterlockedExchange(&(pool[i].inuse), OVERLAP_STATUS_NOT_INUSE);
    }
private:
};
class Socket
{
public:
    Socket()
    {
        handle = INVALID_SOCKET;
    }

    void cleanup()
    {
        closesocket(handle);
    }

    SOCKET handle;
    Address local;
    RequestPool overlapPool;
    ULONG_PTR completionKey;
};

/*
    NetworkState 
    Network State.
*/
class NetworkState
{
public:
    NetworkState(){}
    typedef void(*IOHandler)(void* state, Request* request, uint64_t id);
    Socket            socket;
    HANDLE            ioPort;
    std::list<Thread> threads;
    uint32_t          maxThreads;
    uint16_t          port;
    uint64_t          threadidmax;
    IOHandler         ioHandler;
    void*             handlercontext;
};
/*
"Network" is an API for asynchronously sending and recieving UDP packets.
It uses IOCP and UDP IPv4 sockets to implement it all.
State is maintained in NetworkState, and is an argument to all the apis.

It creates worker threads.
Users specify Callbacks that are invoked when the worker threads needs to notify
of important events.
Spoiler: "ConnMan" is the user of "Network".
*/
class Network
{
private:
    Network();
public:
    ~Network();

    enum class ResultType
    {
        SUCCESS,
        FAILED,
        INFO_CLIENT_RXQUEUE_EMPTY,
        FAILED_IOPORT_ASSOCIATION,
        FAILED_IOPORT_CREATE,
        FAILED_THREAD_CREATE,
        FAILED_THREAD_START,
        FAILED_SYNCHR_CREATE_READER_MUTEX,
        FAILED_SOCKET_RECVFROM,
        FAILED_SOCKET_NOMOREOVERLAPPED,
        FAILED_SOCKET_SENDTO,
        FAILED_SOCKET_BIND,
        FAILED_SOCKET_CREATE,
        FAILED_SOCKET_STARTUP,
        FAILED_SOCKET_CLEANUP,
        INFO_SOCKET_READ_COMPLETED
    };

    class Result
    {
    public:
        Result(ResultType t) { type = t; code = 0; }
        Result(ResultType t, uint32_t c) { type = t; code = c; }
        ResultType type;
        uint32_t code;
    };

    static
    Network::Result
    initialize(
        NetworkState& netstate,
        uint32_t maxThreads,
        uint16_t port,
        NetworkState::IOHandler handler,
        void* handlercontext
    );

    static
    Network::Result
    start(
        NetworkState& state
    );
    
    static
    Network::Result
    stop(
        NetworkState& state
    );

    // Posts a write-request to the IOCP
    static
    Network::Result
    write(
        NetworkState& state,
        Packet & data
    );

    // Posts a read-request to the IOCP
    static
    Network::Result
    read(
        NetworkState& state
    );

    static
    Network::Result
    InitializeWriteRequest(
        Request* req,
        Packet* packet
    );

    static
    Network::Result
    InitializeReadRequest(
        Request* req
    );

private:
    static
    bool
    GetLocalAddressInfo(NetworkState& state, Socket & s);
    
    static
    Network::Result
    initnet();

    static
    Network::Result
    cleanup(
        NetworkState& state
    );

    static
    Network::Result
    createWorkerThreads(
        NetworkState& state
    );

    static
    Network::Result
    startWorkerThreads(
        NetworkState& state
    );

    static
    Network::Result
    createPort(
        HANDLE & ioPort
    );

    static
    Network::Result
    associateSocketWithIOCPort(
        HANDLE iocport,
        Socket & s
    );
public:
    static
    Network::Result
    createSocket(
        Socket & s,
        ULONG_PTR completionKey
    );

    static
    Network::Result
    bindSocket(
        NetworkState& state
    );
private:
    static
    Network::Result
    shutdownWorkerThreads(
        NetworkState& state
    );

private:
    static void* WorkerThread(NetworkState* state);
};



}

#endif // NETWORK_H_
