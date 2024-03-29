#include <windows.h>
#include <commctrl.h>

#include "../mem.h"

#pragma warning(disable:6011)

void runtime_assert(bool condition, const char* file, int line) {
	char* data = (char*)((void*)0);
	if (condition == false) {
		*data = '\0';
	}
}

#define WinAssert(condition) runtime_assert(condition, __FILE__, __LINE__)

#define IDT_TIMER1 1001
#define IDC_LIST 1
#define IDC_STATIC 2
#define ID_UPDOWN 3
#define ID_EDIT 4
#define ID_ALLOCATE_MEM 5
#define ID_FREE_MEM 6
#define ID_FREE_MEM_ALL 7
#define ID_REFRESH_MEM 8
#define ID_DUMP_ALLOC 9

#define UD_MAX_POS (4096*4)
#define UD_MIN_POS 0

#ifndef HINST_THISCOMPONENT
EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#define HINST_THISCOMPONENT ((HINSTANCE)&__ImageBase)
#endif

#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

extern "C" int _fltused = 0;

i32 scrollY;
u32 oldNumRows; 
HWND gMemoryWindow;
struct FrameBuffer* gFrameBuffer;
struct Win32Color* bgColor;
struct Win32Color* freeMemoryColor;
struct Win32Color* usedMemoryColor;
struct Win32Color* trackMemoryColor;
struct Win32Color* boxColor;
struct Win32Color* textColor;

#define KB(x)   ((size_t) (x) << 10)
#define MB(x)   ((size_t) (x) << 20)
#define GB(x)   ((size_t) (x) << 30)

namespace Memory {
	Allocator* GlobalAllocator = 0;
}

struct MemoryDebugInfo {
	u8* PageMask;
	u32 NumberOfPages;

	u32 NumFreePages;
	u32 NumUsedPages;
	u32 NumOverheadPages;

	MemoryDebugInfo(Memory::Allocator* allocator) {
#if ATLAS_64
		u64 allocatorHeaderSize = sizeof(Memory::Allocator);
#elif ATLAS_32
		u32 allocatorHeaderSize = sizeof(Memory::Allocator);
#else
	#error Unknown Platform
#endif
		PageMask = ((u8*)allocator) + allocatorHeaderSize;

		NumberOfPages = allocator->size / allocator->pageSize; // 1 page = 4096 bytes, how many are needed
		WinAssert(allocator->size % allocator->pageSize == 0); // Allocator size should line up with page size
		
		u32 maskSize = AllocatorPageMaskSize(allocator) / (sizeof(u32) / sizeof(u8)); // convert from u8 to u32
		u32 metaDataSizeBytes = AllocatorPaddedSize() + (maskSize * sizeof(u32));
		u32 numberOfMasksUsed = metaDataSizeBytes / allocator->pageSize;
		if (metaDataSizeBytes % allocator->pageSize != 0) {
			numberOfMasksUsed += 1;
		}
		metaDataSizeBytes = numberOfMasksUsed * allocator->pageSize; // This way, allocatable will start on a page boundary

		// Account for meta data
		metaDataSizeBytes += allocator->pageSize;
		numberOfMasksUsed += 1;

		u32 allocatorOverheadBytes = metaDataSizeBytes;
		WinAssert(allocatorOverheadBytes % allocator->pageSize == 0); // Offset to allocatable should always line up with page size

		NumFreePages = 0;
		NumUsedPages = 0;
		NumOverheadPages = allocatorOverheadBytes / allocator->pageSize; // No need for a +1 padding allocatorOverheadBytes should be aligned to Memory::PageSize
		//NumOverheadPages += 1; // Debug tracker page

		u32* mask = (u32*)PageMask;
		for (u32 page = NumOverheadPages; page < NumberOfPages; ++page) { // Don't start at page 0?
			const u32 block = page / Memory::TrackingUnitSize;
			const int bit = page % Memory::TrackingUnitSize;

			const bool used = mask[block] & (1 << bit);
			if (!used) {
				NumFreePages += 1;
			}
			else {
				NumUsedPages += 1;
			}
		}

		// These are super useless right now
		WinAssert(NumFreePages + NumUsedPages + NumOverheadPages == NumberOfPages);// Page number does not add up
		WinAssert(NumUsedPages + NumOverheadPages == allocator->numPagesUsed);// Added up wrong number of used pages?!!
	}
private:
	inline u32 AllocatorPageMaskSize(Memory::Allocator* allocator) { // This is the number of u8's that make up the AllocatorPageMask array
		u32 allocatorNumberOfPages = allocator->size / allocator->pageSize; // 1 page = 4096 bytes, how many are needed
		//assert(allocator->size % PageSize == 0, "Allocator size should line up with page size");
		// allocatorNumberOfPages is the number of bits that are required to track memory

		// Pad out to sizeof(32) (if MaskTrackerSize is 32). This is because AllocatorPageMask will often be used as a u32 array
		// and we want to make sure that enough space is reserved.
		u32 allocatorPageArraySize = allocatorNumberOfPages / Memory::TrackingUnitSize + (allocatorNumberOfPages % Memory::TrackingUnitSize ? 1 : 0);
		//assert(allocatorPageArraySize % (TrackingUnitSize / 8) == 0, "allocatorPageArraySize should always be a multiple of 8");
		return allocatorPageArraySize * (Memory::TrackingUnitSize / 8); // In bytes, not bits
	}

	inline u32 AllocatorPaddedSize() {
		u32 allocatorHeaderSize = sizeof(Memory::Allocator);
		return allocatorHeaderSize;
	}
};

struct Win32Color {
	HBRUSH brush;
	COLORREF color;
	unsigned char r;
	unsigned char g;
	unsigned char b;
	unsigned char a;

	Win32Color() {
		color = RGB(0, 0, 0);
		brush = 0;
		r = 0;
		g = 0;
		b = 0;
		a = 255;
		CreateBrushObject();
	}

