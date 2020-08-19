///////////////////////////////////////////////////////////////////////////////
// Daniel J Ferguson
// 2017
///////////////////////////////////////////////////////////////////////////////
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "Network.h"
#include <Ws2tcpip.h>
#include <iostream>
#include <assert.h>
using namespace bali;

#pragma comment(lib, "Ws2_32.lib")

Network::Network()
{

}

Network::~Network()
{

}

Network::Result Network::shutdownWorkerThreads(NetworkState& netstate)
{
    Network::Result result(Network::ResultType::SUCCESS);
    for (auto t = netstate.threads.begin(); t != netstate.threads.end(); ++t)
    {
        PostQueuedCompletionStatus(netstate.ioPort, 0, COMPLETION_KEY_SHUTDOWN, NULL);
    }

    uint32_t threadsdone = 0;
    while (netstate.threads.size() > threadsdone)
    {
        for (auto t = netstate.threads.begin(); t != netstate.threads.end(); ++t)
        {
            if (t->started)
            {
                DWORD ret = WaitForSingleObject(t->handle, 100);
                if (ret == WAIT_OBJECT_0)
                {
                    t->started = 0;
                    threadsdone++;
                    std::cout << "Thread joined\n";
                }
                else if (ret == WAIT_TIMEOUT)
                {
                    std::cout << "Still Waiting\n";
                }
                else
                {
                    std::cout << "WEIRD shutdownworkerthreads\n";
                }
            }
        }
    }


    
    return result;
}

Network::Result Network::initnet()
{
    Network::Result result(Network::ResultType::SUCCESS);
    WSADATA wsaData;
    // Initialize Winsock
    int r = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (r != NO_ERROR) {
        result.type = Network::ResultType::FAILED_SOCKET_STARTUP;
        result.code = r;
    }
    return result;
}

Network::Result Network::cleanup(NetworkState& netstate)
{
    Network::Result result(Network::ResultType::SUCCESS);
    // Clean up and exit.
    CloseHandle(netstate.ioPort);

    wprintf(L"Cleanup.\n");
    WSACleanup();
    return result;
}

Network::Result  Network::createWorkerThreads(NetworkState& netstate)
{
    Network::Result result(Network::ResultType::SUCCESS);
    for (uint32_t t = 0; t < netstate.maxThreads; ++t)
    {
        Thread thread;
        thread.handle = CreateThread(NULL, 0,
            (LPTHREAD_START_ROUTINE)Network::WorkerThread,
            &netstate,
            CREATE_SUSPENDED,
            &thread.id);

        if (thread.handle == INVALID_HANDLE_VALUE)
        {// Bad thread, bail
            result.type = Network::ResultType::FAILED_THREAD_CREATE;
            result.code = GetLastError();
            break;
        }
        netstate.threads.insert(netstate.threads.begin(), thread);
    }
    return result;
}
Network::Result Network::startWorkerThreads(NetworkState& netstate)
{
    Network::Result result(Network::ResultType::SUCCESS);
    for (auto t = netstate.threads.begin(); t != netstate.threads.end(); ++t)
    {
        DWORD ret = ResumeThread(t->handle);
        if (ret == -1)
        {
            result.type = Network::ResultType::FAILED_THREAD_START;
            result.code = GetLastError();
            break;
        }
        Sleep(100);
        t->started = true;
    }

    //// Kick off the perpetual reads
    //if (result.type == Network::ResultType::SUCCESS)
    //{
    //    result = read();
    //}
    return result;
}

//
// createPort creates an IO Completion Port, and returns a handle to it.
//
Network::Result Network::createPort(HANDLE & iocport)
{
    Network::Result result(Network::ResultType::SUCCESS);
    iocport = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
    if (iocport == NULL)
    {
        result.type = Network::ResultType::FAILED_IOPORT_CREATE;
        result.code = GetLastError();
    }
    return result;
}

