 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Definitions for accessing cycle counters on a given machine, if possible.
  *
  * Copyright 1997, 1998 Bernd Schmidt
  * Copyright 2003-2005 Richard Drummond
  */

#ifndef EUAE_MACHDEP_RPT_H
#define EUAE_MACHDEP_RPT_H

#ifndef frame_time_t
#define frame_time_t uae_s64
#endif

extern long retro_ticks(void);

/* read_processor_time() and frame_time_t now come from uae/time.h
 * (upstream moved them there). Keep the libretro tick source as the
 * implementation of uae_time(), which is what uae/time.h calls. */
STATIC_INLINE frame_time_t machdep_gethrtime (void)
{
   return (frame_time_t)retro_ticks();
}

frame_time_t machdep_gethrtimebase (void);
int          machdep_inithrtimer   (void);

#endif /* EUAE_MACHDEP_RPT_H */
