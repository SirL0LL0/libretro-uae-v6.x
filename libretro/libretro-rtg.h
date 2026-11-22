#ifndef LIBRETRO_RTG_H
#define LIBRETRO_RTG_H

#include <stdbool.h>
#include <stddef.h>

/* RTG game presets: a tested machine with a graphics card, a system drive
 * built from the user's Workbench/Picasso96/AHI archives and a game that is
 * started automatically (libretro-rtg.c). */
struct rtg_preset
{
   const char *model;     /* puae_model value */
   const char *label;
   const char *base;      /* model preset it is built on */
   const char *board;     /* gfxcard_type */
   int vram;              /* MB */
   int z3mem;             /* MB of Zorro III Fast RAM */
   const char *card;      /* Picasso96 card driver (Libs/Picasso96/<card>.card, BoardType) */
   bool fast_cpu;         /* sets the CPU core options to the fastest when chosen
                           * ('CPU Compatibility' Normal, 'CPU Speed' Max) */
};

extern const struct rtg_preset rtg_presets[];

const struct rtg_preset *rtg_preset_find(const char *model);

/* Machine configuration lines of the preset (appended to the model preset) */
void rtg_preset_config(const struct rtg_preset *p, char *out, size_t outsize);

/* Build/refresh <saves>/RTG from <system data>/RTG, write its startup for
 * the preset and the game. 'content' is the game (LHA or directory); an
 * archive is extracted to <saves>/TEMP/RTG-<name>, with the files it wrote
 * before (<saves>/RTG-Saves/<name>) put back. 'game_path' is the
 * directory to mount as DH0:. Returns false with a user message when
 * something is missing. */
bool rtg_setup(const struct rtg_preset *p, const char *content,
      const char *data_dir, const char *save_dir,
      char *sys_path, size_t sys_path_size, char *game_path, size_t game_path_size,
      char *msg, size_t msg_size);

/* 'Automatic' model: the RTG preset for the content, or NULL. Tags (CV64),
 * (RTG060), (RTG040)/(RTG); "RTG" as a word of the file name; or a program
 * using an RTG API and no WHDLoad slave. The 060 preset falls back to the
 * 040 one without Mu680x0Libs; a detected (untagged) game needs the support
 * files, else NULL and 'msg' says what is missing. */
const char *rtg_auto_preset(const char *content, const char *data_dir, char *msg, size_t msg_size);

/* Keep what the game wrote (<saves>/RTG-Saves/<name>) and remove its
 * temporary copy; when the game ends or before it is set up again */
void rtg_shutdown(void);

#endif
