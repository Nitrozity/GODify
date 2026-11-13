//==========================================================================================================================
// GodCore.cpp - folder -> XDVDFS (virtual) -> Games on Demand converter. See GodCore.h for an overview.
//
// Format references:
//   iso2god-rs  (MIT, Ilia Pozdnyakov)  - GOD part/hash layout and LIVE header fields
//   extract-xiso (BSD-style, in)        - XDVDFS directory table layout used by "-c" image creation
//==========================================================================================================================
#include "GodCore.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <algorithm>

//--------------------------------------------------------------------------------------------------------------------------
// byte helpers (explicit endianness: the Xbox is big-endian, XDVDFS is little-endian)
//--------------------------------------------------------------------------------------------------------------------------
static void PutLE16(god_u8* p, god_u16 v) { p[0] = (god_u8)v; p[1] = (god_u8)(v >> 8); }
static void PutLE32(god_u8* p, god_u32 v) { p[0] = (god_u8)v; p[1] = (god_u8)(v >> 8); p[2] = (god_u8)(v >> 16); p[3] = (god_u8)(v >> 24); }
static void PutLE64(god_u8* p, god_u64 v) { PutLE32(p, (god_u32)v); PutLE32(p + 4, (god_u32)(v >> 32)); }
static void PutBE16(god_u8* p, god_u16 v) { p[0] = (god_u8)(v >> 8); p[1] = (god_u8)v; }
static void PutBE24(god_u8* p, god_u32 v) { p[0] = (god_u8)(v >> 16); p[1] = (god_u8)(v >> 8); p[2] = (god_u8)v; }
static void PutBE32(god_u8* p, god_u32 v) { p[0] = (god_u8)(v >> 24); p[1] = (god_u8)(v >> 16); p[2] = (god_u8)(v >> 8); p[3] = (god_u8)v; }
static void PutBE64(god_u8* p, god_u64 v) { PutBE32(p, (god_u32)(v >> 32)); PutBE32(p + 4, (god_u32)v); }
static god_u32 GetBE32(const god_u8* p) { return ((god_u32)p[0] << 24) | ((god_u32)p[1] << 16) | ((god_u32)p[2] << 8) | p[3]; }

static god_u64 DivCeil(god_u64 a, god_u64 b) { return (a + b - 1) / b; }
static god_u32 SectorsFor(god_u64 Bytes) { return (god_u32)DivCeil(Bytes, XISO_SECTOR_SIZE); }

static std::string JoinPath(const std::string& a, const std::string& b)
{
	if (a.empty()) return b;
	if (a[a.size() - 1] == '\\') return a + b;
	return a + "\\" + b;
}

static std::string LastComponent(const std::string& Path)
{
	std::string p = Path;
	while (!p.empty() && p[p.size() - 1] == '\\') p.erase(p.size() - 1);
	size_t pos = p.find_last_of('\\');
	return pos == std::string::npos ? p : p.substr(pos + 1);
}

static char UpperAscii(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

static bool EqualsNoCase(const std::string& a, const char* b)
{
	size_t n = strlen(b);
	if (a.size() != n) return false;
	for (size_t i = 0; i < n; i++) if (UpperAscii(a[i]) != UpperAscii(b[i])) return false;
	return true;
}

// Same ordering as extract-xiso's avl_compare_key / the console's GDF lookup: ASCII upper-cased, byte compare.
static int CompareXisoNames(const std::string& a, const std::string& b)
{
	size_t n = a.size() < b.size() ? a.size() : b.size();
	for (size_t i = 0; i < n; i++)
	{
		unsigned char ca = (unsigned char)UpperAscii(a[i]);
		unsigned char cb = (unsigned char)UpperAscii(b[i]);
		if (ca != cb) return ca < cb ? -1 : 1;
	}
	if (a.size() == b.size()) return 0;
	return a.size() < b.size() ? -1 : 1;
}

//--------------------------------------------------------------------------------------------------------------------------
// Shortcut launcher
//--------------------------------------------------------------------------------------------------------------------------
static std::vector<god_u8> g_Launcher;

void GodSetShortcutLauncher(const void* Data, god_u32 Size)
{
	g_Launcher.assign((const god_u8*)Data, (const god_u8*)Data + Size);
}

bool GodHaveShortcutLauncher() { return g_Launcher.size() > 0x18 && memcmp(&g_Launcher[0], "XEX2", 4) == 0; }

//--------------------------------------------------------------------------------------------------------------------------
// Title names / strings
//--------------------------------------------------------------------------------------------------------------------------
struct GodTitleEntry { god_u32 Id; const char* Name; };
static const GodTitleEntry g_TitleNames[] =
{
#include "GodTitleNames.inc"
};

const char* GodLookupTitleName(god_u32 TitleId)
{
	int lo = 0, hi = (int)(sizeof(g_TitleNames) / sizeof(g_TitleNames[0])) - 1;
	while (lo <= hi)
	{
		int mid = (lo + hi) / 2;
		if (g_TitleNames[mid].Id == TitleId) return g_TitleNames[mid].Name;
		if (g_TitleNames[mid].Id < TitleId) lo = mid + 1; else hi = mid - 1;
	}
	return NULL;
}

GodWString GodUtf8ToUtf16(const char* s)
{
	GodWString out;
	const unsigned char* p = (const unsigned char*)s;
	while (p && *p)
	{
		god_u32 cp; int extra;
		if (*p < 0x80) { cp = *p; extra = 0; }
		else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; extra = 1; }
		else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; extra = 2; }
		else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; extra = 3; }
		else { p++; out.push_back('?'); continue; }
		p++;
		bool bad = false;
		for (int i = 0; i < extra; i++)
		{
			if ((*p & 0xC0) != 0x80) { bad = true; break; }
			cp = (cp << 6) | (*p & 0x3F); p++;
		}
		if (bad) { out.push_back('?'); continue; }
		if (cp >= 0x10000)
		{
			cp -= 0x10000;
			out.push_back((god_u16)(0xD800 + (cp >> 10)));
			out.push_back((god_u16)(0xDC00 + (cp & 0x3FF)));
		}
		else out.push_back((god_u16)cp);
	}
	return out;
}

