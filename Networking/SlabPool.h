#ifndef SLABPOOL_H_
#define SLABPOOL_H_

#include <Windows.h>

namespace bali
{

template <typename T>
class SlabPool
{
public:
    static constexpr uint32_t MAX_POOLS = 16;
    static constexpr uint32_t BUCKET_NOT_INUSE = 0;
    static constexpr uint32_t BUCKET_INUSE = 1;

    struct Slab {
        uint32_t inuse;
        uint32_t index;
        T t;
    };

    Slab pool[MAX_POOLS];

    //
    // Initialize pool of overlaps
    //
    SlabPool()
    {
        for (uint32_t i = 0; i < MAX_POOLS; ++i)
        {
            pool[i].inuse = BUCKET_NOT_INUSE;
            pool[i].index = i;
        }
    }

    ~SlabPool()
    {
    }

    //
    // Acquire an unused overlap
    // Return false when there is not an unused overlap available.
    //
    bool acquire(Slab** ppSlab)
    {
        bool acquired = false;
        for (uint32_t i = 0; i <MAX_POOLS; ++i)
        {
            *ppSlab = &pool[i];
            if (InterlockedExchange(&(*ppSlab)->inuse, BUCKET_INUSE) == BUCKET_NOT_INUSE)
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
        InterlockedExchange(&(pool[i].inuse), BUCKET_NOT_INUSE);
    }

};

}

#endif // SLABPOOL_H_