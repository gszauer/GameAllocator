#include "mem.h"

Memory::Allocator* Memory::GlobalAllocator;



#ifndef ATLAS_U16
#define ATLAS_U16
typedef unsigned short u16;
static_assert (sizeof(u16) == 2, "u16 should be defined as a 2 byte type");
#endif 

#ifndef ATLAS_U64
#define ATLAS_U64
typedef unsigned long long u64;
static_assert (sizeof(u64) == 8, "u64 should be defined as an 8 byte type");
#endif

// https://stackoverflow.com/questions/2653214/stringification-of-a-macro-value
#define xstr(a) str(a)
#define str(a) #a
#define __LOCATION__ "On line: " xstr(__LINE__) ", in file: " __FILE__

#if _DEBUG
#define assert(cond, msg) Memory::Assert(cond, msg, __LINE__, __FILE__)
#define NotImplementedException() Memory::Assert(false, "Not Implemented", __LINE__, __FILE__)
#else
#define assert1(cond) ;
#define assert(cond, msg) ;
#define NotImplementedException() ;
#endif


namespace Memory {
	static void Assert(bool condition, const char* msg, u32 line, const char* file) {
		char* data = (char*)((void*)0);
		if (condition == false) {
			*data = '\0';
		}
	}

	static inline u32 AllocatorPaddedSize() {
		u32 allocatorHeaderSize = sizeof(Allocator);
		u32 allocatorHeaderPadding = (((allocatorHeaderSize % DefaultAlignment) > 0) ? DefaultAlignment - (allocatorHeaderSize % DefaultAlignment) : 0);
		return allocatorHeaderSize + allocatorHeaderPadding;
	}

	static inline u8* AllocatorPageMask(Allocator* allocator) {
		u64 allocatorHeaderSize = sizeof(Allocator);
		u64 allocatorHeaderPadding = (((allocatorHeaderSize % DefaultAlignment) > 0) ? DefaultAlignment - (allocatorHeaderSize % DefaultAlignment) : 0);
		return ((u8*)allocator) + allocatorHeaderSize + allocatorHeaderPadding;
	}

	static inline u32 AllocatorPageMaskSize(Allocator* allocator) { // This is the number of u8's that make up the AllocatorPageMask array
		u32 allocatorNumberOfPages = allocator->size / PageSize; // 1 page = 4096 bytes, how many are needed
		assert(allocator->size % PageSize == 0, "Allocator size should line up with page size");
		// allocatorNumberOfPages is the number of bits that are required to track memory

		// Pad out to sizeof(32) (if MaskTrackerSize is 32). This is because AllocatorPageMask will often be used as a u32 array
		// and we want to make sure that enough space is reserved.
		u32 allocatorPageArraySize = allocatorNumberOfPages / TrackingUnitSize + (allocatorNumberOfPages % TrackingUnitSize ? 1 : 0);
		assert(allocatorPageArraySize % (TrackingUnitSize / 8) == 0, "allocatorPageArraySize should always be a multiple of 8");
		return allocatorPageArraySize * (TrackingUnitSize / 8); // In bytes, not bits
	}

	static inline u8* AllocatorAllocatable(Allocator* allocator) {
#if MEM_FIRST_FIT
		return (u8*)allocator + allocator->offsetToAllocatable;
#else
		u32 maskSize = AllocatorPageMaskSize(allocator) / (sizeof(u32) / sizeof(u8)); // convert from u8 to u32

		u32 metaDataSizeBytes = AllocatorPaddedSize() + (maskSize * sizeof(u32));
		u32 numberOfMasksUsed = metaDataSizeBytes / PageSize;
		if (metaDataSizeBytes % PageSize != 0) {
			numberOfMasksUsed += 1;
		}
		metaDataSizeBytes = numberOfMasksUsed * PageSize; // This way, allocatable will start on a page boundary

		return (u8*)allocator + metaDataSizeBytes;
#endif
	}

	static inline u32 AllocatorAllocatableSize(Allocator* allocator) {
#if MEM_FIRST_FIT
		return allocator->size - allocator->offsetToAllocatable;
#else
		u32 maskSize = AllocatorPageMaskSize(allocator) / (sizeof(u32) / sizeof(u8)); // convert from u8 to u32

		u32 metaDataSizeBytes = AllocatorPaddedSize() + (maskSize * sizeof(u32));
		u32 numberOfMasksUsed = metaDataSizeBytes / PageSize;
		if (metaDataSizeBytes % PageSize != 0) {
			numberOfMasksUsed += 1;
		}
		metaDataSizeBytes = numberOfMasksUsed * PageSize; // This way, allocatable will start on a page boundary

		return allocator->size - metaDataSizeBytes;
#endif
	}