std::string GodUtf16ToAscii(const GodWString& W)
{
	std::string s;
	for (size_t i = 0; i < W.size(); i++) s += (W[i] >= 0x20 && W[i] < 0x7F) ? (char)W[i] : '?';
	return s;
}

//--------------------------------------------------------------------------------------------------------------------------
// XEX2 execution info
//--------------------------------------------------------------------------------------------------------------------------
bool GodReadXexExecInfo(const char* XexPath, GodExecInfo& Out, std::string& Err, bool* OutMissing)
{
	memset(&Out, 0, sizeof(Out));
	if (OutMissing) *OutMissing = false;
	god_u64 fileSize = 0;
	GodFile f = GodOpenRead(XexPath, &fileSize);
	if (!f) { Err = "Could not open default.xex"; return false; }

	god_u8 hdr[0x18];
	if (fileSize < sizeof(hdr) || GodRead(f, hdr, sizeof(hdr)) != sizeof(hdr)) { GodClose(f); Err = "default.xex is too small"; return false; }
	if (memcmp(hdr, "XEX2", 4) != 0) { GodClose(f); Err = "default.xex is not a XEX2 file"; return false; }

	god_u32 headerSize = GetBE32(hdr + 0x08);   // offset of the PE data = size of the header area
	god_u32 fieldCount = GetBE32(hdr + 0x14);
	if (headerSize < 0x18 || headerSize > 0x400000 || headerSize > fileSize || fieldCount > 0x1000)
	{
		GodClose(f); Err = "default.xex header looks corrupt"; return false;
	}

	std::vector<god_u8> buf(headerSize);
	memcpy(&buf[0], hdr, sizeof(hdr));
	god_u32 want = headerSize - (god_u32)sizeof(hdr);
	if (want && GodRead(f, &buf[sizeof(hdr)], want) != want) { GodClose(f); Err = "Could not read the default.xex header"; return false; }
	GodClose(f);

	for (god_u32 i = 0; i < fieldCount; i++)
	{
		god_u32 at = 0x18 + i * 8;
		if (at + 8 > headerSize) break;
		god_u32 key = GetBE32(&buf[at]);
		god_u32 val = GetBE32(&buf[at + 4]);
		if (key == 0x00040006)
		{
			if (val + 0x18 > headerSize) { Err = "Execution info is out of range"; return false; }
			const god_u8* e = &buf[val];
			Out.MediaId        = GetBE32(e + 0x00);
			Out.Version        = GetBE32(e + 0x04);
			Out.BaseVersion    = GetBE32(e + 0x08);
			Out.TitleId        = GetBE32(e + 0x0C);
			Out.Platform       = e[0x10];
			Out.ExecutableType = e[0x11];
			Out.DiscNumber     = e[0x12];
			Out.DiscCount      = e[0x13];
			return true;
		}
	}
	if (OutMissing) { *OutMissing = true; return true; }
	Err = "The .xex has no title ID (execution info)";
	return false;
}

//--------------------------------------------------------------------------------------------------------------------------
// XBE (original Xbox): title ID + name from the certificate (same fields iso2god uses)
//--------------------------------------------------------------------------------------------------------------------------
static god_u32 GetLE32(const god_u8* p) { return p[0] | ((god_u32)p[1] << 8) | ((god_u32)p[2] << 16) | ((god_u32)p[3] << 24); }

bool GodReadXbeExecInfo(const char* XbePath, GodExecInfo& Out, GodWString& TitleName, std::string& Err)
{
	memset(&Out, 0, sizeof(Out));
	TitleName.clear();

	god_u64 fileSize = 0;
	GodFile f = GodOpenRead(XbePath, &fileSize);
	if (!f) { Err = "Could not open default.xbe"; return false; }

	// The image header (incl. the certificate) sits at the start of the file; 64KB covers it for retail XBEs.
	god_u32 want = fileSize < 0x10000 ? (god_u32)fileSize : 0x10000;
	std::vector<god_u8> buf(want ? want : 1);
	god_u32 got = 0;
	while (got < want) { god_u32 r = GodRead(f, &buf[got], want - got); if (!r) break; got += r; }
	GodClose(f);

	if (got < 0x178 || memcmp(&buf[0], "XBEH", 4) != 0) { Err = "default.xbe is not an Xbox executable"; return false; }

	god_u32 base = GetLE32(&buf[0x104]);
	god_u32 cert = GetLE32(&buf[0x118]);
	if (cert < base || cert - base + 0x5C > got) { Err = "default.xbe certificate is out of range"; return false; }
	const god_u8* c = &buf[cert - base];

	Out.TitleId    = GetLE32(c + 0x08);
	Out.MediaId    = 0;
	Out.Version    = 0;
	Out.DiscNumber = 1;
	Out.DiscCount  = 1;

	for (int i = 0; i < 40; i++)            // title name: 40 UTF-16LE chars at cert+0x0C
	{
		god_u16 ch = (god_u16)(c[0x0C + i * 2] | (c[0x0C + i * 2 + 1] << 8));
		if (!ch) break;
		TitleName.push_back(ch);
	}
	while (!TitleName.empty() && TitleName[TitleName.size() - 1] == ' ') TitleName.pop_back();
	return true;
}

//--------------------------------------------------------------------------------------------------------------------------
// GodImage
//--------------------------------------------------------------------------------------------------------------------------
GodImage::GodImage() : m_ImageSize(0), m_FileCount(0), m_FileBytes(0), m_FileTime(0), m_SegIdx(0), m_SegPos(0), m_File(NULL) {}
GodImage::~GodImage() { EndRead(); }

struct SortItem { std::string Name; int Idx; };
static bool SortItemLess(const SortItem& a, const SortItem& b) { return CompareXisoNames(a.Name, b.Name) < 0; }

