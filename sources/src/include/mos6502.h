/*
 * C version of upstream's include/mos6502.h (MOS 6502 emulator by
 * Gianluca Ghettini, used by the 6500-1/6570-036 keyboard MCU emulation).
 * The libretro core compiles the emulator as C: the class becomes a struct
 * and every method a mos6502_<Method>() function taking the CPU first.
 * Generated sources: tools/port_kbmcu.js.
 */
#ifndef UAE_MOS6502_H
#define UAE_MOS6502_H

#include <stdint.h>
#include <stdbool.h>

struct mos6502;

typedef void (*mos6502_BusWrite)(uint16_t, uint8_t);
typedef uint8_t (*mos6502_BusRead)(uint16_t);
typedef void (*mos6502_ClockCycle)(struct mos6502 *);

enum mos6502_CycleMethod {
	MOS6502_INST_COUNT,
	MOS6502_CYCLE_COUNT
};

struct mos6502
{
	/* register reset values */
	uint8_t reset_A;
	uint8_t reset_X;
	uint8_t reset_Y;
	uint8_t reset_sp;
	uint8_t reset_status;

	/* registers */
	uint8_t A;
	uint8_t X;
	uint8_t Y;
	uint8_t sp;
	uint16_t pc;
	uint8_t status;

	bool illegalOpcode;

	mos6502_BusRead Read;
	mos6502_BusWrite Write;
	mos6502_ClockCycle Cycle;
};

void mos6502_init(struct mos6502 *self, mos6502_BusRead r, mos6502_BusWrite w, mos6502_ClockCycle c);
void mos6502_NMI(struct mos6502 *self);
void mos6502_IRQ(struct mos6502 *self);
void mos6502_Reset(struct mos6502 *self);
void mos6502_Run(struct mos6502 *self, int32_t cycles, uint64_t *cycleCount, enum mos6502_CycleMethod cycleMethod);
void mos6502_RunEternally(struct mos6502 *self);
uint16_t mos6502_GetPC(struct mos6502 *self);
uint8_t mos6502_GetS(struct mos6502 *self);
uint8_t mos6502_GetP(struct mos6502 *self);
uint8_t mos6502_GetA(struct mos6502 *self);
uint8_t mos6502_GetX(struct mos6502 *self);
uint8_t mos6502_GetY(struct mos6502 *self);
void mos6502_SetResetS(struct mos6502 *self, uint8_t value);
void mos6502_SetResetP(struct mos6502 *self, uint8_t value);
void mos6502_SetResetA(struct mos6502 *self, uint8_t value);
void mos6502_SetResetX(struct mos6502 *self, uint8_t value);
void mos6502_SetResetY(struct mos6502 *self, uint8_t value);
void mos6502_SetPC(struct mos6502 *self, uint16_t value);
void mos6502_SetP(struct mos6502 *self, uint8_t value);
uint8_t mos6502_GetResetS(struct mos6502 *self);
uint8_t mos6502_GetResetP(struct mos6502 *self);
uint8_t mos6502_GetResetA(struct mos6502 *self);
uint8_t mos6502_GetResetX(struct mos6502 *self);
uint8_t mos6502_GetResetY(struct mos6502 *self);

#endif /* UAE_MOS6502_H */
