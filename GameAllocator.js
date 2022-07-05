var Allocator = null;   // Allocation wrapper
var pageBoxes = null;   // Array of div's
var PagesInUse = null;  // Array of bool's
var HeapSize = 0;       // In bytes, user provided
var Allocations = null; // Array of malloc results
var ProgramMemory = null;
var GlobalDumpState = null;

var initPhase = 0;
function init() {
    if (initPhase == 0) {
        // Hide input form
        let configDiv = document.getElementById("config");
        configDiv.style.display = "none";

        // WASM is 64 KiB / page (our allocator is 4 KiB);
        let wasmPageSize = 64 * 1024; // 64 KiB

        let heapSizediv = document.getElementById("heap-size");
        HeapSize = Math.ceil(heapSizediv.valueAsNumber);
        let heapUnitDiv = document.getElementById("heap-unit");
        if (heapUnitDiv.value == "kilobytes") {
            HeapSize = Math.ceil(HeapSize * 1024);
        }
        else if (heapUnitDiv.value == "megabytes") {
            HeapSize = Math.ceil(HeapSize * 1024 * 1024);
        }
        let StackSize = 4194304;  // Defined when compilin

        // The total memory we should request is: data + stack + heap
        let wasmHeapNumPages = Math.ceil(HeapSize / wasmPageSize); // How many pages? (will never grow)
        let wasmStackNumPages = Math.ceil(StackSize / wasmPageSize);
        let wasmDataNumPages = Math.ceil((1 * 1024 * 1024) / wasmPageSize); // I don't think we can know this in advance. Assuming it's going to be 1 page.
        
        let wasmNumPages = wasmHeapNumPages + wasmStackNumPages + wasmDataNumPages;
        ProgramMemory = new WebAssembly.Memory({ initial: wasmNumPages, maximum: wasmNumPages });

        let importObject = {
            module: {},
            env: {
                memory: ProgramMemory,
                wasmConsoleLog: function(ptr, len) {
                    const array = new Uint8Array(ProgramMemory.buffer, ptr, len);
                    const decoder = new TextDecoder()
                    const string = decoder.decode(array);
                    console.log(string);
                },
                wasmBuildMemState: function(ptr, len) {
                    const array = new Uint8Array(ProgramMemory.buffer, ptr, len);
                    const decoder = new TextDecoder()
                    const string = decoder.decode(array);
                    GlobalDumpState += string;
                },
                wasmConsoleLogNumber(num) {
                    console.log(num);
                }
            }
        };

        WebAssembly.instantiateStreaming(fetch('GameAllocator.wasm'), importObject).then(prog => {
            Allocator = prog.instance;
        });
        
        Allocations = []; // Init allocation array

        // Move onto next phase
        initPhase++;
        setTimeout(init, 1);
    }
    else if (initPhase == 1) { 
        // Wait for WebAssembly.instantiateStreaming to finish and wasm to become valid
        if (Allocator == null) {
            setTimeout(init, 100);
            return;
        }

        // It's valid....
        let wasmInstance = Allocator;
        let memoryPointer = wasmInstance.exports.mem_initialize(HeapSize);

        Allocator = {
            instance: wasmInstance, 
            memory: ProgramMemory,
            pointer: memoryPointer,

            GetTotalBytes: function() {
                return this.instance.exports.mem_GetTotalBytes(this.pointer);
            },
            GetTotalPages: function() {
                return this.instance.exports.mem_GetTotalPages(this.pointer);
            },
            GetNumOverheadPages: function() {
                return this.instance.exports.mem_GetNumOverheadPages(this.pointer);
            },
            GetRequestedBytes: function() {
                return this.instance.exports.mem_GetRequestedBytes(this.pointer);
            },
            GetServedBytes: function() {
                return this.instance.exports.mem_GetServedBytes(this.pointer);
            },
            IsPageInUse: function(pageNumber) {
                return this.instance.exports.mem_IsPageInUse(this.pointer, pageNumber);
            },
            Size: function() {
                return this.instance.exports.mem_GetSize(this.pointer);
            },
            Malloc: function(bytes) {
                return this.instance.exports.mem_malloc(this.pointer, bytes);
            },
            Free: function(ptr) {
                this.instance.exports.mem_free(this.pointer, ptr);
            },
            GetDataSectionSize: function() {
                return this.instance.exports.mem_GetDataSectionSize();
            },
            GetAllocationDebugInfo: function(ptr) {
                return this.instance.exports.mem_GetAllocationDebugName(this.pointer, ptr);
            },
            StrLen: function(str) {
                return this.instance.exports.mem_strlen(str);
            },
            DumpState: function(ptr) {
                this.instance.exports.mem_DumpState(this.pointer);
            }
        }

        if (Allocator.GetDataSectionSize() >= (1 * 1024 * 1024)) {
            alert("Error, data section is > 1 MiB");
            console.error("Error, data section is > 1 MiB");
        }

        // Move onto the next phase
        initPhase++;
        setTimeout(init, 1);
    }
    else if (initPhase == 2) { // The allocator is now set up, do HTML stuff
        let memoryDiv = document.getElementById("mem");
        let heapSizediv = document.getElementById("heap-size");
        let chartDiv = document.getElementById("chart");

        let numPages = Allocator.GetTotalPages();
        pageBoxes = [];

        let children = chartDiv.children;
        if (children !== null) { // Purge all old pages
            for(let i = children.length - 1; i >= 0; --i) {
                var child = children[i].remove();
            }
        }
        for (let i = 0, size = numPages; i < size; ++i) { // Add fresh pages
            let box = document.createElement("div");
            chartDiv.appendChild(box);
            box.classList.add('blue');
            pageBoxes.push(box);
        }
        
        // Show the memory screen & let this function return
        memoryDiv.style.display = "block";

        PagesInUse = [];
        for (let i = 0, size = Allocator.GetTotalPages(); i < size; ++i) {
            PagesInUse.push(false);
        }

        setTimeout(update, 1);
        initPhase++;
    }
    else {
        console.log("init called unexpectedly");
    }
}

