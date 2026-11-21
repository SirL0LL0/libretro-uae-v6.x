/*
 * PUAE libretro - 86Box platform globals and functions used by the graphics
 * board chips (gfxboard.c, 86box/video).
 *
 * WinUAE defines these in 86box/winuae/86box_glue.cpp, 86box/pit.cpp,
 * 86box/cpu/cpu.cpp and 86box/winuae/86box_machine.cpp, together with the
 * x86 bridgeboard emulation, which the core does not include. Without a
 * bridgeboard WinUAE never sets the PIT/CPU clocks, so they stay zero here
 * too: the boards' timers run from the Amiga side (86box_video.c, through
 * 86box_video_redirect.h).
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#ifndef _WIN32
#include <sys/mman.h>
#endif

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/nvr.h>
#include "cpu.h"

extern void write_log(const char *format, ...);

/* pit.cpp */
double cpuclock;
double isa_timing;
uint64_t VGACONST1;
uint64_t VGACONST2;

/* cpu/cpu.cpp */
cpu_state_t cpu_state;
uint64_t tsc;

/* 86box_glue.cpp */
int machine;
int pci_burst_time;
int pci_nonburst_time;
int agp_burst_time;
int agp_nonburst_time;

void pclog(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	write_log("%s", buf);
}

void fatal(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	write_log("86BOXFATAL: %s", buf);
}

void *plat_mmap(size_t size, uint8_t executable, uint8_t *large)
{
	if (large)
		*large = 0;
#ifdef _WIN32
	(void)executable;
	return calloc(1, size);
#else
	{
		void *p = mmap(NULL, size, PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0),
				MAP_ANON | MAP_PRIVATE, -1, 0);
		return p == MAP_FAILED ? NULL : p;
	}
#endif
}

void plat_munmap(void *ptr, size_t size)
{
	if (!ptr)
		return;
#ifdef _WIN32
	(void)size;
	free(ptr);
#else
	munmap(ptr, size);
#endif
}

/* timer.c: the NVidia RIVA CPU-independent timer (no RIVA boards here) */
void rivatimer_init(void)
{
}

int rom_present(const char *fn)
{
	return 0;
}

FILE *nvr_fopen(char *str, char *mode)
{
	return NULL;
}

uint8_t mca_add(uint8_t (*read)(uint16_t port, void *priv), void (*write)(uint16_t port, uint8_t val, void *priv),
		uint8_t (*feedb)(void *priv), void (*reset)(void *priv), void *priv)
{
	return 0;
}

/* 86box_machine.cpp: the boards only compare machines[machine].init with
 * these, to apply PC motherboard quirks */
int machine_at_acera1g_init(const machine_t *model)
{
	return 0;
}

int machine_at_advantage40xxd_init(const machine_t *model)
{
	return 0;
}

int machine_at_sb486pv_init(const machine_t *model)
{
	return 0;
}

const machine_t machines[] = {
	{ .name = "Amiga RTG", .internal_name = "amiga_rtg", .bus_flags = MACHINE_PCI },
	{ .name = NULL }
};
