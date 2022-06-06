#include "mem.h"

Memory::Allocator* Memory::GlobalAllocator;

// https://stackoverflow.com/questions/2653214/stringification-of-a-macro-value
#define xstr(a) str(a)
#define str(a) #a
#define __LOCATION__ "On line: " xstr(__LINE__) ", in file: " __FILE__


#define assert1(cond) Memory::Assert(cond, "assert1", __LINE__, __FILE__)
#define assert(cond, msg) Memory::Assert(cond, msg, __LINE__, __FILE__)
#define NotImplementedException() Memory::NotImplemented(__LINE__, __FILE__)

namespace Memory {
	static void Assert(bool condition, const char* msg, u32 line, const char* file) {
		char* data = (char*)((void*)0);
		if (condition == false) {
			*data = '\0';
		}
	}

	static void NotImplemented(u32 line = 0, const char* file = 0) {
		char* data = (char*)((void*)0);
		*data = '\0';
	}

	static u32 AllocatorPaddedSize() {
		u32 allocatorHeaderSize = sizeof(Allocator);
		u32 allocatorHeaderPadding = (((allocatorHeaderSize % DefaultAlignment) > 0) ? DefaultAlignment - (allocatorHeaderSize % DefaultAlignment) : 0);
		return allocatorHeaderSize + allocatorHeaderPadding;
	}

	static u8* AllocatorPageMask(Allocator* allocator) {
		u64 allocatorHeaderSize = sizeof(Allocator);
		u64 allocatorHeaderPadding = (((allocatorHeaderSize % DefaultAlignment) > 0) ? DefaultAlignment - (allocatorHeaderSize % DefaultAlignment) : 0);
		return ((u8*)allocator) + allocatorHeaderSize + allocatorHeaderPadding;
	}

	static u32 AllocatorPageMaskSize(Allocator* allocator) { // This is the number of u8's that make up the AllocatorPageMask array
		u32 allocatorNumberOfPages = allocator->size / PageSize; // 1 page = 4096 bytes, how many are needed
		assert(allocator->size % PageSize == 0, "Allocator size should line up with page size");
		// allocatorNumberOfPages is the number of bits that are required to track memory

		// Pad out to sizeof(32) (if MaskTrackerSize is 32). This is because AllocatorPageMask will often be used as a u32 array
		// and we want to make sure that enough space is reserved.
		u32 allocatorPageArraySize = allocatorNumberOfPages / MaskTrackerSize + (allocatorNumberOfPages % MaskTrackerSize ? 1 : 0);
		assert(allocatorPageArraySize % 8 == 0, "allocatorPageArraySize should always be a multiple of 8");
		return allocatorPageArraySize / 8; // In bytes, not bits
	}

	static u8* AllocatorAllocatable(Allocator* allocator) {
		return (u8*)allocator + allocator->offsetToAllocatable;
	}

	static u32 AllocatorAllocatableSize(Allocator* allocator) {
		return allocator->size - allocator->offsetToAllocatable;
	}

	// Returns 0 on error. Since the first page is always tracking overhead
	// it's invalid for a range
	static u32 FindRange(Allocator* allocator, u32 numPages) {
		assert1(allocator != 0);
		assert1(numPages != 0);

		u32 * mask = (u32*)AllocatorPageMask(allocator);
		u32 numBitsInMask = allocator->size / PageSize;
		assert1(allocator->size % PageSize == 0, "Memory::FindRange, the allocators size must be a multiple of Memory::PageSize, otherwise there would be a partial page at the end");
		assert1(mask != 0);
		assert1(numBitsInMask != 0);

		u32 startBit = 0;
		u32 numBits = 0;

		for (u32 i = 0; i < numBitsInMask; ++i) {
			u32 m = i / MaskTrackerSize;
			u32 b = i % MaskTrackerSize;

			bool set = mask[m] & (1 << b);

			if (!set) {
				if (startBit == 0) {
					startBit = i;
					numBits = 1;
				}
				else {
					numBits++;
				}
			}
			else {
				startBit = 0;
				numBits = 0;
			}

			if (numBits == numPages) {
				break;
			}
		}

		assert(numBits == numPages, "Memory::FindRange Could not find enough memory to fufill request");
		assert(startBit != 0, "Memory::FindRange Could not memory fufill request");
		if (numBits != numPages || startBit == 0 || allocator->size % PageSize != 0) {
			return 0;
		}

		return startBit;
	}

