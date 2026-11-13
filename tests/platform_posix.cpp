// POSIX implementation of GodPlatform.h - used only by the PC test harness.
#include "../GODify/God/GodPlatform.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <openssl/sha.h>

static std::string Fix(const char* p) { std::string s = p; for (size_t i = 0; i < s.size(); i++) if (s[i] == '\\') s[i] = '/'; return s; }

GodFile GodOpenRead(const char* Path, god_u64* OutSize)
{
	FILE* f = fopen(Fix(Path).c_str(), "rb"); if (!f) return NULL;
	struct stat st; fstat(fileno(f), &st); if (OutSize) *OutSize = st.st_size; return f;
}
GodFile GodCreateWrite(const char* Path) { return fopen(Fix(Path).c_str(), "wb"); }
GodFile GodOpenWriteExisting(const char* Path) { return fopen(Fix(Path).c_str(), "r+b"); }
god_u32 GodRead(GodFile F, void* B, god_u32 S) { return (god_u32)fread(B, 1, S, (FILE*)F); }
bool GodWrite(GodFile F, const void* B, god_u32 S) { return fwrite(B, 1, S, (FILE*)F) == S; }
void GodClose(GodFile F) { fclose((FILE*)F); }

bool GodListDir(const char* Path, std::vector<GodDirItem>& Out)
{
	std::string p = Fix(Path); DIR* d = opendir(p.c_str()); if (!d) return false;
	struct dirent* e;
	while ((e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		struct stat st; std::string full = p + "/" + e->d_name; if (stat(full.c_str(), &st)) continue;
		GodDirItem it; it.Name = e->d_name; it.IsDir = S_ISDIR(st.st_mode); it.Size = it.IsDir ? 0 : st.st_size; Out.push_back(it);
	}
	closedir(d); return true;
}
bool GodMakeDir(const char* Path) { std::string p = Fix(Path); mkdir(p.c_str(), 0755); struct stat st; return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
bool GodPathExists(const char* Path) { struct stat st; return stat(Fix(Path).c_str(), &st) == 0; }
bool GodDeleteFile(const char* Path) { return unlink(Fix(Path).c_str()) == 0; }
bool GodDeleteTree(const char* Path)
{
	std::string p = Fix(Path); struct stat st; if (lstat(p.c_str(), &st)) return true;
	if (!S_ISDIR(st.st_mode)) return unlink(p.c_str()) == 0;
	std::vector<GodDirItem> items; GodListDir(Path, items);
	for (size_t i = 0; i < items.size(); i++) GodDeleteTree((std::string(Path) + "\\" + items[i].Name).c_str());
	return rmdir(p.c_str()) == 0;
}
bool GodGetFreeSpace(const char* Path, god_u64* Out) { struct statvfs v; if (statvfs(Fix(Path).c_str(), &v)) return false; *Out = (god_u64)v.f_bavail * v.f_frsize; return true; }
void GodSha1(const void* D, god_u32 S, god_u8 O[20]) { SHA1((const unsigned char*)D, S, O); }
