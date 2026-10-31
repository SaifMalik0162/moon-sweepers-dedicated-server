#include "proxy_exports.h"
.text
.extern g_version_procs
#define TRAMPOLINE(name,index) .globl proxy_##name; .def proxy_##name; .scl 2; .type 32; .endef; proxy_##name: jmp *g_version_procs+8*index(%rip);
VERSION_EXPORTS(TRAMPOLINE)
