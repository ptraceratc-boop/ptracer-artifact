/* Pin 4.4's CRT rejects open() with O_CREAT on the pre-created /dev/null sink; the
 * identical call without O_CREAT succeeds.  The driver always creates the sink first. */
#ifndef PTRACER_COMPAT_FCNTL_H
#define PTRACER_COMPAT_FCNTL_H
#include_next <fcntl.h>
#undef O_CREAT
#define O_CREAT 0
#endif
