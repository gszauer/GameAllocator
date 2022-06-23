#include "mem.h"

#pragma warning(disable:6011)
#pragma warning(disable:28182)

#if MEM_DEFINE_MALLOC
	#undef malloc
	#undef free
	#undef memset
	#undef memcpy
#endif

#if MEM_DEFINE_NEW
	#undef new
	#undef delete
#endif

// Game allocator should run without CRT. There is only one global variable, but it should not be initialized.
Memory::Allocator* Memory::GlobalAllocator;// = 0;

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

#define NotImplementedException() (*(char*)((void*)0) = '\0')

namespace Memory {
	static void Assert(bool condition, const char* msg, u32 line, const char* file) {
		char* data = (char*)((void*)0);
		if (condition == false) {
			*data = '\0';
		}
	}

	static inline u32 AllocatorPaddedSize() {
		static_assert (sizeof(Memory::Allocator) % AllocatorAlignment == 0, "Memory::Allocator size needs to be 8 byte aligned for the allocation mask to start on this alignment without any padding");
		return sizeof(Allocator);
	}

	static inline u8* AllocatorPageMask(Allocator* allocator) {
		static_assert (sizeof(Memory::Allocator) % AllocatorAlignment == 0, "Memory::Allocator size needs to be 8 byte aligned for the allocation mask to start on this alignment without any padding");
		return ((u8*)allocator) + sizeof(Allocator);
	}

	static inline u32 AllocatorPageMaskSize(Allocator* allocator) { // This is the number of u8's that make up the AllocatorPageMask array
		const u32 allocatorNumberOfPages = allocator->size / allocator->pageSize; // 1 page = (probably) 4096 bytes, how many are needed
		assert(allocator->size % allocator->pageSize == 0, "Allocator size should line up with page size");
		// allocatorNumberOfPages is the number of bits that are required to track memory

		// Pad out to sizeof(32) (if MaskTrackerSize is 32). This is because AllocatorPageMask will often be used as a u32 array
		// and we want to make sure that enough space is reserved.
		const u32 allocatorPageArraySize = allocatorNumberOfPages / TrackingUnitSize + (allocatorNumberOfPages % TrackingUnitSize ? 1 : 0);
		return allocatorPageArraySize * (TrackingUnitSize / 8); // In bytes, not bits
	}

	static inline void RemoveFromList(Allocator* allocator, Allocation** list, Allocation* allocation) {
		u32 allocationOffset = (u32)((u8*)allocation - (u8*)allocator);
		u32 listOffset = (u32)((u8*)(*list) - (u8*)allocator);
		
		Allocation* head = *list;

		if (head == allocation) { // Removing head
			if (head->nextOffset != 0) { // There is a next
				Allocation* allocNext = 0;
				if (allocation->nextOffset != 0) {
					allocNext = (Allocation*)((u8*)allocator + allocation->nextOffset);
				}
				Allocation* headerNext = 0;
				if (head->nextOffset != 0) {
					headerNext = (Allocation*)((u8*)allocator + head->nextOffset);
				}
				assert(allocNext == headerNext, "");
				assert(headerNext->prevOffset == allocationOffset, "");
				headerNext->prevOffset = 0;
			}
			Allocation* next = 0;
			if (head != 0 && head->nextOffset != 0) {
				next = (Allocation*)((u8*)allocator + head->nextOffset);
			}
			*list = next;
		}
		else {
			if (allocation->nextOffset != 0) {
				Allocation* _next = (Allocation*)((u8*)allocator + allocation->nextOffset);
				assert(_next->prevOffset == allocationOffset, "");
				_next->prevOffset = allocation->prevOffset;
			}
			if (allocation->prevOffset != 0) {
				Allocation* _prev = (Allocation*)((u8*)allocator + allocation->prevOffset);
				assert(_prev->nextOffset == allocationOffset, "");
				_prev->nextOffset = allocation->nextOffset;
			}
		}

		allocation->prevOffset = 0;
		allocation->nextOffset = 0;
	}

	static inline void AddtoList(Allocator* allocator, Allocation** list, Allocation* allocation) {
		u32 allocationOffset = (u32)((u8*)allocation - (u8*)allocator);
		u32 listOffset = (u32)((u8*)(*list) - (u8*)allocator);
		Allocation* head = *list;

		allocation->prevOffset = 0;
		allocation->nextOffset = 0;
		if (head != 0) {
			allocation->nextOffset = listOffset;
			head->prevOffset = allocationOffset;
		}
		*list = allocation;
	}

	// Returns 0 on error. Since the first page is always tracking overhead it's invalid for a range
	static inline u32 FindRange(Allocator* allocator, u32 numPages, u32 searchStartBit) {
		assert(allocator != 0, "");
		assert(numPages != 0, "");

		u32 * mask = (u32*)AllocatorPageMask(allocator);
		u32 numBitsInMask = AllocatorPageMaskSize(allocator) * 8;
		u32 numElementsInMask = AllocatorPageMaskSize(allocator) / (TrackingUnitSize / 8);
		assert(allocator->size % allocator->pageSize == 0, "Memory::FindRange, the allocators size must be a multiple of Memory::PageSize, otherwise there would be a partial page at the end");
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

		allocator->scanBit = startBit + numPages;

		assert(numBits == numPages, "Memory::FindRange Could not find enough memory to fufill request");
		assert(startBit != 0, "Memory::FindRange Could not memory fufill request");
		if (numBits != numPages || startBit == 0 || allocator->size % allocator->pageSize != 0) {
			assert(false, "");
			return 0;
		}

		return startBit;
	}

