#include "IBAllocator.h"

#include "Platform/IBPlatform.h"
#include "IBLogging.h"

/*
## Virtual Memory And Paging
Let's talk about virtual memory and paging

Virtual memory in our operating system is a system that allows us to allocate memory
as though it were contiguous even though it might not be. It also provides us with the
capability of not allocating physical memory if it's not read or written to.

You can imagine virtual memory as a series of indirections
When accessing a memory address in your program,
the memory address has to be converted from virtual memory to physical memory.

You can imagine that the operating system has a map of every memory address in virtual memory
and it's physical counterpart.

Virtual Addresses | Physical Addresses
[0]                 [42]
[1]                 [4]
[2]                 [154]

Now you can likely imagine that the overhead on a table like this would be significant.
We could have an entry for every single byte in memory.

Instead, the operating system creates "chunks" of virtual memory.
Instead of having a mapping for every single virtual address. It has a mapping for a single chunk.
As a result, if our address is within a single chunk, we can simply determine the physical address of our chunk
and then we can add the offset in our virtual chunk to the physical chunk.

Let's look at an example with chunks of 4 bytes:
Virtual Addresses | Physical Addresses
[0]                 [40]
[4]                 [16]
[8]                 [400]

Now if we have a virtual address of 5,
we determine our chunk address of 4
which maps to physical memory 16
and add our offset of 1 (5-4) to get our final physical memory location of 17

Our operating system will typically make our chunks 4kb large.

These chunks are called memory pages.

You can reserve memory pages, which implies that the virtual addresses will be reserved for future use.
No other calls will provide you with the same addresses in your process.
Reservation is only known to your process, no other process needs to know what memory you have reserved.

You can then commit that memory which will tell the operating system to add a memory page to it's list.
The operating system will maintain a list of allocated memory pages and their physical counterpart.
Commiting a memory page does not necessarily allocate a physical memory page until that page is accessed.

For a primer on virtual memory and memory pages see:
https://www.tutorialspoint.com/operating_system/os_virtual_memory.htm

## Allocation Strategy
The IB Allocator makes use of a 3 stage strategy for memory allocations.
For small memory, it makes use of a slab allocator. (Less than or equal to 512 byte allocations)
For medium memory, it makes use of a buddy allocator. (Less than or equal to 1MB allocations)
And for large memory simply uses memory mapping.

We use this strategy because every allocator has desirable properties for their sizes.

### Slab Allocator
The slab allocator is the simplest.
https://en.wikipedia.org/wiki/Slab_allocation
https://hammertux.github.io/slab-allocator

It works by allocating memory pages for every potential size from 1 to 512.

These memory pages will be treated as arrays of that particular size.
Let's say we ask for a block of memory of size 512 bytes,
Then we would look at memory pages that we've allocated for blocks of 512 bytes

Page of 512
[512][512][512][512][512][512][512][512]
[0]  [1]  [0]  [1]  [0]  [0]  [0]  [0]

And determine which block is free for us. (0)

You can imagine the slab allocator to simply be an object pool for objects of sizes from 1 to 512

We use this strategy because it has 0 fragmentation concerns when allocating blocks.
Our blocks will never cause empty holes of varying sizes.

Our allocator maintains a list of bits to determine which block indices are free and which block indices are allocated.
Every memory page of a size class gets a list of bits for itself.

You can imagine that the memory looks a little like this for a memory page:
[0101000] [512][512][512][512][512][512][512]

You might notice that if we only allocate a single memory page for allocations of 512 bytes
we would only have 8 possible allocations of 512, and you'd be right.

That's why we actually allocate 4kb * 8 memory pages for each size class.
We allocate 4kb * 8 memory pages because we maintain a header page that is only used to
determine if a memory page is full or not.

Imagine that we have 4 memory pages:
[0][1][2][3]

Our header page would have a bit for each page to determine if the page is full or not
[1101]

And to find a free location, we would simply look for a page that has it's bit cleared.

That is the essence of our algorithm.

In steps:
- Determine the size of our memory block. (512)
- Look at our header and iterate through all the bits to find a free bit. (4kb * 8 bits)
- Say our cleared bit is index 1
- Go to our page at index 1
- Iterate through all the slot bits to determine which slot is free
- Say our free slot is 12
- Retrieve the memory for slot 12

### Buddy Allocator
Once our memory allocations are larger than 512, we're in the realm of the buddy allocator.
https://en.wikipedia.org/wiki/Buddy_memory_allocation

Our buddy works by recursively splitting larger blocks in 2 equal sized blocks.
It stops splitting once we reach a block of the desired size.

Every time we split a larger block, we add it's new children blocks to the list of free blocks
and remove the larger block.

When a block is freed, we look for the block that was created alongside it (it's buddy).
If we find it, we merge it back into a larger block.

Here is a small example:
Say we begin with a block of 512 bytes
[512]
But we want a block of 16 bytes.
That means we need to split our block quite a few times
We first split it into 2 blocks of 256 and remove our parent block from the list
[256][256]
Then we split one of our new blocks into blocks of 128 and remove the parent
[128][128][256]
And continue doing so until we reach 16
[16][16][32][64][128][256]

Then we remove the final block which is our allocation block.
Leaving us with a free list of [16][32][64][128][256]
And an allocation list of [16]

When we free a block, we determine if it's buddy if free as well.
Here is another small example.

This time, we'll add a "buddy index" to our blocks (where the index represents which buddy group they belong to)
[16,0][32,5][32,0][64,8]

If we're freeing a block with the ID [16,0]
[16,0][32,5][32,0][64,8][16,0]
Then we look through our list and find that there is another block of [16,0]
We merge it into a block of [32,0]
[32,5][32,0][64,8][32,0]
Then we look to see if there's a block of [32,0] (which there is)
and merge it into a block of [64,0]
[32,5][64,8][64,0]

And that's it!

We use the buddy allocator because it has the desirable property of allocating smaller blocks together
and larger blocks together.

### Memory mapping
Finally, for large memory allocations, we use memory mapping. This simply allocates a large chunk
of memory pages for a large allocation and is largely handled by the operating system.

*/