bool GodImage::ScanDir(int NodeIdx, bool IsRoot, bool SkipSystemUpdate, int Depth, std::string& Err)
{
	if (Depth > 64) { Err = "Folder nesting is too deep"; return false; }

	std::string path = m_Nodes[NodeIdx].Path;
	std::vector<GodDirItem> items;
	if (!GodListDir(path.c_str(), items)) { Err = "Could not read folder: " + path; return false; }

	std::vector<int> kids;
	for (size_t i = 0; i < items.size(); i++)
	{
		const GodDirItem& it = items[i];
		if (it.Name == "." || it.Name == "..") continue;
		if (IsRoot && it.IsDir && SkipSystemUpdate && EqualsNoCase(it.Name, "$SystemUpdate")) continue;
		if (it.Name.empty() || it.Name.size() > 255) { Err = "Bad file name in " + path; return false; }
		if (!it.IsDir && it.Size > 0xFFFFFFFFULL) { Err = "File larger than 4GB: " + it.Name; return false; }

		Node n;
		n.Name   = it.Name;
		n.Path   = JoinPath(path, it.Name);
		n.IsDir  = it.IsDir;
		n.Size   = it.IsDir ? 0 : it.Size;
		n.Sector = 0;
		n.AliasOf = -1;
		n.MemIdx  = -1;
		m_Nodes.push_back(n);               // NOTE: invalidates references into m_Nodes
		kids.push_back((int)m_Nodes.size() - 1);
		if (!it.IsDir) { m_FileCount++; m_FileBytes += it.Size; }
	}

	// boot alias (apps): make e.g. "Aurora.xex" also reachable as "default.xex"
	if (IsRoot && !m_BootAlias.empty() && !EqualsNoCase(m_BootAlias, m_BootName.c_str()))
	{
		int target = -1;
		for (size_t i = 0; i < kids.size(); i++)
			if (!m_Nodes[kids[i]].IsDir && EqualsNoCase(m_Nodes[kids[i]].Name, m_BootAlias.c_str())) target = kids[i];
		if (target < 0) { Err = "Could not find " + m_BootAlias; return false; }

		// an existing file with the boot name is replaced by the alias
		for (size_t i = 0; i < kids.size(); i++)
			if (EqualsNoCase(m_Nodes[kids[i]].Name, m_BootName.c_str()))
			{
				if (m_Nodes[kids[i]].IsDir) { Err = "A folder is named " + m_BootName; return false; }
				m_FileCount--; m_FileBytes -= m_Nodes[kids[i]].Size;
				kids.erase(kids.begin() + i);
				break;
			}

		Node a = m_Nodes[target];
		a.Name = m_BootName;
		a.AliasOf = target;
		m_Nodes.push_back(a);
		kids.push_back((int)m_Nodes.size() - 1);
	}

	// sort children with XDVDFS ordering
	std::vector<SortItem> sorted(kids.size());
	for (size_t i = 0; i < kids.size(); i++) { sorted[i].Name = m_Nodes[kids[i]].Name; sorted[i].Idx = kids[i]; }
	std::sort(sorted.begin(), sorted.end(), SortItemLess);
	for (size_t i = 0; i < kids.size(); i++) kids[i] = sorted[i].Idx;
	for (size_t i = 1; i < kids.size(); i++)
		if (CompareXisoNames(m_Nodes[kids[i - 1]].Name, m_Nodes[kids[i]].Name) == 0)
		{ Err = "Duplicate name (case-insensitive): " + m_Nodes[kids[i]].Name; return false; }

	m_Nodes[NodeIdx].Children = kids;

	for (size_t i = 0; i < kids.size(); i++)
		if (m_Nodes[kids[i]].IsDir)
			if (!ScanDir(kids[i], false, SkipSystemUpdate, Depth + 1, Err)) return false;

	BuildTree(m_Nodes[NodeIdx], 0, (int)m_Nodes[NodeIdx].Children.size());
	ComputeTable(m_Nodes[NodeIdx]);
	return true;
}

int GodImage::BuildTree(Node& d, int Lo, int Hi)
{
	if (Lo >= Hi) return -1;
	int mid = (Lo + Hi) / 2;
	int pos = (int)d.TreeOrder.size();
	d.TreeOrder.push_back(d.Children[mid]);
	d.Left.push_back(-1);
	d.Right.push_back(-1);
	int l = BuildTree(d, Lo, mid);
	int r = BuildTree(d, mid + 1, Hi);
	d.Left[pos] = l;
	d.Right[pos] = r;
	return pos;
}

void GodImage::ComputeTable(Node& d)
{
	d.EntryOffset.clear();
	if (d.TreeOrder.empty()) { d.Size = XISO_SECTOR_SIZE; return; }   // empty dir: one 0xFF sector (extract-xiso)

	god_u32 size = 0;
	for (size_t i = 0; i < d.TreeOrder.size(); i++)
	{
		god_u32 len = 14 + (god_u32)m_Nodes[d.TreeOrder[i]].Name.size();
		len = (len + 3) & ~3u;
		if (SectorsFor(size + len) > SectorsFor(size))           // never let an entry straddle a sector
			size += (XISO_SECTOR_SIZE - (size % XISO_SECTOR_SIZE)) % XISO_SECTOR_SIZE;
		d.EntryOffset.push_back(size);
		size += len;
	}
	d.Size = size;
}

void GodImage::Layout(int NodeIdx, god_u32& Cur)
{
	Node& d = m_Nodes[NodeIdx];
	d.Sector = Cur;
	Cur += SectorsFor(d.Size);
	if (d.TreeOrder.empty()) return;
	for (size_t i = 0; i < d.TreeOrder.size(); i++)
	{
		Node& c = m_Nodes[d.TreeOrder[i]];
		if (!c.IsDir && c.AliasOf < 0) { c.Sector = Cur; Cur += SectorsFor(c.Size); }
	}
	for (size_t i = 0; i < d.TreeOrder.size(); i++)
		if (m_Nodes[d.TreeOrder[i]].IsDir) Layout(d.TreeOrder[i], Cur);
}

