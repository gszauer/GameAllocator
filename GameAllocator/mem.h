#pragma once

// When allocating new memory, if MEM_FIRST_FIT is defined and set to 1 every allocation will scan
// the available memory from the first bit to the last bit looking for enough space to satisfy the
// allocation. If MEM_FIRST_FIT is set to 0, then the memory is searched iterativley. Ie, when we 
// allocate the position in memory after the allocation is saved, and the next allocation starts
// searching from there.
#define MEM_FIRST_FIT 0

// If set to 1, the allocator will clear memory when allocating it
#define MEM_CLEAR_ON_ALLOC 0

// ^ TODO: Maybe make these flags on the actual allocator?!?!?!?!

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
static_assert (sizeof(u64) == 8, "u64 should be defined as an 8 byte type");
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

#ifndef ATLAS_PTR
	#define ATLAS_PTR
	#if ATLAS_64
		typedef u64 ptr_type;
		static_assert (sizeof(ptr_type) == 8, "ptr_type should be defined as an 8 byte type on a 64 bit system");
	#elif ATLAS_32
		typedef u32 ptr_type;
		static_assert (sizeof(ptr_type) == 4, "ptr_type should be defined as a 4 byte type on a 32 bit system");
	#else
		#error "Invalid platform"
	#endif
#endif

namespace Memory {
	struct Allocation {
		Allocation* prev;
		Allocation* next;
		const char* location;
		u32 size;
		u32 alignment;
	};

	struct Allocator { // TODO: The size isn't really special, just add the fast offsets!
		Allocation* free_64;
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

// https://stackoverflow.com/questions/2653214/stringification-of-a-macro-value
#define atlas_xstr(a) atlas_str(a)
#define atlas_str(a) #a
#define __LOCATION__ "On line: " atlas_xstr(__LINE__) ", in file: " __FILE__

void* malloc(u32 bytes);
void free(void* data);
void memset(void* mem, u8 value, u32 size);
void memcpy(void* dest, const void* src, u32 size);
void* calloc(u32 count, u32 size);
void* realloc(void* mem, u32 size);

#define malloc(bytes) Memory::Allocate(bytes, Memory::DefaultAlignment, __LOCATION__, Memory::GlobalAllocator)
#define free(data) Memory::Release(data, __LOCATION__, Memory::GlobalAllocator)
#define memset(mem, val, size) Memory::Set(mem, val, size, __LOCATION__)
#define memcpy(dest, src, size) Memory::Copy(dest, src, size, __LOCATION__)
#define calloc(numelem, elemsize) Memory::AllocateContigous(numelem, elemsize, Memory::DefaultAlignment, __LOCATION__, Memory::GlobalAllocator)
#define realloc(mem, size) Memory::ReAllocate(mem, size, Memory::DefaultAlignment, __LOCATION__, Memory::GlobalAllocator)

namespace std {
	struct nothrow_t;
}

// C++ 11: https://cplusplus.com/reference/new/operator%20new/
void* __cdecl operator new (decltype(sizeof(0)) size);
void* __cdecl operator new (decltype(sizeof(0)) size, const std::nothrow_t& nothrow_value) noexcept;
// void* __cdecl operator new (decltype(sizeof(0)) size, void* ptr) noexcept; Can't overload placement new

// C++ 14: https://cplusplus.com/reference/new/operator%20delete/
void __cdecl operator delete (void* ptr) noexcept;
void __cdecl operator delete (void* ptr, const std::nothrow_t& nothrow_constant) noexcept;
// void __cdecl operator delete (void* ptr, void* voidptr2) noexcept; // Can't overload placement delete
void __cdecl operator delete (void* ptr, decltype(sizeof(0)) size) noexcept;
void __cdecl operator delete (void* ptr, decltype(sizeof(0)) size, const std::nothrow_t& nothrow_constant) noexcept;

// C++ 11: https://cplusplus.com/reference/new/operator%20new[]/
void* __cdecl operator new[](decltype(sizeof(0)) size);
void* __cdecl operator new[](decltype(sizeof(0)) size, const std::nothrow_t& nothrow_value) noexcept;
// void* __cdecl operator new[](decltype(sizeof(0)) size, void* ptr) noexcept; // Can't overload placement new

// C++ 14: https://cplusplus.com/reference/new/operator%20delete[]/
void __cdecl operator delete[](void* ptr) noexcept;
void __cdecl operator delete[](void* ptr, const std::nothrow_t& nothrow_constant) noexcept;
// void __cdecl operator delete[](void* ptr, void* voidptr2) noexcept; // Can't overload placement delete
void __cdecl operator delete[](void* ptr, decltype(sizeof(0)) size) noexcept;
void __cdecl operator delete[](void* ptr, decltype(sizeof(0)) size, const std::nothrow_t& nothrow_constant) noexcept;

// Tracked new is defined after STL allocator so #define new doesn't mess with placement new
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
		typedef ptr_type difference_type;
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
			return (pointer)Allocate(n, DefaultAlignment, "STLAllocator::allocate", GlobalAllocator);
		}

		/// Free memory of pointer p
		inline void deallocate(void* p, size_type n) {
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
			return size_type(-1);
		}

		/// A struct to rebind the allocator to another allocator of type U
		template<typename U>
		struct rebind {
			typedef STLAllocator<U> other;
		};
	};
}


// Tracked
void* operator new (decltype(sizeof(0)) size, u32 alignment, const char* location, Memory::Allocator* allocator) noexcept;
#define new new(Memory::DefaultAlignment, __LOCATION__, Memory::GlobalAllocator)


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