/*
 * PUAE libretro - RTG game presets.
 *
 * A preset is a tested machine with a graphics card (see rtg_presets[]).
 * With one selected, a hard drive content (LHA archive or directory) is
 * treated as an RTG game, the same way WHDLoad games are handled:
 *
 * - <saves>/RTG is a bootable system drive, built once from the user's
 *   archives in <system>/uae[_data]/RTG: a Workbench 3.x disk (ADF, also
 *   zipped), Picasso96.lha and optionally AHI. Their Libs, Devs, C, L, Fonts
 *   and Classes directories are copied; nothing of them ships with the core.
 * - Each launch writes the monitor driver of the preset's board (with the
 *   tooltypes the Picasso96 installer would set), S:Startup-Sequence and
 *   S:RTG-Game, which starts the game's executable on DH0:.
 * - The executable is the one that uses cybergraphics.library,
 *   Picasso96API.library or rtg.library; "<game>.rtg" (next to the content
 *   or in <saves>/RTG) overrides it with a command line relative to DH0:.
 */

#include "libretro-core.h"
#include "libretro-rtg.h"

#include <sys/stat.h>

#include "sysdeps.h"
#include "zfile.h"

/* zfile.c, not in zfile.h */
extern int zfile_readdir_archive_3(struct zdirectory *zd, TCHAR *out, bool fullpath);

#include "file/file_path.h"
#include "retro_dirent.h"
#include "streams/file_stream.h"

const struct rtg_preset rtg_presets[] = {
   { "RTG_UAE",  "A4000/040 RTG: UAE Zorro III, Picasso96 uaegfx (v3.1, 2M Chip + 8M Fast + 64M Z3)",
     "A4040", "ZorroIII", 32, 64, "uaegfx", true },
   /* The real board: with 'CPU Speed' Max its emulation overruns the frame
    * time, so the CPU options are left as they are */
   { "RTG_CV64", "A4000/040 RTG: CyberVision 64 (v3.1, 2M Chip + 8M Fast + 64M Z3)",
     "A4040", "CV64_Z3", 4, 64, "CyberVision", false },
   /* 68060: its FPU is disabled at reset and enabled by 68060.library
    * (Mu680x0Libs.lha); SetPatch loads it through 68040.library */
   { "RTG_UAE060", "A4000/060 RTG: UAE Zorro III, Picasso96 uaegfx (v3.1, 2M Chip + 8M Fast + 128M Z3)",
     "A4060", "ZorroIII", 32, 128, "uaegfx", true },
   { NULL }
};

/* Executables that need command line arguments to run as intended */
static const struct { const char *exe; const char *args; } rtg_known_args[] = {
   { "KoboDeluxe", "-fullscreen" },
   { NULL, NULL }
};

/* "Fixes": Mu680x0Libs' spatch and SetPatch patches (68060 presets) */
static const char *rtg_sysdirs[] = { "Libs", "Devs", "C", "L", "Fonts", "Classes", "Fixes", NULL };

/* Files not installed: AHI drivers for PCI sound cards (they stop the boot
 * with an openpci.library requester) */
static const char *rtg_skip[] = { "emu10kx", NULL };

const struct rtg_preset *rtg_preset_find(const char *model)
{
   int i;
   if (!model)
      return NULL;
   for (i = 0; rtg_presets[i].model; i++)
      if (!strcmp(rtg_presets[i].model, model))
         return &rtg_presets[i];
   return NULL;
}

void rtg_preset_config(const struct rtg_preset *p, char *out, size_t outsize)
{
   snprintf(out, outsize,
         "z3mem_size=%d\n"
         "gfxcard_type=%s\n"
         "gfxcard_size=%d\n"
         /* All 040/060 FPU instructions in the emulator, no 68040.library needed */
         "fpu_no_unimplemented=true\n"
         "cpu_no_unimplemented=true\n"
         /* No MMU emulation (games do not use it, it halves the CPU speed);
          * 'CPU Model' set to a 68030 or better turns it on again */
         "mmu_model=0\n",
         p->z3mem, p->board, p->vram);
}

/* ------------------------------------------------------------- helpers */

static const char *rtg_stristr(const char *s, const char *sub)
{
   size_t n = strlen(sub);
   for (; *s; s++)
      if (!strncasecmp(s, sub, n))
         return s;
   return NULL;
}
#define strcasestr rtg_stristr

static void rtg_mkdirs_for(const char *file)
{
   char dir[RETRO_PATH_MAX];
   snprintf(dir, sizeof(dir), "%s", file);
   path_basedir(dir);
   if (!path_is_directory(dir))
      path_mkdir(dir);
}

static bool rtg_write_file(const char *path, const void *data, size_t len)
{
   FILE *f;
   rtg_mkdirs_for(path);
   if (!(f = fopen(path, "wb")))
      return false;
   if (len && fwrite(data, 1, len, f) != len)
   {
      fclose(f);
      return false;
   }
   fclose(f);
   return true;
}

static uint8_t *rtg_read_file(const char *path, size_t *len)
{
   uint8_t *buf;
   long size;
   FILE *f = fopen(path, "rb");
   *len = 0;
   if (!f)
      return NULL;
   fseek(f, 0, SEEK_END);
   size = ftell(f);
   fseek(f, 0, SEEK_SET);
   buf = (uint8_t*)malloc(size > 0 ? size : 1);
   if (buf && size > 0 && fread(buf, 1, size, f) != (size_t)size)
   {
      free(buf);
      buf = NULL;
   }
   fclose(f);
   if (buf)
      *len = size;
   return buf;
}