void GodImage::BuildTableBytes(int NodeIdx, std::vector<god_u8>& Out)
{
	const Node& d = m_Nodes[NodeIdx];
	Out.assign(SectorsFor(d.Size) * XISO_SECTOR_SIZE, 0xFF);
	for (size_t i = 0; i < d.TreeOrder.size(); i++)
	{
		const Node& c = m_Nodes[d.TreeOrder[i]];
		god_u8* e = &Out[d.EntryOffset[i]];
		god_u16 l = d.Left[i]  < 0 ? 0 : (god_u16)(d.EntryOffset[d.Left[i]] / 4);
		god_u16 r = d.Right[i] < 0 ? 0 : (god_u16)(d.EntryOffset[d.Right[i]] / 4);
		god_u32 size = c.IsDir ? SectorsFor(c.Size) * XISO_SECTOR_SIZE : (god_u32)c.Size;
		PutLE16(e + 0, l);
		PutLE16(e + 2, r);
		PutLE32(e + 4, c.Sector);
		PutLE32(e + 8, size);
		e[12] = c.IsDir ? 0x10 : 0x20;
		e[13] = (god_u8)c.Name.size();
		memcpy(e + 14, c.Name.data(), c.Name.size());
	}
}

void GodImage::AddSeg(SegType T, god_u64 Len, int N)
{
	if (Len == 0) return;
	Segment s; s.Type = T; s.Length = Len; s.Node = N;
	m_Segs.push_back(s);
}

void GodImage::EmitSegments(int NodeIdx)
{
	// directory table (already sector padded with 0xFF)
	m_Tables.push_back(std::vector<god_u8>());
	BuildTableBytes(NodeIdx, m_Tables.back());
	AddSeg(SEG_MEM, m_Tables.back().size(), (int)m_Tables.size() - 1);

	const Node& d = m_Nodes[NodeIdx];
	for (size_t i = 0; i < d.TreeOrder.size(); i++)
	{
		const Node& c = m_Nodes[d.TreeOrder[i]];
		if (c.IsDir || c.AliasOf >= 0) continue;
		AddSeg(SEG_FILE, c.Size, d.TreeOrder[i]);
		AddSeg(SEG_FF, (XISO_SECTOR_SIZE - (c.Size % XISO_SECTOR_SIZE)) % XISO_SECTOR_SIZE, -1);
	}
	for (size_t i = 0; i < d.TreeOrder.size(); i++)
		if (m_Nodes[d.TreeOrder[i]].IsDir) EmitSegments(d.TreeOrder[i]);
}

bool GodImage::Build(const char* Folder, bool SkipSystemUpdate, god_u64 FileTime, std::string& Err,
                     const char* BootAlias, const char* BootName)
{
	EndRead();
	m_BootAlias = BootAlias ? BootAlias : "";
	m_BootName  = BootName ? BootName : "";
	m_Nodes.clear(); m_Tables.clear(); m_Segs.clear();
	m_FileCount = 0; m_FileBytes = 0; m_ImageSize = 0; m_FileTime = FileTime;

	std::string root = Folder;
	while (root.size() > 1 && root[root.size() - 1] == '\\' && root[root.size() - 2] != ':') root.erase(root.size() - 1);

	Node r; r.Name = ""; r.Path = root; r.IsDir = true; r.Size = 0; r.Sector = 0; r.AliasOf = -1; r.MemIdx = -1;
	m_Nodes.push_back(r);
	if (!ScanDir(0, true, SkipSystemUpdate, 0, Err)) return false;
	if (m_Nodes[0].TreeOrder.empty()) { Err = "The game folder is empty"; return false; }
	return Finish(Err);
}

bool GodImage::BuildFromMemory(const std::vector<MemFile>& Files, god_u64 FileTime, std::string& Err)
{
	EndRead();
	m_Nodes.clear(); m_Tables.clear(); m_Segs.clear();
	m_FileCount = 0; m_FileBytes = 0; m_ImageSize = 0; m_FileTime = FileTime;
	m_BootAlias = ""; m_BootName = "";
	m_MemFiles = Files;

	Node r; r.Name = ""; r.Path = ""; r.IsDir = true; r.Size = 0; r.Sector = 0; r.AliasOf = -1; r.MemIdx = -1;
	m_Nodes.push_back(r);

	std::vector<SortItem> sorted;
	for (size_t i = 0; i < m_MemFiles.size(); i++)
	{
		Node n; n.Name = m_MemFiles[i].Name; n.Path = m_MemFiles[i].Name; n.IsDir = false;
		n.Size = m_MemFiles[i].Data.size(); n.Sector = 0; n.AliasOf = -1; n.MemIdx = (int)i;
		m_Nodes.push_back(n);
		m_FileCount++; m_FileBytes += n.Size;
		SortItem it; it.Name = n.Name; it.Idx = (int)m_Nodes.size() - 1; sorted.push_back(it);
	}
	std::sort(sorted.begin(), sorted.end(), SortItemLess);
	for (size_t i = 0; i < sorted.size(); i++) m_Nodes[0].Children.push_back(sorted[i].Idx);
	BuildTree(m_Nodes[0], 0, (int)m_Nodes[0].Children.size());
	ComputeTable(m_Nodes[0]);
	if (m_Nodes[0].TreeOrder.empty()) { Err = "Nothing to put in the package"; return false; }
	return Finish(Err);
}

