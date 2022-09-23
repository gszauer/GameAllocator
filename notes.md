# Notes

I didn't put enough importance on aligned allocations. If the goal of the memory manager is to run in a browser, every allocation (even the sub-allocators) should be aligned on a 4 byte boundary, so it's easy to access / set as a uint32 array.

The easiest way to accomodate this is probably to ensure that all allocation headers land on an 4 byte alignment boundary (unless explicitly requested?), and the the header struct is a multiple of 4 bytes as well.