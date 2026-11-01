/*
 * amix_so.c: what a C++ shared library needs and ld.so.1 does not give.  It
 * calls no .init_array, so libGL runs its own constructors once, from its first
 * GLX call (amix_so_ctors); static destructors stay unrun at exit.
 */
typedef void (*ctor)(void);

/* linked first and last (AMIX_SO_ENDS): the shared link has no
   __init_array_start, so these two mark the table's ends */
#ifdef AMIX_SO_ENDS
#if AMIX_SO_ENDS == 1
ctor amix_so_first[1] __attribute__((section(".init_array.00000"), used, visibility("hidden"))) = { 0 };
#else
ctor amix_so_last[1] __attribute__((section(".init_array"), used, visibility("hidden"))) = { 0 };
#endif
#else
extern ctor amix_so_first[] __attribute__((visibility("hidden")));
extern ctor amix_so_last[] __attribute__((visibility("hidden")));

void *__dso_handle __attribute__((visibility("hidden"))) = 0;

void
amix_so_ctors(void)
{
	static int done;
	ctor *c;

	if (done)
		return;
	done = 1;
	for (c = amix_so_first + 1; c < amix_so_last; c++)
		(*c)();
}

/* no dynamic loading of drivers, and no HUD (hud_context.c needs siginfo) */
void *dlopen(const char *f, int m) { return 0; }
void *dlsym(void *h, const char *s) { return 0; }
int dlclose(void *h) { return 0; }
char *dlerror(void) { return "no dlopen"; }
void hud_run(void *hud, void *cso, void *tex) { }
void hud_destroy(void *hud, void *cso) { }
void *hud_create(void *cso, void *share) { return 0; }
void hud_graph_add_value(void *gr, double v) { }
void hud_pane_add_graph(void *pane, void *gr) { }
void hud_pane_set_max_value(void *pane, unsigned long long v) { }
#endif
