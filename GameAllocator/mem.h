#pragma once

/*
Game Memory Allocator:

	Game Allocator is a generic memory manager intended for games, embedded devices, and web assembly.
	Given a large array of memory, the library provides functions to allocate and release that memory similar to malloc / free.
	The memory will be broken up into pages (4 KiB by default) and tracked at the page granularity. 
	A sub-allocator provided which breaks the page up into a fast free list for smaller allocation.
	Implementations and #defines for malloc, new, new[], free, delete, and delete[] are optionally provided.
	An optional STL allocator is also optionally provided

Usage:

	Let's assume you have a void* to some large area of memory and know how many bytes large that area is.

	Call the Memory::Initialize function. The first two arguments are the memory and size, the third argument
	is the page size with which the memory should be managed. The default page size is 4 KiB

	The memory being passed it should be 8 byte aligned, and the size of the memory should be a multiple of pageSize.

	The Memory::AlignAndTrim helper function will align a region of memory so it's ready for initialize.
	It modifies the memory and size variables that are passed to the function. AlignAndTrim returns the number of bytes lost.

	After creating the main allocator, set the Memory::GlobalAllocator pointer returned by Memory::Initialize. 
	If an allocator isn't specified (like the default new operator, or malloc) the GlobalAllocator will be used.

	After setting the global allocator you can allocate memory with the Memory::Allocate function, and release
	memory with the Memory::Release function. These functions require the same arguments as malloc & free,
	they have some additional arguments as well. Allocate takes an optional alignment, which by default is 0.
	Un-aligned allocation are prefered, they will be 4 or 8 byte aligned, and can utilize a fast free list allocator.
	Both the Allocate and Release functions take an optional Allocator*, this allows the system to support multiple
	allocator. Both functions also take a const char* which is optionally the location of the allocation.

	When you are finished with an allocator, clean it up by calling Memory::Shutdown. The shutdown function 
	will assert in debug builds if there are any memory leaks.

Example:

	void run() {
		// Declare how much memory to use
		// Adding (DefaultPageSize - 1) to size ensures that there is enough space for padding
		unsigned int size = MB(512) + (DefaultPageSize - 1); 

		// Allocate memory from the operating system
		LPVOID memory = VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE); // Windows

		// Initialize the global allocator
		u32 lost = Memory::AlignAndTrim(&m, &size, Memory::DefaultPageSize);
		Memory::GlobalAllocator = Memory::Initialize(m, size, Memory::DefaultPageSize);

		// Allocate & release memory
		int* number = Memory::Allocate(sizeof(int)); // Only the number of bytes is required
		Memory::Release(number); // Only the void* is required

		// Cleanup the global allocator
		Memory::Shutdown(Memory::GlobalAllocator);
		Memory::GlobalAllocator = 0;

		// Release memory back to operating system
		VirtualFree(memory, 0, MEM_RELEASE);
	}

Compile flags:

	MEM_FIRST_FIT         -> This affects how fast memory is allocated. If it's set then every allocation
	                         searches for the first available page from the start of the memory. If it's not
					         set, then an allocation header is maintained. It's advanced with each allocation,
					         and new allocations search for memory from the allocation header.
	MEM_CLEAR_ON_ALLOC    -> When set, memory will be cleared to 0 before being returned from Memory::Allocate
	                         If both clear and debug on alloc are set, clear will take precedence
	MEM_DEBUG_ON_ALLOC    -> If set, full page allocations will fill the padding of the page with "-MEMORY"
	MEM_IMPLEMENT_MALLOC  -> Provide function declarations and implementations for: malloc, free, memset
	                         memcpy, calloc and realloc. This method can not track allocation location.
	MEM_DEFINE_MALLOC     -> If set, malloc will be declared as a #define for Memory::Allocate
	                         and similarly free will be declared as a #define for Memory::Release
	MEM_IMPLEMENT_NEW     -> Provide function declarations and implementations for new & delete
	MEM_DEFINE_NEW        -> If set, new will be declared as a #define for Memory::Allocate,
	                         and similarly delete will be declared as a #define for Memory::Release
	MEM_IMPLEMENT_STL     -> If set, STLAllocator is defined. It can be used for STL allocators like so:
	                         std::vector<int, Memory::STLAllocator<int>> numbers;
	MEM_USE_SUBALLOCATORS -> If set, small allocations will be made using a free list allocaotr. There are free list
	                         allocators for 64, 128, 256, 512, 1024 and 2049 byte allocations. Only allocations that
							 don't specify an alignment can use the fast free list allocator. The sub-allocator will
							 provide better page utilization, for example a 4096 KiB page can hold 32 128 bit allocations.
	MEM_TRACK_LOCATION    -> If set, a const char* will be added to Memory::Allocation which tracks the __LINE__ and __FILE__
	                         of each allocation. Setting this bit will add 8 bytes to the Memory::Allocation struct.

Debugging:

	There are a few debug functions exposed in the Memory::Debug namespace. When an allocator is initialized, the page
	immediateley before the first allocatable page is reserved as a debug page. You can fill this page with whatever 
	data is needed. Any function in Memory::Debug might overwrite the contents of the debug page. You can get a pointer
	to the debug page of an allocator with the Memory::Debug::DevPage function.

	The Memory::Debug::MemInfo function can be used to retrieve information about the state of the memory allocator.
	It provides meta data like how many pages are in use, a list of active allocations, and a visual bitmap chart to
	make debugging the memory bitmask easy. You can write this information to a file like so:

	DeleteFile(L"MemInfo.txt");
	HANDLE hFile = CreateFile(L"MemInfo.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	Memory::Debug::MemInfo(Memory::GlobalAllocator, [](const u8* mem, u32 size, void* fileHandle) {
		HANDLE file = *(HANDLE*)fileHandle;
		DWORD bytesWritten;
		WriteFile(file, mem, size, &bytesWritten, nullptr);
	}, &hFile);
	CloseHandle(hFile);
	
	There is a similar Memory::Debug::PageContent, which given a page number will dump the binary conent of a page.

Resources:

	Compile without CRT 
		https://yal.cc/cpp-a-very-tiny-dll/

	Ready Set Allocate:
		https://web.archive.org/web/20120419125628/http://www.altdevblogaday.com/2011/04/11/ready-set-allocate-part-1/
		https://web.archive.org/web/20120419125404/http://www.altdevblogaday.com/2011/04/26/ready-set-allocate-part-2/
		https://web.archive.org/web/20120419010208/http://www.altdevblogaday.com/2011/05/15/ready-set-allocate-part-3/
		https://web.archive.org/web/20120418212016/http://www.altdevblogaday.com/2011/05/26/ready-set-allocate-part-4/
		https://web.archive.org/web/20120413201435/http://www.altdevblogaday.com/2011/06/08/ready-set-allocate-part-5/
		https://web.archive.org/web/20120321205231/http://www.altdevblogaday.com/2011/06/30/ready-set-allocate-part-6/

	How to combine __LINE__ and __FILE__ into a c string:
		https://stackoverflow.com/questions/2653214/stringification-of-a-macro-value

	C++ overload new, new[], delete, and delete[]
		https://cplusplus.com/reference/new/operator%20new/
		https://cplusplus.com/reference/new/operator%20delete/
		https://cplusplus.com/reference/new/operator%20new[]/
		https://cplusplus.com/reference/new/operator%20delete[]/

	Memory alignment discussion:
		https://stackoverflow.com/questions/227897/how-to-allocate-aligned-memory-only-using-the-standard-library

	Scott Schurr's const string
		https://www.youtube.com/watch?v=BUnNA2dLRsU
*/

