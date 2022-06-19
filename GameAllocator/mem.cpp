#include "mem.h"

Memory::Allocator* Memory::GlobalAllocator = 0;

#ifndef ATLAS_U16
#define ATLAS_U16
typedef unsigned short u16;
static_assert (sizeof(u16) == 2, "u16 should be defined as a 2 byte type");
#endif 

#if _DEBUG
#define assert(cond, msg) Memory::Assert(cond, msg, __LINE__, __FILE__)
#else
#define assert(cond, msg) ;
#endif
#define NotImplementedException() (*(char*)((void*)0) = '\0');

namespace Memory {
	static void Assert(bool condition, const char* msg, u32 line, const char* file) {
		char* data = (char*)((void*)0);
		if (condition == false) {
			*data = '\0';
		}
	}

	static inline u32 AllocatorPaddedSize() {
		const u32 allocatorHeaderSize = sizeof(Allocator);
		const u32 allocatorHeaderPadding = (((allocatorHeaderSize % DefaultAlignment) > 0) ? DefaultAlignment - (allocatorHeaderSize % DefaultAlignment) : 0);
		return allocatorHeaderSize + allocatorHeaderPadding;
	}

	static inline u8* AllocatorPageMask(Allocator* allocator) {
		const u64 allocatorHeaderSize = sizeof(Allocator);
		const u64 allocatorHeaderPadding = (((allocatorHeaderSize % DefaultAlignment) > 0) ? DefaultAlignment - (allocatorHeaderSize % DefaultAlignment) : 0);
		return ((u8*)allocator) + allocatorHeaderSize + allocatorHeaderPadding;
	}

	static inline u32 AllocatorPageMaskSize(Allocator* allocator) { // This is the number of u8's that make up the AllocatorPageMask array
		const u32 allocatorNumberOfPages = allocator->size / PageSize; // 1 page = 4096 bytes, how many are needed
		assert(allocator->size % PageSize == 0, "Allocator size should line up with page size");
		// allocatorNumberOfPages is the number of bits that are required to track memory

		// Pad out to sizeof(32) (if MaskTrackerSize is 32). This is because AllocatorPageMask will often be used as a u32 array
		// and we want to make sure that enough space is reserved.
		const u32 allocatorPageArraySize = allocatorNumberOfPages / TrackingUnitSize + (allocatorNumberOfPages % TrackingUnitSize ? 1 : 0);
		assert(allocatorPageArraySize % (TrackingUnitSize / 8) == 0, "allocatorPageArraySize should always be a multiple of 8");
		return allocatorPageArraySize * (TrackingUnitSize / 8); // In bytes, not bits
	}