	static inline void SetRange(Allocator* allocator, u32 startBit, u32 bitCount) {
		assert(allocator != 0, "");
		assert(bitCount != 0, "");

		u32* mask = (u32*)AllocatorPageMask(allocator);
		assert(allocator->size % allocator->pageSize == 0, "Memory::FindRange, the allocators size must be a multiple of Memory::PageSize, otherwise there would be a partial page at the end");
		assert(mask != 0, "");

#if _DEBUG
		u32 numBitsInMask = AllocatorPageMaskSize(allocator) * 8;
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

		assert(allocator->numPagesUsed <= numBitsInMask, "Memory::FindRange, over allocating");
		assert(allocator->numPagesUsed + bitCount <= numBitsInMask, "Memory::FindRange, over allocating");
		allocator->numPagesUsed += bitCount;
		if (allocator->numPagesUsed > allocator->peekPagesUsed) {
			allocator->peekPagesUsed = allocator->numPagesUsed;
		}
	}

	static inline void ClearRange(Allocator* allocator, u32 startBit, u32 bitCount) {
		assert(allocator != 0, "");
		assert(bitCount != 0, "");

		u32* mask = (u32*)AllocatorPageMask(allocator);
		assert(allocator->size % allocator->pageSize == 0, "Memory::FindRange, the allocators size must be a multiple of Memory::PageSize, otherwise there would be a partial page at the end");
		assert(mask != 0, "");

#if _DEBUG
		u32 numBitsInMask = AllocatorPageMaskSize(allocator) * 8;
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
	// This function will chop the provided page into several blocks. Since the block size is constant, we
	// know that headers will be laid out at a stride of blockSize. There is no additional tracking needed.
	void* SubAllocate(u32 requestedBytes, u32 blockSize, Allocation** freeList, const char* location, Allocator* allocator) {
		assert(blockSize < allocator->pageSize, "Block size must be less than page size");

		// There is no blocks of the requested size available. Reserve 1 page, and carve it up into blocks.
		bool grabNewPage = *freeList == 0;
		if (*freeList == 0) {
			// Find and reserve 1 free page
#if MEM_FIRST_FIT
			const u32 page = FindRange(allocator, 1, 0);
#else
			const u32 page = FindRange(allocator, 1, allocator->scanBit);
#endif
			SetRange(allocator, page, 1);

			// Zero out the pages memory
			u8* mem = (u8*)allocator + allocator->pageSize * page;
			Set(mem, 0, allocator->pageSize, __LOCATION__);

			// Figure out how many blocks fit into this page
			const u32 numBlocks = allocator->pageSize / blockSize;
			assert(numBlocks > 0, "");
			assert(numBlocks < 128, "");

			// For each block in this page, initialize it's header and add it to the free list
			for (u32 i = 0; i < numBlocks; ++i) {
				Allocation* alloc = (Allocation*)mem;
				mem += blockSize;

				// Initialize the allocation header
				alloc->prevOffset = 0;
				alloc->nextOffset = 0;
				alloc->size = 0;
				alloc->alignment = 0;
#if MEM_TRACK_LOCATION
				alloc->location = location;
#endif

				AddtoList(allocator, freeList, alloc);
			}
		}
		assert(*freeList != 0, "The free list literally can't be zero here...");

		// At this point we know the free list has some number of blocks in it. 
		// Save a reference to the current header & advance the free list
		// Advance the free list, we're going to be using this one.
		Allocation* block = *freeList;
#if MEM_CLEAR_ON_ALLOC
		Set((u8*)block + sizeof(Allocation), 0, blockSize - sizeof(Allocation), location);
#elif MEM_DEBUG_ON_ALLOC
		{
			const u8 stamp[] = "-MEMORY-";
			u8* mem = (u8*)block + sizeof(Allocation);
			u32 size = blockSize - sizeof(Allocation);
			for (u32 i = requestedBytes; i < size; ++i) {
				mem[i] = stamp[(i - requestedBytes) % 7];
			}
		}
#endif
		if ((*freeList)->nextOffset != 0) { // Advance one
			Allocation* _next = (Allocation*)((u8*)allocator + (*freeList)->nextOffset);
			_next->prevOffset = 0;
			*freeList = (Allocation*)((u8*)allocator + (*freeList)->nextOffset); // freeList = freeList.next
		}
		else {
			*freeList = 0;
		}

		block->prevOffset = 0;
		block->size = requestedBytes;
		block->alignment = 0;
#if MEM_TRACK_LOCATION
		block->location = location;
#endif

		AddtoList(allocator, &allocator->active, block); // Sets block->next

		if (allocator->allocateCallback != 0) {
			u32 firstPage = ((u32)((u8*)block - (u8*)allocator)) / allocator->pageSize;
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
			assert(false, "");
			return;
		}
		u32 oldSize = header->size;
		header->size = 0;

		// Now remove from the active list.
		RemoveFromList(allocator, &allocator->active, header);
		// Add memory back into the free list
		AddtoList(allocator, freeList, header);
#if _DEBUG & MEM_TRACK_LOCATION
		header->location = "SubRelease released this block";
#endif

		// Find the first allocation inside the page
		u32 startPage = (u32)((u8*)header - (u8*)allocator) / allocator->pageSize;

		u8* mem =(u8*)allocator + startPage * allocator->pageSize;

		// Each sub allocator page contains multiple blocks. check if all of the blocks 
		// belonging to a single page are free, if they are, release the page.
		bool releasePage = true;
		
		const u32 numAllocationsPerPage = allocator->pageSize / blockSize;
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
			mem = (u8*)allocator + startPage * allocator->pageSize;
			for (u32 i = 0; i < numAllocationsPerPage; ++i) {
				Allocation* iter = (Allocation*)mem;
				mem += blockSize;
				assert(iter != 0, "");

				RemoveFromList(allocator, freeList, iter);
			}

			// Clear the tracking bits
			assert(startPage > 0, "");
			ClearRange(allocator, startPage, 1);
		}

		if (allocator->releaseCallback != 0) {
			allocator->releaseCallback(allocator, header, oldSize, blockSize, startPage, releasePage ? 1 : 0);
		}
	}
#endif
} // Namespace Memory


