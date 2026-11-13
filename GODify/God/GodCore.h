//==========================================================================================================================
// GodCore.h - Convert an extracted Xbox 360 game folder (default.xex + files) into a Games on Demand package.
//
// How it works (no temporary ISO is ever written):
//   1. The game folder is scanned and an XDVDFS ("XISO") image layout is computed in memory,
//      using the same layout rules as extract-xiso (root table at sector 0x108, files after their
//      directory table, 0xFF sector padding, image padded to 64KB).
//   2. That virtual image is streamed sequentially straight into GOD data parts (Data0000, Data0001, ...),
//      with the SVOD hash tree iso2god uses: per part a master hash block, then 203 sub-parts of
//      [sub hash block + 204 x 4KB data blocks]; master hash blocks are chained part to part.
//   3. A LIVE (CON) header with content type 0x7000 (Games on Demand) is written next to the data folder.
//
// Output layout (same as iso2god):
//   <ContentRoot>\<TitleID>\00007000\<MediaID>          <- header
//   <ContentRoot>\<TitleID>\00007000\<MediaID>.data\    <- Data0000 ...
// where ContentRoot is normally "Hdd:\Content\0000000000000000".
//==========================================================================================================================
#ifndef __GODCORE_H__
#define __GODCORE_H__
#pragma once

#include "GodPlatform.h"

// ---- constants (match iso2god-rs) ----
#define GOD_BLOCK_SIZE          0x1000
#define GOD_BLOCKS_PER_SUBPART  0xCC        // 204
#define GOD_SUBPARTS_PER_PART   0xCB        // 203
#define GOD_BLOCKS_PER_PART     0xA1C4      // 203 * 204 data blocks
#define GOD_FULL_PART_BLOCKS    0xA290      // data + hash blocks in a full part
#define GOD_HEADER_SIZE         0xB000
#define GOD_CONTENT_TYPE        0x7000      // Games on Demand (Xbox 360 games and apps)
#define GOD_CONTENT_TYPE_XBOX1  0x5000      // Xbox Original
#define GOD_SHORTCUT_LAUNCH_FILE "launch.txt" // inside a shortcut package: full path of the program to start

// Title ID forced into shortcut packages. 0 = the app's own ID (or a made-up stable one if the .xex has none),
// so "View details" shows each app's own name. A shared ID (e.g. QuickBoot's C0DE9999) makes the dashboard show
// one name for every shortcut.
#define GOD_SHORTCUT_TITLE_ID   0

#define XISO_SECTOR_SIZE        0x800

typedef std::vector<god_u16> GodWString;   // UTF-16 code units (wchar_t is 32-bit on some PCs)

struct GodExecInfo
{
	god_u32 MediaId;
	god_u32 Version;
	god_u32 BaseVersion;
	god_u32 TitleId;
	god_u8  Platform;
	god_u8  ExecutableType;
	god_u8  DiscNumber;
	god_u8  DiscCount;
};

// Reads the execution ID (title ID, media ID, disc number ...) from a XEX2 header. Works on encrypted retail xex files.
// If the xex has no execution info at all (e.g. FreestyleDash) and OutMissing is given, returns true with
// *OutMissing = true instead of failing.
bool GodReadXexExecInfo(const char* XexPath, GodExecInfo& Out, std::string& Err, bool* OutMissing = NULL);

// Reads the title ID (and the title name from the certificate) from an original Xbox XBE.
bool GodReadXbeExecInfo(const char* XbePath, GodExecInfo& Out, GodWString& TitleName, std::string& Err);

// The launcher program (GODifyLauncher.xex) that shortcut packages boot. GODify passes the copy embedded in its
// own XEX at start-up. Without it, App/Demo shortcuts can't be made.
void GodSetShortcutLauncher(const void* Data, god_u32 Size);
bool GodHaveShortcutLauncher();

// Title name lookup (built-in list from iso2god-rs). Returns UTF-8 or NULL.
const char* GodLookupTitleName(god_u32 TitleId);
GodWString  GodUtf8ToUtf16(const char* Utf8);
std::string GodUtf16ToAscii(const GodWString& W);   // for display/logging; non-ASCII -> '?'