#pragma warning(disable:28251)

// When allocating new memory, if MEM_FIRST_FIT is defined and set to 1 every allocation will scan
// the available memory from the first bit to the last bit looking for enough space to satisfy the
// allocation. If MEM_FIRST_FIT is set to 0, then the memory is searched iterativley. Ie, when we 
// allocate the position in memory after the allocation is saved, and the next allocation starts
// searching from there.
#define MEM_FIRST_FIT 1

// If set to 1, the allocator will clear or fill memory when allocating it
#define MEM_CLEAR_ON_ALLOC 1 // Clears memory on each allocation
#define MEM_DEBUG_ON_ALLOC 1 // Fills memory with Memory- on each allocation

// If set to 1, "C" functions for malloc, free, etc are provided. 
#define MEM_IMPLEMENT_MALLOC 1

// If set to 1, #define will be declared for malloc
#define MEM_DEFINE_MALLOC 1

// If set to 1, C++ functions for new, delete, etc are provided. 
#define MEM_IMPLEMENT_NEW 1

// If set to 1, #define will be declared for new 
#define MEM_DEFINE_NEW 1

// If set to 1, the Memroy::STLAllocator class is defined 
#define MEM_IMPLEMENT_STL 1

// Disables sub-allocators if defined
#define MEM_USE_SUBALLOCATORS 1