	// Returns 0 on error. Since the first page is always tracking overhead it's invalid for a range
	static inline u32 FindRange(Allocator* allocator, u32 numPages, u32 searchStartBit) {
		assert(allocator != 0, "");
		assert(numPages != 0, "");

		u32 * mask = (u32*)AllocatorPageMask(allocator);
		u32 numBitsInMask = allocator->size / PageSize;
		assert(allocator->size % PageSize == 0, "Memory::FindRange, the allocators size must be a multiple of Memory::PageSize, otherwise there would be a partial page at the end");
		assert(mask != 0, "");
		assert(numBitsInMask != 0, "");

		u32 startBit = 0;
		u32 numBits = 0;

		for (u32 i = searchStartBit; i < numBitsInMask; ++i) {
			u32 m = i / TrackingUnitSize;
			u32 b = i % TrackingUnitSize;

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

		if (numBits != numPages || startBit == 0) {
			startBit = 0;
			numBits = 0;

			for (u32 i = 0; i < searchStartBit; ++i) {
				u32 m = i / TrackingUnitSize;
				u32 b = i % TrackingUnitSize;

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
		}

		assert(numBits == numPages, "Memory::FindRange Could not find enough memory to fufill request");
		assert(startBit != 0, "Memory::FindRange Could not memory fufill request");
		if (numBits != numPages || startBit == 0 || allocator->size % PageSize != 0) {
			return 0;
		}

		return startBit;
	}

	static inline void SetRange(Allocator* allocator, u32 startBit, u32 bitCount) {
		assert(allocator != 0, "");
		assert(bitCount != 0, "");

		u32* mask = (u32*)AllocatorPageMask(allocator);
		assert(allocator->size % PageSize == 0, "Memory::FindRange, the allocators size must be a multiple of Memory::PageSize, otherwise there would be a partial page at the end");
		assert(mask != 0, "");

#if _DEBUG
		u32 numBitsInMask = allocator->size / PageSize;
		assert(numBitsInMask != 0, "");
#endif

		for (u32 i = startBit; i < startBit + bitCount; ++i) {

			u32 m = i / TrackingUnitSize;
			u32 b = i % TrackingUnitSize;

#if _DEBUG
			assert(i < numBitsInMask, "");
			bool set = mask[m] & (1 << b);
			assert(!set, "");
#endif

			mask[m] |= (1 << b);
		}
	}

	static inline void ClearRange(Allocator* allocator, u32 startBit, u32 bitCount) {
		assert(allocator != 0, "");
		assert(bitCount != 0, "");

		u32* mask = (u32*)AllocatorPageMask(allocator);
		assert(allocator->size % PageSize == 0, "Memory::FindRange, the allocators size must be a multiple of Memory::PageSize, otherwise there would be a partial page at the end");
		assert(mask != 0, "");

#if _DEBUG
		u32 numBitsInMask = allocator->size / PageSize;
		assert(numBitsInMask != 0, "");
#endif

		for (u32 i = startBit; i < startBit + bitCount; ++i) {

			u32 m = i / TrackingUnitSize;
			u32 b = i % TrackingUnitSize;

#if _DEBUG
			assert(i < numBitsInMask, "");
			bool set = mask[m] & (1 << b);
			assert(set, "");
#endif

			mask[m] &= ~(1 << b);
		}
	}

	void* SubAllocate(u32 blockSize, Allocation** freeList, const char* location, Allocator* allocator) {
		// Sub allocators are always aligned with the Default Alignment. If we didn't do this, we would
		// have to keep a matrix of block size and alignment, which is too much overhead.
		
		// The block size is how fineley to chop up the page. The minimum allocateion size should be 64 bytes.
		// This is because the Allocation header is 32 bytes its-self, so there is no valid 32 byte allocation.
		
		// This function will chop the provided page into several blocks. Since the block size is constant, we
		// know that headers will be laid out at a stride of blockSize. There is no additional tracking needed.

		// For example, a 4k page could be made up of 64 byte blocks. 4096 / 64 = 64, so we would have 64 64 byte blocks.
		// If the free list is empty, we fill it by grabbing a page and making 64 free blocks. Each of these blocks
		// will have a size of 0, and be kept in a free list in the allocator. 

		// Then we grab one of the blocks from the free list, and use it / return it's memory.

		// There is no blocks of the requested size available. Reserve 1 page, and carve it up into blocks.
		// Add every new block to the free list.
		if (*freeList == 0) {
			// Find and reserve 1 free page
#if MEM_FIRST_FIT
			const u32 page = FindRange(allocator, 1, 0);
#else
			const u32 page = FindRange(allocator, 1, allocator->scanBit);
			allocator->scanBit = page + 1;
#endif
			SetRange(allocator, page, 1);
			
			// Zero out the pages memory
			u8* mem = AllocatorAllocatable(allocator);
			mem += PageSize * page;
			Set(mem, 0, PageSize, __LOCATION__);

			// Figure out how many blocks fit into this page
			const u32 numBlocks = PageSize / blockSize;
			assert(numBlocks > 0, "");
			assert(numBlocks < 128, ""); // a 32 byte allocation has 128 blocks. The size of PageMemoryAllocationHeader is 32 bytes, so we can't allocate anything smaller (smaller allocations take up more blocks inside the page)

			// For each block in this page, initialize it's header
			for (u32 i = 0; i < numBlocks; ++i) {
				Allocation* alloc = (Allocation*)mem;
				mem += blockSize;

				// Initialize the allocation header
				alloc->prev = 0;
				alloc->size = 0;
				alloc->next = *freeList;
				alloc->alignment = DefaultAlignment;
				alloc->location = location;

				assert(((sizeof(Allocation) % DefaultAlignment) == 0), "Sub allocator is not aligned with default alignment");
				if (*freeList != 0) {
					(*freeList)->prev = alloc;
				}
				*freeList = alloc;
			}
		}
		assert(*freeList != 0, "The free list literally can't be zero here...");

		// At this point we know the free list has some number of blocks in it. 
		// Save a reference to the current header & advance the free list
		// Advance the free list, we're going to be using this one.
		Allocation* block = *freeList;
#if MEM_CLEAR_ON_ALLOC
		Set(block, 0, blockSize, location);
#endif
		if ((*freeList)->next != 0) {
			(*freeList)->next->prev = 0;
		}
		*freeList = (*freeList)->next;

		block->prev = 0;
		block->size = blockSize;

		// Track the sub allocator
		block->next = allocator->active;
		if (allocator->active != 0) {
			allocator->active->prev = block;
		}
		allocator->active = block;

#if 0 & _DEBUG
		u32 freeListcount = 0;
		for (Allocation* iter = *freeList; iter != 0; iter = iter->next, freeListcount += 1);
#endif

		// Memory always follows the header
		return (u8*)block + sizeof(Allocation);
	}

	void SubRelease(void* memory, u32 blockSize, Allocation** freeList, const char* location, Allocator* allocator) {
		// Find the allocation header and mark it as free. Early out on double free to avoid breaking.
		Allocation* header = (Allocation*)((u8*)memory - sizeof(Allocation));
		assert(header->size != 0, "Double Free!"); // Make sure it's not a double free
		if (header->size == 0) {
			return;
		}
		header->size = 0;

		// Now remove from the active list.
		if (header == allocator->active) { // Removing head
			if (allocator->active->next != 0) {
				allocator->active->next->prev = 0;
			}
			allocator->active = allocator->active->next;
		}
		else { // Removing link
			if (header->next != 0) {
				header->next->prev = header->prev;
			}
			if (header->prev != 0) {
				header->prev->next = header->next;
			}
		}

		// Add memory back into the free list
		if (*freeList != 0) {
			assert((*freeList)->prev == 0, "");
			(*freeList)->prev = header;
		}
		header->next = *freeList;
		header->prev = 0;
		*freeList = header;

		// Find the first allocation inside the page
		u64 startPage = (u64)((u8*)header - AllocatorAllocatable(allocator)) / PageSize;
		u8* mem = AllocatorAllocatable(allocator) + startPage * PageSize;

		// Each sub allocator page contains multiple blocks. check if all of the blocks 
		// belonging to a single page are free, if they are, release the page.
		bool releasePage = true;
		
		const u32 numAllocationsPerPage = PageSize / blockSize;
		assert(numAllocationsPerPage >= 1, "");
		for (u32 i = 0; i < numAllocationsPerPage; ++i) {
			Allocation* alloc = (Allocation*)mem;
			if (alloc->size > 0) {
				releasePage = false;
				break;
			}
			mem += blockSize;
		}

		// If appropriate, release entire page
		if (releasePage) {
			// Remove from free list
			mem = AllocatorAllocatable(allocator) + startPage * PageSize;
			for (u32 i = 0; i < numAllocationsPerPage; ++i) {
				Allocation* iter = (Allocation*)mem;
				mem += blockSize;
				assert(iter != 0, "");

				if (*freeList == iter) { // Removing head, advance list
					*freeList = (*freeList)->next;
					if ((*freeList) != 0) {
						(*freeList)->prev = 0;
					}
				}
				else { // Unlink not head
					if (iter->next != 0) {
						iter->next->prev = iter->prev;
					}
					if (iter->prev != 0) {
						iter->prev->next = iter->next;
					}
				}
				iter->prev = iter->next = 0;
			}

			// Clear the tracking bits
			assert(startPage > 0, "");
			ClearRange(allocator, startPage, 1);
		}
	}

	static inline void Zero(Allocation& alloc) {
		alloc.prev = 0;
		alloc.next = 0;
		alloc.location = 0;
		alloc.size = 0;
		alloc.alignment = 0;
	}

	static inline void Zero(Allocator& alloc) {
		alloc.free_64 = 0;
		alloc.free_128 = 0;
		alloc.free_256 = 0;
		alloc.free_512 = 0;
		alloc.free_1024 = 0;
		alloc.free_2048 = 0;
		alloc.active = 0;
		alloc.size = 0;
#if MEM_FIRST_FIT
		alloc.offsetToAllocatable = 0;
#else
		alloc.scanBit = 0;
#endif
	}
}

static_assert (Memory::TrackingUnitSize % 8 == 0, "Memory::MaskTrackerSize must be a multiple of 8 (bits / byte)");

Memory::Allocator* Memory::Initialize(void* memory, u32 bytes) {
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
#if MEM_FIRST_FIT
	allocator->offsetToAllocatable = metaDataSizeBytes;
#else
	allocator->scanBit = 0;
#endif
	SetRange(allocator, 0, numberOfMasksUsed);

#if _DEBUG
	{
		// Fill memory with pattern. Memory is zeroed out on allocation, so it's fine to have
		// junk in here as the initial value. Don't need it in production tough...
		const u8 stamp[] = "-MEMORY-";
		u8* mem = AllocatorAllocatable(allocator);
		u32 size = AllocatorAllocatableSize(allocator);
		for (u32 i = 0; i < size; ++i) {
			mem[i] = stamp[i % 7];
		}
		mem[0] = '>'; // Add a null terminator at the end
		mem[size - 2] = '<'; // Add a null terminator at the end
		mem[size - 1] = '\0'; // Add a null terminator at the end
	}
#endif


	if (ptr % DefaultAlignment != 0 || bytes % PageSize != 0 || bytes / PageSize < 10) {
		return 0;
	}
	
	return (Allocator*)memory;
}

Memory::Allocator* Memory::GetGlobalAllocator() {
	return GlobalAllocator;
}

void Memory::SetGlobalAllocator(Allocator* allocator) {
	GlobalAllocator = allocator;
}

void Memory::Shutdown(Allocator* allocator) {
	assert(allocator != 0, "Memory::Shutdown called without it being initialized");
	u32* mask = (u32*)AllocatorPageMask(allocator);
	u32 maskSize = AllocatorPageMaskSize(allocator) / (sizeof(u32) / sizeof(u8)); // convert from u8 to u32
#if MEM_FIRST_FIT
	assert(allocator->offsetToAllocatable != 0, "Memory::Shutdown, trying to shut down an un-initialized allocator");
#endif
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

void Memory::Copy(void* dest, const void* source, u32 size, const char* location) {
#if 0
	u8* dst = (u8*)dest;
	const u8* src = (const u8*)source;
	for (u32 i = 0; i < size; ++i) {
		dst[i] = src[i];
	}
#endif

	// Align to nearest large boundary
#if ATLAS_64
	if (size % sizeof(u64) != 0) { // Align if needed
#else
	if (size % sizeof(u32) != 0) { // Align if needed
#endif
		u8* dst = (u8*)dest;
		const u8* src = (const u8*)source;
		while (size % sizeof(u32) != 0) {
			*dst = *src;
			dst++;
			src++;
			size -= 1;
		}
		dest = dst;
		source = src;
	}

#if ATLAS_64
	u64 size_64 = size / sizeof(u64);
	u64* dst_64 = (u64*)dest;
	const u64* src_64 = (const u64*)source;
	for (u32 i = 0; i < size_64; ++i) {
		dst_64[i] = src_64[i];
	}
#endif

#if ATLAS_64
	u32 size_32 = (size - size_64 * sizeof(u64)) / sizeof(u32);
	u32* dst_32 = (u32*)(dst_64 + size_64);
	const u32* src_32 = (const u32*)(src_64 + size_64);
#else
	u32 size_32 = size / sizeof(u32);
	u32* dst_32 = (u32*)dest;
	const u32* src_32 = (u32*)source;
#endif
	for (u32 i = 0; i < size_32; ++i) {
		dst_32[i] = src_32[i];
	}

#if ATLAS_64
	u32 size_16 = (size - size_64 * sizeof(u64) - size_32 * sizeof(u32)) / sizeof(u16);
#else
	u32 size_16 = (size - size_32 * sizeof(u32)) / sizeof(u16);
#endif
	u16* dst_16 = (u16*)(dst_32 + size_32);
	const u16* src_16 = (const u16*)(src_32 + size_32);
	for (u32 i = 0; i < size_16; ++i) {
		dst_16[i] = src_16[i];
	}

#if ATLAS_64
	u32 size_8 = (size - size_64 * sizeof(u64) - size_32 * sizeof(u32) - size_16 * sizeof(u16));
#else
	u32 size_8 = (size - size_32 * sizeof(u32) - size_16 * sizeof(u16));
#endif
	u8* dst_8 = (u8*)(dst_16 + size_16);
	const u8* src_8 = (const u8*)(src_16 + size_16);
	for (u32 i = 0; i < size_8; ++i) {
		dst_8[i] = src_8[i];
	}

#if ATLAS_64
	assert(size_64 * sizeof(u64) + size_32 * sizeof(u32) + size_16 * sizeof(u16) + size_8 == size, "Number of pages not adding up");
#else
	assert(size_32 * sizeof(u32) + size_16 * sizeof(u16) + size_8 == size, "Number of pages not adding up");
#endif
}


void Memory::Set(void* memory, u8 value, u32 size, const char* location) {
#if 0
	u8* mem = (u8*)memory;
	for (u32 i = 0; i < size; ++i) {
		mem[i] = value;
	}
#endif

	// Above is the naive implementation, and below is a bit more optimized one
	// This could still be optimized further by going wider, it's fast enough for me

#if ATLAS_64
	if (size % sizeof(u64) != 0) { // Align if needed
#else
	if (size % sizeof(u32) != 0) { // Align if needed
#endif
		u8* mem = (u8*)memory;
		while (size % sizeof(u32) != 0) {
			*mem = value;
			mem++;
			size -= 1;
		}
		memory = mem;
	}

#if ATLAS_64
	u64 size_64 = size / sizeof(u64);
	u64* ptr_64 = (u64*)memory;
	u32 v32 = (((u32)value) << 8) | (((u32)value) << 16) | (((u32)value) << 24) | ((u32)value);
	u64 val_64 = (((u64)v32) << 32) | ((u64)v32);
	for (u32 i = 0; i < size_64; ++i) {
		ptr_64[i] = val_64;
	}
#endif

#if ATLAS_64
	u32 size_32 = (size - size_64 * sizeof(u64)) / sizeof(u32);
	u32* ptr_32 = (u32*)(ptr_64 + size_64);;
#else
	u32 size_32 = size / sizeof(u32);
	u32* ptr_32 = (u32*)memory;
#endif
	u32 val_32 = (((u32)value) << 8) | (((u32)value) << 16) | (((u32)value) << 24) | ((u32)value);
	for (u32 i = 0; i < size_32; ++i) {
		ptr_32[i] = val_32;
	}
	
#if ATLAS_64
	u32 size_16 = (size - size_64 * sizeof(u64) - size_32 * sizeof(u32)) / sizeof(u16);
#else
	u32 size_16 = (size - size_32 * sizeof(u32)) / sizeof(u16);
#endif
	u16* ptr_16 = (u16*)(ptr_32 + size_32);
	u32 val_16 = (((u16)value) << 8) | ((u16)value);
	for (u32 i = 0; i < size_16; ++i) {
		ptr_16[i] = val_16;
	}

#if ATLAS_64
	u32 size_8 = (size - size_64 * sizeof(u64) - size_32 * sizeof(u32) - size_16 * sizeof(u16));
#else
	u32 size_8 = (size - size_32 * sizeof(u32) - size_16 * sizeof(u16));
#endif
	u8* ptr_8 = (u8*)(ptr_16 + size_16);
	for (u32 i = 0; i < size_8; ++i) {
		ptr_8[i] = value;
	}

#if ATLAS_64
	assert(size_64 * sizeof(u64) + size_32 * sizeof(u32) + size_16 * sizeof(u16) + size_8 == size, "Number of pages not adding up");
#else
	assert(size_32 * sizeof(u32) + size_16 * sizeof(u16) + size_8 == size, "Number of pages not adding up");
#endif
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
	
	if (alignment == DefaultAlignment) {
		if (allocationSize <= 64) {
			return SubAllocate(64, &allocator->free_64, location, allocator);
		}
		else if (allocationSize <= 128) {
			return SubAllocate(128, &allocator->free_128, location, allocator);
		}
		else if (allocationSize <= 256) {
			return SubAllocate(256, &allocator->free_256, location, allocator);
		}
		else if (allocationSize <= 512) {
			return SubAllocate(512, &allocator->free_512, location, allocator);
		}
		else if (allocationSize <= 1024) {
			return SubAllocate(1024, &allocator->free_1024, location, allocator);
		}
		else if (allocationSize <= 2048) {
			return SubAllocate(2048, &allocator->free_2048, location, allocator);
		}
	}

	// Find enough memory to allocate
#if MEM_FIRST_FIT
	u32 firstPage = FindRange(allocator, numPagesRequested, 0);
#else
	u32 firstPage = FindRange(allocator, numPagesRequested, allocator->scanBit);
	allocator->scanBit = firstPage + numPagesRequested; // TODO: tes thits?
#endif
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
	assert(allocation != allocator->active, ""); // Should be impossible, but we could have bugs...
	if (allocator->active != 0) {
		allocation->next = allocator->active;
		allocator->active->prev = allocation;
	}
	allocator->active = allocation;

	// Return memory
	mem += sizeof(Allocation);
#if MEM_CLEAR_ON_ALLOC
	Set(mem, 0, bytes, location);
#endif
	return mem;
}

void Memory::Release(void* memory, const char* location, Allocator* allocator) {
	assert(memory != 0, "Memory:Free can't free a null pointer");

	if (allocator == 0) {
		allocator = GlobalAllocator;
		assert(allocator != 0, "Memory::Free couldn't assign global allocator");
	}

	// Retrieve allocation information from header. The allocation header always
	// preceeds the allocation.
	u8* mem = (u8*)memory;
	mem -= sizeof(Allocation);
	Allocation* allocation = (Allocation*)mem;
	u32 alignment = allocation->alignment;
	u32 paddedAllocationSize = allocation->size;
	assert(allocation->size != 0, "Memory::Free, double free");

	// Figure out how much to step back to the start of the allocation page & 
	// Step back to the start of the allocation
	u32 allocationHeaderPadding = sizeof(Allocation) % alignment > 0 ? alignment - sizeof(Allocation) % alignment : 0;
	mem -= allocationHeaderPadding;

	if (alignment == DefaultAlignment) {
		if (paddedAllocationSize <= 64) {
			SubRelease(memory, 64, &allocator->free_64, location, allocator);
			return;
		}
		else if (paddedAllocationSize <= 128) {
			SubRelease(memory, 128, &allocator->free_128, location, allocator);
			return;
		}
		else if (paddedAllocationSize <= 256) {
			SubRelease(memory, 256, &allocator->free_256, location, allocator);
			return;
		}
		else if (paddedAllocationSize <= 512) {
			SubRelease(memory, 512, &allocator->free_512, location, allocator);
			return;
		}
		else if (paddedAllocationSize <= 1024) {
			SubRelease(memory, 1024, &allocator->free_1024, location, allocator);
			return;
		}
		else if (paddedAllocationSize <= 2048) {
			SubRelease(memory, 2048, &allocator->free_2048, location, allocator);
			return;
		}
	}

	// Clear the bits that where tracking this memory
	u8* firstMemory = (u8*)allocator;
	u64 address = (u64)(mem - firstMemory);
	u32 firstPage = address / PageSize;
	u32 numPages = paddedAllocationSize / PageSize + (paddedAllocationSize % PageSize ? 1 : 0);
	ClearRange(allocator, firstPage, numPages);

	// Unlink tracking
	if (allocation->next != 0) {
		allocation->next->prev = allocation->prev;
	}
	if (allocation->prev != 0) {
		allocation->prev->next = allocation->next;
	}
	if (allocation == allocator->active) {
		assert(allocation->prev == 0, "");
		allocator->active = allocation->next;
	}

	// Set the size to 0, to indicate that this header has been free-d
	allocation->size = 0;
}

void* Memory::AllocateContigous(u32 num_elems, u32 elem_size, u32 alignment, const char* location, Allocator* allocator) {
	if (allocator == 0) {
		allocator = GlobalAllocator;
	}
	void* mem = Allocate(num_elems * elem_size, alignment, location, allocator);
	if (mem == 0) {
		return 0;
	}
	Set(mem, num_elems * elem_size, 0, location);

	return mem;
}

void* Memory::ReAllocate(void* mem, u32 newSize, u32 newAlignment, const char* location, Allocator* allocator) {
	if (allocator == 0) {
		allocator = GlobalAllocator;
	}

	if (newSize == 0 && mem != 0) {
		Release(mem, location, allocator);
		return 0;
	}

	void* newMem = Allocate(newSize, newAlignment, location, allocator);
	u32 oldMemSize = 0;
	{
		u8* memory = (u8*)mem;
		Allocation* header = (Allocation*)(memory - sizeof(Allocation));
		oldMemSize = header->size - sizeof(Allocation);
		u32 allocationHeaderPadding = sizeof(Allocation) % header->alignment > 0 ? header->alignment - sizeof(Allocation) % header->alignment : 0;
		oldMemSize -= allocationHeaderPadding;
	}

	if (mem != 0 && newMem != 0) {
		u32 copySize = newSize;
		if (newSize > oldMemSize) {
			copySize = oldMemSize;
		}

		Copy(newMem, mem, copySize, location);
		Release(mem, location, allocator);
	}

	return newMem;
}

/*
#undef malloc
#undef free
#undef memset
#undef memcpy
#undef calloc
#undef realloc

void* malloc(u32 bytes) {
	return Memory::Allocate(bytes, Memory::DefaultAlignment, "internal - malloc", Memory::GlobalAllocator);
}

void free(void* data) {
	return Memory::Release(data, "internal - free", Memory::GlobalAllocator);
}

void memset(void* mem, u8 value, u32 size) {
	Memory::Set(mem, value, size, "internal - memset");
}

void memcpy(void* dest, const void* src, u32 size) {
	Memory::Copy(dest, src, size, "internal - memcpy");
}

void* calloc(u32 count, u32 size) {
	return Memory::AllocateContigous(count, size, Memory::DefaultAlignment, "internal - calloc", Memory::GlobalAllocator);
}

void* realloc(void* mem, u32 size) {
	return Memory::ReAllocate(mem, size, Memory::DefaultAlignment, "internal - realloc", Memory::GlobalAllocator);
}

#define malloc(bytes) Memory::Allocate(bytes, Memory::DefaultAlignment, __LOCATION__, Memory::GlobalAllocator)
#define free(data) Memory::Release(data, __LOCATION__, Memory::GlobalAllocator)
#define memset(mem, val, size) Memory::Set(mem, val, size, __LOCATION__)
#define memcpy(dest, src, size) Memory::Copy(dest, src, size, __LOCATION__)
#define calloc(numelem, elemsize) Memory::AllocateContigous(numelem, elemsize, Memory::DefaultAlignment, __LOCATION__, Memory::GlobalAllocator)
#define realloc(mem, size) Memory::ReAllocate(mem, size, Memory::DefaultAlignment, __LOCATION__, Memory::GlobalAllocator)
*/