/* "a/B/c" -> index of the first component that is a system directory */
static const char *rtg_sysdir_tail(const char *rel, char *canon, size_t canonsize)
{
   const char *p = rel;
   while (*p)
   {
      const char *e = p;
      int i;
      while (*e && *e != '/' && *e != '\\')
         e++;
      for (i = 0; rtg_sysdirs[i]; i++)
      {
         size_t n = strlen(rtg_sysdirs[i]);
         if ((size_t)(e - p) == n && !strncasecmp(p, rtg_sysdirs[i], n) && *e)
         {
            /* canonical spelling of the directory, rest as is */
            snprintf(canon, canonsize, "%s%s", rtg_sysdirs[i], e);
            return canon;
         }
      }
      if (!*e)
         break;
      p = e + 1;
   }
   return NULL;
}

/* Copy one file of an archive (zfile path) to the system drive */
/* Contents of a file inside an archive (the zfile belongs to the archive
 * node, as for the UAE filesystem: not closed here) */
static uint8_t *rtg_read_archive_file(const char *zpath, size_t *len)
{
   struct zfile *zf = zfile_open_archive(zpath, 0);
   uae_s64 size;
   uint8_t *buf = NULL;

   *len = 0;
   if (!zf)
      return NULL;
   size = zfile_size(zf);
   zfile_fseek(zf, 0, SEEK_SET);
   if (size >= 0 && size < 64 * 1024 * 1024 && (buf = (uint8_t*)malloc(size ? size : 1)))
   {
      if (zfile_fread(buf, 1, size, zf) == size)
         *len = (size_t)size;
      else
      {
         free(buf);
         buf = NULL;
      }
   }
   return buf;
}

static int rtg_copy_from_archive(const char *zpath, const char *dst)
{
   size_t len = 0;
   uint8_t *buf = rtg_read_archive_file(zpath, &len);
   int ok = buf ? rtg_write_file(dst, buf, len) : 0;
   free(buf);
   return ok;
}

/* Walk an archive (LHA, ZIP, ADF, ADF in ZIP...) and copy the files under
 * its system directories to 'sysdir' */
static int rtg_extract_archive(const char *zdir, const char *root, const char *sysdir, int depth)
{
   struct zdirectory *zd;
   TCHAR full[MAX_DPATH];
   int copied = 0;

   if (depth > 12 || !(zd = zfile_opendir_archive_flags(zdir, ZFD_ALL)))
      return 0;
   while (zfile_readdir_archive_3(zd, full, true))
   {
      int isdir = 0, flags = 0;
      zfile_fill_file_attrs_archive(full, &isdir, &flags, NULL);
      if (getenv("PUAE_RTG_DEBUG"))
         log_cb(RETRO_LOG_INFO, "RTG: [%d] %s (%d)\n", depth, full, isdir);
      if (isdir)
         copied += rtg_extract_archive(full, root, sysdir, depth + 1);
      else
      {
         char canon[RETRO_PATH_MAX], dst[RETRO_PATH_MAX];
         const char *rel = full + strlen(root);
         const char *tail;
         while (*rel == '/' || *rel == '\\')
            rel++;
         if (!(tail = rtg_sysdir_tail(rel, canon, sizeof(canon))))
            continue;
         {
            int k, skip = 0;
            for (k = 0; rtg_skip[k]; k++)
               if (strcasestr(tail, rtg_skip[k]))
                  skip = 1;
            if (skip)
               continue;
         }
         path_join(dst, sysdir, tail);
         copied += rtg_copy_from_archive(full, dst);
      }
   }
   zfile_closedir_archive(zd);
   return copied;
}

/* Extract a whole archive to 'dst' */
static int rtg_extract_all(const char *zdir, const char *root, const char *dst, int depth)
{
   struct zdirectory *zd;
   TCHAR full[MAX_DPATH];
   int copied = 0;

   if (depth > 16 || !(zd = zfile_opendir_archive_flags(zdir, ZFD_ARCHIVE)))
      return 0;
   while (zfile_readdir_archive_3(zd, full, true))
   {
      int isdir = 0, flags = 0;
      char d[RETRO_PATH_MAX], rel[RETRO_PATH_MAX], *c;
      const char *r = full + strlen(root);
      while (*r == '/' || *r == '\\')
         r++;
      snprintf(rel, sizeof(rel), "%s", r);
      for (c = rel; *c; c++)
         if (*c == '\\' || *c == '/')
            *c = DIR_SEP_CHR;
      path_join(d, dst, rel);
      zfile_fill_file_attrs_archive(full, &isdir, &flags, NULL);
      if (isdir > 0)
      {
         path_mkdir(d);
         copied += rtg_extract_all(full, root, dst, depth + 1);
      }
      else if (!isdir)
         copied += rtg_copy_from_archive(full, d);
   }
   zfile_closedir_archive(zd);
   return copied;
}

/* Copy a host directory tree verbatim (<RTG>/Extra) */
static int rtg_copy_tree(const char *src, const char *dst)
{
   RDIR *dir = retro_opendir(src);
   int copied = 0;
   if (!dir)
      return 0;
   while (retro_readdir(dir))
   {
      const char *name = retro_dirent_get_name(dir);
      char s[RETRO_PATH_MAX], d[RETRO_PATH_MAX];
      if (name[0] == '.')
         continue;
      path_join(s, src, name);
      path_join(d, dst, name);
      if (path_is_directory(s))
      {
         path_mkdir(d);
         copied += rtg_copy_tree(s, d);
      }
      else if (!fcopy(s, d))
         copied++;
   }
   retro_closedir(dir);
   return copied;
}