	static void SetRange(Allocator* allocator, u32 startBit, u32 bitCount) {
		assert1(allocator != 0);
		assert1(bitCount != 0);

		u32* mask = (u32*)AllocatorPageMask(allocator);
		assert(allocator->size % PageSize == 0, "Memory::FindRange, the allocators size must be a multiple of Memory::PageSize, otherwise there would be a partial page at the end");
		assert1(mask != 0);

#if _DEBUG
		u32 numBitsInMask = allocator->size / PageSize;
		assert1(numBitsInMask != 0);
#endif

		for (u32 i = startBit; i < startBit + bitCount; ++i) {

			u32 m = i / MaskTrackerSize;
			u32 b = i % MaskTrackerSize;

#if _DEBUG
			assert1(i < numBitsInMask);
			bool set = mask[m] & (1 << b);
			assert1(!set);
#endif

			mask[m] |= (1 << b);
		}
	}

	static void ClearRange(Allocator* allocator, u32 startBit, u32 bitCount) {
		assert1(allocator != 0);
		assert1(bitCount != 0);

		u32* mask = (u32*)AllocatorPageMask(allocator);
		assert(allocator->size % PageSize == 0, "Memory::FindRange, the allocators size must be a multiple of Memory::PageSize, otherwise there would be a partial page at the end");
		assert1(mask != 0);

#if _DEBUG
		u32 numBitsInMask = allocator->size / PageSize;
		assert1(numBitsInMask != 0);
#endif

		for (u32 i = startBit; i < startBit + bitCount; ++i) {

			u32 m = i / MaskTrackerSize;
			u32 b = i % MaskTrackerSize;

#if _DEBUG
			assert1(i < numBitsInMask);
			bool set = mask[m] & (1 << b);
			assert1(set);
#endif

			mask[m] &= ~(1 << b);
		}
	}

	void* SubAllocate(u32 bytes, const char* location, Allocator* allocator, Allocation** freeList);
	void SubFree(void* memory, const char* location, Allocator* allocator, Allocation** freeList);

	void* SuperAllocate(u32 bytes, const char* location, Allocator* allocator, Allocation** freeList);
	void SuperFree(void* memory, const char* location, Allocator* allocator, Allocation** freeList);

	static void Zero(Allocation& alloc) {
		alloc.prev = 0;
		alloc.next = 0;
		alloc.location = 0;
		alloc.size = 0;
		alloc.alignment = 0;
	}

	static void Zero(Allocator& alloc) {
		alloc.free_64 = 0;
		alloc.free_128 = 0;
		alloc.free_256 = 0;
		alloc.free_512 = 0;
		alloc.free_1024 = 0;
		alloc.free_2048 = 0;
		alloc.active = 0;
		alloc.size = 0;
		alloc.offsetToAllocatable = 0;
	}
}

static_assert (Memory::MaskTrackerSize % 8 == 0, "Memory::MaskTrackerSize must be a multiple of 8 (bits / byte)");