	static inline u8* AllocatorAllocatable(Allocator* allocator) {
#if 1
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
#if 1
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
		u32 numBitsInMask = AllocatorPageMaskSize(allocator) * 8;
		u32 numElementsInMask = AllocatorPageMaskSize(allocator) / (TrackingUnitSize / 8);
		assert(allocator->size / PageSize == numBitsInMask, "< this was the old way of calculating numBitsInMask. I like the above new way better");
		assert(allocator->size % PageSize == 0, "Memory::FindRange, the allocators size must be a multiple of Memory::PageSize, otherwise there would be a partial page at the end");
		assert(mask != 0, "");
		assert(numBitsInMask != 0, "");

		u32 startBit = 0;
		u32 numBits = 0;

		for (u32 i = searchStartBit; i < numBitsInMask; ++i) {
			u32 m = i / TrackingUnitSize;
			u32 b = i % TrackingUnitSize;

			assert(m < numElementsInMask, "indexing mask out of range");
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
			assert(false, "");
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
		u32 numBitsInMask = AllocatorPageMaskSize(allocator) * 8;
		assert(allocator->size / PageSize == numBitsInMask, "< this was the old way of calculating numBitsInMask. I like the above new way better");
		assert(numBitsInMask != 0, "");
#endif
		u32 numElementsInMask = AllocatorPageMaskSize(allocator) / (TrackingUnitSize / 8);

		for (u32 i = startBit; i < startBit + bitCount; ++i) {

			u32 m = i / TrackingUnitSize;
			u32 b = i % TrackingUnitSize;

			assert(m < numElementsInMask, "indexing mask out of range");
#if _DEBUG
			assert(i < numBitsInMask, "");
			bool set = mask[m] & (1 << b);
			assert(!set, "");
#endif

			mask[m] |= (1 << b);
		}

		allocator->numPagesUsed += bitCount;
	}

	static inline void ClearRange(Allocator* allocator, u32 startBit, u32 bitCount) {
		assert(allocator != 0, "");
		assert(bitCount != 0, "");

		u32* mask = (u32*)AllocatorPageMask(allocator);
		assert(allocator->size % PageSize == 0, "Memory::FindRange, the allocators size must be a multiple of Memory::PageSize, otherwise there would be a partial page at the end");
		assert(mask != 0, "");

#if _DEBUG
		u32 numBitsInMask = AllocatorPageMaskSize(allocator) * 8;
		assert(allocator->size / PageSize == numBitsInMask, "< this was the old way of calculating numBitsInMask. I like the above new way better");
		assert(numBitsInMask != 0, "");
#endif

		u32 numElementsInMask = AllocatorPageMaskSize(allocator) / (TrackingUnitSize / 8);

		for (u32 i = startBit; i < startBit + bitCount; ++i) {

			u32 m = i / TrackingUnitSize;
			u32 b = i % TrackingUnitSize;

			assert(m < numElementsInMask, "indexing mask out of range");

#if _DEBUG
			assert(i < numBitsInMask, "");
			bool set = mask[m] & (1 << b);
			assert(set, "");
#endif

			mask[m] &= ~(1 << b);
		}

		assert(allocator->numPagesUsed != 0, "");
		assert(allocator->numPagesUsed >= bitCount != 0, "underflow");
		allocator->numPagesUsed -= bitCount;
	}

#if MEM_USE_SUBALLOCATORS
	void* SubAllocate(u32 requestedBytes, u32 blockSize, Allocation** freeList, const char* location, Allocator* allocator) {
		assert(blockSize < PageSize, "Block size must be less than page size");
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
		bool grabNewPage = *freeList == 0;
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
			u8* mem = (u8*)allocator + PageSize * page;
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
				assert(alloc->alignment != 0, "SubAllocate, alignment can't be 0");
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
		Set((u8*)block + sizeof(Allocation), 0, blockSize - sizeof(Allocation), location);
#endif
		if ((*freeList)->next != 0) {
			(*freeList)->next->prev = 0;
		}
		*freeList = (*freeList)->next;

		block->prev = 0;
		block->size = requestedBytes;
		block->location = location;
		block->alignment = DefaultAlignment;

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

		if (allocator->allocateCallback != 0) {
#if ATLAS_64
			u64 firstPage = (u64)(((u8*)block - (u8*)allocator) / PageSize);
#elif ATLAS_32
			u32 firstPage = (u32)(((u8*)block - (u8*)allocator) / PageSize);
#endif
			allocator->allocateCallback(allocator, block, requestedBytes, blockSize, firstPage, grabNewPage? 1 : 0);
		}

		// Memory always follows the header
		return (u8*)block + sizeof(Allocation);
	}
#endif

#if MEM_USE_SUBALLOCATORS
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
				assert(allocator->active->next->prev == allocator->active, "");
				allocator->active->next->prev = 0;
			}
			allocator->active = allocator->active->next;
		}
		else { // Removing link
			if (header->next != 0) {
				assert(header->next->prev == header, "");
				header->next->prev = header->prev;
			}
			if (header->prev != 0) {
				assert(header->prev->next == header, "");
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
#if _DEBUG
		assert(header->alignment != 0, "");
		header->location = "SubRelease released this block";
#endif
		*freeList = header;

		// Find the first allocation inside the page
#if ATLAS_64
		u64 startPage = (u64)((u8*)header - (u8*)allocator) / PageSize;
#elif ATLAS_32
		u32 startPage = (u32)((u8*)header - (u8*)allocator) / PageSize;
#endif
		u8* mem =(u8*)allocator + startPage * PageSize;

		// Each sub allocator page contains multiple blocks. check if all of the blocks 
		// belonging to a single page are free, if they are, release the page.
		bool releasePage = true;
		
		const u32 numAllocationsPerPage = PageSize / blockSize;
		assert(numAllocationsPerPage >= 1, "");
		for (u32 i = 0; i < numAllocationsPerPage; ++i) {
			Allocation* alloc = (Allocation*)mem;
			assert(alloc->alignment != 0, "");
			if (alloc->size > 0) {
				releasePage = false;
				break;
			}
			mem += blockSize;
		}

		// If appropriate, release entire page
		if (releasePage) {
			// Remove from free list
			mem = (u8*)allocator + startPage * PageSize;
			for (u32 i = 0; i < numAllocationsPerPage; ++i) {
				Allocation* iter = (Allocation*)mem;
				mem += blockSize;
				assert(iter != 0, "");

				if (*freeList == iter) { // Removing head, advance list
					*freeList = (*freeList)->next;
					if ((*freeList) != 0) {
						assert((*freeList)->prev == iter, "");
						(*freeList)->prev = 0;
					}
				}
				else { // Unlink not head
					if (iter->next != 0) {
						assert(iter->next->prev == iter, "Sub-release, unexpected active link...");
						iter->next->prev = iter->prev;
					}
					if (iter->prev != 0) {
						assert(iter->prev->next == iter, "Sub-release, unexpected active link...");
						iter->prev->next = iter->next;
					}
				}
				iter->prev = 0;
				iter->next = 0;
			}

			// Clear the tracking bits
			assert(startPage > 0, "");
			ClearRange(allocator, startPage, 1);
		}

		if (allocator->releaseCallback != 0) {
			allocator->releaseCallback(allocator, header, header->size, blockSize, startPage, releasePage ? 1 : 0);
		}
	}
#endif
}