/* List of the user's support files, to rebuild the system drive when it changes */
static void rtg_sources_stamp(const char *srcdir, char *out, size_t outsize)
{
   RDIR *dir = retro_opendir(srcdir);
   out[0] = 0;
   if (!dir)
      return;
   while (retro_readdir(dir))
   {
      const char *name = retro_dirent_get_name(dir);
      char path[RETRO_PATH_MAX], line[RETRO_PATH_MAX + 32];
      if (name[0] == '.')
         continue;
      path_join(path, srcdir, name);
      snprintf(line, sizeof(line), "%s %d\n", name, path_is_directory(path) ? -1 : (int)path_get_size(path));
      strlcat(out, line, outsize);
   }
   retro_closedir(dir);
}

/* Write a copy of a DiskObject (.info) with the given tooltypes (as the
 * Installer's settooltype); returns false if it is not an icon */
static bool rtg_icon_tooltypes(const char *src, const char *dst, const char **tts, int ntts)
{
   size_t len = 0, pos = 78, rest, ttlen = 0;
   uint8_t *in = rtg_read_file(src, &len), *out, *p;
   uint32_t gad_render, sel_render, default_tool, tooltypes, drawerdata;
   bool ok = false;
   int i;

#define BE16(o) ((uint32_t)(in[o] << 8 | in[(o) + 1]))
#define BE32(o) ((uint32_t)in[o] << 24 | (uint32_t)in[(o) + 1] << 16 | (uint32_t)in[(o) + 2] << 8 | in[(o) + 3])
   if (!in || len < 78 || BE16(0) != 0xE310)
      goto end;
   gad_render   = BE32(4 + 18);
   sel_render   = BE32(4 + 22);
   default_tool = BE32(50);
   tooltypes    = BE32(54);
   drawerdata   = BE32(66);
   if (drawerdata)
      pos += 56;
   for (i = 0; i < 2; i++)
   {
      if ((i == 0 && gad_render) || (i == 1 && sel_render))
      {
         uint32_t w = BE16(pos + 4), h = BE16(pos + 6), d = BE16(pos + 8), data = BE32(pos + 10);
         pos += 20;
         if (data)
            pos += ((w + 15) / 16) * 2 * h * d;
      }
   }
   if (default_tool)
      pos += 4 + BE32(pos);
   if (pos > len)
      goto end;
   rest = pos;
   if (tooltypes)
   {
      uint32_t cnt = BE32(rest) / 4 - 1;
      rest += 4;
      while (cnt-- && rest + 4 <= len)
         rest += 4 + BE32(rest);
   }
   for (i = 0; i < ntts; i++)
      ttlen += 4 + strlen(tts[i]) + 1;
   if (!(out = (uint8_t*)malloc(pos + 4 + ttlen + (len - rest))))
      goto end;
   memcpy(out, in, pos);
   out[54] = out[55] = out[56] = 0;
   out[57] = ntts ? 1 : 0;
   p = out + pos;
   if (ntts)
   {
      uint32_t n = (ntts + 1) * 4;
      *p++ = n >> 24; *p++ = n >> 16; *p++ = n >> 8; *p++ = n;
      for (i = 0; i < ntts; i++)
      {
         uint32_t l = strlen(tts[i]) + 1;
         *p++ = l >> 24; *p++ = l >> 16; *p++ = l >> 8; *p++ = l;
         memcpy(p, tts[i], l);
         p += l;
      }
   }
   memcpy(p, in + rest, len - rest);
   p += len - rest;
   ok = rtg_write_file(dst, out, p - out);
   free(out);
#undef BE16
#undef BE32
end:
   free(in);
   return ok;
}

/* ---------------------------------------------------- game executable */

struct rtg_exe
{
   char path[RETRO_PATH_MAX];   /* relative to DH0:, '/' separated */
   int score;
};

static int rtg_score_exe(const char *rel, const uint8_t *buf, size_t len)
{
   static const char *libs[] = { "cybergraphics.library", "Picasso96API.library", "picasso96/rtg.library", NULL };
   static const char *tools[] = { "prefs", "setup", "install", "convert", "config", "uninstall", NULL };
   const char *name = strrchr(rel, '/') ? strrchr(rel, '/') + 1 : rel;
   int score = 0, i, depth = 0;
   const char *c;
   size_t j;

   if (len < 4 || buf[0] || buf[1] || buf[2] != 0x03 || buf[3] != 0xf3)
      return -1;   /* not an AmigaOS executable */
   if (strendswith(name, ".library") || strendswith(name, ".device") || strendswith(name, ".card")
         || strendswith(name, ".chip") || strendswith(name, ".datatype") || strendswith(name, ".audio"))
      return -1;
   for (i = 0; libs[i]; i++)
   {
      size_t n = strlen(libs[i]);
      for (j = 0; j + n <= len; j++)
         if (!strncasecmp((const char*)buf + j, libs[i], n))
         {
            score += 100;
            break;
         }
      if (score)
         break;
   }
   /* Without an RTG API: the game's main program (same name as its
    * directory) for games that use any screen mode */
   {
      const char *slash = strrchr(rel, '/');
      if (slash)
      {
         const char *dir = slash;
         while (dir > rel && dir[-1] != '/')
            dir--;
         if ((size_t)(slash - dir) == strlen(name) && !strncasecmp(dir, name, strlen(name)))
            score += 30;
      }
   }
   if (strcasestr(name, "rtg"))
      score += 20;
   if (strcasestr(name, "cgx") || strcasestr(name, "p96"))
      score += 10;
   for (i = 0; tools[i]; i++)
      if (strcasestr(name, tools[i]))
         score -= 80;
   for (c = rel; *c; c++)
      if (*c == '/')
         depth++;
   return score - depth * 5;
}