// If true, adds char* to each allocation
#define MEM_TRACK_LOCATION 1

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

#ifndef ATLAS_I32
	#define ATLAS_I32
	typedef __int32 i32;
	static_assert (sizeof(i32) == 4, "i32 should be defined as a 4 byte type");
#endif 

#ifndef ATLAS_U64
	#define ATLAS_U64
	typedef unsigned long long u64;
	static_assert (sizeof(u64) == 8, "u64 should be defined as an 8 byte type");
#endif

#ifndef ATLAS_I64
	#define ATLAS_I64
	typedef long long i64;
	static_assert (sizeof(i64) == 8, "i64 should be defined as an 8 byte type");
#endif

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
#else
	#error Unknown platform
#endif

namespace Memory {
	// The callback allocator can be used to register a callback with each allocator. It's the same callback signature for both Allocate and Release
	typedef void (*Callback)(struct Allocator* allocator, void* allocationHeaderAddress, u32 bytesRequested, u32 bytesServed, u32 firstPage, u32 numPages);

	// Allocation struct uses a 32 bit offset instead of a pointer. This makes the maximum amount of memory GameAllocator can manage be 4 GiB
	typedef u32 Offset32;

	struct Allocation {
#if MEM_TRACK_LOCATION
		const char* location;
	#if ATLAS_32
		u32 padding_32bit; // Keep sizeof(Allocation) consistent between x64 & x86
	#endif
#endif
		Offset32 prevOffset; // Offsets are the number of bytes from allocator
		Offset32 nextOffset;
		u32 size; // Unpadded allocation size, ie what you pass to malloc
		u32 alignment;
	};

	// Unlike Allocation, Allocator uses pointers. There is only ever one allocator
	// and saving a few bytes here isn't that important. Similarly, the free list
	// pointers exist even if MEM_USE_SUBALLOCATORS is off. This is done to keep the
	// size of this struct consistent for debugging.
	struct Allocator {
		Callback allocateCallback;	// Callback for malloc / new
		Callback releaseCallback;	// Callback for free / delete

		Allocation* free_64;		// The max size for each of these lists is whatever the number after the
		Allocation* free_128;       // underscore is, minus the size of the Allocation structure, which is 
		Allocation* free_256;       // either 16 or 24 bytes (depenging if the location is tracked or not).
		Allocation* free_512;       // For exampe, the largest allocation the 64 byte free list can hold is 50 bytes
		Allocation* free_1024;      // There isn't much significance to these numbers, tune them to better match your structs
		Allocation* free_2048;		// Only unaligned allocations (alignment of 0) can utilize the sub allocators.

		Allocation* active;			// Memory that has been allocated, but not released

		u32 size;					// In bytes, how much total memory is the allocator managing
		u32 requested;				// How many bytes where requested (raw)
		u32 pageSize;				// Default is 4096, but each allocator can have a unique size
		u32 scanBit;				// Only used if MEM_FIRST_FIT is off

