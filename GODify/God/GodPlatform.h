//==========================================================================================================================
// GodPlatform.h - tiny I/O abstraction so the GOD converter core is platform independent.
//
// The Xbox 360 implementation lives in GodPlatform_Xbox.cpp (Win32-style XDK APIs + XeCryptSha).
// The PC test harness (tests/) supplies a POSIX implementation so the exact same core can be
// verified against iso2god-rs / extract-xiso on a PC.
//
// Paths are always passed with '\' separators (Xbox style, e.g. "Hdd:\Games\Halo 3\default.xex").
//==========================================================================================================================
#ifndef __GODPLATFORM_H__
#define __GODPLATFORM_H__
#pragma once

#include <string>
#include <vector>

typedef unsigned char       god_u8;
typedef unsigned short      god_u16;
typedef unsigned int        god_u32;
typedef unsigned long long  god_u64;

typedef void* GodFile;   // NULL = invalid

struct GodDirItem
{
	std::string Name;     // name only, no path
	bool        IsDir;
	god_u64     Size;     // file size (0 for dirs)
};

// --- files ---
GodFile  GodOpenRead(const char* Path, god_u64* OutSize);           // NULL on failure
GodFile  GodCreateWrite(const char* Path);                          // create/truncate, NULL on failure
GodFile  GodOpenWriteExisting(const char* Path);                    // open existing for writing at offset 0
god_u32  GodRead(GodFile File, void* Buf, god_u32 Size);            // returns bytes read (0 = EOF/error)
bool     GodWrite(GodFile File, const void* Buf, god_u32 Size);     // true if all bytes written
void     GodClose(GodFile File);

// --- filesystem ---
bool     GodListDir(const char* Path, std::vector<GodDirItem>& Out);// false if the dir can't be opened
bool     GodMakeDir(const char* Path);                               // true if it exists afterwards
bool     GodPathExists(const char* Path);
bool     GodDeleteFile(const char* Path);
bool     GodDeleteTree(const char* Path);                            // recursive delete of a dir (or a file)
bool     GodGetFreeSpace(const char* PathOnDevice, god_u64* OutFree);// false if unknown

// --- crypto ---
void     GodSha1(const void* Data, god_u32 Size, god_u8 Out[20]);

#endif
