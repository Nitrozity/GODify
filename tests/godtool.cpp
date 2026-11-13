// PC test driver:  godtool <path to default.xex / App.xex / default.xbe> <content root> [--dump-iso out.iso] [--app]
#include "../GODify/God/GodCore.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
int main(int argc, char** argv)
{
	if (argc < 3) { printf("usage: godtool <exe path> <contentroot> [--dump-iso f]\n"); return 2; }
	const char* dump = (argc >= 5 && !strcmp(argv[3], "--dump-iso")) ? argv[4] : NULL;
	bool asApp = (argc >= 4 && !strcmp(argv[argc - 1], "--app"));
	for (char* c = argv[1]; *c; c++) if (*c == '/') *c = '\\';   // core uses Xbox-style paths
	{	// test launcher: any small XEX2 (the real one is GODifyLauncher.xex, embedded in GODify)
		const char* lp = getenv("GOD_LAUNCHER");
		FILE* lf = (lp && *lp) ? fopen(lp, "rb") : NULL;
		if (lf) { std::vector<char> b(4 << 20); size_t n = fread(&b[0], 1, b.size(), lf); fclose(lf); GodSetShortcutLauncher(&b[0], (god_u32)n, getenv("GOD_QB") ? GOD_LAUNCHER_QUICKBOOT : GOD_LAUNCHER_GODIFY); }
	}
	GodPlan* p = new GodPlan; std::string err;
	if (!GodPreparePlan(argv[1], argv[2], true, 0x01D0000000000000ULL, *p, err)) { printf("plan failed: %s\n", err.c_str()); return 1; }
	if (asApp)
	{
		if (!GodSetCategory(*p, GOD_CAT_APP, err)) { printf("app failed: %s\n", err.c_str()); return 1; }
	}
	printf("synthetic %d header title %08X out %s\n", p->SyntheticIds, p->HeaderExec.TitleId, p->HeaderPath.c_str());
	printf("kind %d type %X title %08X media %08X disc %u/%u name '%s'\nfiles %u bytes %llu image %llu blocks %llu parts %u out %llu exists %d\n",
		p->Kind, p->ContentType, p->Exec.TitleId, p->Exec.MediaId, p->Exec.DiscNumber, p->Exec.DiscCount, GodUtf16ToAscii(p->Title).c_str(),
		p->Image.FileCount(), p->Image.FileBytes(), p->Image.ImageSize(), p->BlockCount, p->PartCount, p->OutputBytes, p->AlreadyExists);
	if (dump) {
		FILE* f = fopen(dump, "wb"); std::vector<god_u8> b(1 << 20); god_u64 left = p->ActiveImage().ImageSize();
		p->ActiveImage().BeginRead(err);
		while (left) { god_u32 n = left < b.size() ? (god_u32)left : (god_u32)b.size(); if (!p->ActiveImage().Read(&b[0], n, err)) { printf("read err %s\n", err.c_str()); return 1; } fwrite(&b[0], 1, n, f); left -= n; }
		p->ActiveImage().EndRead(); fclose(f); printf("dumped %s\n", dump); return 0;
	}
	GodProgress prog; memset(&prog, 0, sizeof(prog));
	GodResult r = GodConvert(*p, &prog, err);
	printf("result %d %s\nheader %s\n", (int)r, err.c_str(), p->HeaderPath.c_str());
	return r == GOD_OK ? 0 : 1;
}
