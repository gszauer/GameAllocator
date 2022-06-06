#include "assert.h"

#ifdef _DEBUG
void runtime_assert(bool condition, const char* file, int line) {
	char* data = (char*)((void*)0);
	if (condition == false) {
		*data = '\0';
	}
}

void NotImplemented() {
	char* data = (char*)((void*)0);
	*data = '\0';
}
#endif