static_assert (Memory::TrackingUnitSize % 8 == 0, "Memory::MaskTrackerSize must be a multiple of 8 (bits / byte)");

u32 Memory::AlignAndTrim(void** memory, u32* size) {
#if ATLAS_64
	u64 ptr = (u64)((const void*)(*memory));
#elif ATLAS_32
	u32 ptr = (u32)((const void*)(*memory));
#endif
	u32 delta = 0;

	// Align to DefaultAlignment
	if (ptr % DefaultAlignment != 0) {
		u8* mem = (u8*)(*memory);

		u32 diff = ptr % DefaultAlignment;
		assert(*size >= diff, "");

		delta += diff;
		mem += diff;
		*size -= diff;
		*memory = mem;
	}

	if ((*size) % PageSize != 0) {
		u32 diff = (*size) % PageSize;
		assert(*size >= diff, "");
		*size -= diff;
		delta += diff;
	}

	return delta;
}

Memory::Allocator* Memory::Initialize(void* memory, u32 bytes) {
	// First, make sure that the memory being passed in is aligned well
#if ATLAS_64
	u64 ptr = (u64)((const void*)memory);
#elif ATLAS_32
	u32 ptr = (u32)((const void*)memory);
#endif
	assert(ptr % DefaultAlignment == 0, "Memory::Initialize, the memory being managed start aligned to Memory::DefaultAlignment");
	assert(bytes % PageSize == 0, "Memory::Initialize, the size of the memory being managed must be aligned to Memory::PageSize");
	assert(bytes / PageSize >= 10, "Memory::Initialize, minimum memory size is 10 pages, page size is Memory::PageSize");

	// Set up the global allocator
	Allocator* allocator = (Allocator*)memory;
	Set(allocator, 0, sizeof(allocator), "Memory::Initialize");
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

	// Add a debug page at the end
	metaDataSizeBytes += PageSize;
	numberOfMasksUsed += 1;

	allocator->offsetToAllocatable = metaDataSizeBytes;
	allocator->scanBit = 0;
	SetRange(allocator, 0, numberOfMasksUsed);
	allocator->requested = 0;

#if _DEBUG & MEM_CLEAR_ON_ALLOC
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
		assert(false, "");
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
	assert(allocator->offsetToAllocatable != 0, "Memory::Shutdown, trying to shut down an un-initialized allocator");
	assert(allocator->size > 0, "Memory::Shutdown, trying to shut down an un-initialized allocator");

	// Unset tracking bits
	u32 metaDataSizeBytes = AllocatorPaddedSize() + (maskSize * sizeof(u32));
	u32 numberOfMasksUsed = metaDataSizeBytes / PageSize;
	if (metaDataSizeBytes % PageSize != 0) {
		numberOfMasksUsed += 1;
	}
	metaDataSizeBytes = numberOfMasksUsed * PageSize;

	// There is a debug between the memory bitmask and allocatable memory
	metaDataSizeBytes += PageSize;
	numberOfMasksUsed += 1;

	ClearRange(allocator, 0, numberOfMasksUsed);
	assert(allocator->requested == 0, "Memory::Shutdown, not all memory has been released");

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
		assert(false, "");
		return 0;
	}
	if (allocator == 0) {
		allocator = GlobalAllocator;
		assert(allocator != 0, "Memory::Allocate couldn't assign global allocator");
		if (allocator == 0) {
			assert(false, "");
			return 0;
		}
	}
	assert(bytes < allocator->size, "Memory::Allocate trying to allocate more memory than is available");
	assert(bytes < allocator->size - allocator->requested, "Memory::Allocate trying to allocate more memory than is available");

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
	
	// We can record the request here. It's made before the allocation callback, and is valid for sub-allocations too.
	allocator->requested += bytes;
	assert(allocator->requested < allocator->size, "");