u32 Memory::AlignAndTrim(void** memory, u32* size, u32 alignment, u32 pageSize) {
#if ATLAS_64
	u64 ptr = (u64)((const void*)(*memory));
#elif ATLAS_32
	u32 ptr = (u32)((const void*)(*memory));
#else
	#error Unknown Platform
#endif
	u32 delta = 0;

	// Align to 8 byte boundary. This is so the mask array lines up on a u64
	if (ptr % alignment != 0) {
		u8* mem = (u8*)(*memory);

		u32 diff = ptr % alignment;
		assert(*size >= diff, "");

		delta += diff;
		mem += diff;
		*size -= diff;
		*memory = mem;
	}

	// Trim to page size (4096) to make sure the provided memory can be chunked up perfectly
	if ((*size) % pageSize != 0) {
		u32 diff = (*size) % pageSize;
		assert(*size >= diff, "");
		*size -= diff;
		delta += diff;
	}

	return delta;
}

Memory::Allocator* Memory::Initialize(void* memory, u32 bytes, u32 pageSize) {
	assert(pageSize % AllocatorAlignment == 0, "Memory::Initialize, Page boundaries are expected to be on 8 bytes");
	// First, make sure that the memory being passed in is aligned well
#if ATLAS_64
	u64 ptr = (u64)((const void*)memory);
#elif ATLAS_32
	u32 ptr = (u32)((const void*)memory);
#else
	#error Unknown platform
#endif
	assert(ptr % AllocatorAlignment == 0, "Memory::Initialize, Memory being managed should be 8 byte aligned. Consider using Memory::AlignAndTrim");
	assert(bytes % pageSize == 0, "Memory::Initialize, the size of the memory being managed must be aligned to Memory::PageSize");
	assert(bytes / pageSize >= 10, "Memory::Initialize, minimum memory size is 10 pages, page size is Memory::PageSize");

	// Set up the allocator
	Allocator* allocator = (Allocator*)memory;
	Set(allocator, 0, sizeof(allocator), "Memory::Initialize");
	allocator->size = bytes;
	allocator->pageSize = pageSize;

	// Set up the mask that will track our allocation data
	u32* mask = (u32*)AllocatorPageMask(allocator);
	u32 maskSize = AllocatorPageMaskSize(allocator) / (sizeof(u32) / sizeof(u8)); // convert from u8 to u32
	Set(mask, 0, sizeof(u32) * maskSize, __LOCATION__);
	
	// Find how many pages the meta data for the header + allocation mask will take up. 
	// Store the offset to first allocatable, 
	u32 metaDataSizeBytes = AllocatorPaddedSize() + (maskSize * sizeof(u32));
	u32 numberOfMasksUsed = metaDataSizeBytes / pageSize;
	if (metaDataSizeBytes % pageSize != 0) {
		numberOfMasksUsed += 1;
	}
	metaDataSizeBytes = numberOfMasksUsed * pageSize; // This way, allocatable will start on a page boundary

	// Add a debug page at the end
	metaDataSizeBytes += pageSize;
	numberOfMasksUsed += 1;

	//allocator->offsetToAllocatable = metaDataSizeBytes;
	allocator->scanBit = 0;
	SetRange(allocator, 0, numberOfMasksUsed);
	allocator->requested = 0;

	if (ptr % AllocatorAlignment != 0 || bytes % pageSize != 0 || bytes / pageSize < 10) {
		assert(false, "");
		return 0;
	}
	
	return (Allocator*)memory;
}