void Memory::Initialize(void* memory, u32 bytes) {
	// First, make sure that the memory being passed in is aligned well
	u64 ptr = (u64)((const void*)memory);
	assert(ptr % DefaultAlignment == 0, "Memory::Initialize, the memory being managed start aligned to Memory::DefaultAlignment");
	assert(bytes % PageSize == 0, "Memory::Initialize, the size of the memory being managed must be aligned to Memory::PageSize");
	assert(bytes / PageSize >= 10, "Memory::Initialize, minimum memory size is 10 pages, page size is Memory::PageSize");

	// Set up the global allocator
	Allocator* allocator = (Allocator*)memory;
	Zero(*allocator);
	allocator->size = bytes;

	// Set up the mask that will track our allocation data
	u32* mask = (u32*)AllocatorPageMask(allocator);
	u32 maskSize = AllocatorPageMaskSize(allocator) / (sizeof(u32) / sizeof(u8)); // convert from u8 to u32
	Set(mask, 0, sizeof(u32) * maskSize, __LOCATION__);
	
	// Find how many pages the meta data for the header + allocation mask will take up. 
	// Store the offset to first allocatable, 
	u32 metaDataSizeBytes = AllocatorPaddedSize() + (maskSize * sizeof(u32));
	u32 numberOfMasksUsed = metaDataSizeBytes / PageSize;
	if (metaDataSizeBytes % PageSize != 0) {
		numberOfMasksUsed += 1;
	}
	metaDataSizeBytes = numberOfMasksUsed * PageSize; // This way, allocatable will start on a page boundary
	allocator->offsetToAllocatable = metaDataSizeBytes;
	SetRange(allocator, 0, numberOfMasksUsed);

#if _DEBUG
	{
		// Fill memory with pattern. Memory is zeroed out on allocation, so it's fine to have
		// junk in here as the initial value. Don't need it in production tough...
		const u8 stamp[] = "-MEMORY-";
		u8* mem = (u8*)allocator + allocator->offsetToAllocatable;
		u32 size = allocator->size - allocator->offsetToAllocatable;
		for (u32 i = 0; i < size; ++i) {
			mem[i] = stamp[i % 7];
		}
		mem[0] = '>'; // Add a null terminator at the end
		mem[size - 2] = '<'; // Add a null terminator at the end
		mem[size - 1] = '\0'; // Add a null terminator at the end
	}
#endif


	if (ptr % DefaultAlignment != 0 || bytes % PageSize != 0 || bytes / PageSize < 10) {
		GlobalAllocator = 0; // Not initialized
		assert1(false); // Break in debug mode
	}
	else {
		GlobalAllocator = (Allocator*)memory;
	}
}

void Memory::Shutdown() {
	assert(GlobalAllocator != 0, "Memory::Shutdown called without it being initialized");
	Allocator* allocator = GlobalAllocator;
	u32* mask = (u32*)AllocatorPageMask(allocator);
	u32 maskSize = AllocatorPageMaskSize(allocator) / (sizeof(u32) / sizeof(u8)); // convert from u8 to u32
	assert(allocator->offsetToAllocatable != 0, "Memory::Shutdown, trying to shut down an un-initialized allocator");
	assert(allocator->size > 0, "Memory::Shutdown, trying to shut down an un-initialized allocator");

	// Unset tracking bits
	u32 metaDataSizeBytes = AllocatorPaddedSize() + (maskSize * sizeof(u32));
	u32 numberOfMasksUsed = metaDataSizeBytes / PageSize;
	if (metaDataSizeBytes % PageSize != 0) {
		numberOfMasksUsed += 1;
	}
	metaDataSizeBytes = numberOfMasksUsed * PageSize;
	ClearRange(allocator, 0, numberOfMasksUsed);

	assert(allocator->active == 0, "There are active allocations in Memory::Shutdown, leaking memory");
	assert(allocator->free_64 == 0, "Free list is not empty in Memory::Shutdown, leaking memory");
	assert(allocator->free_128 == 0, "Free list is not empty in Memory::Shutdown, leaking memory");
	assert(allocator->free_256 == 0, "Free list is not empty in Memory::Shutdown, leaking memory");
	assert(allocator->free_512 == 0, "Free list is not empty in Memory::Shutdown, leaking memory");
	assert(allocator->free_1024 == 0, "Free list is not empty in Memory::Shutdown, leaking memory");
	assert(allocator->free_2048 == 0, "Free list is not empty in Memory::Shutdown, leaking memory");

#if _DEBUG
	// In debug mode only, we will scan the entire mask to make sure all memory has been free-d
	for (u32 i = 0; i < maskSize; ++i) {
		assert(mask[i] == 0, "Page tracking unit isn't empty in Memory::Shutdown, leaking memory.");
	}
#endif
}

void Memory::Set(void* memory, u8 value, u32 size, const char* location) {
	// TODO: Optimize for 64 bit memset!
	u8* mem = (u8*)memory;
	for (u32 i = 0; i < size; ++i) {
		mem[i] = value;
	}
}

