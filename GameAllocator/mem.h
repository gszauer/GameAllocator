#pragma once

// When allocating new memory, if MEM_FIRST_FIT is defined and set to 1 every allocation will scan
// the available memory from the first bit to the last bit looking for enough space to satisfy the
// allocation. If MEM_FIRST_FIT is set to 0, then the memory is searched iterativley. Ie, when we 
// allocate the position in memory after the allocation is saved, and the next allocation starts
// searching from there.
#define MEM_FIRST_FIT 0

// If set to 1, the allocator will clear memory when allocating it
#define MEM_CLEAR_ON_ALLOC 0
#define MEM_DEBUG_ON_ALLOC 1

// If set to 1, "C" functions for malloc, free, etc are provided. 
#define MEM_IMPLEMENT_MALLOC 1

// If set to 1, #define will be declared for malloc
#define MEM_DEFINE_MALLOC 1

// If set to 1, "C++" functions for new, delete, etc are provided. 
#define MEM_IMPLEMENT_NEW 1

// If set to 1, #define will be declared for new 
#define MEM_DEFINE_NEW 1

// If set to 1, the Memroy::STLAllocator class is defined 
#define MEM_IMPLEMENT_STL 1

// Disables sub-allocators if defined
#define MEM_USE_SUBALLOCATORS 1

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
#endif

namespace Memory {
	typedef void (*AllocationCallback)(struct Allocator* allocator, void* allocationHeaderAddress, u32 bytesRequested, u32 bytesServed, u32 firstPage, u32 numPages);
	typedef void (*ReleaseCallback)(struct Allocator* allocator, void* allocationHeaderAddress, u32 bytesRequested, u32 bytesServed, u32 firstPage, u32 numPages);

	struct Allocation {
		Allocation* prev;
		Allocation* next;
		const char* location;
		u32 size; // Unpadded allocation size. total size is size + sizeof(Allocation) + paddingof(Allocation)
		u32 alignment;

		/* To get the actual allocation size:
		u32 allocationHeaderPadding = sizeof(Allocation) % alignment > 0 ? alignment - sizeof(Allocation) % alignment : 0;
		u32 paddedSize = size + sizeof(Memory::Allocation) + allocationHeaderPadding;
		*/
#if ATLAS_32
		u32 padding_32bit[3];
#endif
	};

	struct Allocator {
		Allocation* free_64;
		Allocation* free_128;
		Allocation* free_256;
		Allocation* free_512;
		Allocation* free_1024;
		Allocation* free_2048;

		Allocation* active;

		AllocationCallback allocateCallback;
		ReleaseCallback releaseCallback;

		u32 size; // In bytes, how much total memory is the allocator managing
		u32 requested; // How many bytes where requested (raw)
		u32 offsetToAllocatable;
		u32 scanBit;

		u32 numPagesUsed;
		u32 padding_64bit;

#if ATLAS_32
		u32 padding_32bit[9];
#endif
	};

	extern Allocator* GlobalAllocator;
	const u32 DefaultAlignment = 4;
	const u32 PageSize = 4096;
	const u32 TrackingUnitSize = 32;

	// You can call AlignAndTrim before Initialize to make sure that memory is aligned to DefaultAlignment
	// and to make sure that the size of the memory (after it's been aligned) is a multiple of PageSize
	// both arguments are modified, the return value is how many bytes where removed
	u32 AlignAndTrim(void** memory, u32* size);

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

	namespace Debug {
		typedef void (*DumpCallback)(const u8* mem, u32 size, void* userdata);
		void DumpAllocator(Allocator* allocator, DumpCallback callback, void* userdata = 0);
		void DumpPage(Allocator* allocator, u32 page, DumpCallback callback, void* userdata = 0);
	}
}

static_assert (sizeof(Memory::Allocator) == 96, "Memory::Allocator should be 72 bytes (768 bits)");
static_assert (sizeof(Memory::Allocation) == 32, "Memory::Allocation should be 32 bytes (256 bits)");

// https://stackoverflow.com/questions/2653214/stringification-of-a-macro-value
#define atlas_xstr(a) atlas_str(a)
#define atlas_str(a) #a
#define __LOCATION__ "On line: " atlas_xstr(__LINE__) ", in file: " __FILE__

#if MEM_DEFINE_MALLOC
#undef malloc
#undef free
#undef memset
#undef memcpy
#undef calloc
#undef realloc
#endif