// Progress shared between the worker thread and the UI. All fields are written by the worker only,
// except Cancel which the UI sets.
enum GodPhase { GOD_PHASE_IDLE = 0, GOD_PHASE_SCANNING, GOD_PHASE_WRITING, GOD_PHASE_FINALIZING, GOD_PHASE_DONE };

struct GodProgress
{
	volatile god_u64 BytesDone;
	volatile god_u64 BytesTotal;
	volatile int     Phase;
	volatile int     CurrentPart;
	volatile int     PartCount;
	volatile bool    Cancel;
};

enum GodResult { GOD_OK = 0, GOD_CANCELLED, GOD_FAILED };

//--------------------------------------------------------------------------------------------------------------------------
// XDVDFS image built from a folder, readable as a sequential stream.
//--------------------------------------------------------------------------------------------------------------------------
class GodImage
{
public:
	GodImage();
	~GodImage();

	// Scans the folder and computes the full layout. SkipSystemUpdate drops a root level "$SystemUpdate" folder.
	// BootAlias: name of a root file (e.g. "Aurora.xex") that should also be reachable as BootName ("default.xex").
	// The alias is just a second directory entry pointing at the same sectors - no data is duplicated.
	bool Build(const char* Folder, bool SkipSystemUpdate, god_u64 FileTime, std::string& Err,
	           const char* BootAlias = NULL, const char* BootName = NULL);

	// Builds an image that only contains in-memory files (used for shortcut packages).
	struct MemFile { std::string Name; std::vector<god_u8> Data; };
	bool BuildFromMemory(const std::vector<MemFile>& Files, god_u64 FileTime, std::string& Err);

	god_u64 ImageSize() const   { return m_ImageSize; }
	god_u32 FileCount() const   { return m_FileCount; }
	god_u64 FileBytes() const   { return m_FileBytes; }

	// Sequential reading of the virtual image.
	bool    BeginRead(std::string& Err);
	bool    Read(god_u8* Buf, god_u32 Size, std::string& Err);   // must read exactly Size bytes
	void    EndRead();

private:
	struct Node
	{
		std::string      Name;
		std::string      Path;          // full source path
		bool             IsDir;
		god_u64          Size;          // file size, or directory table size (unpadded)
		god_u32          Sector;
		std::vector<int> Children;      // sorted (dirs only)
		std::vector<int> TreeOrder;     // children in pre-order of the balanced BST (table order)
		std::vector<god_u32> EntryOffset; // byte offset of each TreeOrder entry inside the table
		std::vector<int> Left, Right;   // per TreeOrder position: index into TreeOrder or -1
		int              AliasOf;       // >= 0: this entry points at another node's data
		int              MemIdx;        // >= 0: file data comes from m_MemFiles instead of disk
	};

	enum SegType { SEG_ZERO, SEG_FF, SEG_MEM, SEG_FILE };
	bool Finish(std::string& Err);   // layout + segments once m_Nodes is filled
	std::vector<MemFile> m_MemFiles;
	struct Segment
	{
		SegType Type;
		god_u64 Length;
		int     Node;       // SEG_FILE: node index; SEG_MEM: index into m_Tables (-1 = volume descriptor)
	};

	bool ScanDir(int NodeIdx, bool IsRoot, bool SkipSystemUpdate, int Depth, std::string& Err);
	std::string m_BootAlias, m_BootName;
	int  BuildTree(Node& Dir, int Lo, int Hi);   // returns TreeOrder position of the subtree root, or -1
	void ComputeTable(Node& Dir);
	void Layout(int NodeIdx, god_u32& CurSector);
	void EmitSegments(int NodeIdx);
	void BuildTableBytes(int NodeIdx, std::vector<god_u8>& Out);
	void AddSeg(SegType T, god_u64 Len, int N);

	std::vector<Node>                   m_Nodes;
	std::vector< std::vector<god_u8> >  m_Tables;   // directory table bytes, by segment reference
	std::vector<god_u8>                 m_VolDesc;  // one sector
	std::vector<Segment>                m_Segs;
	god_u64  m_ImageSize;
	god_u32  m_FileCount;
	god_u64  m_FileBytes;
	god_u64  m_FileTime;

