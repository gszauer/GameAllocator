# Game Allocator

Game Allocator is a first fit allocator. Every time an allocation is made, the available space is scanned, and the first range of memory that's large enough to hold the requested allocation is returned. There are accelerations structures for allocation that don't align well to a page.

The Sub Allocator breaks a page up into a number of buckets, and keeps a free list for the unused allocations. If all buckets in a page are free, the page is released. Allocating from the free-list is faster than not. 

The Super Allocator combines multiple pages into a single allocation unit. It's intended for sizes that don't play nice, like a 4097 byte allocation that would otherwise waste more space. The super allocator might reserve 2 pages and fit 3 allocations for better utilization.

Both the Sub Allocator and Super Allocator should be tuned for your project. The ```Memory::Allocator``` struct has room for 6 sub/super allocators, depending on a projects needs.