namespace
{
    // Small Memory Allocations

    constexpr size_t SmallMemoryBoundary = 512;
    constexpr uint64_t NoSlot = 0xFFFFFFFFFFFFFFFF;

    // BlockCount * 1/8 + BlockCount * BlockSize = PageSize
    // (BlockCount)(1/8 + BlockSize) = PageSize
    // BlockCount = PageSize*8/(1 + 8*BlockSize)

    constexpr uint32_t LockPageCount = 64;
    struct PageTable
    {
        alignas(64) uint32_t LockedPages[LockPageCount] = {};
        void *Header = nullptr;
        void *MemoryPages = nullptr;
    };

    PageTable SmallMemoryPageTables[SmallMemoryBoundary] = {};
    const size_t SmallMemoryRange = IB::memoryPageSize() * 8 * IB::memoryPageSize();

    bool areAllSlotsSet(void *memory, uint64_t bitCount)
    {
        bool fullyAllocated = true;

        uint64_t *memoryIter = reinterpret_cast<uint64_t *>(memory);
        for (; static_cast<int32_t>(bitCount) > 0; memoryIter++, bitCount -= 64)
        {
            uint64_t value = *memoryIter;
            uint64_t testBitMask = ~(bitCount < 64 ? (0xFFFFFFFFFFFFFFFF << (bitCount % 64)) : 0); // Create a bitmask of all the bits we want to test
            // If any of our bits were cleared
            if ((value & testBitMask) != testBitMask)
            {
                fullyAllocated = false;
                break;
            }
        }

        return fullyAllocated;
    }

    bool areAllSlotsClear(void *memory, uint64_t bitCount)
    {
        bool fullyFree = true;

        uint64_t *memoryIter = reinterpret_cast<uint64_t *>(memory);
        for (; static_cast<int32_t>(bitCount) > 0; memoryIter++, bitCount -= 64)
        {
            uint64_t value = *memoryIter;
            uint64_t testBitMask = ~(bitCount < 64 ? (0xFFFFFFFFFFFFFFFF << (bitCount % 64)) : 0); // Create a bitmask of all the bits we want to test
            // If any of our bits were set
            if ((value & testBitMask) != 0)
            {
                fullyFree = false;
                break;
            }
        }

        return fullyFree;
    }