#if MEM_IMPLEMENT_MALLOC
extern "C" void* __cdecl malloc(decltype(sizeof(0)) bytes);
extern "C" void __cdecl free(void* data);
extern "C" void* __cdecl memset(void* mem, i32 value, decltype(sizeof(0)) size);
extern "C" void* __cdecl memcpy(void* dest, const void* src, decltype(sizeof(0)) size);
extern "C" void* __cdecl calloc(decltype(sizeof(0)) count, decltype(sizeof(0)) size);
extern "C" void* __cdecl realloc(void* mem, decltype(sizeof(0)) size);
//#pragma intrinsic(memset, memcpy); // This is what we DON't WANT
#pragma function(memset, memcpy); // Function, not intrinsic
#endif

#if MEM_DEFINE_MALLOC
#define malloc(bytes) Memory::Allocate(bytes, Memory::DefaultAlignment, __LOCATION__, Memory::GlobalAllocator)
#define free(data) Memory::Release(data, __LOCATION__, Memory::GlobalAllocator)
#define memset(mem, val, size) Memory::Set(mem, val, size, __LOCATION__)
#define memcpy(dest, src, size) Memory::Copy(dest, src, size, __LOCATION__)
#define calloc(numelem, elemsize) Memory::AllocateContigous(numelem, elemsize, Memory::DefaultAlignment, __LOCATION__, Memory::GlobalAllocator)
#define realloc(mem, size) Memory::ReAllocate(mem, size, Memory::DefaultAlignment, __LOCATION__, Memory::GlobalAllocator)
#endif

#if MEM_IMPLEMENT_NEW
namespace std {
	struct nothrow_t;
}

// C++ 11: https://cplusplus.com/reference/new/operator%20new/
void* __cdecl operator new (decltype(sizeof(0)) size);
void* __cdecl operator new (decltype(sizeof(0)) size, const std::nothrow_t& nothrow_value) noexcept;
void* operator new (decltype(sizeof(0)) size, u32 alignment, const char* location, Memory::Allocator* allocator) noexcept; // Non standard, tracked

// C++ 14: https://cplusplus.com/reference/new/operator%20delete/
void __cdecl operator delete (void* ptr) noexcept;
void __cdecl operator delete (void* ptr, const std::nothrow_t& nothrow_constant) noexcept;
void __cdecl operator delete (void* ptr, decltype(sizeof(0)) size) noexcept;
void __cdecl operator delete (void* ptr, decltype(sizeof(0)) size, const std::nothrow_t& nothrow_constant) noexcept;

// C++ 11: https://cplusplus.com/reference/new/operator%20new[]/
void* __cdecl operator new[](decltype(sizeof(0)) size);
void* __cdecl operator new[](decltype(sizeof(0)) size, const std::nothrow_t& nothrow_value) noexcept;

// C++ 14: https://cplusplus.com/reference/new/operator%20delete[]/
void __cdecl operator delete[](void* ptr) noexcept;
void __cdecl operator delete[](void* ptr, const std::nothrow_t& nothrow_constant) noexcept;
void __cdecl operator delete[](void* ptr, decltype(sizeof(0)) size) noexcept;
void __cdecl operator delete[](void* ptr, decltype(sizeof(0)) size, const std::nothrow_t& nothrow_constant) noexcept;
#endif // Tracked new is defined after STL allocator so #define new doesn't mess with placement new

#if MEM_IMPLEMENT_STL
namespace Memory {
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
#error "Invalid platform"
#endif
#endif

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
			return (pointer)Allocate(n * sizeof(T), DefaultAlignment, "STLAllocator::allocate", GlobalAllocator);
		}

		/// Free memory of pointer p
		inline void deallocate(void* p, size_type n) {
#if _DEBUG
			if (GlobalAllocator == 0) { // Poor mans assert
				*(char*)((void*)0) = '\0';
			}
#endif
			Release(p, "STLAllocator::deallocate", GlobalAllocator);
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
			//return size_type(-1);
			return GlobalAllocator->size - GlobalAllocator->offsetToAllocatable;
		}

		/// A struct to rebind the allocator to another allocator of type U
		template<typename U>
		struct rebind {
			typedef STLAllocator<U> other;
		};
	};
}
#endif

#if MEM_DEFINE_NEW
#define new new(Memory::DefaultAlignment, __LOCATION__, Memory::GlobalAllocator)
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
#endif

#ifndef ATLAS_32
#ifndef ATLAS_64
#error "Unknown platform type. Is this 32 or 64 bit?"
#endif
#endif