		u32 numPagesUsed;
		u32 peekPagesUsed;			// Use this to monitor how much memory your application actually needs

#if ATLAS_32
		u32 padding_32bit[9];		// Padding to make sure the struct stays the same size in x64 / x86 builds
#endif
	};

	// This is the allocator that malloc / new will use. You can have as many allocators as needed,
	// but one of them should always be designated as the global allocator
	extern Allocator* GlobalAllocator; 
	// 4 KiB is a good default page size. Most of your small allocations will go trough the sub-allocators
	// so this page size is mostly important for larger allocations. Feel free to change to something more
	// appropriate if needed.
	const u32 DefaultPageSize = 4096;


	// Don't change tracking unit size. The bitmask that tracks which pages are free is stored as an
	// array of 32 bit integers. Changing this number would require changing how mem.cpp is implemented
	const u32 TrackingUnitSize = 32;
	// Don't change allocator alignment. Every allocator should start at an 8 byte aligned memory address.
	// Internally the allocator uses offsets to access some data, the start alignment is important.
	const u32 AllocatorAlignment = 8; // Should stay 8, even on 32 bit platforms

	// Call AlignAndTrim before Initialize to make sure that memory is aligned to alignment
	// and to make sure that the size of the memory (after it's been aligned) is a multiple of pageSize
	// both arguments are modified, the return value is how many bytes where removed
	u32 AlignAndTrim(void** memory, u32* size, u32 alignment = AllocatorAlignment, u32 pageSize = DefaultPageSize);

	// The initialize function will place the Allocator struct at the start of the provided memory. 
	// The allocaotr struct is followed by a bitmask, in which each bit tracks if a page is in use or not.
	// The bitmask is a bit u32 array. If the end of the bitmask is in the middle of a page, the rest of that
	// page is lost as padding. The next page is a debug page that you can use for anything, only functions in
	// the Memory::Debug namespace mess with the debug page, anything in Memory:: doesn't touch it.
	// The allocator that's returned should be used to set the global allocator.
	Allocator* Initialize(void* memory, u32 bytes, u32 pageSize = DefaultPageSize);

	// After you are finished with an allocator, shut it down. The shutdown function will assert in a debug build
	// if you have any memory that was allocated but not released. This function doesn't do much, it exists
	// to provide a bunch of asserts that ensure that an application is shutting down cleanly.
	void Shutdown(Allocator* allocator);

	// Functions to allocate / release memory from an allocator. The API is self explanatory. You can use the 
	// __LOCATION__ defined at the end of this file to provide an optional location for each api call. 
	// Keep in mind that the location is optional and only used if MEM_TRACK_LOCATION is on
	void* Allocate(u32 bytes, u32 alignment = 0, Allocator* allocator = 0, const char* location = 0);
	void Release(void* memory, Allocator* allocator = 0, const char* location = 0);

	// Memset and Memcpy utility functions. One big difference is that this set function only takes a u8.
	// both of these functions work on larger data types, then work their way down. IE: they try to set or
	// copy the memory using u64's, then u32's, then u16's, and finally u8's
	void Set(void* memory, u8 value, u32 size, const char* location = 0);
	void Copy(void* dest, const void* source, u32 size, const char* location = 0);

	// The debug namespace let's you access information about the current state of the allocator,
	// gives you access to the contents of a page for debugging, and contains a debug page that
	// you can use for whatever. Be careful tough, MemInfo and PageContent might write to the
	// debug page, invalidating what was previously in there.
	namespace Debug {
		typedef void (*WriteCallback)(const u8* mem, u32 size, void* userdata);

		void MemInfo(Allocator* allocator, WriteCallback callback, void* userdata = 0);
		void PageContent(Allocator* allocator, u32 page, WriteCallback callback, void* userdata = 0);
		u8* DevPage(Allocator* allocator);
	}