    uint64_t firstClearedBitIndex(uint64_t value)
    {
        // Mask out all our set bits and the first non-set bit
        // (0010 + 1) ^ 0010 = 0011 ^ 0010 = 0001
        // (0011 + 1) ^ 0011 = 0100 ^ 0011 = 0111
        // (1011 + 1) ^ 1011 = 1100 ^ 1011 = 0111
        // (0111 + 1) ^ 0111 = 1000 ^ 0111 = 1111
        value = value ^ (value + 1);

        // At this point, the number of bits set is equal to the index of our first cleared bit + 1
        // Calculate the number of set bits and subtract 1 to get it's index

        return IB_POPCOUNT(value) - 1;
    }

    uint64_t findClearedSlot(void *memory, uint64_t bitCount)
    {
        uint64_t freeSlot = NoSlot;

        // TODO: handle alignment

        uint64_t *memoryIter = reinterpret_cast<uint64_t *>(memory);
        for (; static_cast<int32_t>(bitCount) > 0; memoryIter++, bitCount -= 64)
        {
            uint64_t value = *memoryIter;
            uint64_t testBitMask = ~(bitCount < 64 ? (0xFFFFFFFFFFFFFFFF << (bitCount % 64)) : 0); // Create a bitmask of all the bits we want to test
            if ((value & testBitMask) != testBitMask)                                              // If any of our bits were cleared
            {
                uint64_t bitIndex = firstClearedBitIndex(value);

                ptrdiff_t chunkIndex = (memoryIter - reinterpret_cast<uint64_t *>(memory));
                freeSlot = chunkIndex * 64 + bitIndex;
                break;
            }
        }

        return freeSlot;
    }

    void setSlot(void *memory, uint64_t index)
    {
        uint64_t *memoryIter = reinterpret_cast<uint64_t *>(memory);
        memoryIter = memoryIter + index / 64;
        *memoryIter |= 1ull << (index % 64);
    }

    void clearSlot(void *memory, uint64_t index)
    {
        uint64_t *memoryIter = reinterpret_cast<uint64_t *>(memory);
        memoryIter = memoryIter + index / 64;
        *memoryIter &= ~(1ull << (index % 64));
    }

    void *getPageSlot(void *page, size_t blockSize, uint64_t blockCount, uint64_t slotIndex)
    {
        uintptr_t pageIter = reinterpret_cast<uintptr_t>(page);
        pageIter += blockCount / 8 + (blockCount % 8 > 0 ? 1 : 0);

        // Make sure we're aligned
        uintptr_t firstSlot = pageIter + ((pageIter % blockSize) > 0 ? (blockSize - pageIter % blockSize) : 0);
        pageIter = firstSlot + slotIndex * blockSize;

        IB_ASSERT(pageIter < reinterpret_cast<uintptr_t>(page) + IB::memoryPageSize(), "Our address is further than our allocated memory! How come?");
        return reinterpret_cast<void *>(pageIter);
    }

    template< typename T >
    T volatileLoad(T* value)
    {
        return *reinterpret_cast<T volatile*>(value);
    }

    template< typename T >
    void volatileStore(T* value, T set)
    {
        *reinterpret_cast<T volatile*>(value) = set;
    }