function TriggerDownload(filename, text) {
    var element = document.createElement('a');
    element.setAttribute('href', 'data:text/plain;charset=utf-8,' + encodeURIComponent(text));
    element.setAttribute('download', filename);
    element.style.display = 'none';
    document.body.appendChild(element);
    element.click();
    document.body.removeChild(element);
}

function ClearSelectBox(selectElement) {
    var i, L = selectElement.options.length - 1;
    for(i = L; i >= 0; i--) {
        selectElement.remove(i);
    }
}

function update() {
    let memBytes = Allocator.GetTotalBytes();
    let overheadPages = Allocator.GetNumOverheadPages();
    let requestedBytes = Allocator.GetRequestedBytes();
    let servedBytes = Allocator.GetServedBytes();

    let freePages = 0;
    let usedPages = 0;
    let numPages = PagesInUse.length;

    for (let i = 0; i < numPages; ++i) {
        let inUse = Allocator.IsPageInUse(i);
        let box = pageBoxes[i];

        box.classList.remove('red');
        box.classList.remove('blue');
        box.classList.remove('green');

        PagesInUse[i] = inUse;
        if (inUse) {
            usedPages++;
            if (i < overheadPages) {
                box.classList.add('red');
            }
            else {
                box.classList.add('green');
            }
        }
        else {
            freePages++;
            box.classList.add('blue');
        }
    }

    let row1 = document.getElementById("metadata-row1");
    let row2 = document.getElementById("metadata-row2");
    let row3 = document.getElementById("metadata-row3");
    let row4 = document.getElementById("metadata-row4");

    let kib = Math.floor(Allocator.Size() / 1024);
    let mib = Math.floor(kib / 1024);
    
    row1.innerHTML = "Tracking " + Allocator.GetTotalPages() + " Pages, " + kib + " KiB (" + mib + " MiB)";
    row2.innerHTML = "Pages: " + freePages + " free, " + usedPages + " used, " + overheadPages + " overhead";
    row3.innerHTML = "Requested: " + requestedBytes + " bytes (~" + Math.floor(Math.floor(requestedBytes / 1024) / 1024) + " MiB)";
    row4.innerHTML = "Served: " + servedBytes + " bytes (~" + Math.floor(Math.floor(servedBytes / 1024) / 1024) + " MiB)";


    let allocList = document.getElementById("allocations");
    ClearSelectBox(allocList);
    let numAllocatons = Allocations.length;
    for (let i = 0; i < numAllocatons; ++i) {
        let strPtr = Allocator.GetAllocationDebugInfo(Allocations[i]);
        let strLen = Allocator.StrLen(strPtr);
        
        const array = new Uint8Array(Allocator.memory.buffer, strPtr, strLen);
        const decoder = new TextDecoder()
        const string = decoder.decode(array);

        var option = document.createElement("option");
        option.text = string;
        allocList.add(option);
    }
}

function allocate_mem() {
    let allocSize = Math.floor(document.getElementById("allocation-size").value);
    let allocUnit = document.getElementById("allocation-unit");

    if (allocUnit.value == "kilobytes") {
        allocSize = Math.floor(allocSize * 1024);
    }
    else if (allocUnit.value == "megabytes") {
        allocSize = Math.floor(allocSize * 1024 * 1024);
    }

    let allocation = Allocator.Malloc(allocSize);
    if (allocation == 0) { // Failed
        alert("allocation failed");
        console.error("allocation failed");
    }
    else { // Track the allocatin
        Allocations.push(allocation);
    }

    setTimeout(update, 1);
}

function free_all() {
    let numAllocs = Allocations.length;
    if (numAllocs == 0) {
        setTimeout(update, 1);
        return;
    }
    for (let i = 0; i < numAllocs; ++i) {
        Allocator.Free(Allocations[i]);
    }
    Allocations = [];
    setTimeout(update, 1);
}

function free_selected() {
    let allocList = document.getElementById("allocations");
    let selected = [];
    for (var option of allocList.options) {
        if (option.selected) {
            selected.push(option.index);
        }
    }   

    let numFree = selected.length;
    for (let i = 0; i < numFree; ++i) {
        Allocator.Free(Allocations[selected[i]]);
    }

    for (let i = numFree - 1; i >= 0; --i) {
        Allocations.splice(selected[i], 1);
    }

    setTimeout(update, 1);
}

function dump_state() {
    GlobalDumpState = "";
    Allocator.DumpState();
    TriggerDownload("MemInfo.txt", GlobalDumpState);
    GlobalDumpState = null;
}