#include "../GODify/God/GodCore.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
static GodProgress prog;
static void* killer(void*) { while (prog.BytesDone < 50ull*1024*1024) usleep(1000); prog.Cancel = true; return 0; }
int main(int, char** argv) {
	std::string err; GodPlan* p = new GodPlan;
	GodPlan* bad = new GodPlan;
	printf("no-xex plan: %d '%s'\n", GodPreparePlan("\\tmp\\t\\game\\many\\default.xex", argv[2], true, 0, *bad, err), err.c_str());
	err.clear();
	for (char* c = argv[1]; *c; c++) if (*c == '/') *c = '\\';
	if (!GodPreparePlan(argv[1], argv[2], true, 0, *p, err)) return 1;
	memset(&prog, 0, sizeof(prog)); pthread_t t; pthread_create(&t, 0, killer, 0);
	GodResult r = GodConvert(*p, &prog, err); pthread_join(t, 0);
	printf("cancel result=%d done=%llu MB headerExists=%d dataExists=%d\n", r, prog.BytesDone >> 20,
		GodPathExists(p->HeaderPath.c_str()), GodPathExists(p->DataDir.c_str()));
}
