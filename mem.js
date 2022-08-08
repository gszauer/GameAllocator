class GameAllocator {

    constructor(totalMemoryBytes, heapSize) {
        this.WebAssemblyMemory = null;
        this.memory = null // Alias for WebAssemblyMemory
        this.AllocatorPtr = null;
        this.WasmExports = null;
        this.RequestedBytes = totalMemoryBytes;
        this.HeapSizeBytes = 0;
        this.RequestedHeapSize = heapSize;
        this.GlobalDumpState = "";

        let self = this;

        // WASM is 64 KiB / page (our allocator is 4 KiB);
        let wasmPageSize = 64 * 1024; // 64 KiB
        let wasmNumPages = Math.ceil(totalMemoryBytes / wasmPageSize);
        self.WebAssemblyMemory = new WebAssembly.Memory( {
            initial: wasmNumPages,
            maximum: wasmNumPages
        });
        self.memory = this.WebAssemblyMemory;
    }

    logError(str) {
        console.logError(str);
    }

    logInfo(str) {
        console.log(str);
    }

    InjectWebAssemblyImportObject(importObject) {
        if (!importObject.hasOwnProperty("env")) {
            importObject.env = {};
        }
        importObject.env.memory = this.WebAssemblyMemory;
        let self = this;

        importObject.env["GameAllocator_jsBuildMemState"] = function(ptr, len) {
            const array = new Uint8Array(self.WebAssemblyMemory.buffer, ptr, len);
            const decoder = new TextDecoder()
            const string = decoder.decode(array);
            self.GlobalDumpState += string;
        };
    }

    InitializeWebAssembly(wasmExports) {
        if (!wasmExports) {
            this.logError("invalid exports object");
        }
        this.WasmExports = wasmExports;
        this.AllocatorPtr = this.WasmExports.GameAllocator_wasmInitialize(this.RequestedHeapSize);
        this.HeapSizeBytes = this.WasmExports.GameAllocator_wasmHeapSize(this.RequestedBytes);
        this.logInfo("Requested heap: " + this.RequestedHeapSize + ", actual heap: " + this.HeapSizeBytes);
        if (this.HeapSizeBytes < this.RequestedHeapSize) {
            console.logError("Actual heap size is less than requested heap size");
        }
    }

    ShutdownWebAssembly() {
        this.WasmExports.GameAllocator_wasmShutdown(this.AllocatorPtr);
        this.AllocatorPtr = 0;
    }

    Allocte(bytes, alignment) {
        if (!alignment) {
            alignment = 0;
        }
        if (!bytes || bytes <= 0) {
            this.logError("Can't allocate <=0 bytes!");
            bytes = 1;
        }

        return this.WasmExports.GameAllocator_wasmAllocate(this.AllocatorPtr, bytes, alignment);
    }

    Release(ptr) {
        this.WasmExports.GameAllocator_wasmRelease(this.AllocatorPtr, ptr);
    }

    Set(ptr, value, size) {
        this.WasmExports.GameAllocator_wasmSet(ptr, value, size);
    }

    Copy(dest_ptr, src_ptr, size) {
        this.WasmExports.GameAllocator_wasmCopy(dest_ptr, src_ptr, size);
    }

    GetNumPages() {
        return this.WasmExports.GameAllocator_wasmGetNumPages(this.AllocatorPtr);
    }

    GetNumPagesInUse() {
        return this.WasmExports.GameAllocator_wasmGetNumPagesInUse(this.AllocatorPtr);
    }

    GetPeekPagesUsed() {
        return this.WasmExports.GameAllocator_wasmGetPeekPagesUsed(this.AllocatorPtr);
    }

    GetRequestedBytes() {
        return this.WasmExports.GameAllocator_wasmGetRequestedBytes(this.AllocatorPtr);
    }

    GetServedBytes() {
        return this.WasmExports.GameAllocator_wasmGetServedBytes(this.AllocatorPtr);
    }
    
    IsPageInUse(page) {
        if (page < 0) {
            this.logError("invalid page");
            page = 0;
        }
        let result = this.WasmExports.GameAllocator_wasmIsPageInUse(this.AllocatorPtr, page);
        return result != 0;
    }

    Size() {
        return this.WasmExports.GameAllocator_wasmGetSize(this.AllocatorPtr);
    }

    GetNumOverheadPages() {
        return this.WasmExports.GameAllocator_wasmGetNumOverheadPages(this.AllocatorPtr);
    }

    StrLen(str_ptr) {
        return this.WasmExports.GameAllocator_wasmStrLen(str_ptr);
    }

    GetAllocationDebugInfo(alloc_ptr) {
        return this.WasmExports.GameAllocator_wasmGetAllocationDebugName(this.AllocatorPtr, alloc_ptr);
    }

    DebugDumpState() {
        this.GlobalDumpState = "";
        this.WasmExports.GameAllocator_wasmDumpState(this.AllocatorPtr);
        return this.GlobalDumpState;
    }
}