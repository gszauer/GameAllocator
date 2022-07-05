#include "../mem.h"
#define export __attribute__ (( visibility( "default" ) )) extern "C"

extern unsigned char __heap_base;
extern unsigned char __data_end;

export void* mem_initialize(int heap_size) {
    if (heap_size == 0) {
        return 0;
    }

    void* memory = &__heap_base;
    unsigned int size = (unsigned int)heap_size;

    // Initialize the global allocator
    u32 lost = Memory::AlignAndTrim(&memory, &size, 4, Memory::DefaultPageSize);
    Memory::GlobalAllocator = Memory::Initialize(memory, size, Memory::DefaultPageSize);
    
    return Memory::GlobalAllocator;
}

export void* mem_GetGlobalAllocator() {
    return Memory::GlobalAllocator;
}

export int mem_GetTotalBytes(void* a) {
    Memory::Allocator* allocator = (Memory::Allocator*)a;
    return (int)allocator->size;
}

export int mem_GetTotalPages(void* a) {
    Memory::Allocator* allocator = (Memory::Allocator*)a;
    u32 numPages = allocator->size / allocator->pageSize;
    return (int)numPages;
}

static inline u32 AllocatorPageMaskSize(Memory::Allocator* allocator) { // This is the number of u8's that make up the AllocatorPageMask array
    const u32 allocatorNumberOfPages = allocator->size / allocator->pageSize; // 1 page = (probably) 4096 bytes, how many are needed
    const u32 allocatorPageArraySize = allocatorNumberOfPages / Memory::TrackingUnitSize + (allocatorNumberOfPages % Memory::TrackingUnitSize ? 1 : 0);
    return allocatorPageArraySize * (Memory::TrackingUnitSize / 8); // In bytes, not bits
}

export int mem_GetNumOverheadPages(void* a) {
    Memory::Allocator* allocator = (Memory::Allocator*)a;
    u32 maskSize = AllocatorPageMaskSize(allocator) / (sizeof(u32) / sizeof(u8)); // convert from u8 to u32
    u32 metaDataSizeBytes = sizeof(Memory::Allocator) + (maskSize * sizeof(u32));
    u32 numberOfMasksUsed = metaDataSizeBytes / allocator->pageSize;
    if (metaDataSizeBytes % allocator->pageSize != 0) {
        numberOfMasksUsed += 1;
    }
    metaDataSizeBytes = numberOfMasksUsed * allocator->pageSize; // This way, allocatable will start on a page boundary
    // Account for meta data
    metaDataSizeBytes += allocator->pageSize;
    numberOfMasksUsed += 1;

    u32 numOverheadPages = metaDataSizeBytes / allocator->pageSize;
    return numOverheadPages;
}

export int mem_GetRequestedBytes(void* a) {
    Memory::Allocator* allocator = (Memory::Allocator*)a;
    return allocator->requested;
}

export int mem_GetSize(void* a) {
    Memory::Allocator* allocator = (Memory::Allocator*)a;
    return allocator->size;
}

export int mem_GetServedBytes(void* a) {
    Memory::Allocator* allocator = (Memory::Allocator*)a;

    u32 maskSize = AllocatorPageMaskSize(allocator) / (sizeof(u32) / sizeof(u8)); // convert from u8 to u32
    u32 metaDataSizeBytes = sizeof(Memory::Allocator) + (maskSize * sizeof(u32));
    u32 numberOfMasksUsed = metaDataSizeBytes / allocator->pageSize;
    if (metaDataSizeBytes % allocator->pageSize != 0) {
        numberOfMasksUsed += 1;
    }
    metaDataSizeBytes = numberOfMasksUsed * allocator->pageSize; // This way, allocatable will start on a page boundary
    // Account for meta data
    metaDataSizeBytes += allocator->pageSize;
    numberOfMasksUsed += 1;

    u32 numPages = allocator->size / allocator->pageSize;
    u32 usedPages = allocator->numPagesUsed;
    u32 freePages = numPages - usedPages;
    u32 overheadPages = metaDataSizeBytes / allocator->pageSize;

    return (usedPages - overheadPages) * allocator->pageSize;
}

static inline u8* AllocatorPageMask(Memory::Allocator* allocator) {
    return ((u8*)allocator) + sizeof(Memory::Allocator);
}

export bool mem_IsPageInUse(void* a, int page) {
    Memory::Allocator* allocator = (Memory::Allocator*)a;
    u32* mask = (u32*)AllocatorPageMask(allocator);
    u32 i = (u32)page;

    u32 m = i / Memory::TrackingUnitSize;
    u32 b = i % Memory::TrackingUnitSize;

    bool set = mask[m] & (1 << b);
    return set;
}

export void* mem_malloc(void* a, int bytes) {
    Memory::Allocator* allocator = (Memory::Allocator*)a;
	return Memory::Allocate((u32)bytes, 0, allocator, "WebAssembly.cpp, void* mem_malloc(void* allocator, int bytes);");
}

export void mem_free(void* a, void* ptr) {
    Memory::Allocator* allocator = (Memory::Allocator*)a;
	Memory::Release(ptr, allocator, "WebAssembly.cpp, void mem_free(void* allocator, void* ptr);");
}