void Memory::Shutdown(Allocator* allocator) {
	assert(allocator != 0, "Memory::Shutdown called without it being initialized");
	u32* mask = (u32*)AllocatorPageMask(allocator);
	u32 maskSize = AllocatorPageMaskSize(allocator) / (sizeof(u32) / sizeof(u8)); // convert from u8 to u32
	assert(allocator->size > 0, "Memory::Shutdown, trying to shut down an un-initialized allocator");

	// Unset tracking bits
	u32 metaDataSizeBytes = AllocatorPaddedSize() + (maskSize * sizeof(u32));
	u32 numberOfMasksUsed = metaDataSizeBytes / allocator->pageSize;
	if (metaDataSizeBytes % allocator->pageSize != 0) {
		numberOfMasksUsed += 1;
	}
	metaDataSizeBytes = numberOfMasksUsed * allocator->pageSize;

	// There is a debug between the memory bitmask and allocatable memory
	metaDataSizeBytes += allocator->pageSize;
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
	u32 size_32 = (u32)((size - size_64 * sizeof(u64)) / sizeof(u32));
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
	u32 size_16 = (u32)((size - size_64 * sizeof(u64) - size_32 * sizeof(u32)) / sizeof(u16));
#else
	u32 size_16 = (size - size_32 * sizeof(u32)) / sizeof(u16);
#endif
	u16* dst_16 = (u16*)(dst_32 + size_32);
	const u16* src_16 = (const u16*)(src_32 + size_32);
	for (u32 i = 0; i < size_16; ++i) {
		dst_16[i] = src_16[i];
	}

#if ATLAS_64
	u32 size_8 = (u32)(size - size_64 * sizeof(u64) - size_32 * sizeof(u32) - size_16 * sizeof(u16));
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
#elif ATLAS_32
	assert(size_32 * sizeof(u32) + size_16 * sizeof(u16) + size_8 == size, "Number of pages not adding up");
#else
	#error Unknown Platform
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
	u32 size_32 = (u32)((size - size_64 * sizeof(u64)) / sizeof(u32));
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
	u32 size_16 = (u32)((size - size_64 * sizeof(u64) - size_32 * sizeof(u32)) / sizeof(u16));
#else
	u32 size_16 = (size - size_32 * sizeof(u32)) / sizeof(u16);
#endif
	u16* ptr_16 = (u16*)(ptr_32 + size_32);
	u32 val_16 = (((u16)value) << 8) | ((u16)value);
	for (u32 i = 0; i < size_16; ++i) {
		ptr_16[i] = val_16;
	}

#if ATLAS_64
	u32 size_8 = (u32)(size - size_64 * sizeof(u64) - size_32 * sizeof(u32) - size_16 * sizeof(u16));
#else
	u32 size_8 = (size - size_32 * sizeof(u32) - size_16 * sizeof(u16));
#endif
	u8* ptr_8 = (u8*)(ptr_16 + size_16);
	for (u32 i = 0; i < size_8; ++i) {
		ptr_8[i] = value;
	}

#if ATLAS_64
	assert(size_64 * sizeof(u64) + size_32 * sizeof(u32) + size_16 * sizeof(u16) + size_8 == size, "Number of pages not adding up");
#elif ATLAS_32
	assert(size_32 * sizeof(u32) + size_16 * sizeof(u16) + size_8 == size, "Number of pages not adding up");
#else
	#error Unknown Platform
#endif
}

