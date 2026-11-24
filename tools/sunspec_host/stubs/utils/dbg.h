#pragma once
#include <cstdio>
#define DBG_INFO 0
#define DBG_WARN 1
#define DPRINT(l, x) printf("%s", (const char*)(x))
#define DBGPRINTLN(x) printf("%s\n", (x).c_str())
#define DPRINTLN(l, x) printf("%s\n", (const char*)(x))