static void rtg_scan_dir(const char *host, const char *rel, struct rtg_exe *best, int depth)
{
   RDIR *dir;
   if (depth > 6 || !(dir = retro_opendir(host)))
      return;
   while (retro_readdir(dir))
   {
      const char *name = retro_dirent_get_name(dir);
      char h[RETRO_PATH_MAX], r[RETRO_PATH_MAX];
      if (name[0] == '.' || strendswith(name, ".info"))
         continue;
      path_join(h, host, name);
      snprintf(r, sizeof(r), "%s%s%s", rel, rel[0] ? "/" : "", name);
      if (path_is_directory(h))
         rtg_scan_dir(h, r, best, depth + 1);
      else
      {
         size_t len = 0;
         uint8_t *buf = rtg_read_file(h, &len);
         int s = buf ? rtg_score_exe(r, buf, len) : -1;
         if (s > best->score)
         {
            best->score = s;
            snprintf(best->path, sizeof(best->path), "%s", r);
         }
         free(buf);
      }
   }
   retro_closedir(dir);
}

/* Command line override: "<game>.rtg" next to the content or in <saves>/RTG */
static bool rtg_override(const char *content, const char *sysdir, char *cmd, size_t cmdsize)
{
   char base[RETRO_PATH_MAX], path[RETRO_PATH_MAX];
   int i;

   snprintf(base, sizeof(base), "%s", path_basename(content));
   path_remove_extension(base);
   for (i = 0; i < 2; i++)
   {
      FILE *f;
      if (i == 0)
      {
         char dir[RETRO_PATH_MAX];
         snprintf(dir, sizeof(dir), "%s", content);
         path_basedir(dir);
         snprintf(path, sizeof(path), "%s%s.rtg", dir, base);
      }
      else
         snprintf(path, sizeof(path), "%s%s%s.rtg", sysdir, DIR_SEP_STR, base);
      if ((f = fopen(path, "r")))
      {
         bool ok = fgets(cmd, (int)cmdsize, f) != NULL;
         fclose(f);
         if (ok)
         {
            cmd[strcspn(cmd, "\r\n")] = 0;
            log_cb(RETRO_LOG_INFO, "RTG: command line from \"%s\": %s\n", path, cmd);
            return cmd[0] != 0;
         }
      }
   }
   return false;
}

/* ------------------------------------------------------------------ AHI */