#if MEM_USE_SUBALLOCATORS
	if (alignment == DefaultAlignment) {
		if (allocationSize <= 64) {
			return SubAllocate(bytes, 64, &allocator->free_64, location, allocator);
		}
		else if (allocationSize <= 128) {
			return SubAllocate(bytes, 128, &allocator->free_128, location, allocator);
		}
		else if (allocationSize <= 256) {
			return SubAllocate(bytes, 256, &allocator->free_256, location, allocator);
		}
		else if (allocationSize <= 512) {
			return SubAllocate(bytes, 512, &allocator->free_512, location, allocator);
		}
		else if (allocationSize <= 1024) {
			return SubAllocate(bytes, 1024, &allocator->free_1024, location, allocator);
		}
		else if (allocationSize <= 2048) {
			return SubAllocate(bytes, 2048, &allocator->free_2048, location, allocator);
		}
	}
#endif

	// Find enough memory to allocate
#if MEM_FIRST_FIT
	u32 firstPage = FindRange(allocator, numPagesRequested, 0);
#else
	u32 firstPage = FindRange(allocator, numPagesRequested, allocator->scanBit);
	allocator->scanBit = firstPage + numPagesRequested;
#endif
	assert(firstPage != 0, "Memory::Allocate failed to find enough pages to fufill allocation");

	SetRange(allocator, firstPage, numPagesRequested);

	if (firstPage == 0 || allocator->size % PageSize != 0) {
		assert(false, "");
		return 0; // Fail this allocation in release mode
	}
	
	// Fill out header
	u8* mem = (u8*)allocator + firstPage * PageSize;
	mem += allocationHeaderPadding;
	Allocation* allocation = (Allocation*)mem;

	allocation->alignment = alignment;
	allocation->size = bytes;
	allocation->location = location;
	allocation->prev = 0;
	allocation->next = 0;

	// Track allocated memory
	assert(allocation != allocator->active, ""); // Should be impossible, but we could have bugs...
	if (allocator->active != 0) {
		allocation->next = allocator->active;
		assert(allocator->active->prev == 0, "");
		allocator->active->prev = allocation;
	}
	allocator->active = allocation;

	// Return memory
	mem += sizeof(Allocation);
#if MEM_CLEAR_ON_ALLOC
	Set(mem, 0, bytes, location);
#endif

	if (allocator->allocateCallback != 0) {
		u8* _mem = (u8*)allocator + firstPage * PageSize;
		_mem += allocationHeaderPadding;
		Allocation* _allocation = (Allocation*)_mem;
		allocator->allocateCallback(allocator, _allocation, bytes, allocationSize, firstPage, numPagesRequested);
	}

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
	assert(alignment != 0, "Memory::Free, bad alignment, probably bad memory");
	
	u32 allocationSize = allocation->size; // Add enough space to pad out for alignment
	allocationSize += (allocationSize % alignment > 0) ? alignment - (allocationSize % alignment) : 0;

	u32 allocationHeaderPadding = sizeof(Allocation) % alignment > 0 ? alignment - sizeof(Allocation) % alignment : 0;
	u32 paddedAllocationSize = allocationSize + allocationHeaderPadding + sizeof(Allocation);
	assert(allocationSize != 0, "Memory::Free, double free");
	//mem -= allocationHeaderPadding; // Will be divided by page size, doesn't really matter
	
	assert(allocator->requested >= allocation->size, "Memory::Free releasing more memory than was requested");
	assert(allocator->requested != 0, "Memory::Free releasing more memory, but there is nothing to release");
	allocator->requested -= allocation->size;

#if MEM_USE_SUBALLOCATORS
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
#endif

	// Clear the bits that where tracking this memory
	u8* firstMemory = (u8*)allocator;
#if ATLAS_64
	u64 address = (u64)(mem - firstMemory);
#elif ATLAS_32
	u32 address = (u32)(mem - firstMemory);
#endif
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

	if (allocator->releaseCallback != 0) {
		allocator->releaseCallback(allocator, allocation, allocation->size, paddedAllocationSize, firstPage, numPages);
	}
}