	Win32Color(unsigned char _r, unsigned char _g, unsigned char _b) {
		color = RGB(_r, _g, _b);
		r = _r;
		g = _g;
		b = _b;
		a = 255;
		CreateBrushObject();
	}

	Win32Color(unsigned char v) {
		color = RGB(v, v, v);
		r = v;
		g = v;
		b = v;
		a = 255;
		CreateBrushObject();
	}

	~Win32Color() {
		DestroyBrushObject();
	}

	void Init(unsigned char _r, unsigned char _g, unsigned char _b) {
		color = RGB(_r, _g, _b);
		r = _r;
		g = _g;
		b = _b;
		a = 255;
		DestroyBrushObject();
		brush = CreateSolidBrush(color);
	}
	
protected:
	void CreateBrushObject() {
		WinAssert(brush == 0);
		brush = CreateSolidBrush(color);
	}

	void DestroyBrushObject() {
		WinAssert(brush != 0);
		DeleteObject(brush);
		brush = 0;
	}

	void Copy(const Win32Color& other) {
		if (brush != 0) {
			DeleteObject(brush);
		}
		brush = 0;

		color = RGB(other.r, other.g, other.b);
		if (other.brush != 0) {
			brush = CreateSolidBrush(color);
		}
		
		r = other.r;
		g = other.g;
		b = other.b;
		a = 255;
	}
};

struct FrameBuffer { // For double buffered window
	BITMAPINFO RenderBufferInfo;
	unsigned char* Memory;
	unsigned int Width;
	unsigned int Height;

	FrameBuffer() {
		Memory = 0;
		Width = 0;
		Height = 0;
		Memory::Set(&RenderBufferInfo, 0, sizeof(BITMAPINFO), 0);
	}

	void Initialize() {
		WinAssert(Memory == 0);

		Width = GetSystemMetrics(SM_CXSCREEN);
		Height = GetSystemMetrics(SM_CYSCREEN);

		RenderBufferInfo.bmiHeader.biSize = sizeof(RenderBufferInfo.bmiHeader);
		RenderBufferInfo.bmiHeader.biWidth = Width;
		RenderBufferInfo.bmiHeader.biHeight = -((int)Height);
		RenderBufferInfo.bmiHeader.biPlanes = 1;
		RenderBufferInfo.bmiHeader.biBitCount = 32;
		RenderBufferInfo.bmiHeader.biCompression = BI_RGB;

		int bitmapMemorySize = (Width * Height) * 4;
		Memory = (unsigned char*)VirtualAlloc(0, bitmapMemorySize, MEM_COMMIT, PAGE_READWRITE);
		//memset(Memory, 0, bitmapMemorySize);
	}

	void Destroy() {
		WinAssert(Memory != 0);
		VirtualFree(Memory, 0, MEM_RELEASE);
		Memory = 0;
	}
};

void log(const char* pszFormat, ...) {
	char buf[1024];
	va_list argList;
	va_start(argList, pszFormat);
	wvsprintfA(buf, pszFormat, argList);
	va_end(argList);
	DWORD done;
	unsigned int sLen = 0;
	for (int i = 0; i < 1024; ++i) {
		if (buf[i] == '\0') {
			break;
		}
		sLen += 1;
	}
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, /*strlen(buf)*/sLen, &done, NULL);
}

#define COPY_RECT(a, b) \
	a.left = b.left; a.right = b.right; a.top = b.top; a.bottom = b.bottom;

#define CLEAR_RECT(a) \
	a.left = 0; a.right = 0; a.top = 0; a.bottom = 0;

struct Win32WindowLayout {
	RECT topArea;
	RECT bottomArea;
	RECT bottomLeftArea;
	RECT bottomCenterArea;

	Win32WindowLayout() {
		CLEAR_RECT(topArea);
		CLEAR_RECT(bottomArea);
		CLEAR_RECT(bottomLeftArea);
		CLEAR_RECT(bottomCenterArea);
	}

	Win32WindowLayout(const RECT& _top, const RECT& _bottom, const RECT& _bottomLeft, const RECT& _bottomCenter) {
		COPY_RECT(topArea, _top);
		COPY_RECT(bottomArea, _bottom);
		COPY_RECT(bottomLeftArea, _bottomLeft);
		COPY_RECT(bottomCenterArea, _bottomCenter);
	}
};