bool GodImage::Finish(std::string& Err)
{
	god_u32 cur = 0x108;                          // XISO_ROOT_DIRECTORY_SECTOR
	Layout(0, cur);
	for (size_t i = 0; i < m_Nodes.size(); i++)   // aliases share their target's sectors
		if (m_Nodes[i].AliasOf >= 0) m_Nodes[i].Sector = m_Nodes[m_Nodes[i].AliasOf].Sector;

	// volume descriptor (sector 0x20)
	m_VolDesc.assign(XISO_SECTOR_SIZE, 0);
	memcpy(&m_VolDesc[0], "MICROSOFT*XBOX*MEDIA", 20);
	PutLE32(&m_VolDesc[20], m_Nodes[0].Sector);
	PutLE32(&m_VolDesc[24], (god_u32)m_Nodes[0].Size);
	PutLE64(&m_VolDesc[28], m_FileTime);
	memcpy(&m_VolDesc[0x7EC], "MICROSOFT*XBOX*MEDIA", 20);

	AddSeg(SEG_ZERO, 0x20 * XISO_SECTOR_SIZE, -1);
	AddSeg(SEG_MEM, XISO_SECTOR_SIZE, -1);
	AddSeg(SEG_ZERO, (god_u64)(0x108 - 0x21) * XISO_SECTOR_SIZE, -1);
	EmitSegments(0);

	god_u64 end = (god_u64)cur * XISO_SECTOR_SIZE;
	god_u64 padded = DivCeil(end, 0x10000) * 0x10000;   // XISO_FILE_MODULUS
	AddSeg(SEG_ZERO, padded - end, -1);
	m_ImageSize = padded;

	// sanity: segments must add up
	god_u64 total = 0;
	for (size_t i = 0; i < m_Segs.size(); i++) total += m_Segs[i].Length;
	if (total != m_ImageSize) { Err = "Internal layout error"; return false; }
	return true;
}

bool GodImage::BeginRead(std::string& Err)
{
	EndRead();
	(void)Err;
	m_SegIdx = 0; m_SegPos = 0;
	return true;
}

void GodImage::EndRead()
{
	if (m_File) { GodClose(m_File); m_File = NULL; }
}

bool GodImage::Read(god_u8* Buf, god_u32 Size, std::string& Err)
{
	while (Size)
	{
		if (m_SegIdx >= m_Segs.size()) { Err = "Read past end of image"; return false; }
		const Segment& s = m_Segs[m_SegIdx];
		god_u64 left = s.Length - m_SegPos;
		god_u32 n = (god_u32)(left < Size ? left : Size);

		switch (s.Type)
		{
		case SEG_ZERO: memset(Buf, 0x00, n); break;
		case SEG_FF:   memset(Buf, 0xFF, n); break;
		case SEG_MEM:
			{
				const std::vector<god_u8>& src = s.Node < 0 ? m_VolDesc : m_Tables[s.Node];
				memcpy(Buf, &src[(size_t)m_SegPos], n);
			}
			break;
		case SEG_FILE:
			if (m_Nodes[s.Node].MemIdx >= 0)
			{
				memcpy(Buf, &m_MemFiles[m_Nodes[s.Node].MemIdx].Data[(size_t)m_SegPos], n);
			}
			else
			{
				if (!m_File)
				{
					god_u64 sz = 0;
					m_File = GodOpenRead(m_Nodes[s.Node].Path.c_str(), &sz);
					if (!m_File) { Err = "Could not open " + m_Nodes[s.Node].Path; return false; }
					if (sz != s.Length) { Err = "File changed size: " + m_Nodes[s.Node].Path; return false; }
				}
				god_u32 got = 0;
				while (got < n)
				{
					god_u32 r = GodRead(m_File, Buf + got, n - got);
					if (r == 0) { Err = "Read error: " + m_Nodes[s.Node].Path; return false; }
					got += r;
				}
			}
			break;
		}

		Buf += n; Size -= n; m_SegPos += n;
		if (m_SegPos == s.Length)
		{
			if (s.Type == SEG_FILE && m_File) { GodClose(m_File); m_File = NULL; }
			m_SegIdx++; m_SegPos = 0;
		}
	}
	return true;
}

//--------------------------------------------------------------------------------------------------------------------------
// LIVE header (field offsets/values as produced by iso2god-rs, see con_header.rs)
//--------------------------------------------------------------------------------------------------------------------------
static void PutTitleUtf16BE(god_u8* p, const GodWString& Title)
{
	size_t n = Title.size(); if (n > 63) n = 63;     // 0x80 bytes per string incl. terminator
	for (size_t i = 0; i < n; i++) PutBE16(p + i * 2, Title[i]);
	PutBE16(p + n * 2, 0);
}

void GodBuildHeader(god_u8* H, god_u32 ContentType, const GodExecInfo& Exec, const GodWString& Title, god_u64 BlockCount,
                    god_u32 PartCount, god_u64 PartsTotalSize, const god_u8 TopHash[20])
{
	memset(H, 0, GOD_HEADER_SIZE);
	memcpy(H, "LIVE", 4);                      // unsigned - fine on RGH/JTAG
	memset(H + 0x22C, 0xFF, 8);               // license entry 0: everyone
	PutBE32(H + 0x340, 0xAD0E);               // header size
	PutBE32(H + 0x344, ContentType);
	PutBE32(H + 0x348, 2);                    // metadata version
	PutBE32(H + 0x354, Exec.MediaId);
	// 0x358 version / 0x35C base version stay 0 (iso2god does the same)
	PutBE32(H + 0x360, Exec.TitleId);
	H[0x364] = Exec.Platform;
	H[0x365] = Exec.ExecutableType;
	H[0x366] = Exec.DiscNumber;
	H[0x367] = Exec.DiscCount;

	// SVOD volume descriptor
	H[0x379] = 0x24;                          // descriptor length
	H[0x37A] = 0x05;                          // block cache element count
	H[0x37B] = 0x05;                          // worker thread processor
	H[0x37C] = 0x11;                          // worker thread priority
	memcpy(H + 0x37D, TopHash, 20);           // hash of the first part's master hash block
	H[0x391] = 0x00;                          // device features
	PutBE24(H + 0x392, (god_u32)BlockCount);  // data block count
	PutBE16(H + 0x395, 0);

	PutBE32(H + 0x39D, PartCount);            // data file count
	PutBE64(H + 0x3A1, PartsTotalSize);       // data file combined size
	PutBE32(H + 0x3A9, 1);                    // descriptor type: SVOD

	PutTitleUtf16BE(H + 0x411, Title);        // display name (locale 0)
	PutTitleUtf16BE(H + 0x1691, Title);       // title name
	PutBE32(H + 0x1712, 0x3841);              // thumbnail sizes: same values iso2god ships (no image data ->
	PutBE32(H + 0x1716, 0x3841);              // the dashboard falls back to its default tile)

	god_u8 digest[20];
	GodSha1(H + 0x344, GOD_HEADER_SIZE - 0x344, digest);
	memcpy(H + 0x32C, digest, 20);
}

