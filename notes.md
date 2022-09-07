# Notes

I didn't put enough importance on aligned allocations. If the goal of the memory manager is to run in a browser, every allocation (even the sub-allocators) should be aligned on a 4 byte boundary, so it's easy to access / set as a uint32 array.