void SetWindowLayout(const Win32WindowLayout& layout, HWND chart, HWND* labels, HWND list, HWND* buttons,HWND upDown, HWND upDownEdit, HWND combo) {
	MemoryDebugInfo memInfo(Memory::GlobalAllocator);
	
	SetWindowPos(chart, 0, layout.topArea.left, layout.topArea.top, layout.topArea.right - layout.topArea.left, layout.topArea.bottom - layout.topArea.top, /*SWP_NOZORDER*/0);

	const u32 labelHeight = 18;
	const u32 buttonHeight = 25;

	RECT labelRect;
	COPY_RECT(labelRect, layout.bottomLeftArea);
	labelRect.top += 125;

	for (u32 i = 0; i < 9; ++i) {
		labelRect.top = layout.bottomLeftArea.top + 125 + i * labelHeight;
		labelRect.bottom = (i == 10)? layout.bottomCenterArea.bottom : labelRect.top + labelHeight;

		SetWindowPos(labels[i], 0, labelRect.left, labelRect.top, labelRect.right - labelRect.left, labelRect.bottom - labelRect.top, /*SWP_NOZORDER*/0);
	}

	Memory::Allocator* allocator = Memory::GlobalAllocator;
	wchar_t displaybuffer[1024];

	int kib = allocator->size / 1024;
	int mib = kib / 1024;// +(kib % 1024 ? 1 : 0);
	//kib = allocator->size / 1024 + (allocator->size % 1024 ? 1 : 0);

	wsprintfW(displaybuffer, L"Tracking %d Pages, %d KiB (%d MiB)", memInfo.NumberOfPages, kib, mib); // Removed with %.2f %% overhead
	SetWindowText(labels[0], displaybuffer);

	wsprintfW(displaybuffer, L"Pages: %d free, %d used, %d overhead", memInfo.NumFreePages, memInfo.NumUsedPages, memInfo.NumOverheadPages);
	SetWindowText(labels[1], displaybuffer);

	kib = allocator->requested / 1024;
	mib = kib / 1024;// + (kib % 1024 ? 1 : 0);
	//kib = allocator->requested / 1024 + (allocator->requested % 1024 ? 1 : 0);

	wsprintfW(displaybuffer, L"Requested: %d bytes (~%d MiB)", allocator->requested, mib);
	SetWindowText(labels[2], displaybuffer);

	kib = (memInfo.NumUsedPages * allocator->pageSize) / 1024;
	mib = kib / 1024;// +(kib % 1024 ? 1 : 0);
	// kib = (memInfo.NumUsedPages * Memory::PageSize) / 1024 + ((memInfo.NumUsedPages * Memory::PageSize) % 1024 ? 1 : 0);

	wsprintfW(displaybuffer, L"Served: %d bytes (~%d MiB)", memInfo.NumUsedPages * allocator->pageSize, mib);
	SetWindowText(labels[3], displaybuffer);

	SetWindowPos(list, 0, layout.bottomCenterArea.left, layout.bottomCenterArea.top, layout.bottomCenterArea.right - layout.bottomCenterArea.left, layout.bottomCenterArea.bottom - layout.bottomCenterArea.top, /*SWP_NOZORDER*/0);

	//SetWindowPos(right, 0, layout.bottomRightArea.left, layout.bottomRightArea.top, layout.bottomRightArea.right - layout.bottomRightArea.left, layout.bottomRightArea.bottom - layout.bottomRightArea.top, /*SWP_NOZORDER*/0);

	RECT allocationRect = { 0 };
	COPY_RECT(allocationRect, layout.bottomLeftArea);
	allocationRect.left += 5;
	allocationRect.right -= 5;
	allocationRect.top += 5;

	allocationRect.right = allocationRect.left + 75 + 50;
	allocationRect.bottom = allocationRect.top + buttonHeight;
	SetWindowPos(upDownEdit, 0, allocationRect.left, allocationRect.top, allocationRect.right - allocationRect.left, allocationRect.bottom - allocationRect.top, /*SWP_NOZORDER*/0);
	SendMessageW(upDown, UDM_SETBUDDY, (WPARAM)upDownEdit, 0);
	
	allocationRect.left = allocationRect.right + 5;
	allocationRect.right = allocationRect.left + 65 + 25;
	SetWindowPos(combo, 0, allocationRect.left, allocationRect.top, allocationRect.right - allocationRect.left, allocationRect.bottom - allocationRect.top + 150, /*SWP_NOZORDER*/0);

	allocationRect.left = allocationRect.right + 5;
	allocationRect.right = allocationRect.left + 100 + 30;
	SetWindowPos(buttons[0], 0, allocationRect.left, allocationRect.top, allocationRect.right - allocationRect.left, allocationRect.bottom - allocationRect.top, /*SWP_NOZORDER*/0);

	allocationRect.top += buttonHeight + 5;
	allocationRect.bottom = allocationRect.top + buttonHeight;
	allocationRect.left = layout.bottomLeftArea.left + 5;
	allocationRect.right = allocationRect.left + 75 + 50 + 65 + 25 + 5;
	SetWindowPos(buttons[1], 0, allocationRect.left, allocationRect.top, allocationRect.right - allocationRect.left, allocationRect.bottom - allocationRect.top, /*SWP_NOZORDER*/0);

	//allocationRect.top += buttonHeight + 5;
	//allocationRect.bottom = allocationRect.top + buttonHeight;
	allocationRect.left = allocationRect.right + 5;
	allocationRect.right = allocationRect.left + 100 + 30;
	SetWindowPos(buttons[4], 0, allocationRect.left, allocationRect.top, allocationRect.right - allocationRect.left, allocationRect.bottom - allocationRect.top, /*SWP_NOZORDER*/0);

	allocationRect.left = layout.bottomLeftArea.left + 5;
	allocationRect.top += buttonHeight + 5;
	allocationRect.bottom = allocationRect.top + buttonHeight;
	SetWindowPos(buttons[3], 0, allocationRect.left, allocationRect.top, allocationRect.right - allocationRect.left, allocationRect.bottom - allocationRect.top, /*SWP_NOZORDER*/0);

	allocationRect.top += buttonHeight + 5;
	allocationRect.bottom = allocationRect.top + buttonHeight;
	SetWindowPos(buttons[2], 0, allocationRect.left, allocationRect.top, allocationRect.right - allocationRect.left, allocationRect.bottom - allocationRect.top, /*SWP_NOZORDER*/0);
}

void ResetListBoxContent(Memory::Allocator* allocator, HWND list) {
	auto selection = SendMessage(list, LB_GETCURSEL, 0, 0);
	SendMessage(list, LB_RESETCONTENT, 0, 0);

	wchar_t displaybuffer[1024];
	for (Memory::Allocation* iter = allocator->active; iter != 0; iter = (iter->nextOffset == 0) ? 0 : (Memory::Allocation*)((u8*)allocator + iter->nextOffset)) {
		size_t len = 1; // To account for '\0' at the end of the string.
		char* it = 0;
#if MEM_TRACK_LOCATION
		it =(char*)iter->location;
#endif
		while (it != 0 && len < 1024 - 256) {
			it += 1;
			len += 1;
		}

		u32 allocationHeaderPadding = 0;
		if (iter->alignment != 0) {  // Add padding to the header to compensate for alignment
			allocationHeaderPadding = iter->alignment - 1; // Somewhere in this range, we will be aligned
		}

		u32 paddedSize = iter->size + sizeof(Memory::Allocation) + allocationHeaderPadding;
		u32 pages = paddedSize / allocator->pageSize + (paddedSize % allocator->pageSize ? 1 : 0);
		wsprintfW(displaybuffer, L"Size: %d/%d bytes, Pages: %d, >", iter->size, paddedSize, pages);
		wchar_t* print_to = displaybuffer;
		while (*print_to != L'>') {
			print_to++;
		}
#if MEM_TRACK_LOCATION
		if (iter->location != 0) {
			MultiByteToWideChar(0, 0, iter->location, (int)len, print_to, (int)len);
		}
#endif

		SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)displaybuffer);
	}

	SendMessage(list, LB_SETCURSEL, selection, 0);
}

