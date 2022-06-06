#pragma once

#ifndef ATLAS_U8
#define ATLAS_U8
typedef unsigned char u8;
static_assert (sizeof(u8) == 1, "u8 should be defined as a 1 byte type");
#endif 

#ifndef ATLAS_U16
#define ATLAS_U16
typedef unsigned short u16;
static_assert (sizeof(u16) == 2, "u16 should be defined as a 2 byte type");
#endif 

#ifndef ATLAS_U32
#define ATLAS_U32
typedef unsigned int u32;
static_assert (sizeof(u32) == 4, "u32 should be defined as a 4 byte type");
#endif 

#ifndef ATLAS_U64
#define ATLAS_U64
typedef unsigned long long u64;
static_assert (sizeof(u64) == 8, "u64 should be defined as a 8 byte type");
#endif 

#ifndef ATLAS_I8
#define ATLAS_I8
typedef __int8 i8;
static_assert (sizeof(i8) == 1, "i8 should be defined as a 1 byte type");
#endif 

#ifndef ATLAS_I16
#define ATLAS_I16
typedef __int16 i16;
static_assert (sizeof(i16) == 2, "i16 should be defined as a 2 byte type");
#endif 

#ifndef ATLAS_I32
#define ATLAS_I32
typedef __int32 i32;
static_assert (sizeof(i32) == 4, "i32 should be defined as a 4 byte type");
#endif 

#ifndef ATLAS_I64
#define ATLAS_I64
typedef __int64 i64;
static_assert (sizeof(i64) == 8, "i64 should be defined as a 8 byte type");
#endif

#ifndef ATLAS_F32
#define ATLAS_F32
typedef float f32;
static_assert (sizeof(f32) == 4, "f32 should be defined as a 4 byte type");
#endif

#ifndef ATLAS_F64
#define ATLAS_F64
typedef double f64;
static_assert (sizeof(f64) == 8, "f64 should be defined as a 8 byte type");
#endif

static_assert (sizeof(void*) == 8, "Not on a 64 bit system");

// https://stackoverflow.com/questions/2653214/stringification-of-a-macro-value
#define xstr(a) str(a)
#define str(a) #a
#define __LOCATION__ "On line: " xstr(__LINE__) ", in file: " __FILE__