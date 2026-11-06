/* Case-insensitive string compares. POSIX <strings.h> on Linux; the CRT's names on Windows. */
#ifndef COMPAT_H
#define COMPAT_H

#ifdef _WIN32
#include <string.h>
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif
#ifndef strtok_r
#define strtok_r strtok_s
#endif
#else
#include <strings.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#endif