void FillBox(RECT& rect, Win32Color& c) {
	const unsigned char r = c.r;
	const unsigned char g = c.g;
	const unsigned char b = c.b;

	const unsigned int BufferSize = gFrameBuffer->Width * gFrameBuffer->Height * 4;

	i32 top = rect.top < 0 ? 0 : rect.top;
	i32 bottom = rect.bottom < 0 ? 0 : rect.bottom;
	i32 left = rect.left < 0 ? 0 : rect.left;
	i32 right = rect.right < 0 ? 0 : rect.right;

	for (int row = top; row < bottom; ++row) {
		for (int col = left; col < right; ++col) {
			unsigned int pixel = (row * gFrameBuffer->Width + col) * 4;
			if (pixel >= BufferSize) {
				break;
			}
			gFrameBuffer->Memory[pixel + 0] = b;
			gFrameBuffer->Memory[pixel + 1] = g;
			gFrameBuffer->Memory[pixel + 2] = r;
			gFrameBuffer->Memory[pixel + 3] = 255;
		}
	}
}

void RedrawMemoryChart(HWND hwnd, Win32Color& bgColor, Win32Color& trackMemoryColor, Win32Color& usedMemoryColor, Win32Color& freeMemoryColor) {
	RECT clientRect;
	GetClientRect(hwnd, &clientRect);
	int clientWidth = clientRect.right - clientRect.left;
	int clientHeight = clientRect.bottom - clientRect.top;

	const u32 pageWidth = 3;
	const u32 pageHeight = 5;
	const u32 pagePadding = 1;

	const u32 numColumns = clientWidth / (pagePadding + pageWidth + pagePadding);
	const u32 numRows = clientHeight / (pagePadding + pageHeight + pagePadding) + (clientHeight % (pagePadding + pageHeight + pagePadding) ? 1 : 0);

	MemoryDebugInfo memInfo(Memory::GlobalAllocator);
	u32* mask = (u32*)memInfo.PageMask;

	u32 firstVisibleRow = scrollY / (pagePadding + pageHeight + pagePadding);
	if (scrollY % (pagePadding + pageHeight + pagePadding) != 0 && firstVisibleRow >= 1) {
		firstVisibleRow -= 1; // the row above is partially visible.
	}
	u32 lastVisibleRow = (memInfo.NumberOfPages) / numColumns;
	if ((memInfo.NumberOfPages) % numColumns != 0) {
		lastVisibleRow += 1;
	}

	if (lastVisibleRow - firstVisibleRow > numRows) {
		lastVisibleRow = firstVisibleRow + numRows;
	}

	FillBox(clientRect, bgColor);

	RECT draw;
	for (u32 row = firstVisibleRow; row <= lastVisibleRow; ++row) {
		for (u32 col = 0; col < numColumns; ++col) {
			u32 index = row * numColumns + col;
			if (index > memInfo.NumberOfPages) {
				break;
			}

			// Get memory, see if it's in use
			const u32 m = index / Memory::TrackingUnitSize;
			const u32 b = index % Memory::TrackingUnitSize;
			const bool used = mask[m] & (1 << b);

			draw.left = col * (pagePadding + pageWidth + pagePadding) + pagePadding;
			draw.right = draw.left + pageWidth;
			draw.top = row * (pagePadding + pageHeight + pagePadding) + pagePadding;
			draw.bottom = draw.top + pageHeight;

			draw.top -= scrollY;
			draw.bottom -= scrollY;

			if (index < memInfo.NumOverheadPages) {
				FillBox(draw, trackMemoryColor);
			}
			else if (used) {
				FillBox(draw, usedMemoryColor);
			}
			else {
				FillBox(draw, freeMemoryColor);
			}
		}
	}
}

Win32WindowLayout GetWindowLayout(HWND hwnd) {
	const i32 sideMargin = 50;
	const i32 topMargin = 25;

	const i32 middleSeperator = 40;
	const i32 minChartWidth = 150;
	const i32 minChartHeight = 150;
	
	const i32 bottomAreaHeight = 300;
	const i32 bottomSeperator = 10;
	
	const i32 bottomLeftWidth = 365;

	RECT clientRect = {0};
	GetClientRect(hwnd, &clientRect);
	const i32 clientHeight = clientRect.bottom - clientRect.top;
	const i32 clientWidth = clientRect.right - clientRect.left;

	// Layout info
	RECT topArea;
	RECT bottomArea;
	RECT bottomLeft;
	RECT bottomRight;

	// Set left and right
	topArea.left = clientRect.left + sideMargin;
	if (clientRect.right < sideMargin || (clientRect.right - sideMargin) < topArea.left || (clientRect.right - sideMargin) - topArea.left < minChartWidth) {
		topArea.right = clientRect.left + minChartWidth;
	}
	else {
		topArea.right = clientRect.right - sideMargin;
	}
	bottomArea.left = topArea.left;
	bottomArea.right = topArea.right;
	const u32 chartWidth = topArea.right - topArea.left;

	// Set top and bottom
	topArea.top = clientRect.top + topMargin;
	i32 maybeBottom = (i32)(clientRect.top + clientHeight);
	maybeBottom -= (i32)topMargin + (i32)bottomAreaHeight + (i32)middleSeperator;

	if (maybeBottom <= 0 || maybeBottom < topArea.top  || maybeBottom - topArea.top < minChartHeight) {
		topArea.bottom = topArea.top + minChartHeight;
	}
	else {
		topArea.bottom = maybeBottom;
	}
	bottomArea.top = topArea.bottom + middleSeperator / 2;
	bottomArea.bottom = bottomArea.top + bottomAreaHeight;

	// Sub-divide the bottom area
	WinAssert(minChartWidth / 3 > bottomSeperator /2);
	const u32 bottomSectorMinWidth = minChartWidth / 3 - bottomSeperator /2;
	u32 bottomSectorWidth = bottomSectorMinWidth;
	if (chartWidth / 3 > bottomSeperator /2 && (chartWidth / 3 - bottomSeperator /2) > bottomSectorMinWidth) {
		bottomSectorWidth = chartWidth / 3 - bottomSeperator /2;
	}

	COPY_RECT(bottomLeft, bottomArea);
	COPY_RECT(bottomRight, bottomArea);

	bottomLeft.right = bottomLeft.left + bottomLeftWidth;
	bottomRight.left = bottomLeft.right + bottomSeperator;
	bottomRight.right = bottomArea.right;

	if (bottomRight.left >= bottomRight.right || bottomRight.right - bottomRight.left < bottomLeftWidth) {
		bottomRight.right = bottomRight.left + bottomLeftWidth;
	}

	return Win32WindowLayout(topArea, bottomArea, bottomLeft, bottomRight);
}