    void *allocateSmallMemory(size_t blockSize)
    {
        size_t tableIndex = blockSize - 1;
        // If our table hasn't been initialized, allocate a page for it
        if (SmallMemoryPageTables[tableIndex].MemoryPages == nullptr)
        {
            // Mark our memory page as 0x01 to lock it.
            // Other threads should know to wait until the value is truly set now.
            if (IB::atomicCompareExchange(&SmallMemoryPageTables[tableIndex].MemoryPages, nullptr, reinterpret_cast<void*>(0x01)) == nullptr)
            {
                SmallMemoryPageTables[tableIndex].Header = IB::reserveMemoryPages(1);
                IB::commitMemoryPages(SmallMemoryPageTables[tableIndex].Header, 1);

                void* memoryPages = IB::reserveMemoryPages(IB::memoryPageSize() * 8);
                // Assure our writes are globally visible before we allow access to our memory pages.
                IB::threadStoreFence();
                volatileStore(&SmallMemoryPageTables[tableIndex].MemoryPages, memoryPages);
            }
        }

        // Busy spin while the other thread is allocating our memory pages.
        while (volatileLoad(&SmallMemoryPageTables[tableIndex].MemoryPages) == reinterpret_cast<void*>(0x01))
        {
        }

        // Find our free page address
        uint64_t lockedPageIndex = UINT64_MAX;
        uint32_t slotOffset = 0;
        uint32_t lockIndex = 0;
        while (lockedPageIndex == UINT64_MAX)
        {
            uint64_t* offsetHeader = reinterpret_cast<uint64_t*>(SmallMemoryPageTables[tableIndex].Header);

            uint64_t pageCount = (IB::memoryPageSize() * 8);
            uint64_t pageIndex = findClearedSlot(offsetHeader + slotOffset/64, pageCount - slotOffset) + slotOffset;
            IB_ASSERT(pageIndex != NoSlot || slotOffset != 0, "Failed to find a slot. We're out of memory."); // We're out of memory pages for this size class! Improve this algorithm to support the use case.

            lockIndex = pageIndex % LockPageCount;
            if (IB::atomicCompareExchange(&SmallMemoryPageTables[tableIndex].LockedPages[lockIndex], 0, 1) == 0)
            {
                lockedPageIndex = pageIndex;
            }
            else
            {
                slotOffset += (pageIndex & ~63) + 64; // Move to the next 64 bit boundary
                if (slotOffset >= pageCount)
                {
                    slotOffset = 0;
                }
            }
        }

        void *page = nullptr;
        {
            uintptr_t pageAddress = reinterpret_cast<uintptr_t>(SmallMemoryPageTables[tableIndex].MemoryPages);
            pageAddress = pageAddress + IB::memoryPageSize() * lockedPageIndex;
            page = reinterpret_cast<void *>(pageAddress);
            IB::commitMemoryPages(page, 1);
        }

        // Find our memory address
        void *memory;
        {
            uint64_t blockCount = (IB::memoryPageSize() * 8) / (1 + blockSize * 8);
            uint64_t freeSlot = findClearedSlot(page, blockCount);
            IB_ASSERT(freeSlot != NoSlot, "Failed to find a slot but our page said it had a free slot!"); // Our page's "fully allocated" bit was cleared, how come we have no space?

            setSlot(page, freeSlot);
            if (areAllSlotsSet(page, blockCount))
            {
                // As long as our locks are on a 64 bit boundary,
                // we can guarantee that no one else will be setting our bit in our
                // 64 bit block
                setSlot(SmallMemoryPageTables[tableIndex].Header, lockedPageIndex);
            }

            memory = getPageSlot(page, blockSize, blockCount, freeSlot);
        }

        // Make sure all our work is exeternally visible
        IB::threadStoreFence();
        // Release our lock
        SmallMemoryPageTables[tableIndex].LockedPages[lockIndex] = 0;

        return memory;
    }

    bool freeSmallMemory(void *memory)
    {
        uint32_t memoryPageIndex = UINT32_MAX;
        for (uint32_t i = 0; i < SmallMemoryBoundary; i++)
        {
            uintptr_t memoryAddress = reinterpret_cast<uintptr_t>(memory);
            uintptr_t memoryStart = reinterpret_cast<uintptr_t>(SmallMemoryPageTables[i].MemoryPages);
            uintptr_t memoryEnd = memoryStart + SmallMemoryRange;

            if (memoryAddress >= memoryStart && memoryAddress < memoryEnd)
            {
                memoryPageIndex = i;
                break;
            }
        }

        if (memoryPageIndex != UINT32_MAX)
        {
            size_t blockSize = memoryPageIndex + 1;
            uint64_t blockCount = (IB::memoryPageSize() * 8) / (1 + blockSize * 8);

            uintptr_t memoryAddress = reinterpret_cast<uintptr_t>(memory);
            uintptr_t pageStart = reinterpret_cast<uintptr_t>(SmallMemoryPageTables[memoryPageIndex].MemoryPages);

            ptrdiff_t offsetFromStart = memoryAddress - pageStart;
            ptrdiff_t pageIndex = offsetFromStart / IB::memoryPageSize();

            uint32_t lockIndex = pageIndex % LockPageCount;
            while (IB::atomicCompareExchange(&SmallMemoryPageTables[memoryPageIndex].LockedPages[lockIndex], 0, 1) != 0)
            {
                // Busy loop until we get our lock
            }

            void *page = reinterpret_cast<void *>(pageStart + pageIndex * IB::memoryPageSize());
            void *firstSlot = getPageSlot(page, blockSize, blockCount, 0);
            uintptr_t indexInPage = (memoryAddress - reinterpret_cast<uintptr_t>(firstSlot)) / blockSize;
            clearSlot(page, indexInPage);
            if (areAllSlotsClear(page, blockCount))
            {
                IB::decommitMemoryPages(page, 1);
            }

            clearSlot(SmallMemoryPageTables[memoryPageIndex].Header, pageIndex);
            IB::threadStoreFence();
            SmallMemoryPageTables[memoryPageIndex].LockedPages[lockIndex] = 0;
        }

        return memoryPageIndex != UINT32_MAX;
    }