static void rtg_put32(uint8_t *p, uint32_t v)
{
   p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

/* Audio ID of the named mode in an AHI AudioModes file (IFF AHIM) */
static uint32_t rtg_ahi_mode_id(const char *path, const char *mode)
{
   size_t len = 0, pos = 12;
   uint8_t *b = rtg_read_file(path, &len);
   uint32_t id = 0;

   if (!b || len < 12 || memcmp(b, "FORM", 4) || memcmp(b + 8, "AHIM", 4))
      goto end;
   while (pos + 8 <= len && !id)
   {
      uint32_t size = (uint32_t)b[pos + 4] << 24 | b[pos + 5] << 16 | b[pos + 6] << 8 | b[pos + 7];
      size_t data = pos + 8, i;
      if (!memcmp(b + pos, "AUDM", 4) && data + size <= len)
      {
         uint32_t cand = 0;
         bool named = false;
         for (i = data; i + 8 <= data + size; i += 4)
         {
            uint32_t tag = (uint32_t)b[i] << 24 | b[i + 1] << 16 | b[i + 2] << 8 | b[i + 3];
            if (tag == 0x80000064)   /* AHIDB_AudioID */
            {
               cand = (uint32_t)b[i + 4] << 24 | b[i + 5] << 16 | b[i + 6] << 8 | b[i + 7];
               break;
            }
         }
         for (i = data; i + strlen(mode) < data + size; i++)
            if (!memcmp(b + i, mode, strlen(mode)) && b[i + strlen(mode)] == 0)
               named = true;
         if (named)
            id = cand;
      }
      pos = data + size + (size & 1);
   }
end:
   free(b);
   return id;
}

/* ENVARC:Sys/ahi.prefs with a Paula mode for unit 0 and the music unit
 * (AHI_DEFAULT_ID): without it AHI games stay silent */
static void rtg_ahi_prefs(const char *sysdir)
{
   char modes[RETRO_PATH_MAX], prefs[RETRO_PATH_MAX];
   uint8_t buf[12 + 14 + 8 + 24 + 2 * (8 + 32)], *p = buf;
   uint32_t id;
   int u;

   path_join(modes, sysdir, "Devs/AudioModes/PAULA");
   path_join(prefs, sysdir, "Prefs/Env-Archive/Sys/ahi.prefs");
   if (path_is_valid(prefs) || !path_is_valid(modes))
      return;
   if (!(id = rtg_ahi_mode_id(modes, "Paula:8 bit stereo++")))
      return;

   memcpy(p, "FORM", 4); rtg_put32(p + 4, sizeof(buf) - 8); memcpy(p + 8, "PREF", 4); p += 12;
   /* PRHD: version, type, flags */
   memcpy(p, "PRHD", 4); rtg_put32(p + 4, 6); memset(p + 8, 0, 6); p += 14;
   /* AHIG: debug level, no surround/echo off, fast echo off, max CPU 90%,
    * clip master volume, pad, anticlick time, scale mode */
   memcpy(p, "AHIG", 4); rtg_put32(p + 4, 24); memset(p + 8, 0, 24);
   rtg_put32(p + 8 + 8, 0x0000e666);
   p += 8 + 24;
   for (u = 0; u < 2; u++)
   {
      memcpy(p, "AHIU", 4); rtg_put32(p + 4, 32); memset(p + 8, 0, 32);
      p[8] = u ? 255 : 0;               /* unit 0, AHI_NO_UNIT (music unit) */
      p[11] = 4;                        /* channels */
      rtg_put32(p + 12, id);            /* audio mode */
      rtg_put32(p + 16, 22050);         /* frequency */
      rtg_put32(p + 20, 0);             /* monitor volume */
      rtg_put32(p + 24, 0x10000);       /* input gain */
      rtg_put32(p + 28, 0x10000);       /* output volume */
      p += 8 + 32;
   }
   if (rtg_write_file(prefs, buf, p - buf))
      log_cb(RETRO_LOG_INFO, "RTG: AHI prefs written, Paula mode 0x%08x\n", id);
}

/* ENVARC:Sys/screenmode.prefs: Workbench on the board in 16 bit, for games
 * that open a window on it. uaegfx mode IDs are the resolution's
 * DisplayID (rtg.c) + 0x100 for 16 bit; other boards keep FakeNativeModes. */
static void rtg_screenmode_prefs(const struct rtg_preset *p, const char *sysdir)
{
   char path[RETRO_PATH_MAX];
   uint8_t buf[12 + 14 + 8 + 28], *q = buf;

   path_join(path, sysdir, "Prefs/Env-Archive/Sys/screenmode.prefs");
   if (strcmp(p->card, "uaegfx"))
   {
      if (path_is_valid(path))
         retro_remove(path);
      return;
   }
   memcpy(q, "FORM", 4); rtg_put32(q + 4, sizeof(buf) - 8); memcpy(q + 8, "PREF", 4); q += 12;
   memcpy(q, "PRHD", 4); rtg_put32(q + 4, 6); memset(q + 8, 0, 6); q += 14;
   memcpy(q, "SCRM", 4); rtg_put32(q + 4, 28); memset(q + 8, 0, 28);
   rtg_put32(q + 8 + 16, 0x50031100);   /* UAE: 640x480 16bit PC */
   q[8 + 20] = 640 >> 8; q[8 + 21] = 640 & 0xff;
   q[8 + 22] = 480 >> 8; q[8 + 23] = 480 & 0xff;
   q[8 + 25] = 16;                       /* depth */
   q += 8 + 28;
   rtg_write_file(path, buf, q - buf);
}

/* --------------------------------------------------------------- setup */

/* ----------------------------------------------- game files and saves
 *
 * An archived game runs from a temporary copy, <saves>/TEMP/RTG-<name>
 * (the core empties <saves>/TEMP when it closes). What the game writes
 * there (new or changed files) is kept in <saves>/RTG-Saves/<name> when
 * the game ends, and laid over the fresh copy on the next start. A game
 * directory is used in place. */

static char rtg_run_dir[RETRO_PATH_MAX];      /* temporary copy, "" for a directory */
static char rtg_saves_dir[RETRO_PATH_MAX];
static char rtg_manifest[RETRO_PATH_MAX];

static void rtg_file_stamp(const char *path, long long *size, long long *mtime)
{
   struct stat st;
   *size = *mtime = -1;
   if (!stat(path, &st))
   {
      *size  = (long long)st.st_size;
      *mtime = (long long)st.st_mtime;
   }
}

/* "rel<TAB>size<TAB>mtime" for every file of the copy */
static void rtg_manifest_walk(const char *root, const char *rel, FILE *out)
{
   char dirpath[RETRO_PATH_MAX];
   RDIR *dir;

   if (rel[0])
      path_join(dirpath, root, rel);
   else
      snprintf(dirpath, sizeof(dirpath), "%s", root);
   if (!(dir = retro_opendir(dirpath)))
      return;
   while (retro_readdir(dir))
   {
      const char *name = retro_dirent_get_name(dir);
      char r[RETRO_PATH_MAX], h[RETRO_PATH_MAX];
      if (!strcmp(name, ".") || !strcmp(name, ".."))
         continue;
      if (rel[0])
         snprintf(r, sizeof(r), "%s%s%s", rel, DIR_SEP_STR, name);
      else
         snprintf(r, sizeof(r), "%s", name);
      path_join(h, root, r);
      if (path_is_directory(h))
         rtg_manifest_walk(root, r, out);
      else
      {
         long long size, mtime;
         rtg_file_stamp(h, &size, &mtime);
         fprintf(out, "%s\t%lld\t%lld\n", r, size, mtime);
      }
   }
   retro_closedir(dir);
}

/* Whether a file of the copy is unchanged since the start (in the manifest) */
static bool rtg_manifest_has(const char *manifest, const char *rel, long long size, long long mtime)
{
   char line[RETRO_PATH_MAX + 64];
   const char *p = manifest;
   snprintf(line, sizeof(line), "%s\t%lld\t%lld\n", rel, size, mtime);
   while ((p = strstr(p, line)))
   {
      if (p == manifest || p[-1] == '\n')
         return true;
      p++;
   }
   return false;
}

static int rtg_persist_walk(const char *root, const char *rel, const char *manifest, const char *saves)
{
   char dirpath[RETRO_PATH_MAX];
   RDIR *dir;
   int n = 0;

   if (rel[0])
      path_join(dirpath, root, rel);
   else
      snprintf(dirpath, sizeof(dirpath), "%s", root);
   if (!(dir = retro_opendir(dirpath)))
      return 0;
   while (retro_readdir(dir))
   {
      const char *name = retro_dirent_get_name(dir);
      char r[RETRO_PATH_MAX], h[RETRO_PATH_MAX], d[RETRO_PATH_MAX];
      if (!strcmp(name, ".") || !strcmp(name, ".."))
         continue;
      if (rel[0])
         snprintf(r, sizeof(r), "%s%s%s", rel, DIR_SEP_STR, name);
      else
         snprintf(r, sizeof(r), "%s", name);
      path_join(h, root, r);
      if (path_is_directory(h))
         n += rtg_persist_walk(root, r, manifest, saves);
      else
      {
         long long size, mtime;
         rtg_file_stamp(h, &size, &mtime);
         if (!rtg_manifest_has(manifest, r, size, mtime))
         {
            path_join(d, saves, r);
            rtg_mkdirs_for(d);
            if (!fcopy(h, d))
            {
               n++;
               log_cb(RETRO_LOG_INFO, "RTG: saved \"%s\"\n", r);
            }
         }
      }
   }
   retro_closedir(dir);
   return n;
}

void rtg_shutdown(void)
{
   size_t len = 0;
   char *manifest;

   if (!rtg_run_dir[0])
      return;
   manifest = (char*)rtg_read_file(rtg_manifest, &len);
   if (manifest && path_is_directory(rtg_run_dir))
   {
      int n;
      char *m = (char*)realloc(manifest, len + 1);
      if (m)
      {
         manifest = m;
         manifest[len] = 0;
         n = rtg_persist_walk(rtg_run_dir, "", manifest, rtg_saves_dir);
         log_cb(RETRO_LOG_INFO, "RTG: %d written file(s) kept in \"%s\"\n", n, rtg_saves_dir);
      }
   }
   free(manifest);
   if (path_is_directory(rtg_run_dir))
      remove_recurse(rtg_run_dir);
   retro_remove(rtg_manifest);
   rtg_run_dir[0] = 0;
}

static void rtg_game_dir(const char *content, const char *save_dir, char *game, size_t gamesize)
{
   char base[RETRO_PATH_MAX], temp[RETRO_PATH_MAX], dirname[RETRO_PATH_MAX];
   FILE *f;
   int n;

   /* A previous run of this session (restart): keep its files first */
   rtg_shutdown();

   snprintf(game, gamesize, "%s", content);
   if (path_is_directory(content))
      return;

   snprintf(base, sizeof(base), "%s", path_basename(content));
   path_remove_extension(base);
   path_join(temp, save_dir, "TEMP");
   path_mkdir(temp);
   snprintf(dirname, sizeof(dirname), "RTG-%s", base);
   path_join(rtg_run_dir, temp, dirname);
   snprintf(rtg_manifest, sizeof(rtg_manifest), "%s.manifest", rtg_run_dir);
   path_join(rtg_saves_dir, save_dir, "RTG-Saves");
   path_mkdir(rtg_saves_dir);
   strlcat(rtg_saves_dir, DIR_SEP_STR, sizeof(rtg_saves_dir));
   strlcat(rtg_saves_dir, base, sizeof(rtg_saves_dir));

   if (path_is_directory(rtg_run_dir))
      remove_recurse(rtg_run_dir);
   path_mkdir(rtg_run_dir);
   n = rtg_extract_all(content, content, rtg_run_dir, 0);
   log_cb(RETRO_LOG_INFO, "RTG: game extracted to \"%s\" (%d files)\n", rtg_run_dir, n);

   /* The files the game wrote last time */
   if (path_is_directory(rtg_saves_dir))
   {
      n = rtg_copy_tree(rtg_saves_dir, rtg_run_dir);
      log_cb(RETRO_LOG_INFO, "RTG: %d saved file(s) restored from \"%s\"\n", n, rtg_saves_dir);
   }

   if ((f = fopen(rtg_manifest, "w")))
   {
      rtg_manifest_walk(rtg_run_dir, "", f);
      fclose(f);
   }
   snprintf(game, gamesize, "%s", rtg_run_dir);
}

bool rtg_setup(const struct rtg_preset *p, const char *content,
      const char *data_dir, const char *save_dir,
      char *sys_path, size_t sys_path_size, char *game_path, size_t game_path_size,
      char *msg, size_t msg_size)
{
   char srcdir[RETRO_PATH_MAX], sysdir[RETRO_PATH_MAX], path[RETRO_PATH_MAX], path2[RETRO_PATH_MAX];
   char stamp_new[8192], *stamp_old = NULL;
   char script[4096], exe_dir[RETRO_PATH_MAX], exe_cmd[RETRO_PATH_MAX];
   size_t len = 0;
   struct rtg_exe best = { {0}, -1 };
   const char *tts[5];
   RDIR *dir;
   int i;
   bool is060;

   msg[0] = 0;
   path_join(srcdir, data_dir, "RTG");
   path_join(sysdir, save_dir, "RTG");
   snprintf(sys_path, sys_path_size, "%s", sysdir);

   if (!path_is_directory(srcdir))
   {
      path_mkdir(srcdir);
      snprintf(msg, msg_size, "RTG: put a Workbench 3.x disk (ADF) and Picasso96.lha in \"%s\".", srcdir);
      return false;
   }

   /* (Re)build the system drive when the support files changed */
   rtg_sources_stamp(srcdir, stamp_new, sizeof(stamp_new));
   path_join(path, sysdir, ".sources");
   stamp_old = (char*)rtg_read_file(path, &len);
   if (!stamp_old || len != strlen(stamp_new) || memcmp(stamp_old, stamp_new, len))
   {
      int copied = 0;
      log_cb(RETRO_LOG_INFO, "RTG: building the system drive \"%s\" from \"%s\"...\n", sysdir, srcdir);
      if (path_is_directory(sysdir))
         remove_recurse(sysdir);
      path_mkdir(sysdir);
      if ((dir = retro_opendir(srcdir)))
      {
         while (retro_readdir(dir))
         {
            const char *name = retro_dirent_get_name(dir);
            int n;
            if (name[0] == '.')
               continue;
            path_join(path2, srcdir, name);
            if (path_is_directory(path2))
            {
               /* "Extra": copied as is (68060.library, other libraries...) */
               if (!strcasecmp(name, "Extra"))
                  copied += rtg_copy_tree(path2, sysdir);
               continue;
            }
            n = rtg_extract_archive(path2, path2, sysdir, 0);
            log_cb(RETRO_LOG_INFO, "RTG: %d files from \"%s\"\n", n, name);
            copied += n;
         }
         retro_closedir(dir);
      }
      /* The Picasso96 monitor driver is the template for the board's one */
      path_join(path, sysdir, "Devs/Monitors/Picasso96");
      path_join(path2, sysdir, "P96Monitor");
      rename(path, path2);
      path_join(path, sysdir, "Devs/Monitors/Picasso96.info");
      path_join(path2, sysdir, "P96Monitor.info");
      rename(path, path2);
      /* The settings file for VGA monitors */
      path_join(path, sysdir, "Devs/Picasso96Settings.31");
      path_join(path2, sysdir, "Devs/Picasso96Settings");
      if (path_is_valid(path) && !path_is_valid(path2))
         fcopy(path, path2);
      rtg_ahi_prefs(sysdir);
      /* gtlayout.lha ships the library as gtlayout.library.020/.68k */
      path_join(path, sysdir, "Libs/gtlayout.library.020");
      path_join(path2, sysdir, "Libs/gtlayout.library");
      if (!path_is_valid(path2) && path_is_valid(path))
         fcopy(path, path2);
      path_join(path, sysdir, ".sources");
      rtg_write_file(path, stamp_new, strlen(stamp_new));
      log_cb(RETRO_LOG_INFO, "RTG: %d files installed\n", copied);
   }
   free(stamp_old);

   /* Everything the preset needs */
   path_join(path, sysdir, "Libs/Picasso96/rtg.library");
   path_join(path2, sysdir, "P96Monitor");
   if (!path_is_valid(path) || !path_is_valid(path2))
   {
      snprintf(msg, msg_size, "RTG: Picasso96.lha (aminet.net/driver/video/Picasso96.lha) is missing in \"%s\".", srcdir);
      return false;
   }
   path_join(path, sysdir, "C/Execute");
   path_join(path2, sysdir, "Libs/mathieeedoubtrans.library");
   if (!path_is_valid(path) || !path_is_valid(path2))
   {
      snprintf(msg, msg_size, "RTG: a Workbench 3.x disk (ADF or zipped ADF) is missing in \"%s\".", srcdir);
      return false;
   }
   /* 68060: SetPatch 40.16 only loads 68040.library. Mu680x0Libs patches
    * it (Fixes/SetPatch_40_16.pch) to load 680x0.library, which opens the
    * library of the CPU (68060.library); the startup applies the patch */
   is060 = !strcmp(p->base, "A4060");
   if (is060)
   {
      char pch[RETRO_PATH_MAX], sp[RETRO_PATH_MAX];
      path_join(path, sysdir, "Libs/68060.library");
      path_join(path2, sysdir, "Libs/680x0.library");
      path_join(pch, sysdir, "Fixes/SetPatch_40_16.pch");
      path_join(sp, sysdir, "Fixes/spatch");
      if (!path_is_valid(path) || !path_is_valid(path2) || !path_is_valid(pch) || !path_is_valid(sp))
      {
         snprintf(msg, msg_size, "RTG: the 68060 presets need Mu680x0Libs.lha (aminet.net/util/sys/Mu680x0Libs.lha) in \"%s\".", srcdir);
         return false;
      }
   }
   snprintf(path, sizeof(path), "%s%sLibs%sPicasso96%s%s.card", sysdir, DIR_SEP_STR, DIR_SEP_STR, DIR_SEP_STR, p->card);
   if (!path_is_valid(path))
   {
      snprintf(msg, msg_size, "RTG: Picasso96 has no \"%s.card\" driver.", p->card);
      return false;
   }

   /* Monitor driver of the board, nothing else in DEVS:Monitors */
   path_join(path, sysdir, "Devs/Monitors");
   if (path_is_directory(path))
      remove_recurse(path);
   path_mkdir(path);
   snprintf(path, sizeof(path), "%s%sDevs%sMonitors%s%s", sysdir, DIR_SEP_STR, DIR_SEP_STR, DIR_SEP_STR, p->card);
   path_join(path2, sysdir, "P96Monitor");
   fcopy(path2, path);
   {
      char tt_board[64];
      snprintf(tt_board, sizeof(tt_board), "BoardType=%s", p->card);
      tts[0] = tt_board;
      tts[1] = "SettingsFile=SYS:Devs/Picasso96Settings";
      tts[2] = "IgnoreMask=Yes";
      /* native screen modes on the board: the whole session stays on RTG */
      tts[3] = "FakeNativeModes=Yes";
      strlcat(path, ".info", sizeof(path));
      path_join(path2, sysdir, "P96Monitor.info");
      if (!rtg_icon_tooltypes(path2, path, tts, 4))
      {
         snprintf(msg, msg_size, "RTG: cannot write the monitor driver icon.");
         return false;
      }
   }

   rtg_screenmode_prefs(p, sysdir);

   /* Startup */
   snprintf(script, sizeof(script),
         "C:MakeDir >NIL: RAM:ENV RAM:T RAM:Clipboards\n"
         "C:Assign >NIL: ENV: RAM:ENV\n"
         "C:Assign >NIL: T: RAM:T\n"
         "C:Assign >NIL: CLIPS: RAM:Clipboards\n"
         "%s"
         "IF EXISTS SYS:Prefs/Env-Archive\n"
         "   C:Assign >NIL: ENVARC: SYS:Prefs/Env-Archive\n"
         "   C:Copy >NIL: ENVARC: ENV: ALL QUIET\n"
         "ENDIF\n"
         "IF EXISTS SYS:Classes\n"
         "   C:Assign >NIL: LIBS: SYS:Classes ADD\n"
         "ENDIF\n"
         "IF EXISTS C:AddAudioModes\n"
         "   C:AddAudioModes >NIL: REFRESH QUIET\n"
         "ENDIF\n"
         "DEVS:Monitors/%s\n"
         "IF EXISTS C:IPrefs\n"
         "   C:IPrefs\n"
         "ENDIF\n"
         "C:Execute S:RTG-Game\n",
         is060
            ? "IF NOT EXISTS C:SetPatch060\n"
              "   SYS:Fixes/spatch >NIL: -oC:SetPatch060 -pSYS:Fixes/SetPatch_40_16.pch C:SetPatch\n"
              "ENDIF\n"
              "C:SetPatch060 QUIET\n"
            : "C:SetPatch QUIET\n",
         p->card);
   path_join(path, sysdir, "S/Startup-Sequence");
   rtg_write_file(path, script, strlen(script));

   /* The game, writable */
   rtg_game_dir(content, save_dir, game_path, game_path_size);
   exe_dir[0] = exe_cmd[0] = 0;
   if (!rtg_override(content, sysdir, exe_cmd, sizeof(exe_cmd)))
   {
      const char *name, *args = "";
      rtg_scan_dir(game_path, "", &best, 0);
      if (best.score < 0)
      {
         snprintf(msg, msg_size, "RTG: no executable using CyberGraphX or Picasso96 found in the game.");
         return false;
      }
      name = strrchr(best.path, '/') ? strrchr(best.path, '/') + 1 : best.path;
      for (i = 0; rtg_known_args[i].exe; i++)
         if (!strcasecmp(name, rtg_known_args[i].exe))
            args = rtg_known_args[i].args;
      /* Another World 20th Anniversary Edition: the largest version whose
       * backgrounds have been converted (game/RAW/data<size>) */
      if (!strcasecmp(name, "AnotherWorld_RTG"))
      {
         static const char *aw_sizes[][2] = {
            { "RAW/data1728x1080", "SIZE 1728x1080" },
            { "RAW/data1280x800",  "SIZE 1280x800" },
            { "RAW/data640x400",   "SIZE 640x400" },
         };
         char exedir[RETRO_PATH_MAX], d[RETRO_PATH_MAX];
         snprintf(exedir, sizeof(exedir), "%s", best.path);
         *(strrchr(exedir, '/') ? strrchr(exedir, '/') : exedir) = 0;
         for (i = 0; i < 3; i++)
         {
            snprintf(d, sizeof(d), "%s%s%s%sgame/%s", game_path, DIR_SEP_STR,
                  exedir, exedir[0] ? "/" : "", aw_sizes[i][0]);
            if (path_is_directory(d))
            {
               args = aw_sizes[i][1];
               break;
            }
         }
      }
      snprintf(exe_cmd, sizeof(exe_cmd), "\"%s\" %s", best.path, args);
      log_cb(RETRO_LOG_INFO, "RTG: game executable \"%s\" %s\n", best.path, args);
   }
   /* Run from the executable's directory (PROGDIR: data) */
   {
      char exe[RETRO_PATH_MAX], *e, *slash;
      const char *cmd = exe_cmd;
      bool quoted = *cmd == '"';
      if (quoted)
         cmd++;
      snprintf(exe, sizeof(exe), "%s", cmd);
      e = exe + strcspn(exe, quoted ? "\"" : " ");
      *e = 0;
      slash = strrchr(exe, '/');
      if (slash)
      {
         *slash = 0;
         snprintf(exe_dir, sizeof(exe_dir), "%s", exe);
      }
   }
   /* "DH0:" in front of a quoted path goes inside the quotes */
   if (exe_cmd[0] == '"')
      snprintf(script, sizeof(script), "CD \"DH0:%s\"\nStack 500000\n\"DH0:%s\n", exe_dir, exe_cmd + 1);
   else
      snprintf(script, sizeof(script), "CD \"DH0:%s\"\nStack 500000\nDH0:%s\n", exe_dir, exe_cmd);
   path_join(path, sysdir, "S/RTG-Game");
   rtg_write_file(path, script, strlen(script));
   return true;
}