LRESULT CALLBACK MemoryChartProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam) {
	// Great orange: (255, 125, 64);

	switch (iMsg) {
	case WM_CREATE:
		scrollY = 0;
		SetClassLongPtr(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)bgColor->brush);
		break;
	case WM_DESTROY:
		
		break;
	case WM_VSCROLL:
		{
			auto action = LOWORD(wParam);
			HWND hScroll = (HWND)lParam;
			int pos = -1;
			if (action == SB_THUMBPOSITION || action == SB_THUMBTRACK) {
				pos = HIWORD(wParam);
			}
			else if (action == SB_LINEDOWN) {
				pos = scrollY + 30;
			}
			else if (action == SB_LINEUP) {
				pos = scrollY - 30;
			}
			if (pos == -1) {
				break;
			}
			SCROLLINFO si = { 0 };
			si.cbSize = sizeof(SCROLLINFO);
			si.fMask = SIF_POS;
			si.nPos = pos;
			si.nTrackPos = 0;
			SetScrollInfo(hwnd, SB_VERT, &si, true);
			GetScrollInfo(hwnd, SB_VERT, &si);
			pos = si.nPos;
			POINT pt;
			pt.x = 0;
			pt.y = pos - scrollY;
			auto hdc = GetDC(hwnd);
			LPtoDP(hdc, &pt, 1);
			ReleaseDC(hwnd, hdc);
			ScrollWindow(hwnd, 0, -pt.y, NULL, NULL);
			scrollY = pos;

			RedrawMemoryChart(hwnd, *bgColor, *trackMemoryColor, *usedMemoryColor, *freeMemoryColor);
			InvalidateRect(hwnd, 0, false);
		}
		break;
	case WM_SIZE:
	{
		RECT clientRect;

		GetClientRect(hwnd, &clientRect);
		int clientWidth = clientRect.right - clientRect.left;
		int clientHeight = clientRect.bottom - clientRect.top;

		const u32 pageWidth = 3;
		const u32 pageHeight = 5;
		const u32 pagePadding = 1;

		const u32 numColumns = clientWidth / (pagePadding + pageWidth + pagePadding);
		MemoryDebugInfo memInfo(Memory::GlobalAllocator);
		const u32 maxRows = memInfo.NumberOfPages / numColumns + (memInfo.NumberOfPages % numColumns ? 1 : 0) + 1;

		if (oldNumRows != maxRows) {
			SCROLLINFO si = { 0 };
			si.cbSize = sizeof(SCROLLINFO);
			si.fMask = SIF_ALL;
			si.nMin = 0;
			si.nMax = maxRows * (pagePadding + pageHeight + pagePadding);
			si.nPage = (clientRect.bottom - clientRect.top);
			si.nPos = 0;
			si.nTrackPos = 0;
			SetScrollInfo(hwnd, SB_VERT, &si, true);
			oldNumRows = maxRows;
		}
	}
		break;
	case WM_PAINT:
	case WM_ERASEBKGND:
		{
			//MemoryDebugInfo memInfo(Memory::GlobalAllocator);
			//u32* mask = (u32*)memInfo.PageMask;

			PAINTSTRUCT ps;
			RECT clientRect;

			HDC hdc = BeginPaint(hwnd, &ps);

			GetClientRect(hwnd, &clientRect);
			int clientWidth = clientRect.right - clientRect.left;
			int clientHeight = clientRect.bottom - clientRect.top;

			WinAssert(clientWidth < (int)gFrameBuffer->Width);
			WinAssert(clientHeight < (int)gFrameBuffer->Height);

			if (clientWidth > (int)gFrameBuffer->Width) {
				clientWidth = (int)gFrameBuffer->Width;
			}
			if (clientHeight > (int)gFrameBuffer->Height) {
				clientHeight = (int)gFrameBuffer->Height;
			}

			StretchDIBits(hdc,
				0, 0, clientWidth, clientHeight,
				0, 0, clientWidth, clientHeight,
				gFrameBuffer->Memory, &gFrameBuffer->RenderBufferInfo,
				DIB_RGB_COLORS, SRCCOPY);

#if 0
			GetClientRect(hwnd, &clientRect);
			clientWidth = clientRect.right - clientRect.left;
			clientHeight = clientRect.bottom - clientRect.top;

			const u32 pageWidth = 3;
			const u32 pageHeight = 5;
			const u32 pagePadding = 1;
			
			const u32 numColumns = clientWidth / (pagePadding + pageWidth + pagePadding);
			const u32 numRows = clientHeight / (pagePadding + pageHeight + pagePadding) + (clientHeight % (pagePadding + pageHeight + pagePadding) ? 1 : 0);

			if (oldNumRows != numRows) {
				SCROLLINFO si = { 0 };
				si.cbSize = sizeof(SCROLLINFO);
				si.fMask = SIF_ALL;
				si.nMin = 0;
				si.nMax = numRows * (pagePadding + pageHeight + pagePadding);
				si.nPage = (clientRect.bottom - clientRect.top);
				si.nPos = 0;
				si.nTrackPos = 0;
				SetScrollInfo(hwnd, SB_VERT, &si, true);
				oldNumRows = numRows;
			}

			u32 firstVisibleRow = scrollY / (pagePadding + pageHeight + pagePadding);
			if (scrollY % (pagePadding + pageHeight + pagePadding) != 0 && firstVisibleRow >= 1) {
				firstVisibleRow -= 1; // the row above is partially visible.
			}
			u32 lastVisibleRow = (memInfo.NumberOfPages) / numColumns;
			if ((memInfo.NumberOfPages) % numColumns != 0) {
				lastVisibleRow += 1;
			}
			
			if (lastVisibleRow - firstVisibleRow > numRows) {
				lastVisibleRow = firstVisibleRow + numRows;
			}

			FillRect(hdc, &clientRect, bgColor.brush);

			RECT draw;
			for (u32 row = firstVisibleRow; row <= lastVisibleRow; ++row) {
				for (u32 col = 0; col < numColumns; ++col) {
					u32 index = row * numColumns + col;
					if (index > memInfo.NumberOfPages) {
						break;
					}

					// Get memory, see if it's in use
					const u32 m = index / Memory::TrackingUnitSize;
					const u32 b = index % Memory::TrackingUnitSize;
					const bool used = mask[m] & (1 << b);

					draw.left = col * (pagePadding + pageWidth + pagePadding) + pagePadding;
					draw.right = draw.left + pageWidth;
					draw.top = row * (pagePadding + pageHeight + pagePadding) + pagePadding;
					draw.bottom = draw.top + pageHeight;

					if (index < memInfo.NumOverheadPages) {
						FillRect(hdc, &draw, trackMemoryColor.brush);
					}
					else if (used) {
						FillRect(hdc, &draw, usedMemoryColor.brush);
					}
					else {
						FillRect(hdc, &draw, freeMemoryColor.brush);
					}
				}
			}
#endif

			EndPaint(hwnd, &ps);
		}
		return 0;
	}
	return DefWindowProc(hwnd, iMsg, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam) {
	if (iMsg != WM_GETMINMAXINFO && iMsg != WM_NCCREATE && iMsg != WM_NCCALCSIZE) {
		WinAssert(gMemoryWindow != 0);
	}

	static HWND hwndChart = 0;
	static HWND hwndLabels[9] = { 0 };
	static HWND hwndList = 0;
	static HWND hwndButtons[5] = { 0 };
	static HWND hwndUpDown = { 0 };
	static HWND hwndUpDownEdit = { 0 };
	static HWND hwndCombo = { 0 };

	switch (iMsg) {
	case WM_NCCREATE:
		bgColor->Init(30, 30, 30);
		boxColor->Init(50, 50, 50);
		textColor->Init(220, 220, 220);
		freeMemoryColor->Init(110, 110, 220);
		usedMemoryColor->Init(110, 240, 110);
		trackMemoryColor->Init(255, 110, 110);

		WinAssert(gMemoryWindow == 0);
		gMemoryWindow = hwnd;
		break;
	case WM_NCDESTROY:
		WinAssert(gMemoryWindow != 0);
		WinAssert(gMemoryWindow == hwnd);
		gMemoryWindow = 0;
		break;
	case WM_CREATE:
		WinAssert(gMemoryWindow == hwnd);
		gFrameBuffer->Initialize();
		{ // Set up render styles
			SetClassLongPtr(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)bgColor->brush);
		}
		{ // Create Memory chart window
			WNDCLASSEX wc;
			wc.cbSize = sizeof(WNDCLASSEX);
			wc.style = CS_HREDRAW | CS_VREDRAW;
			wc.lpfnWndProc = MemoryChartProc;
			wc.cbClsExtra = 0;
			wc.cbWndExtra = 0;
			wc.hInstance = GetModuleHandle(NULL);
			wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
			wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
			wc.hCursor = LoadCursor(NULL, IDC_ARROW);
			wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
			wc.lpszMenuName = 0;
			wc.lpszClassName = L"MemoryDebuggerParams";
			RegisterClassEx(&wc);

			hwndChart = CreateWindowEx(WS_EX_CLIENTEDGE, L"MemoryDebuggerParams", L"",
				WS_CHILD | WS_VISIBLE, 10, 10, 50, 50, hwnd, NULL, GetModuleHandle(NULL), NULL);
		}
		{ // Create labels for the left side of the window
			for (u32 i = 0; i < 9; ++i) {
				hwndLabels[i] = CreateWindowW(L"Static", L"",
					WS_CHILD | WS_VISIBLE | SS_LEFT,
					10, 10, 50, 50,
					hwnd, (HMENU)1, NULL, NULL);
			}
		}
		{ // Create list box to hold a visual reference to all the allocations
			hwndList = CreateWindowW(WC_LISTBOXW, NULL, WS_CHILD
				| WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL, 10, 10, 50, 50, hwnd,
				(HMENU)IDC_LIST, NULL, NULL);
		}
		{ // Create the right side
			hwndButtons[0] = CreateWindowW(L"Button", L"Allocate", WS_CHILD | WS_VISIBLE, 10, 10, 50, 50, hwnd, (HMENU)ID_ALLOCATE_MEM, NULL, NULL);
			hwndButtons[1] = CreateWindowW(L"Button", L"Free Selected", WS_CHILD | WS_VISIBLE, 10, 10, 50, 50, hwnd, (HMENU)ID_FREE_MEM, NULL, NULL);
			hwndButtons[2] = CreateWindowW(L"Button", L"Refresh Display", WS_CHILD | WS_VISIBLE, 10, 10, 50, 50, hwnd, (HMENU)ID_REFRESH_MEM, NULL, NULL);
			hwndButtons[3] = CreateWindowW(L"Button", L"Dump Allocator", WS_CHILD | WS_VISIBLE, 10, 10, 50, 50, hwnd, (HMENU)ID_DUMP_ALLOC, NULL, NULL);
			hwndButtons[4] = CreateWindowW(L"Button", L"Free All", WS_CHILD | WS_VISIBLE, 10, 10, 50, 50, hwnd, (HMENU)ID_FREE_MEM_ALL, NULL, NULL);

			hwndUpDown = CreateWindowW(UPDOWN_CLASSW, NULL, WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT, 0, 0, 0, 0, hwnd, (HMENU)ID_UPDOWN, NULL, NULL);
			hwndUpDownEdit = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, NULL, WS_CHILD | WS_VISIBLE | ES_RIGHT, 10, 10, 75, 25, hwnd, (HMENU)ID_EDIT, NULL, NULL);
			SendMessageW(hwndUpDown, UDM_SETBUDDY, (WPARAM)hwndUpDownEdit, 0);
			SendMessageW(hwndUpDown, UDM_SETRANGE, 0, MAKELPARAM(UD_MAX_POS, UD_MIN_POS));
			SendMessageW(hwndUpDown, UDM_SETPOS32, 0, 1);

			hwndCombo = CreateWindow(WC_COMBOBOX, TEXT(""), CBS_DROPDOWN | CBS_HASSTRINGS | WS_CHILD | WS_OVERLAPPED | WS_VISIBLE, 0, 0, 50, 50, hwnd, NULL, HINST_THISCOMPONENT, NULL);
			static const wchar_t* items[] = { L"bytes", L"KiB", L"MiB" };
			int debug_0 = (int)SendMessage(hwndCombo, (UINT)CB_ADDSTRING, 0, (LPARAM)items[0]);
			int debug_1 = (int)SendMessage(hwndCombo, (UINT)CB_ADDSTRING, 0, (LPARAM)items[1]);
			int debug_2 = (int)SendMessage(hwndCombo, (UINT)CB_ADDSTRING, 0, (LPARAM)items[2]);
			SendMessage(hwndCombo, CB_SETCURSEL, (WPARAM)1, (LPARAM)0);
		}
		{ // Layout the window elements that where just created to the size of the window
			Win32WindowLayout layout = GetWindowLayout(hwnd);
			SetWindowLayout(layout, hwndChart, hwndLabels, hwndList, hwndButtons, hwndUpDown, hwndUpDownEdit, hwndCombo);
			ResetListBoxContent(Memory::GlobalAllocator, hwndList);
			RedrawMemoryChart(hwndChart, *bgColor, *trackMemoryColor, *usedMemoryColor, *freeMemoryColor);
			InvalidateRect(hwnd, 0, false);
		}
		break;
	case WM_TIMER:
		if (wParam == IDT_TIMER1) {
		}
		break;
	case WM_CLOSE:
		WinAssert(gMemoryWindow == hwnd);
		gFrameBuffer->Destroy();
		break;
	case WM_COMMAND:
	{
		bool update = false;
		if (LOWORD(wParam) == ID_ALLOCATE_MEM) {
			int howMany = (int)SendMessage(hwndUpDown, UDM_GETPOS, 0, 0);
			LRESULT units = SendMessage(hwndCombo, CB_GETCURSEL, 0, 0);
			if (units == 0) {
				units = 1;
			}
			else if (units == 1) {
				units = 1024;
			}
			else {
				units = 1024 * 1024;
			}

			Memory::GlobalAllocator->Allocate(howMany * (u32)units);

			update = true;
		}
		if (LOWORD(wParam) == ID_FREE_MEM) {
			int selection = (int)SendMessage(hwndList, LB_GETCURSEL, 0, 0);
			if (selection >= 0) {
				int counter = 0;
				Memory::Allocation* iter = Memory::GlobalAllocator->active;
				WinAssert(iter != 0);
				for (; iter != 0 && counter != selection; iter = (iter->nextOffset == 0)? 0 : (Memory::Allocation*)((u8*)Memory::GlobalAllocator + iter->nextOffset), counter++);
				WinAssert(counter == selection);
				u8* mem = (u8*)iter + sizeof(Memory::Allocation);
				Memory::GlobalAllocator->Release(mem);
			}
			update = true;
		}
		if (LOWORD(wParam) == ID_FREE_MEM_ALL) {
			Memory::Allocation* iter = Memory::GlobalAllocator->active;
			while (iter != 0) {
				Memory::Allocation* next = 0;
				if (iter->nextOffset != 0) {
					next = (Memory::Allocation*)((u8*)Memory::GlobalAllocator + iter->nextOffset);
				}

				u8* mem = (u8*)iter + sizeof(Memory::Allocation);
				Memory::GlobalAllocator->Release(mem);

				iter = next;
			}
			update = true;
		}
		if (LOWORD(wParam) == ID_REFRESH_MEM) {
			update = true;
		}
		if (LOWORD(wParam) == ID_DUMP_ALLOC) {
			update = true;

			DeleteFile(L"MemInfo.txt");
			HANDLE hFile = CreateFile(
				L"MemInfo.txt",     // Filename
				GENERIC_WRITE,          // Desired access
				FILE_SHARE_READ,        // Share mode
				NULL,                   // Security attributes
				CREATE_NEW,             // Creates a new file, only if it doesn't already exist
				FILE_ATTRIBUTE_NORMAL,  // Flags and attributes
				NULL);                  // Template file handle
			WinAssert(hFile != INVALID_HANDLE_VALUE);

			Memory::Debug::MemInfo(Memory::GlobalAllocator, [](const u8* mem, u32 size, void* fileHandle) {
				HANDLE file = *(HANDLE*)fileHandle;
				DWORD bytesWritten;
				WriteFile(
					file,            // Handle to the file
					mem,  // Buffer to write
					size,   // Buffer size
					&bytesWritten,    // Bytes written
					nullptr);         // Overlapped
			}, &hFile);
			CloseHandle(hFile);
		}
		if (update) {
			SetWindowLayout(GetWindowLayout(hwnd), hwndChart, hwndLabels, hwndList, hwndButtons, hwndUpDown, hwndUpDownEdit, hwndCombo);
			ResetListBoxContent(Memory::GlobalAllocator, hwndList);
			RedrawMemoryChart(hwndChart, *bgColor, *trackMemoryColor, *usedMemoryColor, *freeMemoryColor);
			InvalidateRect(hwnd, 0, false);
			return 0;
		}
		break;
	}
	case WM_NOTIFY:
		break;
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLORLISTBOX:
		for (u32 i = 0; i < 11; ++i) {
			HDC hdcStatic = (HDC)wParam;
			if (hwndLabels[i] == (HWND)lParam) {
				SetTextColor(hdcStatic, textColor->color);
				SetBkColor(hdcStatic, boxColor->color);
				return (INT_PTR)boxColor->brush;
			}
		}
		if (hwndList == (HWND)lParam) {
			HDC hdcStatic = (HDC)wParam;
			SetTextColor(hdcStatic, textColor->color);
			SetBkColor(hdcStatic, boxColor->color);
			return (INT_PTR)boxColor->brush;
		}
		break;
	case WM_SIZE:
	{
		UINT width = LOWORD(lParam);
		UINT height = HIWORD(lParam);

#if _DEBUG
		RECT clientRect = { 0 };
		GetClientRect(hwnd, &clientRect);
		WinAssert(clientRect.right - clientRect.left == width);
		WinAssert(clientRect.bottom - clientRect.top == height);
#endif
		Win32WindowLayout layout = GetWindowLayout(hwnd);
		SetWindowLayout(layout, hwndChart, hwndLabels, hwndList, hwndButtons, hwndUpDown, hwndUpDownEdit, hwndCombo);
		InvalidateRect(hwnd, 0, false);
	}
	break;
	}

	return DefWindowProc(hwnd, iMsg, wParam, lParam);
}