    // Medium Memory Allocations

    uint8_t logBase2(size_t value)
    {
        value = value | (value >> 1);
        value = value | (value >> 2);
        value = value | (value >> 4);
        value = value | (value >> 8);
        value = value | (value >> 16);
        value = value | (value >> 32);

        return IB_POPCOUNT(value) - 1;
    }

    constexpr uint32_t MaxBuddyBlockCount = 4096; // Maximum block size is 4096 * SmallestBuddyChunkSize (4096 * 1024 = 4MB)
    constexpr size_t SmallestBuddyBlockSize = SmallMemoryBoundary * 2;
    constexpr size_t BuddyChunkSize = MaxBuddyBlockCount * SmallestBuddyBlockSize;
    constexpr size_t MediumMemoryBoundary = MaxBuddyBlockCount * SmallestBuddyBlockSize / 2; // We don't want to be able to allocate a whole buddy chunk.
    constexpr uint32_t BuddyChunkCount = 1024;                                               // arbitrary. Is roughly 4GB with MaxBuddyBlockCount of 4096 and SmallestBuddyChunkSize of 1024

    struct BuddyBlock
    {
        uint16_t Index = 0; // Our index in terms of our layer's size. It can go up to MaxBuddyBlockCount
        uint8_t Layer = 0;
    };

    struct BuddyChunk
    {
        void *MemoryPages = nullptr;
        BuddyBlock AllocatedBlocks[MaxBuddyBlockCount];
        BuddyBlock FreeBlocks[MaxBuddyBlockCount];
        uint32_t AllocatedBlockCount = 0;
        uint32_t FreeBlockCount = 0;
        uint32_t Locked = 0;
    };
    BuddyChunk *BuddyChunks = nullptr;

    size_t getSizeFromLayer(uint8_t layer)
    {
        return 1ull << (layer + logBase2(SmallestBuddyBlockSize));
    }

    uint8_t getLayerFromSize(size_t size)
    {
        uint8_t layer = logBase2(size) - logBase2(SmallestBuddyBlockSize);
        if (getSizeFromLayer(layer) < size)
        {
            layer++;
        }

        return layer;
    }

