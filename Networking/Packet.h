#pragma once
#include <WinSock2.h>
#include <windows.h>

#define OVERLAP_STATUS_INUSE       1
#define OVERLAP_STATUS_NOT_INUSE   0

namespace bali
{
const ULONG MAX_OVERLAPS = 16;

class Address
{
public:
    Address()
    {
        ZeroMemory(&addr, sizeof(SOCKADDR_STORAGE));
        len = sizeof(SOCKADDR_STORAGE);
    }
    SOCKADDR_STORAGE    addr;
    INT                 len;
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
    OverlapPool overlapPool;
    ULONG_PTR completionKey;
};

class Data
{
public:
    Data()
    {
        payloadsize = 0;
        remote.len = 0;
    }
    Data(const Data & _data)
    {
        *this = _data;
    }

    Data(uint32_t payloadsize_, void* payload_, SOCKADDR_STORAGE & addr, int addrLen)
    {
        payloadsize = payloadsize_;
        memcpy(payload, payload_, MAX_PACKET_SIZE);
        remote.len = addrLen;
        memcpy(&remote.addr, &addr, sizeof(SOCKADDR_STORAGE));
    }

    Data & operator=(const Data & data_)
    {
        if (this != &data_)
        {
            this->payloadsize = data_.payloadsize;
            memcpy(this->payload, data_.payload, MAX_PACKET_SIZE);
            remote.len = data_.remote.len;
            memcpy(&this->remote.addr, &data_.remote.addr, sizeof(SOCKADDR_STORAGE));
        }
        return *this;
    }
    Address remote;
    uint32_t payloadsize;
    uint8_t payload[MAX_PACKET_SIZE];
};

class Overlapped
{
public:
    Overlapped()
    {
        this->wsaoverlapped.Internal = 0;
        this->wsaoverlapped.InternalHigh = 0;
        this->wsaoverlapped.Offset = 0;
        this->wsaoverlapped.OffsetHigh = 0;
        this->wsaoverlapped.hEvent = 0;
        inuse = OVERLAP_STATUS_NOT_INUSE;
    }
    // WSAOVERLAPPED must be first member
    WSAOVERLAPPED       wsaoverlapped;
    WSABUF              wsabuf;
    Address             remote;
    uint32_t            index;
    uint32_t            inuse;
};

class IORequest : public Overlapped
{
public:
    enum class IOType: uint32_t
    {
        READ,
        WRITE
    };
    IOType              ioType;
    uint32_t            buffersize;
    UCHAR               buffer[MAX_PACKET_SIZE];
};

class OverlapPool
{
public:
    uint32_t index;
    Overlapped pool[MAX_OVERLAPS];

    OverlapPool()
    {
        index = 0;
        for (uint32_t i = 0; i < MAX_OVERLAPS; ++i)
        {
            pool[i].inuse = OVERLAP_STATUS_NOT_INUSE;
            pool[i].index = i;
        }
    }

    ~OverlapPool()
    {
    }

    //
    // Acquire an unused overlap
    // Return false when there is not an unused overlap available.
    //
    bool acquire(T** overlapped)
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

}