void CreateMemoryWindow() {
	HINSTANCE hInstance = GetModuleHandle(NULL);
	const wchar_t className[] = L"MemoryDebug";

	WNDCLASSEX wc;
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszMenuName = 0;
	wc.lpszClassName = className;
	RegisterClassEx(&wc);

	// Create the window.
	int screenWidth = GetSystemMetrics(SM_CXSCREEN);
	int screenHeight = GetSystemMetrics(SM_CYSCREEN);
	int clientWidth = 1900;
	int clientHeight = 1000;
	RECT windowRect;
	SetRect(&windowRect, (screenWidth / 2) - (clientWidth / 2), (screenHeight / 2) - (clientHeight / 2), (screenWidth / 2) + (clientWidth / 2), (screenHeight / 2) + (clientHeight / 2));

	//DWORD style = (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX); // WS_THICKFRAME to resize
	DWORD style = (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME);
	AdjustWindowRectEx(&windowRect, style, FALSE, 0);

	HWND hwnd = CreateWindowEx(
		0,                              // Optional window styles.
		className,                     // Window class
		L"Memory Viewer",    // Window text
		style,            // Window style

		// Size and position
		windowRect.left, windowRect.top,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,

		NULL,       // Parent window    
		NULL,       // Menu
		hInstance,  // Instance handle
		NULL        // Additional application data
	);
	WinAssert(gMemoryWindow != 0);
	WinAssert(gMemoryWindow == hwnd);

	ShowWindow(gMemoryWindow, SW_SHOWDEFAULT);
	UpdateWindow(gMemoryWindow);
}