	// Define pointer types based on current platform.
#ifndef ATLAS_PTR
	#define ATLAS_PTR
	#if ATLAS_64
		typedef u64 ptr_type;
		typedef i64 diff_type;
		static_assert (sizeof(ptr_type) == 8, "ptr_type should be defined as an 8 byte type on a 64 bit system");
		static_assert (sizeof(diff_type) == 8, "diff_type should be defined as an 8 byte type on a 64 bit system");
	#elif ATLAS_32
		typedef u32 ptr_type;
		typedef i32 diff_type;
		static_assert (sizeof(ptr_type) == 4, "ptr_type should be defined as a 4 byte type on a 32 bit system");
		static_assert (sizeof(diff_type) == 4, "diff_type should be defined as a 4 byte type on a 32 bit system");
	#else
		#error Unknown platform
	#endif
#endif
}


// Some compile time asserts to make sure that all our memory is sized correctly and aligns well
static_assert (sizeof(Memory::Allocator) % 8 == 0, "Memory::Allocator size needs to be 8 byte alignable for the allocation mask to start on u64 alignment without any padding");
static_assert (sizeof(Memory::Allocation) % 8 == 0, "Memory::Allocation should be 8 byte alignable");
static_assert (Memory::TrackingUnitSize% Memory::AllocatorAlignment == 0, "Memory::MaskTrackerSize must be a multiple of 8 (bits / byte)");
static_assert (sizeof(Memory::Allocator) == 96, "Memory::Allocator should be 72 bytes (768 bits)");
#if MEM_TRACK_LOCATION
static_assert (sizeof(Memory::Allocation) == 24, "Memory::Allocation should be 24 bytes (192 bits)");
#else
static_assert (sizeof(Memory::Allocation) == 16, "Memory::Allocation should be 16 bytes (128 bits)");
#endif

// Use the __LOCATION__ macro to pack both __LINE__ and __FILE__ into a c string
#define atlas_xstr(a) atlas_str(a)
#define atlas_str(a) #a
#define __LOCATION__ "On line: " atlas_xstr(__LINE__) ", in file: " __FILE__

// Provide interface for malloc / free
#if MEM_IMPLEMENT_MALLOC
	#if MEM_DEFINE_MALLOC
		#undef malloc
		#undef free
		#undef memset
		#undef memcpy
	#endif

	extern "C" void* __cdecl malloc(Memory::ptr_type bytes);
	extern "C" void __cdecl free(void* data);
	extern "C" void* __cdecl memset(void* mem, i32 value, Memory::ptr_type size);
	extern "C" void* __cdecl memcpy(void* dest, const void* src, Memory::ptr_type size);
	// for MSVC, these are functions, not intrinsic
	#pragma function(memset, memcpy)
#endif

// Provide interface for new / delete
#if MEM_IMPLEMENT_NEW
	#if MEM_DEFINE_NEW
		#undef new
		#undef delete
	#endif

	namespace std {
		struct nothrow_t;
	}

	void* __cdecl operator new (Memory::ptr_type size);
	void* __cdecl operator new (Memory::ptr_type size, const std::nothrow_t& nothrow_value) noexcept;
	void* operator new (Memory::ptr_type size, u32 alignment, const char* location, Memory::Allocator* allocator) noexcept; // Non standard, tracked

	void __cdecl operator delete (void* ptr) noexcept;
	void __cdecl operator delete (void* ptr, const std::nothrow_t& nothrow_constant) noexcept;
	void __cdecl operator delete (void* ptr, Memory::ptr_type size) noexcept;
	void __cdecl operator delete (void* ptr, Memory::ptr_type size, const std::nothrow_t& nothrow_constant) noexcept;

	void* __cdecl operator new[](Memory::ptr_type size);
	void* __cdecl operator new[](Memory::ptr_type size, const std::nothrow_t& nothrow_value) noexcept;

	void __cdecl operator delete[](void* ptr) noexcept;
	void __cdecl operator delete[](void* ptr, const std::nothrow_t& nothrow_constant) noexcept;
	void __cdecl operator delete[](void* ptr, Memory::ptr_type size) noexcept;
	void __cdecl operator delete[](void* ptr, Memory::ptr_type size, const std::nothrow_t& nothrow_constant) noexcept;
