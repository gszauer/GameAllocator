#pragma once

#ifndef _DEBUG
#define assert(condition) ;
#define Assert(condition) ;
#define NotImplemented() ;
#else 
void runtime_assert(bool condition, const char* file, int line);
#define assert(condition) runtime_assert(condition, __FILE__, __LINE__)
#define Assert(condition) runtime_assert(condition, __FILE__, __LINE__)
void NotImplemented();
#endif