//
// associateSocketWithIOCPort associates a Socket with an IO completion port
//
Network::Result Network::associateSocketWithIOCPort(HANDLE hIOPort, Socket & s)
{
    Network::Result result(Network::ResultType::SUCCESS);
    HANDLE temp = CreateIoCompletionPort((HANDLE)s.handle, hIOPort, s.completionKey, 0);
    if (temp == NULL)
    {
        result.type = Network::ResultType::FAILED_IOPORT_ASSOCIATION;
        result.code = GetLastError();
    }

    return result;
}

Network::Result Network::createSocket(Socket & s, ULONG_PTR completionKey)
{
    Network::Result result(Network::ResultType::SUCCESS);
    s.handle = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (s.handle != INVALID_SOCKET)
    {
        s.completionKey = completionKey;
        ZeroMemory(&s.local, sizeof(s.local));

        // HACKing
        char enable = 1;
        setsockopt(s.handle, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    }
    else
    {
        result.type = Network::ResultType::FAILED_SOCKET_CREATE;
        result.code = WSAGetLastError();
    }
    return result;
}

Network::Result Network::bindSocket(NetworkState& netstate)
{
    Network::Result result(Network::ResultType::SUCCESS);
    //Network::GetLocalAddressInfo(netstate, netstate.socket);

    memset(&netstate.socket.local.addr, 0, sizeof(netstate.socket.local.addr));
    ((sockaddr_in*)&netstate.socket.local.addr)->sin_family = AF_INET;
    ((sockaddr_in*)&netstate.socket.local.addr)->sin_addr.S_un.S_addr = htonl(INADDR_ANY);// inet_addr("10.0.0.93");
    ((sockaddr_in*)&netstate.socket.local.addr)->sin_port = htons(netstate.port);

    int ret = bind(netstate.socket.handle, (SOCKADDR*)&netstate.socket.local.addr, sizeof(netstate.socket.local.addr));
    if (ret == SOCKET_ERROR)
    {
        result.type = Network::ResultType::FAILED_SOCKET_BIND;
        result.code = WSAGetLastError();
        std::cout << result.code << std::endl;
    }
    return result;
}

Network::Result Network::InitializeWriteRequest(Request* req, Packet* packet)
{
    Network::Result result(Network::ResultType::SUCCESS);
    req->ioType = Request::IOType::WRITE;
    // Copy Address
    //memcpy(&req->packet.address.addr, &packet->address.addr, sizeof(SOCKADDR_STORAGE));
    //req->packet.address.len = packet->address.len;
    req->packet.address = packet->address;

    assert(packet->buffersize <= MAX_PACKET_SIZE);

    // Copy Buffer (truncate at MAX_PACKET_SIZE)
    req->packet.buffersize = min(packet->buffersize, MAX_PACKET_SIZE);
    memcpy(req->packet.buffer, packet->buffer, req->packet.buffersize);
    // Plumb WSABUF
    req->wsabuf.buf = (CHAR*)req->packet.buffer;
    req->wsabuf.len = req->packet.buffersize;
    return result;
}
Network::Result Network::InitializeReadRequest(Request* req)
{
    Network::Result result(Network::ResultType::SUCCESS);
    req->ioType = Request::IOType::READ;
    req->wsabuf.buf = (CHAR*)req->packet.buffer;
    req->wsabuf.len = MAX_PACKET_SIZE;
    return result;
}
//
// write() posts a write-request to the IOCP
//
Network::Result Network::write(NetworkState& netstate, Packet & packet)
{
    Network::Result result(Network::ResultType::SUCCESS);
    Request* req = nullptr;
    if (netstate.socket.overlapPool.acquire(&req))
    {
        DWORD flags = 0;
        int ret = 0;


        Network::InitializeWriteRequest(req, &packet);

        ret =
            WSASendTo(
                netstate.socket.handle,
                &req->wsabuf,
                1,
                NULL,
                flags,
                (SOCKADDR*)&req->packet.address.addr,
                req->packet.address.len,
                (OVERLAPPED*)req,
                NULL);
        if (ret == 0)
        {//Good
         // SendTo finished now, but we'll let the worker thread handle the completion.
        }
        else
        {
            // Problem
            int err = WSAGetLastError();
            if (err != WSA_IO_PENDING) {
                result.type = Network::ResultType::FAILED_SOCKET_SENDTO;
                result.code = err;
            }
            else
            {//Good
             // No Problem. The read has been posted.
            }
        }
    }
    else
    {
        result.type = Network::ResultType::FAILED_SOCKET_NOMOREOVERLAPPED;
        std::cout << "Problem: Network::write() says No More Overlaps!\n";
    }
    return result;
}



//
// read() posts a read-request to the IOCP
//
// Result.type == Sucess
//  When a Read was posted to the IOCP
// Result.type == RecvFrom
//  When the API WSARecvFrom fails.
//
Network::Result Network::read(NetworkState& netstate)
{
    Network::Result result(Network::ResultType::SUCCESS);
    Request* req = nullptr;
    if (netstate.socket.overlapPool.acquire(&req))
    {
        Network::InitializeReadRequest(req);
        //req->wsabuf.len = 0;
        DWORD flags = 0;// MSG_PEEK;
        int ret = 0;
        ret =
            WSARecvFrom(netstate.socket.handle,
                &req->wsabuf,
                1,
                NULL,//&o->bytesReceived,// This can be NULL because lpOverlapped parameter is not NULL.
                &flags,
                (SOCKADDR*)&req->packet.address.addr,
                &req->packet.address.len,
                (OVERLAPPED*)req,
                NULL);
        if (ret == 0)
        {
            // No Problem - the read completed here and now
            // But we will just let a workerthread process it like normal.
            // keep things simple.
        }
        else
        {
            // Problem
            int err = WSAGetLastError();
            if (err != WSA_IO_PENDING) {
                result.type = Network::ResultType::FAILED_SOCKET_RECVFROM;
                result.code = err;
            }
            else
            {
                // No Problem - the read has been successfully posted
            }
        }
    }
    else
    {
        result.type = Network::ResultType::FAILED_SOCKET_NOMOREOVERLAPPED;
        std::cout << "Problem: Network::read() says No More Overlaps!\n";
    }
    return result;
}

//
// Completes IO requests that were previously posted
// to the IO completion port.
// Processes Reads and Writes.
//
void* Network::WorkerThread(NetworkState* netstate)
{
    Network::Result result(Network::ResultType::SUCCESS);
    DWORD bytesTrans = 0;
    ULONG_PTR ckey = COMPLETION_KEY_UNKNOWN;
    LPWSAOVERLAPPED pOver;

    uint64_t tid = InterlockedIncrement(&netstate->threadidmax);
    bool done = false;
    std::cout << "Worker Thread [" << tid << "] is Starting up.\n";
    while (!done)
    {
        BOOL ret = GetQueuedCompletionStatus(netstate->ioPort, &bytesTrans, &ckey, (LPOVERLAPPED*)&pOver, INFINITE);

        if (ret == TRUE)
        {// Ok
            if (ckey == COMPLETION_KEY_IO)
            {
                assert(pOver != NULL);
                if (pOver != NULL)
                {
                    Request* request = reinterpret_cast<Request*>(pOver);

                    // Populate buffer size for user
                    request->packet.buffersize = bytesTrans;

                    // Notify the user
                    netstate->ioHandler(netstate->handlercontext, request, tid);

                    // Relinquish this overlapped structure
                    netstate->socket.overlapPool.release(request->index);
                }
            }
            else if (ckey == COMPLETION_KEY_SHUTDOWN)
            {
                std::cout << "Worker: Shutdown Request Received!\n";
                done = true;
                continue;
            }
        }
        else
        {

            DWORD gle = GetLastError();
            std::cout << "Worker: GetLastError() -> " << gle << std::endl;

            if (pOver != NULL)
            {
                std::cout << "Worker: IO Packet Valid"  << std::endl;
                Request* request = reinterpret_cast<Request*>(pOver);
                // Relinquish this overlapped structure
                netstate->socket.overlapPool.release(request->index);
            }
            else
            {
                std::cout << "Worker: IO Packet Invalid" << std::endl;
            }
        }
    }
    std::cout << "Worker Thread [" << tid << "] is Shutting down.\n";
    return NULL;
}

bool Network::GetLocalAddressInfo(NetworkState& netstate, Socket & s)
{
    ADDRINFOW hints, *res;
    int ret;
    bool status = true;
    wchar_t port_buffer[6];

    wsprintf(port_buffer, L"%hu", netstate.port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    // Setting AI_PASSIVE will give you a wildcard address if addr is NULL
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV | AI_PASSIVE;

    if ((ret = GetAddrInfo(NULL, port_buffer, &hints, &res)) != 0)
    {
        //(L"getaddrinfo: %s\n", gai_strerror(status));
        status = false;
    }
    else
    {
        // Note; we're hoping that the first valid address is the right address. glta. lol.
        memset(&s.local.addr, 0, sizeof(s.local.addr));
        s.local.len = res->ai_addrlen;
        memcpy(&s.local.addr, res->ai_addr, res->ai_addrlen);



        // Print for debug
        //char ipstr[INET6_ADDRSTRLEN];
        ADDRINFOW* ptr = NULL;
        for (ptr = res; ptr != NULL; ptr = ptr->ai_next) 
        {


            struct sockaddr_in *ipv4 = (struct sockaddr_in *) ptr->ai_addr;//(struct sockaddr_in *)&netstate.socket.local.addr;
            //void* addr = &(ipv4->sin_addr);
            

            //PCSTR ptr = inet_ntop(AF_INET, addr, ipstr, sizeof(ipstr));
            std::cout << "Server Bound to IP: " << inet_ntoa(ipv4->sin_addr) << "\n";
        }
        FreeAddrInfo(res);
    }

    return status;
}

Network::Result Network::initialize(NetworkState& netstate,
                                    uint32_t maxThreads,
                                    uint16_t port,
                                    NetworkState::IOHandler handler,
                                    void* handlercontext
)
{
    Network::Result result(Network::ResultType::SUCCESS);

    netstate.maxThreads = maxThreads;
    netstate.port = port;
    netstate.ioPort = INVALID_HANDLE_VALUE;
    netstate.ioHandler = handler;
    netstate.handlercontext = handlercontext;

    Network::initnet();
    Network::createSocket(netstate.socket, COMPLETION_KEY_IO);

    //  When a client grace-lessly disconnects... the ICMP CONNRESET fucks us up.
    // TODO: hmm... feels cheap... root cause please.
    // SIO_UDP_CONNRESET := (IOC_IN | IOC_VENDOR | 12);
    uint32_t flag = 0;
    DWORD ret = 0;
    WSAIoctl(netstate.socket.handle, IOC_IN | IOC_VENDOR | 12, &flag, 4, NULL, 0, &ret, NULL, 0);

    Network::bindSocket(netstate);
    
    Network::createPort(netstate.ioPort);
    Network::createWorkerThreads(netstate);
    return result;
}

Network::Result Network::start(NetworkState& netstate)
{
    Network::Result result(Network::ResultType::SUCCESS);
    Network::associateSocketWithIOCPort(netstate.ioPort, netstate.socket);

    Network::startWorkerThreads(netstate);

    // Weird way to kick of first read. but ok...
    uint32_t rc=0;
    do
    {
        if (Network::read(netstate).type == bali::Network::ResultType::SUCCESS)
        {
            std::cout << "Read Queued.\n";
        }
        else
        {
            std::cout << "ERROR: Read NOT Queued.\n";
            break;
        }
        rc++;
    }while (rc < netstate.maxThreads/2);
    return result;
}

Network::Result Network::stop(NetworkState& netstate)
{
    Network::Result result(Network::ResultType::SUCCESS);
    Network::shutdownWorkerThreads(netstate);
    netstate.socket.cleanup();
    CloseHandle(netstate.ioPort);
    return result;
}


