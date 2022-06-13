#pragma once

// When allocating new memory, if MEM_FIRST_FIT is defined and set to 1 every allocation will scan
// the available memory from the first bit to the last bit looking for enough space to satisfy the
// allocation. If MEM_FIRST_FIT is set to 0, then the memory is searched iterativley. Ie, when we 
// allocate the position in memory after the allocation is saved, and the next allocation starts
// searching from there.
#define MEM_FIRST_FIT 0

// If set to 1, the allocator will clear memory when allocating it
#define MEM_CLEAR_ON_ALLOC 0

#ifndef ATLAS_U8
#define ATLAS_U8
typedef unsigned char u8;
static_assert (sizeof(u8) == 1, "u8 should be defined as a 1 byte type");
#endif 

#ifndef ATLAS_U32
#define ATLAS_U32
typedef unsigned int u32;
static_assert (sizeof(u32) == 4, "u32 should be defined as a 4 byte type");
#endif 

namespace Memory {
	struct Allocation {
		Allocation* prev;
		Allocation* next;
		const char* location;
		u32 size;
		u32 alignment;
	};

	struct Allocator {
		Allocation* free_64; // I can probably just remove this...
		Allocation* free_128;
		Allocation* free_256;
		Allocation* free_512;
		Allocation* free_1024;
		Allocation* free_2048;

		Allocation* active;
		u32 size;
#if MEM_FIRST_FIT
		// TODO: Remove free_64 so we can have fast scan bit and offset.
		u32 offsetToAllocatable;
#else
		u32 scanBit;
#endif
	};

	// TODO: STD class so i can std::vector<int, Memory::STL> 
	
	extern Allocator* GlobalAllocator;
	const u32 DefaultAlignment = 4;
	const u32 PageSize = 4096;
	const u32 TrackingUnitSize = 32;

	// TODO: Make a trim function that will trim memory down so initialize works.
	// u32 PreInitAlign(void** memory, u32* size); // Returns the number of bytes lost

	Allocator* Initialize(void* memory, u32 bytes);
	void Shutdown(Allocator* allocator);

	Allocator* GetGlobalAllocator();
	void SetGlobalAllocator(Allocator* allocator);

	void* Allocate(u32 bytes, u32 alignment = DefaultAlignment, const char* location = 0, Allocator* allocator = 0);
	void Release(void* memory, const char* location = 0, Allocator* allocator = 0);

	void Set(void* memory, u8 value, u32 size, const char* location = 0); 
	void Copy(void* dest, const void* source, u32 size, const char* location = 0);

	void* AllocateContigous(u32 num_elems, u32 elem_size, u32 alignment = DefaultAlignment, const char* location = 0, Allocator* allocator = 0);
	void* ReAllocate(void* mem, u32 newSize, u32 newAlignment = DefaultAlignment, const char* location = 0, Allocator* allocator = 0);
}

static_assert (sizeof(Memory::Allocator) == 64, "Memory::Allocator should be 64 bytes (512 bits)");
static_assert (sizeof(Memory::Allocation) == 32, "Memory::Allocation should be 32 bytes (256 bits)");

#define xstr(a) str(a)
#define str(a) #a
#define __LOCATION__ "On line: " xstr(__LINE__) ", in file: " __FILE__

#define malloc(bytes) Memory::Allocate(bytes, Memory::DefaultAlignment, __LOCATION__, Memory::GlobalAllocator)
#define free(data) Memory::Release(data, __LOCATION__, Memory::GlobalAllocator)
#define memset(mem, val, size) Memory::Set(mem, val, size, __LOCATION__)
#define memcpy(dest, src, size) Memory::Copy(dest, src, size, __LOCATION__)
#define calloc(numelem, elemsize) Memory::AllocateContigous(numelem, elemsize, Memory::DefaultAlignment, __LOCATION__, Memory::GlobalAllocator)
#define realloc(mem, size) Memory::ReAllocate(mem, size, Memory::DefaultAlignment, __LOCATION__, Memory::GlobalAllocator);
//TODO: Probably want to implement these ac C functions in the source file, undef and re-def them

// TODO: override new and delete here as well




#if _WIN64
	#ifdef ATLAS_32
		#error Can't define both 32 and 64 bit system
	#endif
	#define ATLAS_64 1
#elif _WIN32
	#ifdef ATLAS_64
		#error Can't define both 32 and 64 bit system
	#endif
	#define ATLAS_32 1
#endif

#ifndef ATLAS_32
	#ifndef ATLAS_64
		#error "Unknown platform type. Is this 32 or 64 bit?"
	#endif
#endif