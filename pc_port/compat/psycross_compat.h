#ifndef MUSASHI_PSYCROSS_COMPAT_H
#define MUSASHI_PSYCROSS_COMPAT_H

/* Force-included into every PsyCross translation unit; never included by
 * decomp code.
 *
 * PsyCross puts include/psx on the search path so Psy-Q sources see Psy-Q
 * headers. That directory carries its own strings.h, a compatibility stub that
 * only pulls in string.h and ctype.h. Because -I directories are searched
 * before the system directories, `#include <strings.h>` anywhere in PsyCross
 * resolves to that stub and never to glibc's, so strcasecmp is never declared
 * on Linux even though upstream includes the header. Upstream builds with
 * MSVC, where _stricmp is native and the stub is never consulted.
 *
 * Declaring the prototype here, with C linkage and glibc's exact signature,
 * sidesteps the shadow without removing the stub Psy-Q code depends on and
 * without hard-coding a system header path. The definition comes from libc.
 */

#ifdef __cplusplus
extern "C" {
#endif

int strcasecmp(const char *s1, const char *s2);

#ifdef __cplusplus
}
#endif

#endif