extern "C" DWORD CALLBACK run() {
	scrollY = 0;
	oldNumRows = 0;
	//_fltused = 0;
	unsigned int size = MB(512);

	LPVOID memory = VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

	void* m = memory;
	u32 trimmed = Memory::AlignAndTrim(&m, &size, Memory::DefaultPageSize);
	Memory::GlobalAllocator = Memory::Initialize(m, size, Memory::DefaultPageSize);
#if ATLAS_64
	WinAssert((u64)((void*)Memory::GlobalAllocator) % 8 == 0);
#elif ATLAS_32
	WinAssert((u32)((void*)Memory::GlobalAllocator) % 8 == 0);
#else
	#error Unknown platform
#endif

	gFrameBuffer = Memory::GlobalAllocator->New<FrameBuffer>();
	bgColor = Memory::GlobalAllocator->New <Win32Color>(255);
	freeMemoryColor = Memory::GlobalAllocator->New < Win32Color>();
	usedMemoryColor = Memory::GlobalAllocator->New < Win32Color>();
	trackMemoryColor = Memory::GlobalAllocator->New < Win32Color>();
	boxColor = Memory::GlobalAllocator->New < Win32Color>();
	textColor = Memory::GlobalAllocator->New < Win32Color>();

	int* x = Memory::GlobalAllocator->New < int>();
	Memory::GlobalAllocator->Delete(x);

	CreateMemoryWindow();

	MSG msg = { 0 };
	do {
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				break;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		Sleep(1);
	} while (gMemoryWindow != 0);

	Memory::GlobalAllocator->Delete(gFrameBuffer);
	Memory::GlobalAllocator->Delete(bgColor);
	Memory::GlobalAllocator->Delete(freeMemoryColor);
	Memory::GlobalAllocator->Delete(usedMemoryColor);
	Memory::GlobalAllocator->Delete(trackMemoryColor);
	Memory::GlobalAllocator->Delete(boxColor);
	Memory::GlobalAllocator->Delete(textColor);

	// Free up any dangling memory (maybe add to debug?)
	Memory::Allocation* iter = Memory::GlobalAllocator->active;
	while (iter != 0) {
		Memory::Allocation* next = (iter->nextOffset == 0) ? 0 :  (Memory::Allocation*)((u8*)Memory::GlobalAllocator + iter->nextOffset);
		u8* mem = (u8*)iter + sizeof(Memory::Allocation);
		Memory::GlobalAllocator->Release(mem);

		iter = next;
	}

	Memory::Shutdown(Memory::GlobalAllocator);
	Memory::GlobalAllocator = 0;
	VirtualFree(memory, 0, MEM_RELEASE);

	return 0;
}