//--------------------------------------------------------------------------------------------------------------------------
// Plan + convert
//--------------------------------------------------------------------------------------------------------------------------
static bool MakeDirs(const std::string& Path)
{
	// create every component after the device ("Hdd:\")
	size_t start = Path.find(":\\");
	start = (start == std::string::npos) ? 0 : start + 2;
	if (start == 0 && !Path.empty() && Path[0] == '\\') start = 1;
	for (size_t i = start; i <= Path.size(); i++)
	{
		if (i == Path.size() || Path[i] == '\\')
		{
			std::string part = Path.substr(0, i);
			if (part.empty() || part[part.size() - 1] == ':') continue;
			if (!GodMakeDir(part.c_str())) return false;
		}
	}
	return true;
}

static god_u64 PartFileBytes(god_u64 BlocksInPart)
{
	god_u64 subparts = DivCeil(BlocksInPart, GOD_BLOCKS_PER_SUBPART);
	return (god_u64)GOD_BLOCK_SIZE * (1 + subparts + BlocksInPart);
}

static god_u32 Fnv1a(const std::string& s, god_u32 h = 0x811C9DC5u)
{
	for (size_t i = 0; i < s.size(); i++) { h ^= (god_u8)UpperAscii(s[i]); h *= 0x01000193u; }
	return h;
}

static std::string LowerExt(const std::string& Name)
{
	size_t dot = Name.find_last_of('.');
	std::string e = dot == std::string::npos ? "" : Name.substr(dot);
	for (size_t i = 0; i < e.size(); i++) if (e[i] >= 'A' && e[i] <= 'Z') e[i] = (char)(e[i] + 32);
	return e;
}

bool GodPreparePlan(const char* ExePath, const char* ContentRoot, bool SkipSystemUpdate, god_u64 FileTime,
                    GodPlan& P, std::string& Err)
{
	P.ExePath = ExePath;
	size_t cut = P.ExePath.find_last_of('\\');
	if (cut == std::string::npos) { Err = "Bad path"; return false; }
	P.SourceFolder = P.ExePath.substr(0, cut);
	std::string exeName = P.ExePath.substr(cut + 1);
	std::string ext = LowerExt(exeName);
	P.ContentRoot  = ContentRoot;
	P.FileTime     = FileTime;
	P.AlreadyExists = false;
	P.ShortcutOnly = false;
	P.SyntheticIds = false;
	P.Category     = GOD_CAT_GAME;

	const char* bootAlias = NULL;
	const char* bootName  = NULL;
	GodWString certName;

	if (ext == ".xbe")
	{
		if (!EqualsNoCase(exeName, "default.xbe")) { Err = "Original Xbox games must boot from default.xbe"; return false; }
		if (!GodReadXbeExecInfo(P.ExePath.c_str(), P.Exec, certName, Err)) return false;
		P.Kind = GOD_KIND_XBOX1;
		P.ContentType = GOD_CONTENT_TYPE_XBOX1;
	}
	else if (ext == ".xex")
	{
		bool missing = false;
		if (!GodReadXexExecInfo(P.ExePath.c_str(), P.Exec, Err, &missing)) return false;
		if (missing)
		{
			// e.g. FreestyleDash: no execution info. Make up stable IDs from the folder + file name.
			god_u32 h = Fnv1a(P.SourceFolder.substr(P.SourceFolder.find_last_of('\\') + 1), Fnv1a(exeName));
			P.Exec.TitleId = 0xFFFE0000u | (h & 0xFFFF);
			P.Exec.MediaId = h;
			P.SyntheticIds = true;
		}
		// Some homebrew says "disc 0 of 0" (DashLaunch) - the dashboard expects at least 1 of 1.
		if (P.Exec.DiscCount == 0) P.Exec.DiscCount = 1;
		if (P.Exec.DiscNumber == 0 || P.Exec.DiscNumber > P.Exec.DiscCount) P.Exec.DiscNumber = 1;
		if (EqualsNoCase(exeName, "default.xex")) P.Kind = GOD_KIND_360_GAME;
		else { P.Kind = GOD_KIND_360_APP; bootAlias = exeName.c_str(); bootName = "default.xex"; }
	}
	else { Err = "Pick a .xex or default.xbe file"; return false; }

	// Executables not named default.xex (GODify.xex, Aurora.xex...) often sit in a folder full of unrelated files,
	// and nothing tells which of them belong to it - so these only get a shortcut, and the folder isn't scanned.
	P.ShortcutOnly = (P.Kind == GOD_KIND_360_APP);

	// Safety: refuse a folder that holds other game folders (e.g. a whole "Games" library with a stray default.xex).
	if (!P.ShortcutOnly)
	{
		std::vector<GodDirItem> items;
		GodListDir(P.SourceFolder.c_str(), items);
		std::string found; int count = 0;
		for (size_t i = 0; i < items.size(); i++)
		{
			if (!items[i].IsDir) continue;
			std::string sub = JoinPath(P.SourceFolder, items[i].Name);
			if (!GodPathExists(JoinPath(sub, "default.xex").c_str()) && !GodPathExists(JoinPath(sub, "default.xbe").c_str())) continue;
			if (count < 3) { if (count) found += ", "; found += items[i].Name; }
			count++;
		}
		if (count)
		{
			Err = "This folder contains other game folders (" + found + (count > 3 ? ", ..." : "") +
			      "). Open one of them and pick its executable instead.";
			return false;
		}
	}

	// Title: name list -> XBE certificate -> app file name / folder name
	const char* known = GodLookupTitleName(P.Exec.TitleId);
	if (known) P.Title = GodUtf8ToUtf16(known);
	else if (!certName.empty()) P.Title = certName;
	else if (P.Kind == GOD_KIND_360_APP) P.Title = GodUtf8ToUtf16(exeName.substr(0, exeName.size() - 4).c_str());
	else P.Title = GodUtf8ToUtf16(LastComponent(P.SourceFolder).c_str());

	if (P.ShortcutOnly) return GodSetCategory(P, GOD_CAT_APP, Err);

	if (!P.Image.Build(P.SourceFolder.c_str(), SkipSystemUpdate, FileTime, Err, bootAlias, bootName)) return false;

	if (!GodSetCategory(P, GOD_CAT_GAME, Err)) return false;
	return true;
}