export int mem_GetDataSectionSize() {
    void* memory = &__data_end;
    return (int)memory;
}

export u32 mem_strlen(const u8* str) {
    const u8* s;
    for (s = str; *s; ++s);
    return (u32)(s - str);
}

static u32 u32toa(u8* dest, u32 destSize, u32 num) { // Returns length of string
    Memory::Set(dest, 0, destSize, "WebAssembly::u32toa");

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

export void* mem_GetAllocationDebugName(void* a, void* _m) {
	const char* l = "mem_GetAllocationDebugName";
    Memory::Allocator* allocator = (Memory::Allocator*)a;

    u8* debugPage = Memory::Debug::DevPage(allocator);
    u32 debugSize = allocator->pageSize;
    
    // Reset memory buffer
	Memory::Set(debugPage, 0, debugSize, l);
	u8* i_to_a_buff = debugPage; // Used to convert numbers to strings
	const u32 i_to_a_buff_size = mem_strlen((const u8*)"18446744073709551615") + 1; // u64 max
	
    u8* mem = i_to_a_buff + i_to_a_buff_size;
	u32 memSize = allocator->pageSize - i_to_a_buff_size;
    
    u8* m = (u8*)_m - sizeof(Memory::Allocation);
    Memory::Allocation* iter = (Memory::Allocation*)m;

    Memory::Copy(mem, "Address: ", 9, l);
    mem += 9; memSize -= 9;

    u32 allocationOffset = (u32)((u8*)iter - (u8*)allocator);
    i32 i_len = u32toa(i_to_a_buff, i_to_a_buff_size, allocationOffset);
    Memory::Copy(mem, i_to_a_buff, i_len, l);
    mem += i_len;
    memSize -= i_len;

    Memory::Copy(mem, ", size: ", 8, l);
    mem += 8; memSize -= 8;

    i_len = u32toa(i_to_a_buff, i_to_a_buff_size, iter->size);
    Memory::Copy(mem, i_to_a_buff, i_len, l);
    mem += i_len;
    memSize -= i_len;

    Memory::Copy(mem, ", padded: ", 10, l);
    mem += 10; memSize -= 10;

    u32 alignment = iter->alignment;
    u32 allocationHeaderPadding = 0;
    if (alignment != 0) {  // Add padding to the header to compensate for alignment
        allocationHeaderPadding = alignment - 1; // Somewhere in this range, we will be aligned
    }

    u32 realSize = iter->size + (u32)(sizeof(Memory::Allocation)) + allocationHeaderPadding;
    i_len = u32toa(i_to_a_buff, i_to_a_buff_size, realSize);
    Memory::Copy(mem, i_to_a_buff, i_len, l);
    mem += i_len;
    memSize -= i_len;

    Memory::Copy(mem, ", alignment: ", 13, l);
    mem += 13; memSize -= 13;

    i_len = u32toa(i_to_a_buff, i_to_a_buff_size, iter->alignment);
    Memory::Copy(mem, i_to_a_buff, i_len, l);
    mem += i_len;
    memSize -= i_len;

    Memory::Copy(mem, ", first page: ", 14, l);
    mem += 14; memSize -= 14;

    i_len = u32toa(i_to_a_buff, i_to_a_buff_size, (allocationOffset) / allocator->pageSize);
    Memory::Copy(mem, i_to_a_buff, i_len, l);
    mem += i_len;
    memSize -= i_len;

    Memory::Copy(mem, ", prev: ", 8, l);
    mem += 8; memSize -= 8;

    i_len = u32toa(i_to_a_buff, i_to_a_buff_size, iter->prevOffset);
    Memory::Copy(mem, i_to_a_buff, i_len, l);
    mem += i_len;
    memSize -= i_len;

    Memory::Copy(mem, ", next: ", 8, l);
    mem += 8; memSize -= 8;

    i_len = u32toa(i_to_a_buff, i_to_a_buff_size, iter->nextOffset);
    Memory::Copy(mem, i_to_a_buff, i_len, l);
    mem += i_len;
    memSize -= i_len;

    u32 pathLen = 0;
#if MEM_TRACK_LOCATION
    if (iter->location != 0) {
        pathLen = mem_strlen((const u8*)iter->location);
    }
#endif

    Memory::Copy(mem, ", location: ", 12, l);
    mem += 12; memSize -= 12;

#if MEM_TRACK_LOCATION
    if (iter->location == 0) {
#else
    {
#endif
        Memory::Copy(mem, "null", 4, l);
        mem += 4; memSize -= 4;
    }   
#if MEM_TRACK_LOCATION
    else {
        Memory::Copy(mem, iter->location, pathLen, l);
        mem += pathLen;
        memSize -= pathLen;
    }
#endif

    *mem = '\0';

    return debugPage + i_to_a_buff_size;
}

extern "C" { 
    extern void wasmBuildMemState(const u8* msg, int len);
}

export void mem_DumpState(void* a) {
    Memory::Allocator* allocator = (Memory::Allocator*)a;

    Memory::Debug::MemInfo(allocator, [](const u8* mem, u32 size, void* userdata) {
        wasmBuildMemState(mem, (int)size);
    }, 0);
}