	// read state
	size_t   m_SegIdx;
	god_u64  m_SegPos;
	GodFile  m_File;
};

//--------------------------------------------------------------------------------------------------------------------------
// A planned conversion: everything the confirm screen needs, plus the image layout.
//--------------------------------------------------------------------------------------------------------------------------
enum GodKind { GOD_KIND_360_GAME = 0, GOD_KIND_360_APP, GOD_KIND_XBOX1 };   // from the file: default.xex / other .xex / .xbe
// chosen by the user (xex only):
//   GAME: the whole folder is converted into the package (<TitleID>\00007000\<MediaID>)
//   APP:  a small shortcut package that starts the program from its folder (<TitleID>\00007000\<Name>_<tag>),
//         so apps that save into their own folder (FSD, Aurora...) keep working.
enum GodCategory { GOD_CAT_GAME = 0, GOD_CAT_APP };

struct GodPlan
{
	std::string  ExePath;        // the executable that was picked (default.xex, Aurora.xex, default.xbe ...)
	std::string  SourceFolder;   // folder that contains it (becomes the root of the image)
	int          Kind;           // GodKind
	int          Category;       // GodCategory (always GAME for .xbe, always APP for other .xex names)
	bool         ShortcutOnly;   // .xex not named default.xex: its folder isn't "the game", so only a shortcut is made
	god_u32      ContentType;    // 0x7000 (games and apps) or 0x5000 (original Xbox)
	bool         SyntheticIds;   // the xex had no title/media ID - stable made-up ones are used
	GodExecInfo  HeaderExec;     // what goes into the package header
	god_u64      FileTime;       // timestamp used in the image
	std::string  ContentRoot;    // e.g. "Hdd:\Content\0000000000000000"
	GodExecInfo  Exec;
	GodWString   Title;          // shown on the dashboard (max 63 chars used)
	GodImage     Image;          // full image of the folder (Game)
	GodImage     ShortcutImage;  // launcher + launch.txt (App or Demo)
	GodImage&    ActiveImage() { return Category == GOD_CAT_APP ? ShortcutImage : Image; }

	std::string  TitleDir;       // ContentRoot\TTTTTTTT\00007000 (00005000 for Xbox Original)
	std::string  HeaderPath;     // TitleDir\MMMMMMMM (title ID for Xbox Original)
	std::string  DataDir;        // HeaderPath + ".data"
	god_u64      BlockCount;
	god_u32      PartCount;
	god_u64      OutputBytes;    // approx. bytes written (header + all parts)
	bool         AlreadyExists;
};

// Reads the executable, scans its folder and computes output paths/sizes.
//   *.xex  -> Games on Demand (0x7000). If it isn't named default.xex (apps: "Aurora.xex"), the image also gets a
//             "default.xex" entry pointing at it, because that is what the console boots from a package.
//   *.xbe  -> Xbox Original (0x5000), must be default.xbe.
// Title: built-in name list, then the XBE certificate name, then the folder / file name.
bool GodPreparePlan(const char* ExePath, const char* ContentRoot, bool SkipSystemUpdate, god_u64 FileTime,
                    GodPlan& Plan, std::string& Err);

// Switches a planned .xex between Game (full conversion) and App (shortcut). Recomputes output paths, sizes and
// AlreadyExists. Ignored for .xbe. Fails only if App is chosen and no launcher is available.
bool GodSetCategory(GodPlan& Plan, int Category, std::string& Err);

// Runs the conversion. On cancel/failure the partial output is removed.
GodResult GodConvert(GodPlan& Plan, GodProgress* Progress, std::string& Err);

// Builds the 0xB000 byte LIVE header (exposed for tests).
void GodBuildHeader(god_u8* Header, god_u32 ContentType, const GodExecInfo& Exec, const GodWString& Title, god_u64 BlockCount,
                    god_u32 PartCount, god_u64 PartsTotalSize, const god_u8 TopHash[20]);

#endif