// "Freestyle Dash 3" -> "Freestyle_Dash_3" (FATX-safe, max 24 chars)
static std::string CleanFileName(const std::string& In)
{
	std::string out;
	for (size_t i = 0; i < In.size() && out.size() < 24; i++)
	{
		char c = In[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') out += c;
		else if (!out.empty() && out[out.size() - 1] != '_') out += '_';
	}
	while (!out.empty() && out[out.size() - 1] == '_') out.erase(out.size() - 1);
	return out.empty() ? "App" : out;
}

static void ComputeSizes(GodPlan& P)
{
	GodImage& img = P.ActiveImage();
	P.BlockCount = DivCeil(img.ImageSize(), GOD_BLOCK_SIZE);
	P.PartCount  = (god_u32)DivCeil(P.BlockCount, GOD_BLOCKS_PER_PART);
	god_u64 lastBlocks = P.BlockCount - (god_u64)(P.PartCount - 1) * GOD_BLOCKS_PER_PART;
	P.OutputBytes = GOD_HEADER_SIZE + (god_u64)(P.PartCount - 1) * GOD_FULL_PART_BLOCKS * GOD_BLOCK_SIZE + PartFileBytes(lastBlocks);
}

bool GodSetCategory(GodPlan& P, int Category, std::string& Err)
{
	if (P.Kind == GOD_KIND_XBOX1) Category = GOD_CAT_GAME;
	if (P.ShortcutOnly) Category = GOD_CAT_APP;

	char buf[32];
	std::string fileName;
	if (Category == GOD_CAT_APP)
	{
		if (!GodHaveShortcutLauncher()) { Err = "The shortcut launcher is missing from this build of GODify"; return false; }

		// Shortcut package: the launcher as default.xex + launch.txt with the program's full path.
		std::vector<GodImage::MemFile> files(2);
		files[0].Name = "default.xex";
		files[0].Data = g_Launcher;
		files[1].Name = GOD_SHORTCUT_LAUNCH_FILE;
		files[1].Data.assign(P.ExePath.begin(), P.ExePath.end());
		if (!P.ShortcutImage.BuildFromMemory(files, P.FileTime, Err)) return false;

		// Saved under the app's own title ID (so the dashboard shows its own name), with a readable file name
		// plus a stable tag, e.g. "DashLaunch_A2A251DB" (FATX allows 42 chars; ".data" needs 5 of them).
		std::string exeName = LastComponent(P.ExePath);
		std::string folder  = LastComponent(P.SourceFolder);
		sprintf(buf, "%08X%08X", P.Exec.TitleId, P.Exec.MediaId);
		god_u32 tag = Fnv1a(folder + "\\" + exeName, Fnv1a(buf));
		std::string base = EqualsNoCase(exeName, "default.xex") ? folder : exeName.substr(0, exeName.size() - 4);
		sprintf(buf, "_%08X", tag);
		fileName = CleanFileName(base) + buf;
		P.ContentType = GOD_CONTENT_TYPE;
	}
	else if (P.Kind == GOD_KIND_XBOX1)
	{
		P.ContentType = GOD_CONTENT_TYPE_XBOX1;
		sprintf(buf, "%08X", P.Exec.TitleId);                    // iso2god naming
		fileName = buf;
	}
	else
	{
		P.ContentType = GOD_CONTENT_TYPE;
		sprintf(buf, "%08X", P.Exec.MediaId);                    // iso2god naming
		fileName = buf;
	}

	P.Category   = Category;
	P.HeaderExec = P.Exec;
#if GOD_SHORTCUT_TITLE_ID
	if (Category == GOD_CAT_APP) P.HeaderExec.TitleId = GOD_SHORTCUT_TITLE_ID;
#endif
	ComputeSizes(P);

	char tid[16], ctype[16];
	sprintf(tid, "%08X", P.HeaderExec.TitleId);
	sprintf(ctype, "%08X", P.ContentType);
	P.TitleDir   = JoinPath(JoinPath(P.ContentRoot, tid), ctype);
	P.HeaderPath = JoinPath(P.TitleDir, fileName);
	P.DataDir    = P.HeaderPath + ".data";
	P.AlreadyExists = GodPathExists(P.HeaderPath.c_str()) || GodPathExists(P.DataDir.c_str());
	return true;
}

static void DeleteDirIfEmpty(const std::string& Dir)
{
	std::vector<GodDirItem> items;
	if (GodListDir(Dir.c_str(), items) && items.empty()) GodDeleteTree(Dir.c_str());
}

static void Cleanup(GodPlan& P)
{
	GodDeleteTree(P.DataDir.c_str());
	GodDeleteFile(P.HeaderPath.c_str());
	DeleteDirIfEmpty(P.TitleDir);                                   // ...\TTTTTTTT\00007000
	size_t cut = P.TitleDir.find_last_of('\\');
	if (cut != std::string::npos) DeleteDirIfEmpty(P.TitleDir.substr(0, cut));   // ...\TTTTTTTT
}

GodResult GodConvert(GodPlan& P, GodProgress* Prog, std::string& Err)
{
	GodProgress dummy; memset(&dummy, 0, sizeof(dummy));
	if (!Prog) Prog = &dummy;
	Prog->Phase = GOD_PHASE_WRITING;
	Prog->BytesDone = 0;
	Prog->BytesTotal = P.ActiveImage().ImageSize();
	Prog->PartCount = (int)P.PartCount;
	Prog->CurrentPart = 0;

	// replace an older conversion of the same disc
	if (GodPathExists(P.DataDir.c_str()) && !GodDeleteTree(P.DataDir.c_str())) { Err = "Could not remove old " + P.DataDir; return GOD_FAILED; }
	if (GodPathExists(P.HeaderPath.c_str()) && !GodDeleteFile(P.HeaderPath.c_str())) { Err = "Could not remove old " + P.HeaderPath; return GOD_FAILED; }

	god_u64 freeBytes = 0;
	if (GodGetFreeSpace(P.ContentRoot.c_str(), &freeBytes) && freeBytes < P.OutputBytes + 0x100000)
	{
		char msg[160];
		sprintf(msg, "Not enough free space (need %u MB, have %u MB)",
			(unsigned)(P.OutputBytes >> 20), (unsigned)(freeBytes >> 20));
		Err = msg;
		return GOD_FAILED;
	}

	if (!MakeDirs(P.DataDir)) { Err = "Could not create " + P.DataDir; return GOD_FAILED; }
	GodImage& img = P.ActiveImage();
	if (!img.BeginRead(Err)) return GOD_FAILED;

	const god_u32 bufSize = GOD_BLOCK_SIZE * (1 + GOD_BLOCKS_PER_SUBPART);
	god_u8* buf = (god_u8*)malloc(bufSize);
	std::vector<god_u8> masters((size_t)P.PartCount * GOD_BLOCK_SIZE, 0);
	god_u64 lastPartSize = 0;
	GodResult result = GOD_OK;

	if (!buf) { Err = "Out of memory"; img.EndRead(); Cleanup(P); return GOD_FAILED; }

	god_u64 blockIdx = 0;
	for (god_u32 part = 0; part < P.PartCount && result == GOD_OK; part++)
	{
		Prog->CurrentPart = (int)part + 1;
		char name[16]; sprintf(name, "Data%04u", part);
		std::string partPath = JoinPath(P.DataDir, name);
		GodFile out = GodCreateWrite(partPath.c_str());
		if (!out) { Err = "Could not create " + partPath; result = GOD_FAILED; break; }

		god_u8* master = &masters[(size_t)part * GOD_BLOCK_SIZE];
		memset(buf, 0, GOD_BLOCK_SIZE);
		if (!GodWrite(out, buf, GOD_BLOCK_SIZE)) { Err = "Write failed (disk full?)"; result = GOD_FAILED; GodClose(out); break; }
		god_u64 partSize = GOD_BLOCK_SIZE;

		for (god_u32 sub = 0; sub < GOD_SUBPARTS_PER_PART && blockIdx < P.BlockCount; sub++)
		{
			if (Prog->Cancel) { result = GOD_CANCELLED; break; }

			god_u64 left = P.BlockCount - blockIdx;
			god_u32 nb = (god_u32)(left < GOD_BLOCKS_PER_SUBPART ? left : GOD_BLOCKS_PER_SUBPART);
			god_u8* data = buf + GOD_BLOCK_SIZE;
			if (!img.Read(data, nb * GOD_BLOCK_SIZE, Err)) { result = GOD_FAILED; break; }

			memset(buf, 0, GOD_BLOCK_SIZE);                     // sub hash block
			for (god_u32 b = 0; b < nb; b++) GodSha1(data + b * GOD_BLOCK_SIZE, GOD_BLOCK_SIZE, buf + b * 20);
			GodSha1(buf, GOD_BLOCK_SIZE, master + sub * 20);

			if (!GodWrite(out, buf, GOD_BLOCK_SIZE * (1 + nb))) { Err = "Write failed (disk full?)"; result = GOD_FAILED; break; }
			partSize += (god_u64)GOD_BLOCK_SIZE * (1 + nb);
			blockIdx += nb;
			Prog->BytesDone = blockIdx * GOD_BLOCK_SIZE;
		}
		GodClose(out);
		lastPartSize = partSize;
	}

	free(buf);
	img.EndRead();

	if (result == GOD_OK)
	{
		Prog->Phase = GOD_PHASE_FINALIZING;

		// chain: each master hash block carries the hash of the next part's master hash block
		for (int i = (int)P.PartCount - 2; i >= 0; i--)
			GodSha1(&masters[(size_t)(i + 1) * GOD_BLOCK_SIZE], GOD_BLOCK_SIZE, &masters[(size_t)i * GOD_BLOCK_SIZE + GOD_SUBPARTS_PER_PART * 20]);

		for (god_u32 part = 0; part < P.PartCount && result == GOD_OK; part++)
		{
			char name[16]; sprintf(name, "Data%04u", part);
			std::string partPath = JoinPath(P.DataDir, name);
			GodFile f = GodOpenWriteExisting(partPath.c_str());
			if (!f || !GodWrite(f, &masters[(size_t)part * GOD_BLOCK_SIZE], GOD_BLOCK_SIZE)) { Err = "Could not finalize " + partPath; result = GOD_FAILED; }
			if (f) GodClose(f);
		}
	}

	if (result == GOD_OK)
	{
		god_u8 top[20];
		GodSha1(&masters[0], GOD_BLOCK_SIZE, top);
		god_u64 total = lastPartSize + (god_u64)(P.PartCount - 1) * GOD_BLOCK_SIZE * GOD_FULL_PART_BLOCKS;

		std::vector<god_u8> hdr(GOD_HEADER_SIZE);
		GodBuildHeader(&hdr[0], P.ContentType, P.HeaderExec, P.Title, P.BlockCount, P.PartCount, total, top);
		GodFile h = GodCreateWrite(P.HeaderPath.c_str());
		if (!h || !GodWrite(h, &hdr[0], GOD_HEADER_SIZE)) { Err = "Could not write " + P.HeaderPath; result = GOD_FAILED; }
		if (h) GodClose(h);
	}

	if (result != GOD_OK) Cleanup(P);
	Prog->Phase = GOD_PHASE_DONE;
	return result;
}
