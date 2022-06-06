#pragma once

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

#ifndef ATLAS_U64
#define ATLAS_U64
typedef unsigned long long u64;
static_assert (sizeof(u64) == 8, "u64 should be defined as a 8 byte type");
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
		Allocation* free_64;
		Allocation* free_128;
		Allocation* free_256;
		Allocation* free_512;
		Allocation* free_1024;
		Allocation* free_2048;

		Allocation* active;
		u32 size;
		u32 offsetToAllocatable;
	};

	// TODO: STD class so i can std::vector<int, Memory::STL> 
	
	extern Allocator* GlobalAllocator;
	const u32 DefaultAlignment = 4;
	const u32 PageSize = 4096;
	const u32 MaskTrackerSize = 32;

	// TODO: Make a trim function that will trim memory down so initialize works.
	// u32 PreInitAlign(void** memory, u32* size); // Returns the number of bytes lost
	void Initialize(void* memory, u32 bytes);
	void Shutdown();

	void* Allocate(u32 bytes, u32 alignment = DefaultAlignment, const char* location = 0, Allocator* allocator = 0);
	void Free(void* memory, const char* location = 0, Allocator* allocator = 0);

	void Set(void* memory, u8 value, u32 size, const char* location = 0); 
}

static_assert (sizeof(Memory::Allocator) == 64, "Memory::Allocator should be 64 bytes (512 bits)");
static_assert (sizeof(Memory::Allocation) == 32, "Memory::Allocation should be 32 bytes (256 bits)");

#define malloc(bytes) Memory::Allocate(bytes, Memory::DefaultAlignment, "On line: " xstr(__LINE__) ", in file: " __FILE__, Memory::GlobalAllocator)
#define free(data) Memory::Free(data, "On line: " xstr(__LINE__) ", in file: " __FILE__, Memory::GlobalAllocator)
#define memset(mem, val, size) Memory::Set(mem, val, size, "On line: " xstr(__LINE__) ", in file: " __FILE__)

// TODO: realloc, calloc, etc...
// TODO: Implement the c functions just in case of linking, but with a different __LOCATION__ that would be obvious

// TODO: override new and delete here as well