#endif // The tracked allocator is #define-d as new is defined after STL allocator so #define new doesn't mess with placement new

// Provide interface for STL allocators
#if MEM_IMPLEMENT_STL
namespace Memory {
	template<typename T>
	struct stl_identity {
		typedef T type;
	};

	template <typename T>
	T&& stl_forward(typename stl_identity<T>::type& param) {
		return static_cast<T&&>(param);
	}

	template<typename T>
	class STLAllocator {
	public:
		typedef ptr_type size_type;
		typedef diff_type difference_type;
		typedef T* pointer;
		typedef const T* const_pointer;
		typedef T& reference;
		typedef const T& const_reference;
		typedef T value_type;
	public:
		/// Default constructor
		inline STLAllocator() throw() { }

		inline ~STLAllocator() { }

		/// Copy constructor
		inline STLAllocator(const STLAllocator& other) throw() { }

		/// Copy constructor with another type
		template<typename U>
		inline STLAllocator(const STLAllocator<U>&) throw() { }

		/// Copy
		inline STLAllocator<T>& operator=(const STLAllocator& other) {
			return *this;
		}

		/// Copy with another type
		template<typename U>
		inline STLAllocator& operator=(const STLAllocator<U>& other) {
			return *this;
		}

		/// Get address of a reference
		inline pointer address(reference x) const {
			return &x;
		}

		/// Get const address of a const reference
		inline const_pointer address(const_reference x) const {
			return &x;
		}

		/// Allocate n elements of type T
		inline pointer allocate(size_type n, const void* hint = 0) {
#if _DEBUG
			if (GlobalAllocator == 0) { // Poor mans assert
				*(char*)((void*)0) = '\0';
			}
#endif
			return (pointer)Allocate(n * sizeof(T), 0, GlobalAllocator, "STLAllocator::allocate");
		}

		/// Free memory of pointer p
		inline void deallocate(void* p, size_type n) {
#if _DEBUG
			if (GlobalAllocator == 0) { // Poor mans assert
				*(char*)((void*)0) = '\0';
			}
#endif
			Release(p, GlobalAllocator, "STLAllocator::deallocate");
		}

		/// Call the constructor of p
		inline void construct(pointer p, const T& val) {
			new ((T*)p) T(val);
		}

		/// Call the constructor of p with many arguments. C++11
		template<typename U, typename... Args>
		inline void construct(U* p, Args&&... args) {
			::new((void*)p) U(stl_forward<Args>(args)...);
		}

		/// Call the destructor of p
		inline void destroy(pointer p) {
			p->~T();
		}

		/// Call the destructor of p of type U
		template<typename U>
		inline void destroy(U* p) {
			p->~U();
		}

		/// Get the max allocation size
		inline size_type max_size() const {
#if _DEBUG
			if (GlobalAllocator == 0) { // Poor mans assert
				*(char*)((void*)0) = '\0';
			}
#endif
			return GlobalAllocator->size;
		}

		/// A struct to rebind the allocator to another allocator of type U
		template<typename U>
		struct rebind {
			typedef STLAllocator<U> other;
		};
	};
} // End namespace memory
#endif

// Finish overriding new and delete
#if MEM_DEFINE_NEW
	#define new new(0, __LOCATION__, Memory::GlobalAllocator)
#endif

#if MEM_DEFINE_MALLOC
	#define malloc(bytes) Memory::Allocate(bytes, 0, Memory::GlobalAllocator, __LOCATION__)
	#define free(data) Memory::Release(data, Memory::GlobalAllocator, __LOCATION__)
	#define memset(mem, val, size) Memory::Set(mem, val, size, __LOCATION__)
	#define memcpy(dest, src, size) Memory::Copy(dest, src, size, __LOCATION__)
#endif

// Make sure hte platform is set
#ifndef ATLAS_32
	#ifndef ATLAS_64
		#error "Unknown platform type. Is this 32 or 64 bit?"
	#endif
#endif

#pragma warning(default:28251)