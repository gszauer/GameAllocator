# Notes

I didn't put enough importance on aligned allocations. If the goal of the memory manager is to run in a browser, every allocation (even the sub-allocators) should be aligned on a 4 byte boundary, so it's easy to access / set as a uint32 array.

The easiest way to accomodate this is probably to ensure that all allocation headers land on an 4 byte alignment boundary (unless explicitly requested?), and the the header struct is a multiple of 4 bytes as well.

The memory library should expose free-standing functions as needed

```
importObject.env.memcpy = function(ptr_dest, ptr_src, int_len) {
    let dst_buffer = new Uint8Array(allocator.memory.buffer, ptr_dest, int_len);
    let src_buffer = new Uint8Array(allocator.memory.buffer, ptr_src, int_len);
    for (let i = 0; i < int_len; ++i) {
        dst_buffer[i] = src_buffer[i];
    }
    return dest;
}
```