void* Memory::Allocate(u32 bytes, u32 alignment, const char* location, Allocator* allocator) {
	assert(bytes != 0, "Memory::Allocate can't allocate 0 bytes");
	assert(alignment != 0, "Memory::Allocate alignment can't be 0 bytes");
	if (bytes == 0 || alignment == 0) {
		return 0;
	}
	if (allocator == 0) {
		allocator = GlobalAllocator;
		assert(allocator != 0, "Memory::Allocate couldn't assign global allocator");
		if (allocator == 0) {
			return 0;
		}
	}

	// Add padding to compensate for alignment
	u32 allocationSize = bytes; // Add enough space to pad out for alignment
	allocationSize += (bytes % alignment > 0) ? alignment - (bytes % alignment) : 0;
	
	// Align the header so the begenning is aligned. This will only run if the 
	// alignment size is greater than the allocation header size (32 bytes)
	u32 allocationHeaderPadding = sizeof(Allocation) % alignment > 0 ? alignment - sizeof(Allocation) % alignment : 0;
	u32 allocationHeaderTotalSize = sizeof(Allocation) + allocationHeaderPadding;

	// Add the header size to our allocation size
	allocationSize += allocationHeaderTotalSize;

	// Figure out how many pages are going to be needed to hold that much memory
	u32 numPagesRequested = allocationSize / PageSize + (allocationSize % PageSize ? 1 : 0);
	assert(numPagesRequested > 0, "Memory::Allocate needs to request at least 1 page");
	
	// TODO: Accelerated allocation based on allocationSize

	// Find enough memory to allocate
	u32 firstPage = FindRange(allocator, numPagesRequested);
	assert(firstPage != 0, "Memory::Allocate failed to find enough pages to fufill allocation");
	SetRange(allocator, firstPage, numPagesRequested);

	if (firstPage == 0 || allocator->size % PageSize != 0) {
		return 0; // Fail this allocation in release mode
	}
	
	// Fill out header
	u8* mem = (u8*)allocator + firstPage * PageSize;
	mem += allocationHeaderPadding;
	Allocation* allocation = (Allocation*)mem;

	allocation->alignment = alignment;
	allocation->size = allocationSize;
	allocation->location = location;
	allocation->prev = 0;
	allocation->next = 0;

	// Track allocated memory
	assert1(allocation != allocator->active); // Should be impossible, but we could have bugs...
	if (allocator->active != 0) {
		allocation->next = allocator->active;
		allocator->active->prev = allocation;
	}
	allocator->active = allocation;

	// Return memory
	mem += sizeof(Allocation);
	return mem;
}

void Memory::Free(void* memory, const char* location, Allocator* allocator) {
	assert(memory != 0, "Memory:Free can't free a null pointer");

	if (allocator == 0) {
		allocator = GlobalAllocator;
		assert(allocator != 0, "Memory::Free couldn't assign global allocator");
	}

	u8* mem = (u8*)memory;
	mem -= sizeof(Allocation);
	Allocation* allocation = (Allocation*)mem;
	u32 alignment = allocation->alignment;
	u32 size = allocation->size;
	assert(allocation->size != 0, "Memory::Free, double free");

	// Figure out how much to step back to the start of the allocation page & 
	// Step back to the start of the allocation
	u32 allocationHeaderPadding = sizeof(Allocation) % alignment > 0 ? alignment - sizeof(Allocation) % alignment : 0;
	mem -= allocationHeaderPadding;

	// TODO: Early out based on accelerators

	// Clear the bits that where tracking this memory
	u8* firstMemory = (u8*)allocator;
	u64 address = (u64)(mem - firstMemory);
	u32 firstPage = address / PageSize;
	u32 numPages = size / PageSize + (size % PageSize ? 1 : 0);
	ClearRange(allocator, firstPage, numPages);

	// Unlink tracking
	if (allocation->next != 0) {
		allocation->next->prev = allocation->prev;
	}
	if (allocation->prev != 0) {
		allocation->prev->next = allocation->next;
	}
	if (allocation == allocator->active) {
		assert1(allocation->prev == 0);
		allocator->active = allocation->next;
	}

	// Set the size to 0, to indicate that this header has been free-d
	allocation->size = 0;
}