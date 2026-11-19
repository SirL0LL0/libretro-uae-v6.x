//	Altirra - Atari 800/800XL/5200 emulator
//	Coprocessor library - 8048 emulator
//	Copyright (C) 2009-2015 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	C version of upstream's kbmcu/8048/co8048.h for the libretro core, which
//	compiles the emulator as C: the class becomes a struct and every method an
//	ATCoProc8048_<Method>() function taking the CPU first.
//	Generated sources: tools/port_kbmcu.js.

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int32_t sint32;

#ifndef f_ATCOPROC_CO8048_H
#define f_ATCOPROC_CO8048_H

struct ATCoProc8048 {
	int		mTStatesLeft;

	uint8	mA;
	uint8	mPSW;
	uint8	*mpRegBank;
	const uint8	*mpProgramBanks[2];
	const uint8	*mpProgramBank;

	uint8	mT;
	uint8	mP1;
	uint8	mP2;
	bool	mbDBF;
	bool	mbPBK;
	bool	mbIF;
	bool	mbTIF;
	bool	mbTF;
	bool	mbCountEnabled;
	bool	mbF1;
	bool	mbIrqEnabled;
	bool	mbIrqPending;
	bool	mbIrqAttention;
	bool	mbTimerIrqPending;

	uint16		mPC;
	sint32		mCyclesLeft;
	uint32		mCyclesBase;
	uint32		mCyclesSaved;

	bool	mbTimerActive;
	uint32		mTimerDeadline;

	const uint8	*mpNextState;
	uint8		mReadOpcodeState;

	uint8	mRAM[256];
};

/* Constructor: the member initializers of the C++ class */
void ATCoProc8048_init(struct ATCoProc8048 *self);

void ATCoProc8048_SetProgramBanks(struct ATCoProc8048 *self, const void *p0, const void *p1);

static inline uint16 ATCoProc8048_GetPC(const struct ATCoProc8048 *self) { return self->mPC; }
static inline uint8 ATCoProc8048_GetSP(const struct ATCoProc8048 *self) { return self->mPSW & 7; }
static inline uint8 ATCoProc8048_GetPort1Output(const struct ATCoProc8048 *self) { return self->mP1; }
static inline uint8 ATCoProc8048_GetPort2Output(const struct ATCoProc8048 *self) { return self->mP2; }
static inline uint32 ATCoProc8048_GetTime(const struct ATCoProc8048 *self) { return self->mCyclesBase - self->mCyclesLeft; }
static inline uint32 ATCoProc8048_GetTimeBase(const struct ATCoProc8048 *self) { return self->mCyclesBase; }
static inline uint32 ATCoProc8048_GetTStatesPending(const struct ATCoProc8048 *self) { return self->mTStatesLeft; }
static inline uint32 ATCoProc8048_GetCyclesLeft(const struct ATCoProc8048 *self) { return self->mCyclesLeft; }
static inline void ATCoProc8048_AddCycles(struct ATCoProc8048 *self, sint32 cycles) { self->mCyclesBase += cycles; self->mCyclesLeft += cycles; }

uint8 ATCoProc8048_ReadByte(struct ATCoProc8048 *self, uint8 addr);
void ATCoProc8048_WriteByte(struct ATCoProc8048 *self, uint8 addr, uint8 val);

void ATCoProc8048_ColdReset(struct ATCoProc8048 *self);
void ATCoProc8048_WarmReset(struct ATCoProc8048 *self);

void ATCoProc8048_AssertIrq(struct ATCoProc8048 *self);
void ATCoProc8048_NegateIrq(struct ATCoProc8048 *self);

void ATCoProc8048_AssertHighToLowT1(struct ATCoProc8048 *self);

void ATCoProc8048_Run(struct ATCoProc8048 *self);

void ATCoProc8048_DispatchExternalIrq(struct ATCoProc8048 *self);
void ATCoProc8048_DispatchTimerIrq(struct ATCoProc8048 *self);
void ATCoProc8048_UpdateTimer(struct ATCoProc8048 *self);
void ATCoProc8048_UpdateTimerDeadline(struct ATCoProc8048 *self);
void ATCoProc8048_StartCount(struct ATCoProc8048 *self);
void ATCoProc8048_StopCount(struct ATCoProc8048 *self);

void mpFnWritePort(int, uint8);
uint8 mpFnReadBus(void);
void mpFnWriteBus(uint8);
uint8 mpFnReadPort(int, uint8);
uint8 mpFnReadT0(void);
uint8 mpFnReadT1(void);
uint8 mpFnReadXRAM(uint8);
void mpFnWriteXRAM(uint8, uint8);

#endif	// f_ATCOPROC_CO8048_H