    void *allocateMediumMemory(size_t blockSize)
    {
        if (BuddyChunks == nullptr)
        {
            if (IB::atomicCompareExchange(reinterpret_cast<void**>(&BuddyChunks), nullptr, reinterpret_cast<void*>(0x01)) == nullptr)
            {
                uint32_t memoryPageCount = static_cast<uint32_t>(sizeof(BuddyChunk) * BuddyChunkCount / IB::memoryPageSize());
                BuddyChunk* buddyChunks = reinterpret_cast<BuddyChunk *>(IB::reserveMemoryPages(memoryPageCount));
                IB::commitMemoryPages(buddyChunks, memoryPageCount);

                volatileStore(&BuddyChunks, buddyChunks);
            }
        }

        // Busy loop until our buddy chunk is commited
        while (volatileLoad(&BuddyChunks) == reinterpret_cast<void*>(0x01))
        {
        }

        for (uint32_t buddyChunkIndex = 0; buddyChunkIndex < BuddyChunkCount; buddyChunkIndex++)
        {
            if (BuddyChunks[buddyChunkIndex].Locked == 0)
            {
                // Attempt to lock our chunk
                if (IB::atomicCompareExchange(&BuddyChunks[buddyChunkIndex].Locked, 0, 1) != 0)
                {
                    // If we've reached the end but we're skipping because our chunk is locked,
                    // restart the loop
                    if (buddyChunkIndex == BuddyChunkCount - 1)
                    {
                        buddyChunkIndex = 0;
                    }
                    continue;
                }
            }
            else
            {
                // If we've reached the end but we're skipping because our chunk is locked,
                // restart the loop
                if (buddyChunkIndex == BuddyChunkCount - 1)
                {
                    buddyChunkIndex = 0;
                }
                continue;
            }

            if (BuddyChunks[buddyChunkIndex].MemoryPages == nullptr)
            {
                BuddyBlock initialBlock{};
                initialBlock.Index = 0;
                initialBlock.Layer = getLayerFromSize(BuddyChunkSize);

                BuddyChunks[buddyChunkIndex].FreeBlocks[0] = initialBlock;
                BuddyChunks[buddyChunkIndex].FreeBlockCount = 1;

                BuddyChunks[buddyChunkIndex].MemoryPages = IB::reserveMemoryPages(static_cast<uint32_t>(BuddyChunkSize / IB::memoryPageSize()));
            }

            uint8_t requestedLayer = getLayerFromSize(blockSize);
            // TODO: Our layer's size might be larger than our block size. Log internal fragmentation.

            uint32_t closestBlockIndex = UINT32_MAX;
            uint8_t closestBlockLayer = UINT8_MAX;

            BuddyBlock *freeBlocks = BuddyChunks[buddyChunkIndex].FreeBlocks;
            for (uint32_t i = 0; i < BuddyChunks[buddyChunkIndex].FreeBlockCount; i++)
            {
                if (freeBlocks[i].Layer >= requestedLayer && freeBlocks[i].Layer < closestBlockLayer)
                {
                    closestBlockLayer = freeBlocks[i].Layer;
                    closestBlockIndex = i;
                }
            }

            if (closestBlockIndex != UINT32_MAX)
            {
                // Recursively split our block until it's the right size
                uint32_t currentBlockIndex = closestBlockIndex;
                BuddyBlock currentBlock = freeBlocks[currentBlockIndex];

                while (currentBlock.Layer > requestedLayer)
                {
                    // Remove our block
                    freeBlocks[currentBlockIndex] = freeBlocks[BuddyChunks[buddyChunkIndex].FreeBlockCount - 1];
                    BuddyChunks[buddyChunkIndex].FreeBlockCount--;

                    BuddyBlock nextBlock = BuddyBlock{};
                    nextBlock.Layer = currentBlock.Layer - 1;
                    nextBlock.Index = currentBlock.Index * 2;
                    freeBlocks[BuddyChunks[buddyChunkIndex].FreeBlockCount] = nextBlock;

                    nextBlock.Index = currentBlock.Index * 2 + 1;
                    freeBlocks[BuddyChunks[buddyChunkIndex].FreeBlockCount + 1] = nextBlock;

                    currentBlockIndex = BuddyChunks[buddyChunkIndex].FreeBlockCount;
                    currentBlock = freeBlocks[BuddyChunks[buddyChunkIndex].FreeBlockCount];
                    BuddyChunks[buddyChunkIndex].FreeBlockCount += 2;
                }
                IB_ASSERT(currentBlock.Layer == requestedLayer, "How come we couldn't create our layer?");

                // Remove our final block
                freeBlocks[currentBlockIndex] = freeBlocks[BuddyChunks[buddyChunkIndex].FreeBlockCount - 1];
                BuddyChunks[buddyChunkIndex].FreeBlockCount--;

                BuddyChunks[buddyChunkIndex].AllocatedBlocks[BuddyChunks[buddyChunkIndex].AllocatedBlockCount] = currentBlock;
                BuddyChunks[buddyChunkIndex].AllocatedBlockCount++;

                size_t layerSize = getSizeFromLayer(currentBlock.Layer);
                ptrdiff_t memoryOffset = layerSize * currentBlock.Index;
                uintptr_t memoryAddress = reinterpret_cast<uintptr_t>(BuddyChunks[buddyChunkIndex].MemoryPages) + memoryOffset;

                uintptr_t alignedMemoryAddress = memoryAddress / IB::memoryPageSize() * IB::memoryPageSize();
                // Use blockSize in our memory page count, we don't need to commit all of our unused buddy space.
                uint32_t memoryPageCount = static_cast<uint32_t>(blockSize / IB::memoryPageSize()) + (blockSize % IB::memoryPageSize() != 0 ? 1 : 0);

                IB::commitMemoryPages(reinterpret_cast<void *>(alignedMemoryAddress), memoryPageCount);

                IB::threadStoreFence();
                volatileStore<uint32_t>(&BuddyChunks[buddyChunkIndex].Locked, 0); // unlock our chunk
                return reinterpret_cast<void *>(memoryAddress);
            }

            IB::threadStoreFence();
            volatileStore<uint32_t>(&BuddyChunks[buddyChunkIndex].Locked, 0); // unlock our chunk

            // Continue looping if we didn't find a slot in this buddy chunk.
        }

        return nullptr;
    }

