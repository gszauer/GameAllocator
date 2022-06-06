#include <windows.h>
#include <commctrl.h>

#include "mem.h"
#include "types.h"
#include "assert.h"

#define IDT_TIMER1 1001
#define IDC_LIST 1
#define IDC_STATIC 2
#define ID_UPDOWN 3
#define ID_EDIT 4
#define ID_ALLOCATE_MEM 5
#define ID_FREE_MEM 6
#define ID_FREE_MEM_ALL 7
#define ID_REFRESH_MEM 8
#define ID_TESTS_MEM 9

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

// https://stackoverflow.com/questions/16031470/header-with-memory-size-definitions
#define KB(x)   ((size_t) (x) << 10)
#define MB(x)   ((size_t) (x) << 20)
#define GB(x)   ((size_t) (x) << 30)

struct MemoryDebugInfo {
	u8* PageMask;
	u32 NumberOfPages;

	u32 NumFreePages;
	u32 NumUsedPages;
	u32 NumOverheadPages;

	MemoryDebugInfo(Memory::Allocator* allocator) {
		u64 allocatorHeaderSize = sizeof(Memory::Allocator);
		u64 allocatorHeaderPadding = (((allocatorHeaderSize % Memory::DefaultAlignment) > 0) ? Memory::DefaultAlignment - (allocatorHeaderSize % Memory::DefaultAlignment) : 0);
		PageMask = ((u8*)allocator) + allocatorHeaderSize + allocatorHeaderPadding;

		NumberOfPages = allocator->size / Memory::PageSize; // 1 page = 4096 bytes, how many are needed
		assert(allocator->size % Memory::PageSize == 0, "Allocator size should line up with page size");
		
		u32 allocatorOverheadBytes = allocator->offsetToAllocatable;
		assert(allocatorOverheadBytes % Memory::PageSize == 0, "Offset to allocatable should always line up with page size");

		NumFreePages = 0;
		NumUsedPages = 0;
		NumOverheadPages = allocatorOverheadBytes / Memory::PageSize; // No need for a +1 padding allocatorOverheadBytes should be aligned to Memory::PageSize

		u32* mask = (u32*)PageMask;
		for (int page = NumOverheadPages; page < NumberOfPages; ++page) { // Don't start at page 0?
			const u32 block = page / 32;
			const int bit = page % 32;

			const bool used = mask[block] & (1 << bit);
			if (!used) {
				NumFreePages += 1;
			}
			else {
				NumUsedPages += 1;
			}
		}

		assert(NumFreePages + NumUsedPages + NumOverheadPages == NumberOfPages, "Page number does not add up");
	}

	bool IsPageSet(u32 page) {
		NotImplemented();
		return false;
	}
};

struct Win32Color {
	HBRUSH brush;
	COLORREF color;
	unsigned char r;
	unsigned char g;
	unsigned char b;
	unsigned char a;

	Win32Color(unsigned char _r, unsigned char _g, unsigned char _b) {
		color = RGB(_r, _g, _b);
		brush = 0;// CreateSolidBrush(color);
		r = _r;
		g = _g;
		b = _b;
		a = 255;
	}

	void CreateBrushObject() {
		Assert(brush == 0);
		brush = CreateSolidBrush(color);
	}

	void DestroyBrushObject() {
		Assert(brush != 0);
		DeleteObject(brush);
		brush = 0; 
	}

	Win32Color(const Win32Color& other) {
		Copy(other);
	}

	Win32Color& operator=(const Win32Color& other) {
		if (&other != this) {
			Copy(other);
		}
		return *this;
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

	~Win32Color() {
		Assert(brush == 0);
	}
};

static HWND gMemoryWindow;

// https://yal.cc/cpp-a-very-tiny-dll/
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
	int mib = kib / 1024 + (kib % 1024 ? 1 : 0);
	kib = allocator->size / 1024 + (allocator->size % 1024 ? 1 : 0);

	wsprintfW(displaybuffer, L"Tracking %d Pages, %d KiB (%d MiB)", memInfo.NumberOfPages, kib, mib); // Removed with %.2f %% overhead
	SetWindowText(labels[0], displaybuffer);