void* Memory::AllocateContigous(u32 num_elems, u32 elem_size, u32 alignment, const char* location, Allocator* allocator) {
	if (allocator == 0) {
		allocator = GlobalAllocator;
	}
	void* mem = Allocate(num_elems * elem_size, alignment, location, allocator);
	if (mem == 0) {
		assert(false, "");
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

#if MEM_IMPLEMENT_MALLOC
#if MEM_DEFINE_MALLOC
#undef malloc
#undef free
#undef memset
#undef memcpy
#undef calloc
#undef realloc
#endif

extern "C" void* __cdecl malloc(decltype(sizeof(0)) bytes) {
	return Memory::Allocate(bytes, Memory::DefaultAlignment, "internal - malloc", Memory::GlobalAllocator);
}

extern "C" void __cdecl free(void* data) {
	return Memory::Release(data, "internal - free", Memory::GlobalAllocator);
}

extern "C" void* __cdecl memset(void* mem, i32 value, decltype(sizeof(0)) size) {
	Memory::Set(mem, value, size, "internal - memset");
	return mem;
}

extern "C" void* __cdecl memcpy(void* dest, const void* src, decltype(sizeof(0)) size) {
	Memory::Copy(dest, src, size, "internal - memcpy");
	return dest;
}

extern "C" void* __cdecl calloc(decltype(sizeof(0)) count, decltype(sizeof(0)) size) {
	return Memory::AllocateContigous(count, size, Memory::DefaultAlignment, "internal - calloc", Memory::GlobalAllocator);
}

extern "C" void* __cdecl realloc(void* mem, decltype(sizeof(0)) size) {
	return Memory::ReAllocate(mem, size, Memory::DefaultAlignment, "internal - realloc", Memory::GlobalAllocator);
}

#if MEM_DEFINE_MALLOC
#define malloc(bytes) Memory::Allocate(bytes, Memory::DefaultAlignment, __LOCATION__, Memory::GlobalAllocator)
#define free(data) Memory::Release(data, __LOCATION__, Memory::GlobalAllocator)
#define memset(mem, val, size) Memory::Set(mem, val, size, __LOCATION__)
#define memcpy(dest, src, size) Memory::Copy(dest, src, size, __LOCATION__)
#define calloc(numelem, elemsize) Memory::AllocateContigous(numelem, elemsize, Memory::DefaultAlignment, __LOCATION__, Memory::GlobalAllocator)
#define realloc(mem, size) Memory::ReAllocate(mem, size, Memory::DefaultAlignment, __LOCATION__, Memory::GlobalAllocator)
#endif
#endif

#if MEM_IMPLEMENT_NEW
#if MEM_DEFINE_NEW
#undef new
#endif

void* __cdecl operator new (decltype(sizeof(0)) size) {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Allocate(size, Memory::DefaultAlignment, "internal - ::new(size_t)", Memory::GlobalAllocator);
}

void* __cdecl operator new (decltype(sizeof(0)) size, const std::nothrow_t& nothrow_value) noexcept {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Allocate(size, Memory::DefaultAlignment, "internal - ::new(size_t, nothrow_t&)", Memory::GlobalAllocator);
}

void* __cdecl  operator new (decltype(sizeof(0)) size, u32 alignment, const char* location, Memory::Allocator* allocator) noexcept {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Allocate(size, alignment, location, allocator);
}

void __cdecl operator delete (void* ptr) noexcept {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Release(ptr, "internal - ::delete(void*)", Memory::GlobalAllocator);
}

void __cdecl operator delete (void* ptr, const std::nothrow_t& nothrow_constant) noexcept {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Release(ptr, "internal - ::delete(void*, nothrow_t&)", Memory::GlobalAllocator);
}

void __cdecl operator delete(void* memory, const char* location, Memory::Allocator* allocator) {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Release(memory, location, allocator);
}

void __cdecl operator delete (void* ptr, decltype(sizeof(0)) size) noexcept {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Release(ptr, "internal - ::delete(void*, size_t)", Memory::GlobalAllocator);
}

void __cdecl operator delete (void* ptr, decltype(sizeof(0)) size, const std::nothrow_t& nothrow_constant) noexcept {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Release(ptr, "internal - ::delete(void*, size_t, nothrow_t&)", Memory::GlobalAllocator);
}

void* __cdecl operator new[](decltype(sizeof(0)) size) {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Allocate(size, Memory::DefaultAlignment, "internal - ::new[](size_t)", Memory::GlobalAllocator);
}

void* __cdecl operator new[](decltype(sizeof(0)) size, const std::nothrow_t& nothrow_value) noexcept {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Allocate(size, Memory::DefaultAlignment, "internal - ::new[](size_t, nothrow_t&)", Memory::GlobalAllocator);
}

void __cdecl operator delete[](void* ptr) noexcept {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Release(ptr, "internal - ::delete[](void*)", Memory::GlobalAllocator);
}

void __cdecl operator delete[](void* ptr, const std::nothrow_t& nothrow_constant) noexcept {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Release(ptr, "internal - ::delete[](void*, nothrow_t&)", Memory::GlobalAllocator);
}

void __cdecl operator delete[](void* ptr, decltype(sizeof(0)) size) noexcept {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Release(ptr, "internal - ::delete[](void*, size_t)", Memory::GlobalAllocator);
}

void __cdecl operator delete[](void* ptr, decltype(sizeof(0)) size, const std::nothrow_t& nothrow_constant) noexcept {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Release(ptr, "internal - ::delete[](void*, size_t, nothrow_t&)", Memory::GlobalAllocator);
}

#if MEM_DEFINE_NEW
#define new new(Memory::DefaultAlignment, __LOCATION__, Memory::GlobalAllocator
#endif
#endif

// Scott Schurr const string
// https://gist.github.com/creative-quant/6aa863e1cb415cbb9056f3d86f23b2c4
namespace Memory {
	namespace Debug {
		class str_const { // constexpr string
		private:
			const char* const p_;
			const ptr_type sz_;
		private:
			str_const& operator= (const str_const& other) = delete;
			str_const(const str_const&& other) = delete;
			str_const& operator= (const str_const&& other) = delete;
		public:
			template<ptr_type N>
			constexpr str_const(const char(&a)[N]) noexcept : // ctor
				p_(a), sz_(N - 1) {
			}
			constexpr char operator[](ptr_type n) const noexcept { // []
				return n < sz_ ? p_[n] : (*(char*)((void*)0) = '\0');
			}
			constexpr ptr_type size() const noexcept { // string length
				return sz_;
			} // size()
			const char* begin() const noexcept { // start iterator
				return p_;
			} // begin()
			const char* end() const noexcept { // End iterator
				return p_ + sz_;
			} // end()
			template<typename T>
			T& operator<<(T& stream) { // Stream op
				stream << p_;
				return stream;
			} // <<
		};

		u32 u64toa(u8* dest, u32 destSize, u64 num) { // Returns length of string
			Set(dest, 0, destSize, "Memory::Debug::u64toa");

			u32 count = 0;
			u32 tmp = num;
			while (tmp != 0) {
				tmp = tmp / 10;
				count = count + 1;
			}

			if (count == 0) {
				*dest = '0';
				return 1;
			}

			u8* last = dest + count - 1;
			while (num != 0) {
				u64 digit = num % 10;
				num = num / 10;

				*last-- = '0' + digit;
			}

			return count;
		}

		u32 strlen(const u8* str) {
			const u8* s;
			for (s = str; *s; ++s);
			return (u32)(s - str);
		}
	} // namespace Debug
} // namespace Memory

void Memory::Debug::DumpAllocator(Allocator* allocator, DumpCallback callback) {
	const char* l = "Memory::Debug::DumpAllocationHeaders";

	u8* debugPage = AllocatorAllocatable(allocator) - PageSize; // Debug page is always one page before allocatable
	u32 debugSize = PageSize;

	// Reset memory buffer
	Set(debugPage, 0, debugSize, l);
	u8* i_to_a_buff = debugPage; // Used to convert numbers to strings
	const u32 i_to_a_buff_size = strlen((const u8*)"18446744073709551615") + 1; // u64 max
	u8* mem = i_to_a_buff + i_to_a_buff_size;
	u32 memSize = PageSize - i_to_a_buff_size;

	{ // Tracking %d Pages, %d KiB (%d MiB)
		constexpr str_const out0("Tracking ");
		Copy(mem, out0.begin(), out0.size(), l);
		mem += out0.size();
		memSize -= out0.size();

		u32 numPages = allocator->size / Memory::PageSize;
		assert(allocator->size % Memory::PageSize == 0, l);

		u32 i_len = u64toa(i_to_a_buff, i_to_a_buff_size, numPages);
		Copy(mem, i_to_a_buff, i_len, l);
		mem += i_len;
		memSize -= i_len;

		constexpr str_const out1(" pages, size: ");
		Copy(mem, out1.begin(), out1.size(), l);
		mem += out1.size();
		memSize -= out1.size();

		u32 kib = allocator->size / 1024;
		i_len = u64toa(i_to_a_buff, i_to_a_buff_size, kib);
		Copy(mem, i_to_a_buff, i_len, l);
		mem += i_len;
		memSize -= i_len;

		constexpr str_const out2(" KiB (");
		Copy(mem, out2.begin(), out2.size(), l);
		mem += out2.size();
		memSize -= out2.size();

		u32 mib = kib / 1024;
		i_len = u64toa(i_to_a_buff, i_to_a_buff_size, mib);
		Copy(mem, i_to_a_buff, i_len, l);
		mem += i_len;
		memSize -= i_len;

		constexpr str_const out3(" MiB)\n");
		Copy(mem, out3.begin(), out3.size(), l);
		mem += out3.size();
		memSize -= out3.size();
	}

	// Dump what's been written so far
	mem = i_to_a_buff + i_to_a_buff_size;
	callback(mem, (PageSize - i_to_a_buff_size) - memSize);

	// Reset memory buffer
	Set(debugPage, 0, debugSize, l);
	i_to_a_buff = debugPage; // Used to convert numbers to strings
	mem = i_to_a_buff + i_to_a_buff_size;
	memSize = PageSize - i_to_a_buff_size;

	{ // Pages: %d free, %d used, %d overhead
		constexpr str_const out0("Page breakdown: ");
		Copy(mem, out0.begin(), out0.size(), l);
		mem += out0.size();
		memSize -= out0.size();

		u32 maskSize = AllocatorPageMaskSize(allocator) / (sizeof(u32) / sizeof(u8)); // convert from u8 to u32
		u32 metaDataSizeBytes = AllocatorPaddedSize() + (maskSize * sizeof(u32));
		u32 numberOfMasksUsed = metaDataSizeBytes / Memory::PageSize;
		if (metaDataSizeBytes % Memory::PageSize != 0) {
			numberOfMasksUsed += 1;
		}
		metaDataSizeBytes = numberOfMasksUsed * Memory::PageSize; // This way, allocatable will start on a page boundary
		// Account for meta data
		metaDataSizeBytes += Memory::PageSize;
		numberOfMasksUsed += 1;

		u32 numPages = allocator->size / Memory::PageSize;
		assert(allocator->size % Memory::PageSize == 0, l);
		u32 usedPages = allocator->numPagesUsed;
		assert(usedPages <= numPages, l);
		u32 freePages = numPages - usedPages;
		u32 overheadPages = metaDataSizeBytes / Memory::PageSize;
		assert(usedPages >= overheadPages, l);
		usedPages -= overheadPages;

		u32 i_len = u64toa(i_to_a_buff, i_to_a_buff_size, freePages);
		Copy(mem, i_to_a_buff, i_len, l);
		mem += i_len;
		memSize -= i_len;

		constexpr str_const out1(" free, ");
		Copy(mem, out1.begin(), out1.size(), l);
		mem += out1.size();
		memSize -= out1.size();

		i_len = u64toa(i_to_a_buff, i_to_a_buff_size, usedPages);
		Copy(mem, i_to_a_buff, i_len, l);
		mem += i_len;
		memSize -= i_len;

		constexpr str_const out2(" used, ");
		Copy(mem, out2.begin(), out2.size(), l);
		mem += out2.size();
		memSize -= out2.size();

		i_len = u64toa(i_to_a_buff, i_to_a_buff_size, overheadPages);
		Copy(mem, i_to_a_buff, i_len, l);
		mem += i_len;
		memSize -= i_len;

		constexpr str_const out3(" overhead\n");
		Copy(mem, out3.begin(), out3.size(), l);
		mem += out3.size();
		memSize -= out3.size();
	}

	// Dump what's been written so far
	mem = i_to_a_buff + i_to_a_buff_size;
	callback(mem, (PageSize - i_to_a_buff_size) - memSize);

	// Reset memory buffer
	Set(debugPage, 0, debugSize, l);
	i_to_a_buff = debugPage; // Used to convert numbers to strings
	mem = i_to_a_buff + i_to_a_buff_size;
	memSize = PageSize - i_to_a_buff_size;

	{ // Dump active list
		constexpr str_const out0("\nActive List:\n");
		Copy(mem, out0.begin(), out0.size(), l);
		mem += out0.size();
		memSize -= out0.size();

		for (Allocation* iter = allocator->active; iter != 0; iter = iter->next) {
			u64 address = (u64)((void*)((u8*)iter));

			constexpr str_const out5("\t");
			Copy(mem, out5.begin(), out5.size(), l);
			mem += out5.size();
			memSize -= out5.size();

			i32 i_len = u64toa(i_to_a_buff, i_to_a_buff_size, address);
			Copy(mem, i_to_a_buff, i_len, l);
			mem += i_len;
			memSize -= i_len;

			constexpr str_const out2(", size: ");
			Copy(mem, out2.begin(), out2.size(), l);
			mem += out2.size();
			memSize -= out2.size();

			i_len = u64toa(i_to_a_buff, i_to_a_buff_size, iter->size);
			Copy(mem, i_to_a_buff, i_len, l);
			mem += i_len;
			memSize -= i_len;

			constexpr str_const out3(" / ");
			Copy(mem, out3.begin(), out3.size(), l);
			mem += out3.size();
			memSize -= out3.size();

			u32 allocationHeaderPadding = sizeof(Allocation) % iter->alignment > 0 ? iter->alignment - sizeof(Allocation) % iter->alignment : 0;

			u64 realSize = iter->size + sizeof(Allocation) + allocationHeaderPadding;
			i_len = u64toa(i_to_a_buff, i_to_a_buff_size, realSize);
			Copy(mem, i_to_a_buff, i_len, l);
			mem += i_len;
			memSize -= i_len;

			constexpr str_const out6(", alignment: ");
			Copy(mem, out6.begin(), out6.size(), l);
			mem += out6.size();
			memSize -= out6.size();

			i_len = u64toa(i_to_a_buff, i_to_a_buff_size, iter->alignment);
			Copy(mem, i_to_a_buff, i_len, l);
			mem += i_len;
			memSize -= i_len;

			constexpr str_const out0(", prev: ");
			Copy(mem, out0.begin(), out0.size(), l);
			mem += out0.size();
			memSize -= out0.size();

			address = (u64)((void*)(iter->prev));
			i_len = u64toa(i_to_a_buff, i_to_a_buff_size, address);
			Copy(mem, i_to_a_buff, i_len, l);
			mem += i_len;
			memSize -= i_len;

			constexpr str_const out1(", next: ");
			Copy(mem, out1.begin(), out1.size(), l);
			mem += out1.size();
			memSize -= out1.size();

			address = (u64)((void*)(iter->next));
			i_len = u64toa(i_to_a_buff, i_to_a_buff_size, address);
			Copy(mem, i_to_a_buff, i_len, l);
			mem += i_len;
			memSize -= i_len;

			constexpr str_const out4("\n");
			Copy(mem, out4.begin(), out4.size(), l);
			mem += out4.size();
			memSize -= out4.size();

			if (memSize < PageSize / 4) { // Drain occasiaonally
				// Dump what's been written so far
				mem = i_to_a_buff + i_to_a_buff_size;
				callback(mem, (PageSize - i_to_a_buff_size) - memSize);

				// Reset memory buffer
				Set(debugPage, 0, debugSize, l);
				i_to_a_buff = debugPage; // Used to convert numbers to strings
				mem = i_to_a_buff + i_to_a_buff_size;
				memSize = PageSize - i_to_a_buff_size;
			}
		}

		if (memSize != PageSize - i_to_a_buff_size) { // Drain if needed
			// Dump what's been written so far
			mem = i_to_a_buff + i_to_a_buff_size;
			callback(mem, (PageSize - i_to_a_buff_size) - memSize);

			// Reset memory buffer
			Set(debugPage, 0, debugSize, l);
			i_to_a_buff = debugPage; // Used to convert numbers to strings
			mem = i_to_a_buff + i_to_a_buff_size;
			memSize = PageSize - i_to_a_buff_size;
		}
	}

	// Reset memory buffer
	Set(debugPage, 0, debugSize, l);
	i_to_a_buff = debugPage; // Used to convert numbers to strings
	mem = i_to_a_buff + i_to_a_buff_size;
	memSize = PageSize - i_to_a_buff_size;

	constexpr str_const newline("\n");
	constexpr str_const isSet("0");
	constexpr str_const notSet("-");

	{ // Draw a pretty graph
		u32 numPages = allocator->size / Memory::PageSize;
		u32* mask = (u32*)AllocatorPageMask(allocator);

		constexpr str_const out5("\nPage chart:\n");
		Copy(mem, out5.begin(), out5.size(), l);
		mem += out5.size();
		memSize -= out5.size();

		for (u32 i = 0; i < numPages; ++i) {
			u32 m = i / TrackingUnitSize;
			u32 b = i % TrackingUnitSize;

			bool set = mask[m] & (1 << b);
			if (set) {
				Copy(mem, isSet.begin(), isSet.size(), l);
				mem += isSet.size();
				memSize -= isSet.size();
			}
			else {
				Copy(mem, notSet.begin(), notSet.size(), l);
				mem += notSet.size();
				memSize -= notSet.size();
			}

			if ((i + 1) % 80 == 0) {
				Copy(mem, newline.begin(), newline.size(), l);
				mem += newline.size();
				memSize -= newline.size();
			}

			if (memSize < PageSize / 4) { // Drain occasiaonally
				// Dump what's been written so far
				mem = i_to_a_buff + i_to_a_buff_size;
				callback(mem, (PageSize - i_to_a_buff_size) - memSize);

				// Reset memory buffer
				Set(debugPage, 0, debugSize, l);
				i_to_a_buff = debugPage; // Used to convert numbers to strings
				mem = i_to_a_buff + i_to_a_buff_size;
				memSize = PageSize - i_to_a_buff_size;
			}
		}

		if (memSize != PageSize - i_to_a_buff_size) { // Drain if needed
			// Dump what's been written so far
			mem = i_to_a_buff + i_to_a_buff_size;
			callback(mem, (PageSize - i_to_a_buff_size) - memSize);

			// Reset memory buffer
			Set(debugPage, 0, debugSize, l);
			i_to_a_buff = debugPage; // Used to convert numbers to strings
			mem = i_to_a_buff + i_to_a_buff_size;
			memSize = PageSize - i_to_a_buff_size;
		}
	}
}