void* Memory::Allocate(u32 bytes, u32 alignment, Allocator* allocator, const char* location) {
	if (bytes == 0) {
		bytes = 1; // At least one byte required
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

	u32 allocationHeaderPadding = 0;
	if (alignment != 0) { // Add paddnig to make sure we can align the memory
		allocationHeaderPadding = alignment - 1; // Somewhere in this range, we will be aligned
	}
	u32 allocationHeaderSize = sizeof(Allocation) + allocationHeaderPadding;

	// Add the header size to our allocation size
	u32 allocationSize = bytes; // Add enough space to pad out for alignment
	allocationSize += allocationHeaderSize;

	// Figure out how many pages are going to be needed to hold that much memory
	u32 numPagesRequested = allocationSize / allocator->pageSize + (allocationSize % allocator->pageSize ? 1 : 0);
	assert(numPagesRequested > 0, "Memory::Allocate needs to request at least 1 page");
	
	// We can record the request here. It's made before the allocation callback, and is valid for sub-allocations too.
	allocator->requested += bytes;
	assert(allocator->requested < allocator->size, "");

#if MEM_USE_SUBALLOCATORS
	if (alignment == 0) {
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
#endif
	assert(firstPage != 0, "Memory::Allocate failed to find enough pages to fufill allocation");

	SetRange(allocator, firstPage, numPagesRequested);

	if (firstPage == 0 || allocator->size % allocator->pageSize != 0) {
		assert(false, "");
		return 0; // Fail this allocation in release mode
	}
	
	// Fill out header
	u8* mem = (u8*)allocator + firstPage * allocator->pageSize;

	u32 alignmentOffset = 0;
	if (alignment != 0) { 
#if ATLAS_64
		u64 mem_addr = (u64)((void*)mem) + sizeof(Allocation);
#elif ATLAS_32
		u32 mem_addr = (u32)((void*)mem) + sizeof(Allocation);
#else
		#error Unknown platform
#endif
		if (mem_addr % alignment != 0) {
			mem_addr = (mem_addr + (alignment - 1)) / alignment * alignment;
			mem = (u8*)(mem_addr - sizeof(Allocation));
		}
	}

	Allocation* allocation = (Allocation*)mem;
	mem += sizeof(Allocation);

	allocation->alignment = alignment;
	allocation->size = bytes;
	allocation->prevOffset = 0;
	allocation->nextOffset = 0;
#if MEM_TRACK_LOCATION
	allocation->location = location;
#endif

	// Track allocated memory
	assert(allocation != allocator->active, ""); // Should be impossible, but we could have bugs...
	AddtoList(allocator, &allocator->active, allocation);

	// Return memory
#if MEM_CLEAR_ON_ALLOC
	Set(mem, 0, bytes, location);
#elif MEM_DEBUG_ON_ALLOC
	const u8 stamp[] = "-MEMORY-";
	u32 size = PageSize - allocationHeaderPadding - sizeof(Allocation);
	for (u32 i = bytes; i < size; ++i) {
		mem[i] = stamp[(i - bytes) % 7];
	}
#endif

	if (allocator->allocateCallback != 0) {
		u8* _mem = (u8*)allocator + firstPage * allocator->pageSize;
		_mem += allocationHeaderPadding;
		Allocation* _allocation = (Allocation*)_mem;
		allocator->allocateCallback(allocator, _allocation, bytes, allocationSize, firstPage, numPagesRequested);
	}

	return mem;
}

void Memory::Release(void* memory, Allocator* allocator, const char* location) {
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
	assert(allocation != 0, "Can't free null");
	u32 alignment = allocation->alignment;
	
	u32 allocationSize = allocation->size; // Add enough space to pad out for alignment
	
	u32 allocationHeaderPadding = 0;
	if (alignment != 0) {  // Add padding to the header to compensate for alignment
		allocationHeaderPadding = alignment - 1; // Somewhere in this range, we will be aligned
	}
	u32 paddedAllocationSize = allocationSize + allocationHeaderPadding + sizeof(Allocation);
	assert(allocationSize != 0, "Memory::Free, double free");
	
	assert(allocator->requested >= allocation->size, "Memory::Free releasing more memory than was requested");
	assert(allocator->requested != 0, "Memory::Free releasing more memory, but there is nothing to release");
	allocator->requested -= allocation->size;

#if MEM_USE_SUBALLOCATORS
	if (alignment == 0) {
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
	u32 address = (u32)((u8*)mem - (u8*)firstMemory);

	u32 firstPage = address / allocator->pageSize;
	u32 numPages = paddedAllocationSize / allocator->pageSize + (paddedAllocationSize % allocator->pageSize ? 1 : 0);
	ClearRange(allocator, firstPage, numPages);

	// Unlink tracking
	RemoveFromList(allocator, &allocator->active, allocation);

	// Set the size to 0, to indicate that this header has been free-d
	u32 oldSize = allocation->size;
	allocation->size = 0;

	if (allocator->releaseCallback != 0) {
		allocator->releaseCallback(allocator, allocation, oldSize, paddedAllocationSize, firstPage, numPages);
	}
}

#if MEM_IMPLEMENT_MALLOC

extern "C" void* __cdecl malloc(Memory::ptr_type bytes) {
	return Memory::Allocate((u32)bytes, 0, Memory::GlobalAllocator, "internal - malloc");
}

extern "C" void __cdecl free(void* data) {
	return Memory::Release(data, Memory::GlobalAllocator, "internal - free");
}

extern "C" void* __cdecl memset(void* mem, i32 value, Memory::ptr_type size) {
	Memory::Set(mem, value, (u32)size, "internal - memset");
	return mem;
}

extern "C" void* __cdecl memcpy(void* dest, const void* src, Memory::ptr_type size) {
	Memory::Copy(dest, src, (u32)size, "internal - memcpy");
	return dest;
}
#endif

#if MEM_IMPLEMENT_NEW
void* __cdecl operator new (Memory::ptr_type size) {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Allocate((u32)size, 0, Memory::GlobalAllocator, "internal - ::new(size_t)");
}

void* __cdecl operator new (Memory::ptr_type size, const std::nothrow_t& nothrow_value) noexcept {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Allocate((u32)size, 0, Memory::GlobalAllocator, "internal - ::new(size_t, nothrow_t&)");
}

void* __cdecl  operator new (Memory::ptr_type size, u32 alignment, const char* location, Memory::Allocator* allocator) noexcept {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Allocate((u32)size, alignment, allocator, location);
}

void __cdecl operator delete (void* ptr) noexcept {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Release(ptr, Memory::GlobalAllocator, "internal - ::delete(void*)");
}

void __cdecl operator delete (void* ptr, const std::nothrow_t& nothrow_constant) noexcept {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Release(ptr, Memory::GlobalAllocator, "internal - ::delete(void*, nothrow_t&)");
}

void __cdecl operator delete (void* ptr, Memory::ptr_type size) noexcept {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Release(ptr, Memory::GlobalAllocator, "internal - ::delete(void*, size_t)");
}

void __cdecl operator delete (void* ptr, Memory::ptr_type size, const std::nothrow_t& nothrow_constant) noexcept {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Release(ptr, Memory::GlobalAllocator, "internal - ::delete(void*, size_t, nothrow_t&)");
}

void* __cdecl operator new[](Memory::ptr_type size) {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Allocate((u32)size, 0, Memory::GlobalAllocator, "internal - ::new[](size_t)");
}

void* __cdecl operator new[](Memory::ptr_type size, const std::nothrow_t& nothrow_value) noexcept {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Allocate((u32)size, 0, Memory::GlobalAllocator, "internal - ::new[](size_t, nothrow_t&)");
}

void __cdecl operator delete[](void* ptr) noexcept {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Release(ptr, Memory::GlobalAllocator, "internal - ::delete[](void*)");
}

void __cdecl operator delete[](void* ptr, const std::nothrow_t& nothrow_constant) noexcept {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Release(ptr, Memory::GlobalAllocator, "internal - ::delete[](void*, nothrow_t&)");
}

void __cdecl operator delete[](void* ptr, Memory::ptr_type size) noexcept {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Release(ptr, Memory::GlobalAllocator, "internal - ::delete[](void*, size_t)");
}

void __cdecl operator delete[](void* ptr, Memory::ptr_type size, const std::nothrow_t& nothrow_constant) noexcept {
	assert(Memory::GlobalAllocator != 0, "Global allocator can't be null");
	return Memory::Release(ptr, Memory::GlobalAllocator, "internal - ::delete[](void*, size_t, nothrow_t&)");
}
#endif

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
			constexpr u32 size() const noexcept { // string length
				return (u32)sz_;
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

		u32 u32toa(u8* dest, u32 destSize, u32 num) { // Returns length of string
			Set(dest, 0, destSize, "Memory::Debug::u32toa");

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
				u32 digit = num % 10;
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

void Memory::Debug::MemInfo(Allocator* allocator, WriteCallback callback, void* userdata) {
	const char* l = "Memory::Debug::DumpAllocationHeaders";

	u8* debugPage = Memory::Debug::DevPage(allocator);
	u32 debugSize = allocator->pageSize;

	// Reset memory buffer
	Set(debugPage, 0, debugSize, l);
	u8* i_to_a_buff = debugPage; // Used to convert numbers to strings
	const u32 i_to_a_buff_size = strlen((const u8*)"18446744073709551615") + 1; // u64 max
	u8* mem = i_to_a_buff + i_to_a_buff_size;
	u32 memSize = allocator->pageSize - i_to_a_buff_size;

	{ // Tracking %d Pages, %d KiB (%d MiB)
		constexpr str_const out0("Tracking ");
		Copy(mem, out0.begin(), out0.size(), l);
		mem += out0.size();
		memSize -= out0.size();

		u32 numPages = allocator->size / allocator->pageSize;
		assert(allocator->size % allocator->pageSize == 0, l);

		u32 i_len = u32toa(i_to_a_buff, i_to_a_buff_size, numPages);
		Copy(mem, i_to_a_buff, i_len, l);
		mem += i_len;
		memSize -= i_len;

		constexpr str_const out1(" pages, Page size: ");
		Copy(mem, out1.begin(), out1.size(), l);
		mem += out1.size();
		memSize -= out1.size();

		i_len = u32toa(i_to_a_buff, i_to_a_buff_size, allocator->pageSize);
		Copy(mem, i_to_a_buff, i_len, l);
		mem += i_len;
		memSize -= i_len;

		constexpr str_const out11(" bytes\nTotal memory size: ");
		Copy(mem, out11.begin(), out11.size(), l);
		mem += out11.size();
		memSize -= out11.size();

		u32 kib = allocator->size / 1024;
		i_len = u32toa(i_to_a_buff, i_to_a_buff_size, kib);
		Copy(mem, i_to_a_buff, i_len, l);
		mem += i_len;
		memSize -= i_len;

		constexpr str_const out2(" KiB (");
		Copy(mem, out2.begin(), out2.size(), l);
		mem += out2.size();
		memSize -= out2.size();

		u32 mib = kib / 1024;
		i_len = u32toa(i_to_a_buff, i_to_a_buff_size, mib);
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
	callback(mem, (allocator->pageSize - i_to_a_buff_size) - memSize, userdata);

	// Reset memory buffer
	Set(debugPage, 0, debugSize, l);
	i_to_a_buff = debugPage; // Used to convert numbers to strings
	mem = i_to_a_buff + i_to_a_buff_size;
	memSize = allocator->pageSize - i_to_a_buff_size;

	{ // Pages: %d free, %d used, %d overhead
		constexpr str_const out0("Page state: ");
		Copy(mem, out0.begin(), out0.size(), l);
		mem += out0.size();
		memSize -= out0.size();

		u32 maskSize = AllocatorPageMaskSize(allocator) / (sizeof(u32) / sizeof(u8)); // convert from u8 to u32
		u32 metaDataSizeBytes = AllocatorPaddedSize() + (maskSize * sizeof(u32));
		u32 numberOfMasksUsed = metaDataSizeBytes / allocator->pageSize;
		if (metaDataSizeBytes % allocator->pageSize != 0) {
			numberOfMasksUsed += 1;
		}
		metaDataSizeBytes = numberOfMasksUsed * allocator->pageSize; // This way, allocatable will start on a page boundary
		// Account for meta data
		metaDataSizeBytes += allocator->pageSize;
		numberOfMasksUsed += 1;

		u32 numPages = allocator->size / allocator->pageSize;
		assert(allocator->size % allocator->pageSize == 0, l);
		u32 usedPages = allocator->numPagesUsed;
		assert(usedPages <= numPages, l);
		u32 freePages = numPages - usedPages;
		u32 overheadPages = metaDataSizeBytes / allocator->pageSize;
		assert(usedPages >= overheadPages, l);
		usedPages -= overheadPages;

		u32 i_len = u32toa(i_to_a_buff, i_to_a_buff_size, freePages);
		Copy(mem, i_to_a_buff, i_len, l);
		mem += i_len;
		memSize -= i_len;

		constexpr str_const out1(" free, ");
		Copy(mem, out1.begin(), out1.size(), l);
		mem += out1.size();
		memSize -= out1.size();

		i_len = u32toa(i_to_a_buff, i_to_a_buff_size, usedPages);
		Copy(mem, i_to_a_buff, i_len, l);
		mem += i_len;
		memSize -= i_len;

		constexpr str_const out2(" used, ");
		Copy(mem, out2.begin(), out2.size(), l);
		mem += out2.size();
		memSize -= out2.size();

		i_len = u32toa(i_to_a_buff, i_to_a_buff_size, overheadPages);
		Copy(mem, i_to_a_buff, i_len, l);
		mem += i_len;
		memSize -= i_len;

		constexpr str_const out3(" overhead\nRequested: ");
		Copy(mem, out3.begin(), out3.size(), l);
		mem += out3.size();
		memSize -= out3.size();

		i_len = u32toa(i_to_a_buff, i_to_a_buff_size, allocator->requested);
		Copy(mem, i_to_a_buff, i_len, l);
		mem += i_len;
		memSize -= i_len;

		constexpr str_const out4(" bytes, Served: ");
		Copy(mem, out4.begin(), out4.size(), l);
		mem += out4.size();
		memSize -= out4.size();

		i_len = u32toa(i_to_a_buff, i_to_a_buff_size, usedPages * allocator->pageSize);
		Copy(mem, i_to_a_buff, i_len, l);
		mem += i_len;
		memSize -= i_len;

		constexpr str_const out5(" bytes\n");
		Copy(mem, out5.begin(), out5.size(), l);
		mem += out5.size();
		memSize -= out5.size();
	}

	// Dump what's been written so far
	mem = i_to_a_buff + i_to_a_buff_size;
	callback(mem, (allocator->pageSize - i_to_a_buff_size) - memSize, userdata);

	// Reset memory buffer
	Set(debugPage, 0, debugSize, l);
	i_to_a_buff = debugPage; // Used to convert numbers to strings
	mem = i_to_a_buff + i_to_a_buff_size;
	memSize = allocator->pageSize - i_to_a_buff_size;

	{ // Dump active list
		constexpr str_const out0("\nActive allocations:\n");
		Copy(mem, out0.begin(), out0.size(), l);
		mem += out0.size();
		memSize -= out0.size();

		for (Allocation* iter = allocator->active; iter != 0; iter = (iter->nextOffset == 0)? 0 : (Allocation*)((u8*)allocator + iter->nextOffset)) {
			//u64 address = (u64)((void*)iter);
			u64 alloc_address = (u64)((void*)allocator);

			constexpr str_const out5("\t");
			Copy(mem, out5.begin(), out5.size(), l);
			mem += out5.size();
			memSize -= out5.size();

			u32 allocationOffset = (u32)((u8*)iter - (u8*)allocator);
			i32 i_len = u32toa(i_to_a_buff, i_to_a_buff_size, allocationOffset);
			Copy(mem, i_to_a_buff, i_len, l);
			mem += i_len;
			memSize -= i_len;

			constexpr str_const out2(", size: ");
			Copy(mem, out2.begin(), out2.size(), l);
			mem += out2.size();
			memSize -= out2.size();

			i_len = u32toa(i_to_a_buff, i_to_a_buff_size, iter->size);
			Copy(mem, i_to_a_buff, i_len, l);
			mem += i_len;
			memSize -= i_len;

			constexpr str_const out3(", padded: ");
			Copy(mem, out3.begin(), out3.size(), l);
			mem += out3.size();
			memSize -= out3.size();

			u32 alignment = iter->alignment;
			u32 allocationHeaderPadding = 0;
			if (alignment != 0) {  // Add padding to the header to compensate for alignment
				allocationHeaderPadding = alignment - 1; // Somewhere in this range, we will be aligned
			}

			u32 realSize = iter->size + (u32)(sizeof(Allocation)) + allocationHeaderPadding;
			i_len = u32toa(i_to_a_buff, i_to_a_buff_size, realSize);
			Copy(mem, i_to_a_buff, i_len, l);
			mem += i_len;
			memSize -= i_len;

			constexpr str_const out6(", alignment: ");
			Copy(mem, out6.begin(), out6.size(), l);
			mem += out6.size();
			memSize -= out6.size();

			i_len = u32toa(i_to_a_buff, i_to_a_buff_size, iter->alignment);
			Copy(mem, i_to_a_buff, i_len, l);
			mem += i_len;
			memSize -= i_len;

			constexpr str_const outfp(", first page: ");
			Copy(mem, outfp.begin(), outfp.size(), l);
			mem += outfp.size();
			memSize -= outfp.size();

			i_len = u32toa(i_to_a_buff, i_to_a_buff_size, (allocationOffset) / allocator->pageSize);
			Copy(mem, i_to_a_buff, i_len, l);
			mem += i_len;
			memSize -= i_len;

			constexpr str_const out0(", prev: ");
			Copy(mem, out0.begin(), out0.size(), l);
			mem += out0.size();
			memSize -= out0.size();

			i_len = u32toa(i_to_a_buff, i_to_a_buff_size, iter->prevOffset);
			Copy(mem, i_to_a_buff, i_len, l);
			mem += i_len;
			memSize -= i_len;

			constexpr str_const out1(", next: ");
			Copy(mem, out1.begin(), out1.size(), l);
			mem += out1.size();
			memSize -= out1.size();

			i_len = u32toa(i_to_a_buff, i_to_a_buff_size, iter->nextOffset);
			Copy(mem, i_to_a_buff, i_len, l);
			mem += i_len;
			memSize -= i_len;

			u32 pathLen = 0;
#if MEM_TRACK_LOCATION
			pathLen = strlen((const u8*)iter->location);
#endif

			if (memSize < allocator->pageSize / 4 || memSize < (pathLen + pathLen / 4)) { // Drain occasiaonally
				// Dump what's been written so far
				mem = i_to_a_buff + i_to_a_buff_size;
				callback(mem, (allocator->pageSize - i_to_a_buff_size) - memSize, userdata);

				// Reset memory buffer
				Set(debugPage, 0, debugSize, l);
				i_to_a_buff = debugPage; // Used to convert numbers to strings
				mem = i_to_a_buff + i_to_a_buff_size;
				memSize = allocator->pageSize - i_to_a_buff_size;
			}

			constexpr str_const out_loc(", location: ");
			Copy(mem, out_loc.begin(), out_loc.size(), l);
			mem += out_loc.size();
			memSize -= out_loc.size();

#if MEM_TRACK_LOCATION
			if (iter->location == 0) {
#else
			{
#endif
				assert(pathLen == 0, "");
				
				constexpr str_const out_loc("null");
				Copy(mem, out_loc.begin(), out_loc.size(), l);
				mem += out_loc.size();
				memSize -= out_loc.size();
			}
#if MEM_TRACK_LOCATION
			else {
				assert(pathLen != 0, "");
				Copy(mem, iter->location, pathLen, l);
				mem += pathLen;
				memSize -= pathLen;
			}
#endif

			constexpr str_const out4("\n");
			Copy(mem, out4.begin(), out4.size(), l);
			mem += out4.size();
			memSize -= out4.size();
		}

		if (memSize != allocator->pageSize - i_to_a_buff_size) { // Drain if needed
			// Dump what's been written so far
			mem = i_to_a_buff + i_to_a_buff_size;
			callback(mem, (allocator->pageSize - i_to_a_buff_size) - memSize, userdata);

			// Reset memory buffer
			Set(debugPage, 0, debugSize, l);
			i_to_a_buff = debugPage; // Used to convert numbers to strings
			mem = i_to_a_buff + i_to_a_buff_size;
			memSize = allocator->pageSize - i_to_a_buff_size;
		}
	}

	// Reset memory buffer
	Set(debugPage, 0, debugSize, l);
	i_to_a_buff = debugPage; // Used to convert numbers to strings
	mem = i_to_a_buff + i_to_a_buff_size;
	memSize = allocator->pageSize - i_to_a_buff_size;

	constexpr str_const newline("\n\t");
	constexpr str_const isSet("0");
	constexpr str_const notSet("-");

	{ // Draw a pretty graph
		u32 numPages = allocator->size / allocator->pageSize;
		u32* mask = (u32*)AllocatorPageMask(allocator);

		constexpr str_const out5("\nPage chart:\n\t");
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

			if (memSize < allocator->pageSize / 4) { // Drain occasiaonally
				// Dump what's been written so far
				mem = i_to_a_buff + i_to_a_buff_size;
				callback(mem, (allocator->pageSize - i_to_a_buff_size) - memSize, userdata);

				// Reset memory buffer
				Set(debugPage, 0, debugSize, l);
				i_to_a_buff = debugPage; // Used to convert numbers to strings
				mem = i_to_a_buff + i_to_a_buff_size;
				memSize = allocator->pageSize - i_to_a_buff_size;
			}
		}

		if (memSize != allocator->pageSize - i_to_a_buff_size) { // Drain if needed
			// Dump what's been written so far
			mem = i_to_a_buff + i_to_a_buff_size;
			callback(mem, (allocator->pageSize - i_to_a_buff_size) - memSize, userdata);

			// Reset memory buffer
			Set(debugPage, 0, debugSize, l);
			i_to_a_buff = debugPage; // Used to convert numbers to strings
			mem = i_to_a_buff + i_to_a_buff_size;
			memSize = allocator->pageSize - i_to_a_buff_size;
		}
	}
}

void Memory::Debug::PageContent(Allocator* allocator, u32 page, WriteCallback callback, void* userdata) {
	u8* mem = (u8*)allocator + page * allocator->pageSize;
	u32 chunk = allocator->pageSize / 4; // Does not need to be a multiple of 4
	
	callback(mem, chunk, userdata);
	mem += chunk;
	callback(mem, chunk, userdata);
	mem += chunk;
	callback(mem, chunk, userdata);
	mem += chunk;
	callback(mem, allocator->pageSize - (allocator->pageSize / 4) * 3, userdata);
}

u8* Memory::Debug::DevPage(Allocator* allocator) {
	// Set up the mask that will track our allocation data
	u32* mask = (u32*)AllocatorPageMask(allocator);
	u32 maskSize = AllocatorPageMaskSize(allocator) / (sizeof(u32) / sizeof(u8)); // convert from u8 to u32

	// Find how many pages the meta data for the header + allocation mask will take up. 
	// Store the offset to first allocatable, 
	u32 metaDataSizeBytes = AllocatorPaddedSize() + (maskSize * sizeof(u32));
	u32 numberOfMasksUsed = metaDataSizeBytes / allocator->pageSize;
	if (metaDataSizeBytes % allocator->pageSize != 0) {
		numberOfMasksUsed += 1;
	}
	metaDataSizeBytes = numberOfMasksUsed * allocator->pageSize; // This way, allocatable will start on a page boundary

	// Add a debug page at the end
	metaDataSizeBytes += allocator->pageSize;
	numberOfMasksUsed += 1;

	u8* debugPage = (u8*)allocator + metaDataSizeBytes - allocator->pageSize; // Debug page is always one page before allocatable
	return debugPage;
}


#if MEM_DEFINE_NEW
	#define new new(0, __LOCATION__, Memory::GlobalAllocator)
#endif

#if MEM_DEFINE_MALLOC
	#define malloc(bytes) Memory::Allocate(bytes, 0, Memory::GlobalAllocator, __LOCATION__)
	#define free(data) Memory::Release(data, Memory::GlobalAllocator, __LOCATION__)
	#define memset(mem, val, size) Memory::Set(mem, val, size, __LOCATION__)
	#define memcpy(dest, src, size) Memory::Copy(dest, src, size, __LOCATION__)
#endif

#pragma warning(default:6011)
#pragma warning(default:28182)