	wsprintfW(displaybuffer, L"Pages: %d free, %d used, %d overhead", memInfo.NumFreePages, memInfo.NumUsedPages, memInfo.NumOverheadPages);
	SetWindowText(labels[1], displaybuffer);

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
	for (Memory::Allocation* iter = allocator->active; iter != 0; iter = iter->next) {
		size_t len = 1; // To account for '\0' at the end of the string.
		char* it = (char*)iter->location;
		while (it != 0 && len < 1024 - 256) {
			it += 1;
			len += 1;
		}

		u32 pages = iter->size / Memory::PageSize + (iter->size % Memory::PageSize ? 1 : 0);
		wsprintfW(displaybuffer, L"Size: %d bytes, Pages: %d, >", iter->size, pages);
		wchar_t* print_to = displaybuffer;
		while (*print_to != L'>') {
			print_to++;
		}
		MultiByteToWideChar(0, 0, iter->location, len, print_to, len);

		SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)displaybuffer);
	}

	SendMessage(list, LB_SETCURSEL, selection, 0);
}

Win32WindowLayout GetWindowLayout(HWND hwnd) {
	const u32 sideMargin = 50;
	const u32 topMargin = 25;

	const u32 middleSeperator = 40;
	const u32 minChartWidth = 150;
	const u32 minChartHeight = 150;
	
	const u32 bottomAreaHeight = 300;
	const u32 bottomSeperator = 10;
	
	const u32 bottomLeftWidth = 365;

	RECT clientRect = {0};
	GetClientRect(hwnd, &clientRect);
	const u32 clientHeight = clientRect.bottom - clientRect.top;
	const u32 clientWidth = clientRect.right - clientRect.left;

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
	assert(minChartWidth / 3 > bottomSeperator /2);
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
	static Win32Color bgColor(30, 30, 30);
	// Great orange: (255, 125, 64);
	static Win32Color freeMemoryColor(110, 110, 220);
	static Win32Color usedMemoryColor(110, 240, 110);
	static Win32Color trackMemoryColor(255, 110, 110);
	static i32 scrollY = 0;

	switch (iMsg) {
	case WM_CREATE:
		bgColor.CreateBrushObject();
		freeMemoryColor.CreateBrushObject();
		usedMemoryColor.CreateBrushObject();
		trackMemoryColor.CreateBrushObject();
		SetClassLongPtr(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)bgColor.brush);
		break;
	case WM_DESTROY:
		bgColor.DestroyBrushObject();
		freeMemoryColor.DestroyBrushObject();
		usedMemoryColor.DestroyBrushObject();
		trackMemoryColor.DestroyBrushObject();
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
			WCHAR buf[20];
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

			InvalidateRect(hwnd, 0, false);
		}
		break;
	case WM_PAINT:
	case WM_ERASEBKGND:
		{
			MemoryDebugInfo memInfo(Memory::GlobalAllocator);
			u32* mask = (u32*)memInfo.PageMask;

			PAINTSTRUCT ps;
			RECT clientRect;

			HDC hdc = BeginPaint(hwnd, &ps);

			GetClientRect(hwnd, &clientRect);
			int clientWidth = clientRect.right - clientRect.left;
			int clientHeight = clientRect.bottom - clientRect.top;

			const u32 pageWidth = 3;
			const u32 pageHeight = 5;
			const u32 pagePadding = 1;
			
			const u32 numColumns = clientWidth / (pagePadding + pageWidth + pagePadding);
			const u32 numRows = clientHeight / (pagePadding + pageHeight + pagePadding) + (clientHeight % (pagePadding + pageHeight + pagePadding) ? 1 : 0);

			static u32 oldNumRows = 0;
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
					const u32 m = index / 32;
					const u32 b = index % 32;
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

			EndPaint(hwnd, &ps);
		}
		return 0;
	}

	return DefWindowProc(hwnd, iMsg, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam) {
	if (iMsg != WM_GETMINMAXINFO && iMsg != WM_NCCREATE && iMsg != WM_NCCALCSIZE) {
		Assert(gMemoryWindow != 0);
	}

	static Win32Color bgColor(30, 30, 30);
	static HWND hwndChart = 0;
	static HWND hwndLabels[9] = { 0 };
	static HWND hwndList = 0;
	static HWND hwndButtons[5] = { 0 };
	static HWND hwndUpDown = { 0 };
	static HWND hwndUpDownEdit = { 0 };
	static HWND hwndCombo = { 0 };

	static Win32Color boxColor(50, 50, 50);
	static Win32Color textColor(220, 220, 220);

	switch (iMsg) {
	case WM_NCCREATE:
		Assert(gMemoryWindow == 0);
		gMemoryWindow = hwnd;
		break;
	case WM_NCDESTROY:
		Assert(gMemoryWindow != 0);
		Assert(gMemoryWindow == hwnd);
		gMemoryWindow = 0;
		break;
	case WM_CREATE:
		Assert(gMemoryWindow == hwnd);
		{ // Set up render styles
			bgColor.CreateBrushObject();
			boxColor.CreateBrushObject();
			textColor.CreateBrushObject();
			SetClassLongPtr(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)bgColor.brush);
		}
		{ // Create Memory chart window
			// https://stackoverflow.com/questions/32094254/how-to-control-scrollbar-in-vc-win32-api
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
			hwndButtons[3] = CreateWindowW(L"Button", L"Run Tests", WS_CHILD | WS_VISIBLE, 10, 10, 50, 50, hwnd, (HMENU)ID_TESTS_MEM, NULL, NULL);
			hwndButtons[4] = CreateWindowW(L"Button", L"Free All", WS_CHILD | WS_VISIBLE, 10, 10, 50, 50, hwnd, (HMENU)ID_FREE_MEM_ALL, NULL, NULL);

			hwndUpDown = CreateWindowW(UPDOWN_CLASSW, NULL, WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT, 0, 0, 0, 0, hwnd, (HMENU)ID_UPDOWN, NULL, NULL);
			hwndUpDownEdit = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, NULL, WS_CHILD | WS_VISIBLE | ES_RIGHT, 10, 10, 75, 25, hwnd, (HMENU)ID_EDIT, NULL, NULL);
			SendMessageW(hwndUpDown, UDM_SETBUDDY, (WPARAM)hwndUpDownEdit, 0);
			SendMessageW(hwndUpDown, UDM_SETRANGE, 0, MAKELPARAM(UD_MAX_POS, UD_MIN_POS));
			SendMessageW(hwndUpDown, UDM_SETPOS32, 0, 1);

			hwndCombo = CreateWindow(WC_COMBOBOX, TEXT(""), CBS_DROPDOWN | CBS_HASSTRINGS | WS_CHILD | WS_OVERLAPPED | WS_VISIBLE, 0, 0, 50, 50, hwnd, NULL, HINST_THISCOMPONENT, NULL);
			static const wchar_t* items[] = { L"bytes", L"KiB", L"MiB" };
			int debug_0 = SendMessage(hwndCombo, (UINT)CB_ADDSTRING, 0, (LPARAM)items[0]);
			int debug_1 = SendMessage(hwndCombo, (UINT)CB_ADDSTRING, 0, (LPARAM)items[1]);
			int debug_2 = SendMessage(hwndCombo, (UINT)CB_ADDSTRING, 0, (LPARAM)items[2]);
			SendMessage(hwndCombo, CB_SETCURSEL, (WPARAM)1, (LPARAM)0);
		}
		{ // Layout the window elements that where just created to the size of the window
			Win32WindowLayout layout = GetWindowLayout(hwnd);
			SetWindowLayout(layout, hwndChart, hwndLabels, hwndList, hwndButtons, hwndUpDown, hwndUpDownEdit, hwndCombo);
			ResetListBoxContent(Memory::GlobalAllocator, hwndList);
			InvalidateRect(hwnd, 0, false);
		}
		break;
	case WM_TIMER:
		if (wParam == IDT_TIMER1) {
		}
		break;
	case WM_CLOSE:
		Assert(gMemoryWindow == hwnd);
		bgColor.DestroyBrushObject();
		boxColor.DestroyBrushObject();
		textColor.DestroyBrushObject();
		break;
	case WM_COMMAND:
	{
		bool update = false;
		if (LOWORD(wParam) == ID_ALLOCATE_MEM) {
			int howMany = SendMessage(hwndUpDown, UDM_GETPOS, 0, 0);
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

			malloc(howMany * units);

			update = true;
		}
		if (LOWORD(wParam) == ID_FREE_MEM) {
			int selection = SendMessage(hwndList, LB_GETCURSEL, 0, 0);
			if (selection >= 0) {
				int counter = 0;
				Memory::Allocation* iter = Memory::GlobalAllocator->active;
				assert(iter != 0);
				for (; iter != 0 && counter != selection; iter = iter->next, counter++);
				assert(counter == selection);
				u32 allocationHeaderSize = sizeof(Memory::Allocation);
				u32 allocationHeaderPadding = sizeof(Memory::Allocation) % iter->alignment > 0 ? iter->alignment - sizeof(Memory::Allocation) % iter->alignment : 0;
				u8* mem = (u8*)iter + allocationHeaderSize + allocationHeaderPadding;
				free(mem);
			}
			update = true;
		}
		if (LOWORD(wParam) == ID_FREE_MEM_ALL) {
			Memory::Allocation* iter = Memory::GlobalAllocator->active;
			while (iter != 0) {
				Memory::Allocation* next = iter->next;

				const u32 allocationHeaderSize = sizeof(Memory::Allocation);
				const u32 allocationHeaderPadding = sizeof(Memory::Allocation) % iter->alignment > 0 ? iter->alignment - sizeof(Memory::Allocation) % iter->alignment : 0;
				u8* mem = (u8*)iter + allocationHeaderSize + allocationHeaderPadding;
				free(mem);

				iter = next;
			}
			update = true;
		}
		if (LOWORD(wParam) == ID_REFRESH_MEM) {
			update = true;
		}
		if (update) {
			SetWindowLayout(GetWindowLayout(hwnd), hwndChart, hwndLabels, hwndList, hwndButtons, hwndUpDown, hwndUpDownEdit, hwndCombo);
			ResetListBoxContent(Memory::GlobalAllocator, hwndList);
			InvalidateRect(hwnd, 0, false);
		}
	};
	case WM_NOTIFY:
		break;
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLORLISTBOX:
		for (u32 i = 0; i < 11; ++i) {
			HDC hdcStatic = (HDC)wParam;
			if (hwndLabels[i] == (HWND)lParam) {
				SetTextColor(hdcStatic, textColor.color);
				SetBkColor(hdcStatic, boxColor.color);
				return (INT_PTR)boxColor.brush;
			}
		}
		if (hwndList == (HWND)lParam) {
			HDC hdcStatic = (HDC)wParam;
			SetTextColor(hdcStatic, textColor.color);
			SetBkColor(hdcStatic, boxColor.color);
			return (INT_PTR)boxColor.brush;
		}
		break;
	case WM_SIZE:
	{
		UINT width = LOWORD(lParam);
		UINT height = HIWORD(lParam);

#if _DEBUG
		RECT clientRect = { 0 };
		GetClientRect(hwnd, &clientRect);
		assert(clientRect.right - clientRect.left == width);
		assert(clientRect.bottom - clientRect.top == height);
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
	Assert(gMemoryWindow != 0);
	Assert(gMemoryWindow == hwnd);

	ShowWindow(gMemoryWindow, SW_SHOWDEFAULT);
	UpdateWindow(gMemoryWindow);
}

// https ://stackoverflow.com/questions/58513890/how-to-create-minimal-win32-c-program-with-no-windows-apis-in-import-table-with
extern "C" DWORD CALLBACK run() {
	unsigned int size = MB(32);
	LPVOID memory = VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	Memory::Initialize(memory, size);

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

	Memory::Shutdown();
	VirtualFree(memory, 0, MEM_RELEASE);

	return 0;
}

int main(int argc, char** argv) {
	return run();
}