    bool freeMediumMemory(void *memory)
    {
        uint32_t memoryPageIndex = UINT32_MAX;
        for (uint32_t i = 0; i < BuddyChunkCount; i++)
        {
            uintptr_t memoryAddress = reinterpret_cast<uintptr_t>(memory);
            uintptr_t memoryStart = reinterpret_cast<uintptr_t>(BuddyChunks[i].MemoryPages);
            uintptr_t memoryEnd = memoryStart + BuddyChunkSize;

            if (memoryAddress >= memoryStart && memoryAddress < memoryEnd)
            {
                memoryPageIndex = i;
                break;
            }
        }

        if (memoryPageIndex != UINT32_MAX)
        {
            // Busy loop until we're unlocked, definitely can be improved.
            while (IB::atomicCompareExchange(&BuddyChunks[memoryPageIndex].Locked, 0, 1) != 0)
            {
            }

            uintptr_t pageStart = reinterpret_cast<uintptr_t>(BuddyChunks[memoryPageIndex].MemoryPages);
            ptrdiff_t offsetFromStart = reinterpret_cast<uintptr_t>(memory) - pageStart;

            uint32_t blockIndex = UINT32_MAX;
            for (uint32_t i = 0; i < BuddyChunks[memoryPageIndex].AllocatedBlockCount; i++)
            {
                BuddyBlock currentBlock = BuddyChunks[memoryPageIndex].AllocatedBlocks[i];

                size_t blockSize = getSizeFromLayer(currentBlock.Layer);
                ptrdiff_t memoryOffset = blockSize * currentBlock.Index;

                if (memoryOffset == offsetFromStart)
                {
                    blockIndex = i;
                    break;
                }
            }

            IB_ASSERT(blockIndex != UINT32_MAX, "We're not in the memory block but our address matches?");
            if (blockIndex != UINT32_MAX)
            {
                BuddyBlock currentBlock = BuddyChunks[memoryPageIndex].AllocatedBlocks[blockIndex];

                BuddyChunks[memoryPageIndex].AllocatedBlocks[blockIndex] = BuddyChunks[memoryPageIndex].AllocatedBlocks[BuddyChunks[memoryPageIndex].AllocatedBlockCount - 1];
                BuddyChunks[memoryPageIndex].AllocatedBlockCount--;

                BuddyChunks[memoryPageIndex].FreeBlocks[BuddyChunks[memoryPageIndex].FreeBlockCount] = currentBlock;
                BuddyChunks[memoryPageIndex].FreeBlockCount++;

                if (currentBlock.Layer >= getLayerFromSize(IB::memoryPageSize()))
                {
                    size_t blockSize = getSizeFromLayer(currentBlock.Layer);
                    ptrdiff_t memoryOffset = blockSize * currentBlock.Index;
                    uintptr_t memoryAddress = reinterpret_cast<uintptr_t>(BuddyChunks[memoryPageIndex].MemoryPages) + memoryOffset;

                    uintptr_t alignedMemoryAddress = memoryAddress / IB::memoryPageSize() * IB::memoryPageSize();
                    uint32_t memoryPageCount = static_cast<uint32_t>(blockSize / IB::memoryPageSize()) + (blockSize % IB::memoryPageSize() != 0 ? 1 : 0);

                    IB::decommitMemoryPages(reinterpret_cast<void *>(alignedMemoryAddress), memoryPageCount);
                }

                // Coallesce our buddies back into bigger blocks
                bool blockFound = true;
                while (blockFound)
                {
                    blockFound = false;

                    // Don't test the block we've just added, it's always at the end of our list
                    uint32_t freeBlockEnd = BuddyChunks[memoryPageIndex].FreeBlockCount - 1;

                    uint16_t evenIndex = currentBlock.Index & ~1;
                    for (uint32_t i = 0; i < freeBlockEnd; i++)
                    {
                        BuddyBlock otherBlock = BuddyChunks[memoryPageIndex].FreeBlocks[i];
                        uint16_t otherEvenIndex = otherBlock.Index & ~1;
                        if (otherEvenIndex == evenIndex && otherBlock.Layer == currentBlock.Layer)
                        {
                            // Since the last element of our free blocks is our current block,
                            // Pull the second from last block instead
                            BuddyChunks[memoryPageIndex].FreeBlocks[i] = BuddyChunks[memoryPageIndex].FreeBlocks[BuddyChunks[memoryPageIndex].FreeBlockCount - 2];
                            BuddyChunks[memoryPageIndex].FreeBlockCount -= 2;

                            BuddyBlock parentBlock{};
                            parentBlock.Index = evenIndex / 2;
                            parentBlock.Layer = currentBlock.Layer + 1;

                            currentBlock = parentBlock;
                            BuddyChunks[memoryPageIndex].FreeBlocks[BuddyChunks[memoryPageIndex].FreeBlockCount] = parentBlock;
                            BuddyChunks[memoryPageIndex].FreeBlockCount++;

                            if (currentBlock.Layer >= getLayerFromSize(IB::memoryPageSize()))
                            {
                                size_t blockSize = getSizeFromLayer(currentBlock.Layer);
                                ptrdiff_t memoryOffset = blockSize * currentBlock.Index;
                                uintptr_t memoryAddress = reinterpret_cast<uintptr_t>(BuddyChunks[memoryPageIndex].MemoryPages) + memoryOffset;

                                uintptr_t alignedMemoryAddress = memoryAddress / IB::memoryPageSize() * IB::memoryPageSize();
                                uint32_t memoryPageCount = static_cast<uint32_t>(blockSize / IB::memoryPageSize()) + (blockSize % IB::memoryPageSize() != 0 ? 1 : 0);

                                IB::decommitMemoryPages(reinterpret_cast<void *>(alignedMemoryAddress), memoryPageCount);
                            }

                            blockFound = true;
                            break;
                        }
                    }
                }
            }

            // Unlock our block and make sure our changes are visible
            IB::threadStoreFence();
            volatileStore<uint32_t>(&BuddyChunks[memoryPageIndex].Locked, 0);
        }

        return memoryPageIndex != UINT32_MAX;
    }

} // namespace

namespace IB
{
    void *memoryAllocate(size_t size, size_t alignment)
    {
        IB_ASSERT(size != 0, "Can't allocate block of size 0!");
        size_t blockSize = size;
        if (size != alignment && size % alignment != 0)
        {
            if (size > alignment)
            {
                blockSize = ((size / alignment) + 1) * alignment;
            }
            else
            {
                blockSize = alignment;
            }
        }
        // By this point, if blockSize is larger than size, then we have internal fragmentation.
        // TODO: Log internal fragmentation

        void *memory = nullptr;
        if (blockSize <= SmallMemoryBoundary)
        {
            memory = allocateSmallMemory(blockSize);
        }
        else if (blockSize <= MediumMemoryBoundary)
        {
            memory = allocateMediumMemory(blockSize);
        }
        else
        {
            memory = IB::mapLargeMemoryBlock(blockSize);
        }

        return memory;
    }

    void memoryFree(void *memory)
    {
        if (!freeSmallMemory(memory))
        {
            if (!freeMediumMemory(memory))
            {
                IB::unmapLargeMemoryBlock(memory);
            }
        }
    }

} // namespace IB
