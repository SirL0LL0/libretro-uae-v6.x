#include "libretro.h"
#include "libretro-core.h"
#include "libretro-mapper.h"
#include "libretro-graph.h"

#include "retrodep/WHDLoad_files.zip.c"
#include "retrodep/WHDLoad_prefs.gz.c"

#include "sysdeps.h"
#include "uae.h"
#include "uae/types.h"
#include "options.h"
#include "devices.h"
#include "inputdevice.h"
#include "savestate.h"
#include "custom.h"
#include "drawing.h"
#include "xwin.h"
#include "disk.h"
#include "gui.h"
#include "audio.h"
#include "memory.h"
#include "autoconf.h"
#include "rommgr.h"
#include "gfxboard.h"
#include "sounddep/sound.h"

#ifndef MAX_STOP
#define MAX_STOP 30000
#endif

/* Config parse copypasta from cfgfile.c */
static const TCHAR *cscompa[] = {
   _T("-"), _T("Generic"), _T("CDTV"), _T("CDTV-CR"), _T("CD32"), _T("A500"), _T("A500+"), _T("A600"),
   _T("A1000"), _T("A1200"), _T("A2000"), _T("A3000"), _T("A3000T"), _T("A4000"), _T("A4000T"),
   _T("Velvet"), _T("Casablanca"), _T("DraCo"),
   NULL
};
static const TCHAR *csmode[] = { _T("ocs"), _T("ecs_agnus"), _T("ecs_denise"), _T("ecs"), _T("aga"), _T("a1000"), 0 };

#ifdef USE_LIBRETRO_VFS
#undef utf8_to_local_string_alloc
#define utf8_to_local_string_alloc strdup
#endif

#define CORE_OPTION_VALUE_RANGE(min, max, step, unit) \
{ \
   int value = min; \
   while (value <= max) \
   { \
      char str[6]; \
      snprintf(str, sizeof(str), "%d", value); \
      if (!option_defs_us[i].values[j].value) \
         option_defs_us[i].values[j].value = strdup(str); \
      if (unit) \
      { \
         snprintf(str, sizeof(str), "%d%s", value, unit); \
         if (!option_defs_us[i].values[j].label) \
            option_defs_us[i].values[j].label = strdup(str); \
      } \
      value += step; \
      ++j; \
   } \
} \

bool libretro_runloop_active = false;
bool libretro_frame_end = false;
unsigned short int retro_bmp[RETRO_BMP_SIZE] = {0};
unsigned int retro_bmp_offset = 0;
unsigned short int defaultw = EMULATOR_DEF_WIDTH / 2;
unsigned short int defaulth = EMULATOR_DEF_HEIGHT / 2;
unsigned short int retrow = EMULATOR_DEF_WIDTH / 2;
unsigned short int retroh = EMULATOR_DEF_HEIGHT / 2;
unsigned short int retrow_max = EMULATOR_DEF_WIDTH;
unsigned short int retroh_max = EMULATOR_DEF_HEIGHT;
unsigned short int retrow_crop = 0;
unsigned short int retroh_crop = 0;
unsigned short int retrox_crop = 0;
unsigned short int retroy_crop = 0;
float aspect_ratio = 0;

extern uae_u16 bplcon0;
extern int detected_screen_resolution;
/* The emulation's display window totals changed their unit with WinUAE 6
 * (Denise pixel counter instead of window coordinates). The frontend keeps
 * working with window coordinates, which are derived in libretro-glue.c:
 * the former "diwfirstword_total" and "diwlastword_total" are
 * retro_min_diwstart and retro_max_diwstop. */
extern int m68k_go(int may_quit, int resume);
extern void compute_vsynctime(void);
extern int nr_units(void);

unsigned int opt_model_options_display = 0;
unsigned int opt_audio_options_display = 0;
unsigned int opt_video_options_display = 0;
unsigned int opt_mapping_options_display = 1;
char opt_model[10] = {0};
char opt_model_fd[10] = {0};
char opt_model_hd[10] = {0};
char opt_model_cd[10] = {0};
char opt_kickstart[64] = {0};
static char opt_keyboard_type[32] = "uae";
static void retro_config_keyboard(void);
static char opt_cpuboard[32] = "none";
static int opt_cpuboard_mem = -1;
static void retro_config_cpuboard(void);

struct preset_options
{
   int cpu_model;
   int fpu_model;
   int mmu_model;
   int chipmem_size;
   int bogomem_size;
   int fastmem_size;
   int z3mem_size;
   int chipset;
   int cs_compatible;
   int address_space_24;
};

static struct preset_options override_opt;
static struct preset_options preset_opt;

static bool opt_region_auto = true;
static char opt_video_resolution_auto = RESOLUTION_AUTO_NONE;
static bool opt_video_vresolution_auto = false;
bool opt_floppy_sound_empty_mute = false;
bool opt_floppy_multidrive = false;
bool opt_floppy_write_redirect = false;
uint8_t opt_autoloadfastforward = 0;
uint8_t opt_use_whdload = 1;
uint8_t opt_use_whdload_theme = 0;
uint8_t opt_use_whdload_prefs = 0;
bool opt_use_whdload_nowritecache = false;
bool opt_use_whdload_buttonwait = true;
uint8_t opt_use_boot_hd = 0;
bool opt_shared_nvram = false;
bool opt_cd_startup_delayed_insert = false;
int opt_statusbar = 0;
int opt_statusbar_position = 0;
int opt_statusbar_position_old = 0;
int opt_statusbar_position_offset = 0;
unsigned int opt_vkbd_theme = 0;
libretro_graph_alpha_t opt_vkbd_alpha = GRAPH_ALPHA_75;
libretro_graph_alpha_t opt_vkbd_dim_alpha = GRAPH_ALPHA_25;
bool opt_keyrah_keypad = false;
bool opt_keyboard_pass_through = false;
unsigned int opt_physicalmouse = 1;
int opt_joyport_pointer_color = -1;
unsigned int opt_dpadmouse_speed = 6;
unsigned int opt_analogmouse = 0;
unsigned int opt_analogmouse_deadzone = 20;
float opt_analogmouse_speed_left = 1.0;
float opt_analogmouse_speed_right = 1.0;
unsigned int opt_cd32pad_options = RETROPAD_OPTIONS_DISABLED;
unsigned int opt_retropad_options = RETROPAD_OPTIONS_DISABLED;
char opt_joyport_order[5] = "1234";

#if defined(NATMEM_OFFSET)
extern uae_u8 *natmem_offset;
extern uae_u32 natmem_size;
#endif

char full_path[RETRO_PATH_MAX] = {0};
char info_path[RETRO_PATH_MAX] = {0};
static char *uae_argv[] = { "puae" };
static int restart_pending = 0;
static struct puae_core_option_info puae_carts[RETRO_NUM_CORE_OPTION_VALUES_MAX] = {0};
static struct puae_core_option_info puae_fmvs[RETRO_NUM_CORE_OPTION_VALUES_MAX] = {0};
static char puae_picassoiv_rom[RETRO_PATH_MAX] = {0};
static struct puae_core_option_info puae_kickstarts[RETRO_NUM_CORE_OPTION_VALUES_MAX] = {0};

static long retro_now = 0;
float retro_refresh = 0;

bool retro_message = false;
char retro_message_msg[1024] = {0};
bool retro_statusbar = false;

extern bool retro_mousemode;
extern bool mousemode_locked;
extern bool retro_vkbd;
extern void print_vkbd(void);

extern bool retro_turbo_fire;
extern bool turbo_fire_locked;
extern unsigned int turbo_fire_button;
extern unsigned int turbo_pulse;
extern bool inputdevice_finalized;
uint8_t pix_bytes = 2;
static bool pix_bytes_initialized = false;
static bool cpu_cycle_exact_force = false;
#define SOUND_FILTER_TYPE_UPDATE_TIMER 2
static unsigned char automatic_sound_filter_type_update_timer = SOUND_FILTER_TYPE_UPDATE_TIMER;
static bool fake_ntsc = false;
static bool real_ntsc = false;
static signed char forced_video = -1;
static bool locked_video_horizontal = false;
bool video_productivity = false;
bool request_update_av_info = false;
bool retro_av_info_change_timing = false;
bool retro_av_info_change_geometry = true;
bool retro_av_info_is_ntsc = false;
bool retro_av_info_is_lace = false;
bool request_reset_drawing = false;
bool request_reset_soft = false;
bool request_reset_hard = false;
static unsigned char request_init_custom_timer = 0;
unsigned char crop_id = 0;
unsigned char opt_crop_id = 0;
unsigned char crop_mode_id = 0;
static bool crop_delay = true;
unsigned char width_multiplier = 1;

static int opt_vertical_offset = 0;
static bool opt_vertical_offset_auto = true;
extern int retro_minfirstline;
static int retro_thisframe_counter = 0;
extern int retro_thisframe_first_drawn_line;
static int retro_thisframe_first_drawn_line_old = -1;
static int retro_thisframe_first_drawn_line_start = -1;
extern int retro_thisframe_last_drawn_line;
static int retro_thisframe_last_drawn_line_old = -1;
static int retro_thisframe_last_drawn_line_start = -1;
extern int retro_thisframe_y_adjust;
static int retro_thisframe_y_adjust_old = -1;

static int opt_horizontal_offset = 0;
static bool opt_horizontal_offset_auto = true;
static int retro_max_diwlastword_hires = 938;
static int retro_max_diwlastword = 938;
extern int retro_min_diwstart;
static int retro_min_diwstart_old = -1;
extern int retro_max_diwstop;
static int retro_max_diwstop_old = -1;
static int retro_diwstartstop_counter = 0;
extern int retro_visible_left_border;
static int retro_visible_left_border_old = 0;

/* The window is cut out of the emulation's raster (libretro-glue.c) */
extern void retro_video_blit(void);
#ifdef WITH_MPEG2
extern int cd32_fmv_active;
#endif

#define PAL_KS2_CROP_SAFE_FIRST_LINE  99
#define PAL_KS2_CROP_SAFE_LAST_LINE   244
#define NTSC_KS2_CROP_SAFE_FIRST_LINE 71
#define NTSC_KS2_CROP_SAFE_LAST_LINE  216

unsigned short int video_config = 0;
unsigned short int video_config_old = 0;
unsigned short int video_config_aspect = 0;
unsigned short int video_config_geometry = 0;
uint8_t video_config_allow_hz_change = 0;
bool opt_aspect_ratio_locked = false;

struct zfile *retro_deserialize_file = NULL;
static size_t save_state_file_size = 0;
static void retro_update_memory_info(void);

unsigned int retro_devices[RETRO_DEVICES] = {0};
extern void display_current_image(const char *image, bool inserted);

retro_log_printf_t log_cb = NULL;
static retro_set_led_state_t led_state_cb = NULL;
static retro_video_refresh_t video_cb = NULL;
static retro_audio_sample_t audio_cb = NULL;
static retro_audio_sample_batch_t audio_batch_cb = NULL;
static retro_environment_t environ_cb = NULL;

retro_input_state_t input_state_cb = NULL;
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

static retro_input_poll_t input_poll_cb = NULL;
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }

static struct retro_perf_callback perf_cb;

bool libretro_supports_bitmasks = false;
static bool libretro_supports_ff_override = false;
bool libretro_ff_enabled = false;
static bool libretro_supports_option_categories = false;
#define HAVE_NO_LANGEXTRA

char retro_save_directory[RETRO_PATH_MAX] = {0};
char retro_temp_directory[RETRO_PATH_MAX] = {0};
char retro_system_directory[RETRO_PATH_MAX] = {0};
char retro_system_data_directory[RETRO_PATH_MAX] = {0};
static char retro_content_directory[RETRO_PATH_MAX] = {0};

/* Disk Control context */
dc_storage *dc = NULL;

/* Configs */
#define UAE_CONFIG_SIZE 32768
static char uae_preset[20] = {0};
static char uae_kickstart[RETRO_PATH_MAX] = {0};
static char uae_kickstart_ext[RETRO_PATH_MAX] = {0};
static char uae_config[4096] = {0};
static char uae_model_config[1024] = {0};
static char uae_preset_config[2048] = {0};
static char uae_custom_config[2048] = {0};
char uae_full_config[UAE_CONFIG_SIZE] = {0};

char *retro_get_uae_full_config(void)
{
   return uae_full_config;
}

/* Audio output buffer */
static struct {
   int16_t *data;
   int32_t size;
   int32_t capacity;
} output_audio_buffer = {NULL, 0, 0};

/* FPS counter + mapper tick */
long retro_ticks(void)
{
   if (!perf_cb.get_time_usec)
      return retro_now;

   return perf_cb.get_time_usec();
}

static unsigned int retro_led_state[RETRO_LED_NUM] = {0};
static void retro_led_interface(void)
{
   /* 0: Power
    * 1: Floppy drives
    * 2: HD/CD/MD (NVRAM)
    * 3: DF0
    * 4: DF1
    * 5: DF2
    * 6: DF3
    * 7: HD
    * 8: CD/MD (NVRAM) */

   unsigned int led_state[RETRO_LED_NUM] = {0};
   unsigned int l                        = 0;
   unsigned int i                        = 0;

   led_state[RETRO_LED_POWER] = gui_data.powerled;

   /* Floppy drives */
   for (i = 0; i < MAX_FLOPPY_DRIVES; i++)
   {
      if (gui_data.drives[i].df[0])
      {
         if (!led_state[RETRO_LED_DRIVES])
            led_state[RETRO_LED_DRIVES] = gui_data.drives[i].drive_motor;

         switch (i)
         {
            case 0:
               led_state[RETRO_LED_DRIVE0] = gui_data.drives[i].drive_motor;
               break;
            case 1:
               led_state[RETRO_LED_DRIVE1] = gui_data.drives[i].drive_motor;
               break;
            case 2:
               led_state[RETRO_LED_DRIVE2] = gui_data.drives[i].drive_motor;
               break;
            case 3:
               led_state[RETRO_LED_DRIVE3] = gui_data.drives[i].drive_motor;
               break;
         }
      }
   }

   /* HD */
   if (gui_data.hd >= 0)
   {
      led_state[RETRO_LED_HD] = gui_data.hd;
      if (!led_state[RETRO_LED_HDCDMD])
         led_state[RETRO_LED_HDCDMD] = gui_data.hd;
   }

   /* CD */
   if (gui_data.cd >= 0)
   {
      led_state[RETRO_LED_CDMD] = gui_data.cd & (LED_CD_ACTIVE | LED_CD_AUDIO);
      if (!led_state[RETRO_LED_HDCDMD])
         led_state[RETRO_LED_HDCDMD] = gui_data.cd & (LED_CD_ACTIVE | LED_CD_AUDIO);
   }

   /* MD (NVRAM) */
   if (gui_data.md >= 0)
   {
      led_state[RETRO_LED_CDMD] = gui_data.md;
      if (!led_state[RETRO_LED_HDCDMD])
         led_state[RETRO_LED_HDCDMD] = gui_data.md;
   }

   for (l = 0; l < RETRO_LED_NUM; l++)
   {
      if (retro_led_state[l] != led_state[l])
      {
         retro_led_state[l] = led_state[l];
         led_state_cb(l, led_state[l]);
      }
   }
}

void retro_fastforwarding(bool enabled)
{
   struct retro_fastforwarding_override ff_override;
   bool frontend_ff_enabled = false;

   if (!libretro_supports_ff_override)
      return;

   environ_cb(RETRO_ENVIRONMENT_GET_FASTFORWARDING, &frontend_ff_enabled);
   if (enabled && frontend_ff_enabled)
      return;

   ff_override.ratio          = -1;
   ff_override.fastforward    = enabled;
   ff_override.inhibit_toggle = enabled;
   libretro_ff_enabled        = enabled;

   environ_cb(RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE, &ff_override);
}

static bool paula_playing(void)
{
   if (paula_sndbuffer[0])
   {
      for (unsigned i = 2; i < 20; i++)
      {
         int target = (i % 2 == 0) ? 0 : 1;
         if (paula_sndbuffer[i] != paula_sndbuffer[target])
            return true;
      }
   }
   return false;
}

static int ff_counter_on    = 0;
static int ff_counter_off   = 0;
static int ff_counter_stuck = 0;
static int ff_counter_audio = 0;
static int drive_track_prev = 0;
static void retro_autoloadfastforwarding(void)
{
   if (!libretro_supports_ff_override)
      return;

   if (mapper_keys_pressed_time)
      return;

   if (opt_autoloadfastforward & AUTOLOADFASTFORWARD_FD && (gui_data.drives[0].df[0] || gui_data.drives[1].df[0]))
   {
      int ff             = -1;
      int drive_led      = retro_led_state[RETRO_LED_DRIVES];
      int drive_track    = gui_data.drives[0].drive_track;
      bool audio_playing = paula_playing();

      if (gui_data.drives[1].drive_motor)
         drive_track = gui_data.drives[1].drive_track;
      if (gui_data.drives[2].drive_motor)
         drive_track = gui_data.drives[2].drive_track;
      if (gui_data.drives[3].drive_motor)
         drive_track = gui_data.drives[3].drive_track;

      /* No FF when audio is playing */
      if (libretro_ff_enabled && audio_playing)
      {
         ff_counter_audio++;
         if (ff_counter_audio > 1)
            ff = 3;
      }
      else if ((drive_track == drive_track_prev) && drive_led && libretro_ff_enabled)
      {
         ff_counter_on = ff_counter_off = ff_counter_audio = 0;
         ff_counter_stuck++;
         if (ff_counter_stuck > 59)
            ff = 2;
      }
      else if ((drive_track == drive_track_prev) && !drive_led && libretro_ff_enabled)
      {
         ff_counter_on = ff_counter_stuck = ff_counter_audio = 0;
         ff_counter_off++;
         if (ff_counter_off > 29)
            ff = 0;
      }
      else if ((drive_track != drive_track_prev || ff_counter_on) && drive_led && !libretro_ff_enabled && !audio_playing)
      {
         ff_counter_off = ff_counter_stuck = ff_counter_audio = 0;
         ff_counter_on++;
         if (ff_counter_on > 9)
            ff = 1;
      }
      else
      {
         ff_counter_on = ff_counter_off = ff_counter_stuck = ff_counter_audio = 0;
         ff = -2;
      }

      if (ff > -1)
         retro_fastforwarding((ff > 1) ? false : (ff) ? true : false);
#if 0
      if (ff > -1)
         printf("FD FF:%2d track:%3d prev:%3d led:%d - on:%3d off:%3d stuck:%3d audio:%3d - playing:%d\n",
               ff, drive_track, drive_track_prev, drive_led,
               ff_counter_on, ff_counter_off, ff_counter_stuck, ff_counter_audio, audio_playing);
#endif
      drive_track_prev = drive_track;
   }

   if (((opt_autoloadfastforward & AUTOLOADFASTFORWARD_CD && gui_data.cd >= 0) ||
       (opt_autoloadfastforward & AUTOLOADFASTFORWARD_HD && gui_data.hd >= 0)) &&
       !gui_data.drives[0].df[0] && !gui_data.drives[1].df[0])
   {
      int ff             = -1;
      int drive_led      = 0;
      bool audio_playing = paula_playing();

      if (opt_autoloadfastforward & AUTOLOADFASTFORWARD_CD && gui_data.cd >= 0)
      {
         drive_led = gui_data.cd;
         if (drive_led & LED_CD_AUDIO)
            drive_led = 0;
         if (!drive_led && gui_data.md >= 0)
            drive_led = gui_data.md;
      }
      if (opt_autoloadfastforward & AUTOLOADFASTFORWARD_HD && gui_data.hd >= 0)
      {
         drive_led = gui_data.hd;
      }

      /* No FF when audio is playing */
      if (libretro_ff_enabled && audio_playing)
      {
         ff_counter_on = ff_counter_off = 0;
         ff_counter_audio++;
         if (ff_counter_audio > 1)
            ff = 3;
      }
      else if (!drive_led && libretro_ff_enabled)
      {
         ff_counter_on = ff_counter_audio = 0;
         ff_counter_off++;
         if (ff_counter_off > 19)
            ff = 0;
      }
      else if (drive_led && !libretro_ff_enabled && !audio_playing)
      {
         ff_counter_off = ff_counter_audio = 0;
         ff_counter_on++;
         if (ff_counter_on > 3)
            ff = 1;
      }
      else
      {
         ff_counter_on = ff_counter_off = ff_counter_audio = 0;
         ff = -2;
      }

      if (ff > -1)
         retro_fastforwarding((ff > 1) ? false : (ff) ? true : false);
#if 0
      if (ff > -1)
         printf("HD/CD FF:%2d led:%d - on:%3d off:%3d audio:%3d - playing:%d\n",
               ff, drive_led,
               ff_counter_on, ff_counter_off, ff_counter_audio, audio_playing);
#endif
   }
}

static void ensure_output_audio_buffer_capacity(int32_t capacity)
{
   if (capacity <= output_audio_buffer.capacity) {
      return;
   }

   output_audio_buffer.data = realloc(output_audio_buffer.data, capacity * sizeof(*output_audio_buffer.data));
   output_audio_buffer.capacity = capacity;
   log_cb(RETRO_LOG_DEBUG, "Output audio buffer capacity set to %d\n", capacity);
}

static void init_output_audio_buffer(int32_t capacity)
{
   output_audio_buffer.data = NULL;
   output_audio_buffer.size = 0;
   output_audio_buffer.capacity = 0;
   ensure_output_audio_buffer_capacity(capacity);
}

static void free_output_audio_buffer()
{
   free(output_audio_buffer.data);
   output_audio_buffer.data = NULL;
   output_audio_buffer.size = 0;
   output_audio_buffer.capacity = 0;
}

static void upload_output_audio_buffer()
{
   audio_batch_cb(output_audio_buffer.data, output_audio_buffer.size / 2);
   output_audio_buffer.size = 0;
}

static void floppy_open_redirect(int8_t drive)
{
   /* Force check for redirected save disks and uncompress found gzs */
   if (opt_floppy_write_redirect)
   {
      uint8_t i;
      uint8_t drive_i    = 0;
      uint8_t max_drives = MAX_FLOPPY_DRIVES;

      if (drive > -1)
      {
         drive_i    = drive;
         max_drives = drive + 1;
      }

      for (i = drive_i; i < max_drives; i++)
      {
         if (!string_is_empty(changed_prefs.floppyslots[i].df))
         {
            const char *saveimagepath = DISK_get_saveimagepath(changed_prefs.floppyslots[i].df, -2);
            char parent_path[RETRO_PATH_MAX];

            strlcpy(parent_path, changed_prefs.floppyslots[i].df, sizeof(parent_path));
            path_parent_dir(parent_path, strlen(parent_path));

            if (parent_path[strlen(parent_path)-1] == DIR_SEP_CHR)
               parent_path[strlen(parent_path)-1] = '\0';

            if (string_is_equal(parent_path, retro_save_directory))
               continue;

            if (!string_is_empty(saveimagepath) && !path_is_valid(saveimagepath))
            {
               char gz_saveimagepath[RETRO_PATH_MAX];
               snprintf(gz_saveimagepath, sizeof(gz_saveimagepath), "%s%s",
                     saveimagepath, ".gz");

               if (path_is_valid(gz_saveimagepath))
                  gz_uncompress(gz_saveimagepath, saveimagepath);
            }

            disk_setwriteprotect(&currprefs, i, changed_prefs.floppyslots[i].df, 0);
            DISK_reinsert(i);
         }
      }
   }
}

static void floppy_close_redirect(int8_t drive)
{
   /* Force check for redirected save disks to remove unsaved */
   {
      uint8_t i;
      uint8_t drive_i    = 0;
      uint8_t max_drives = MAX_FLOPPY_DRIVES;

      if (drive > -1)
      {
         drive_i    = drive;
         max_drives = drive + 1;
      }

      for (i = drive_i; i < max_drives; i++)
      {
         if (!string_is_empty(changed_prefs.floppyslots[i].df))
         {
            const char *saveimagepath = DISK_get_saveimagepath(changed_prefs.floppyslots[i].df, -2);
            char parent_path[RETRO_PATH_MAX];

            strlcpy(parent_path, changed_prefs.floppyslots[i].df, sizeof(parent_path));
            path_parent_dir(parent_path, strlen(parent_path));

            if (parent_path[strlen(parent_path)-1] == DIR_SEP_CHR)
               parent_path[strlen(parent_path)-1] = '\0';

            if (string_is_equal(parent_path, retro_save_directory))
               continue;

            disk_setwriteprotect(&currprefs, i, changed_prefs.floppyslots[i].df, 1);
            disk_eject(i);

            if (!string_is_empty(saveimagepath) && path_is_valid(saveimagepath))
            {
               char gz_saveimagepath[RETRO_PATH_MAX];
               snprintf(gz_saveimagepath, sizeof(gz_saveimagepath), "%s%s",
                     saveimagepath, ".gz");

               gz_compress(saveimagepath, gz_saveimagepath);
               if (path_is_valid(gz_saveimagepath))
                  retro_remove(saveimagepath);
            }
         }
      }
   }
}

static bool is_hdf_rdb(const char *path)
{
   /* Detect RDB */
   bool hdf_rdb = false;
   FILE *hdf_fp;
   char filebuf[4];
   if ((hdf_fp = fopen(path, "r")))
   {
      fgets(filebuf, sizeof(filebuf), hdf_fp);
      fclose(hdf_fp);

      if (!strcmp(filebuf, "RDS"))
         hdf_rdb = true;
   }
   return hdf_rdb;
}

static void retro_set_paths(void)
{
   const char *system_dir = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) && system_dir)
   {
      strlcpy(retro_system_directory,
              system_dir,
              sizeof(retro_system_directory));
   }

   const char *content_dir = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY, &content_dir) && content_dir)
   {
      strlcpy(retro_content_directory,
              content_dir,
              sizeof(retro_content_directory));
   }

   const char *save_dir = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir) && save_dir)
   {
      /* If save directory is defined use it, otherwise use system directory */
      strlcpy(retro_save_directory,
              string_is_empty(save_dir) ? retro_system_directory : save_dir,
              sizeof(retro_save_directory));
   }
   else if (!retro_save_directory)
   {
      /* Make retro_save_directory the same in case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY is not implemented by the frontend */
      strlcpy(retro_save_directory,
              retro_system_directory,
              sizeof(retro_save_directory));
   }

   /* Remove ending slash created by 'savefiles_in_content_dir' */
   if (retro_save_directory[strlen(retro_save_directory)-1] == DIR_SEP_CHR)
      retro_save_directory[strlen(retro_save_directory)-1] = '\0';

   /* Hack: Remove one-letter subdirectories from save path,
    * to prevent multiple WHDLoad images */
   if (retro_save_directory[strlen(retro_save_directory)-2] == DIR_SEP_CHR)
      retro_save_directory[strlen(retro_save_directory)-2] = '\0';

   /* Temp directory for ZIPs */
   snprintf(retro_temp_directory, sizeof(retro_temp_directory), "%s%s%s", retro_save_directory, DIR_SEP_STR, "TEMP");

   /* Use system directory for data files such as driveclick and cartridges */
   snprintf(retro_system_data_directory, sizeof(retro_system_data_directory), "%s%s%s",
         retro_system_directory, DIR_SEP_STR, "uae_data");
   if (!path_is_directory(retro_system_data_directory))
      snprintf(retro_system_data_directory, sizeof(retro_system_data_directory), "%s%s%s",
            retro_system_directory, DIR_SEP_STR, "uae");
}

static void free_puae_core_options(void)
{
   size_t i;
   for (i = 0; i < RETRO_NUM_CORE_OPTION_VALUES_MAX; i++)
   {
      free(puae_carts[i].value);
      puae_carts[i].value = NULL;

      free(puae_carts[i].label);
      puae_carts[i].label = NULL;

      free(puae_fmvs[i].value);
      puae_fmvs[i].value = NULL;

      free(puae_fmvs[i].label);
      puae_fmvs[i].label = NULL;

      free(puae_kickstarts[i].value);
      puae_kickstarts[i].value = NULL;

      free(puae_kickstarts[i].label);
      puae_kickstarts[i].label = NULL;
   }
}

/* ROM type of a file, from WinUAE's ROM database (by contents, so file
 * names don't matter); 0 when unknown */
static uae_u32 puae_rom_type(const char *path)
{
   struct romdata *rd = NULL;
   uae_u8 *buf;
   long size;
   FILE *f = fopen(path, "rb");

   if (!f)
      return 0;
   fseek(f, 0, SEEK_END);
   size = ftell(f);
   fseek(f, 0, SEEK_SET);
   /* Cartridges and board ROMs are 8 KB - 512 KB */
   if (size >= 8192 && size <= 524288 && (buf = (uae_u8 *)malloc(size)))
   {
      if (fread(buf, 1, size, f) == (size_t)size)
         rd = getromdatabydata(buf, (int)size);
      free(buf);
   }
   fclose(f);
   return rd ? rd->type : 0;
}

/* Add to an option the ROMs in the system data directory whose type is in
 * the 'group' ROM type group */
static void puae_scan_roms(struct retro_core_option_v2_definition *def, int *j,
      struct puae_core_option_info *store, uae_u32 group)
{
   RDIR *dir;

   if (!path_is_directory(retro_system_data_directory))
      return;
   if (!(dir = retro_opendir(retro_system_data_directory)))
      return;

   while (retro_readdir(dir) && *j < RETRO_NUM_CORE_OPTION_VALUES_MAX - 1)
   {
      const char *name = retro_dirent_get_name(dir);
      char path[RETRO_PATH_MAX] = {0};
      char label[128]           = {0};

      if (!strendswith(name, ".rom") && !strendswith(name, ".bin"))
         continue;

      snprintf(path, sizeof(path), "%s%s%s", retro_system_data_directory, DIR_SEP_STR, name);
      if ((puae_rom_type(path) & ROMTYPE_GROUP_MASK) != group)
         continue;

      snprintf(label, sizeof(label), "%s", name);
      path_remove_extension(label);

      store[*j].value = strdup(name);
      store[*j].label = strdup(label);
      def->values[*j].value = store[*j].value;
      def->values[*j].label = store[*j].label;
      ++*j;
   }
   retro_closedir(dir);
}

/* Full path of the first ROM of exactly this type in the system data
 * directory, or an empty string */
static void puae_find_rom(uae_u32 type, char *out, size_t outsize)
{
   RDIR *dir;

   out[0] = 0;
   if (!path_is_directory(retro_system_data_directory))
      return;
   if (!(dir = retro_opendir(retro_system_data_directory)))
      return;
   while (retro_readdir(dir))
   {
      const char *name = retro_dirent_get_name(dir);
      char path[RETRO_PATH_MAX] = {0};

      if (!strendswith(name, ".rom") && !strendswith(name, ".bin"))
         continue;
      snprintf(path, sizeof(path), "%s%s%s", retro_system_data_directory, DIR_SEP_STR, name);
      if ((puae_rom_type(path) & ROMTYPE_MASK) == type)
      {
         snprintf(out, outsize, "%s", path);
         break;
      }
   }
   retro_closedir(dir);
}

static void retro_set_core_options()
{
   /* Core categories */
   static struct retro_core_option_v2_category option_cats_us[] =
   {
      {
         "machine",
         "Machine",
         "Model and presets, Kickstart, chipset, video standard, clock."
      },
      {
         "cpu",
         "CPU",
         "Processor, FPU, compatibility and speed."
      },
      {
         "memory",
         "Memory",
         "Chip, Slow and Fast RAM."
      },
      {
         "expansion",
         "Expansions",
         "Accelerator boards, cartridges, keyboard."
      },
      {
         "rtg",
         "Graphics Card",
         "Picasso96 (uaegfx) and graphics boards."
      },
      {
         "fmv",
         "CD32 FMV",
         "Full Motion Video cartridge for the CD32."
      },
      {
         "chipset",
         "Chipset",
         "Emulation accuracy of blitter, collisions and drawing."
      },
      {
         "media",
         "Media",
         "Floppy disks, hard drives, WHDLoad and compact discs."
      },
      {
         "video",
         "Video",
         "Configure video options."
      },
      {
         "audio",
         "Audio",
         "Configure audio options."
      },
      {
         "input",
         "Input",
         "Configure input options."
      },
      {
         "hotkey",
         "Hotkey Mapping",
         "Configure keyboard hotkey mapping options."
      },
      {
         "retropad",
         "RetroPad Mapping",
         "Configure RetroPad mapping options."
      },
      {
         "osd",
         "On-Screen Display",
         "Configure OSD options."
      },
      { NULL, NULL, NULL },
   };

   /* Core options */
   static struct retro_core_option_v2_definition option_defs_us[] =
   {
      {
         "puae_model",
         "Machine > Model",
         "Model",
         "'Automatic' defaults to 'A500' with floppy disks, 'A1200' with hard drives and 'CD32' with compact discs. 'Automatic' can be overridden with file path tags.\nCore restart required.",
         NULL,
         "machine",
         {
            { "auto", "Automatic" },
            { "A1000", "A1000 (v1.3, 0.5M Chip)" },
            { "A500OG", "A500 (v1.3, 0.5M Chip)" },
            { "A500", "A500 (v1.3, 0.5M Chip + 0.5M Slow)" },
            { "A500ECS", "A500 (v1.3, 1.0M Chip)" },
            { "A500PLUS", "A500+ (v2.04, 1M Chip)" },
            { "A600OG", "A600 (v3.1, 2M Chip)" },
            { "A600", "A600 (v3.1, 2M Chip + 8M Fast)" },
            { "A1200OG", "A1200 (v3.1, 2M Chip)" },
            { "A1200", "A1200 (v3.1, 2M Chip + 8M Fast)" },
            { "A2000OG", "A2000 (v1.3, 0.5M Chip + 0.5M Slow)" },
            { "A2000", "A2000 (v3.1, 1M Chip)" },
            { "A3000", "A3000 (v3.1, 2M Chip + 8M Fast, 68030)" },
            { "A4030", "A4000/030 (v3.1, 2M Chip + 8M Fast)" },
            { "A4040", "A4000/040 (v3.1, 2M Chip + 8M Fast)" },
            { "A4060", "A4000/060 (v3.1, 2M Chip + 8M Fast)" },
            { "A4000T", "A4000T (v3.1, 2M Chip + 8M Fast, 68040)" },
            { "CDTV", "CDTV (1M Chip)" },
            { "CDTVCR", "CDTV-CR (v2.05, 1M Chip)" },
            { "CD32", "CD32 (2M Chip)" },
            { "CD32FR", "CD32 (2M Chip + 8M Fast)" },
            { NULL, NULL },
         },
         "auto"
      },
      {
         "puae_model_options_display",
         "Machine > Show Automatic Model Options",
         "Show Automatic Model Options",
         "Show/hide default model options (Floppy/HD/CD) for 'Automatic' model.",
         NULL,
         "machine",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_model_fd",
         "Model > Automatic Floppy",
         "Automatic Floppy",
         "Default model when floppies are launched with 'Automatic' model.\nCore restart required.",
         NULL,
         "machine",
         {
            { "A1000", "A1000 (v1.3, 0.5M Chip)" },
            { "A500OG", "A500 (v1.3, 0.5M Chip)" },
            { "A500", "A500 (v1.3, 0.5M Chip + 0.5M Slow)" },
            { "A500ECS", "A500 (v1.3, 1.0M Chip)" },
            { "A500PLUS", "A500+ (v2.04, 1M Chip)" },
            { "A600OG", "A600 (v3.1, 2M Chip)" },
            { "A600", "A600 (v3.1, 2M Chip + 8M Fast)" },
            { "A1200OG", "A1200 (v3.1, 2M Chip)" },
            { "A1200", "A1200 (v3.1, 2M Chip + 8M Fast)" },
            { "A2000OG", "A2000 (v1.3, 0.5M Chip + 0.5M Slow)" },
            { "A2000", "A2000 (v3.1, 1M Chip)" },
            { "A3000", "A3000 (v3.1, 2M Chip + 8M Fast, 68030)" },
            { "A4030", "A4000/030 (v3.1, 2M Chip + 8M Fast)" },
            { "A4040", "A4000/040 (v3.1, 2M Chip + 8M Fast)" },
            { NULL, NULL },
         },
         "A500"
      },
      {
         "puae_model_hd",
         "Model > Automatic HD",
         "Automatic HD",
         "Default model when HD interface is used with 'Automatic' model. Affects WHDLoad installs and other hard drive images.\nCore restart required.",
         NULL,
         "machine",
         {
            { "A600OG", "A600 (v3.1, 2M Chip)" },
            { "A600", "A600 (v3.1, 2M Chip + 8M Fast)" },
            { "A1200OG", "A1200 (v3.1, 2M Chip)" },
            { "A1200", "A1200 (v3.1, 2M Chip + 8M Fast)" },
            { "A2000", "A2000 (v3.1, 1M Chip)" },
            { "A3000", "A3000 (v3.1, 2M Chip + 8M Fast, 68030)" },
            { "A4030", "A4000/030 (v3.1, 2M Chip + 8M Fast)" },
            { "A4040", "A4000/040 (v3.1, 2M Chip + 8M Fast)" },
            { "A4060", "A4000/060 (v3.1, 2M Chip + 8M Fast)" },
            { "A4000T", "A4000T (v3.1, 2M Chip + 8M Fast, 68040)" },
            { NULL, NULL },
         },
         "A1200"
      },
      {
         "puae_model_cd",
         "Model > Automatic CD",
         "Automatic CD",
         "Default model when compact discs are launched with 'Automatic' model.\nCore restart required.",
         NULL,
         "machine",
         {
            { "CDTV", "CDTV (1M Chip)" },
            { "CDTVCR", "CDTV-CR (v2.05, 1M Chip)" },
            { "CD32", "CD32 (2M Chip)" },
            { "CD32FR", "CD32 (2M Chip + 8M Fast)" },
            { NULL, NULL },
         },
         "CD32"
      },
      {
         "puae_kickstart",
         "Machine > Kickstart ROM",
         "Kickstart ROM",
         "Kickstart ROMs are searched from 'system'.\nCore reset required.",
         NULL,
         "machine",
         {
            /* Options filled dynamically later */
            { NULL, NULL },
         },
         NULL
      },
      {
         "puae_chipset",
         "Machine > Chipset",
         "Chipset",
         "Overrides the chipset of the model, e.g. an A500 with ECS Denise for productivity screen modes.\nCore restart required.",
         NULL,
         "machine",
         {
            { "auto", "Automatic" },
            { "ocs", "OCS" },
            { "ecs_agnus", "ECS Agnus" },
            { "ecs_denise", "ECS Denise" },
            { "ecs", "Full ECS" },
            { "aga", "AGA" },
            { NULL, NULL },
         },
         "auto"
      },
      {
         "puae_video_standard",
         "Machine > Video Standard",
         "Video Standard",
         "Output Hz & height:\n- 'PAL': 50Hz - 288px / 576px\n- 'NTSC': 60Hz - 240px / 480px\n- 'Automatic' switches region per file path tags.",
         NULL,
         "machine",
         {
            { "PAL auto", "Automatic PAL" },
            { "NTSC auto", "Automatic NTSC" },
            { "PAL", NULL },
            { "NTSC", NULL },
            { NULL, NULL },
         },
         "PAL auto"
      },
      {
         "puae_rtc",
         "Machine > Real-Time Clock",
         "Real-Time Clock",
         "Battery backed clock, set from the host time. 'Automatic' follows the model: MSM6242B on most models, RP5C01A on A3000/A4000. 'None' for programs that misbehave with a clock.\nCore restart required.",
         NULL,
         "machine",
         {
            { "auto", "Automatic" },
            { "none", "None" },
            { "MSM6242B", "MSM6242B (A500 trapdoor, A2000)" },
            { "RP5C01A", "RP5C01A (A3000, A4000)" },
            { "MSM6242B_A2000", "MSM6242B (A2000 mainboard)" },
            { NULL, NULL },
         },
         "auto"
      },
      {
         "puae_cpu_model",
         "CPU > CPU Model",
         "CPU Model",
         "'Automatic' defaults to the current preset model.\nCore reset required.",
         NULL,
         "cpu",
         {
            { "auto", "Automatic" },
            { "68000", NULL },
            { "68010", NULL },
            { "68020", NULL },
            { "68030", NULL },
            { "68040", NULL },
            { "68060", NULL },
            { NULL, NULL },
         },
         "auto"
      },
      {
         "puae_fpu_model",
         "CPU > FPU Model",
         "FPU Model",
         "'Automatic' defaults to the current preset model.\nCore reset required.",
         NULL,
         "cpu",
         {
            { "auto", "Automatic" },
            { "0", "None" },
            { "68881", NULL },
            { "68882", NULL },
            { "cpu", "CPU internal" },
            { NULL, NULL },
         },
         "auto"
      },
      {
         "puae_cpu_compatibility",
         "CPU > CPU Compatibility",
         "CPU Compatibility",
         "Some games have graphic and/or speed issues without 'Cycle-exact'. 'Cycle-exact' can be forced with '(CE)' file path tag. (DMA/Memory) is forced to (Full) with 68000.",
         NULL,
         "cpu",
         {
            { "normal", "Normal" },
            { "compatible", "More compatible" },
            { "memory", "Cycle-exact (DMA/Memory)" },
            { "exact", "Cycle-exact (Full)" },
            { NULL, NULL },
         },
#if defined(__x86_64__)
         "exact"
#else
         "normal"
#endif
      },
      {
         "puae_cpu_throttle",
         "CPU > CPU Speed",
         "CPU Speed",
         "Needs 'CPU Compatibility' set to 'Normal' or 'More compatible' (ignored with 'Cycle-exact'). 'Max' runs the CPU as fast as the PC allows, using the time left in each frame.",
         NULL,
         "cpu",
         {
            { "-900.0", "-90%" },
            { "-800.0", "-80%" },
            { "-700.0", "-70%" },
            { "-600.0", "-60%" },
            { "-500.0", "-50%" },
            { "-400.0", "-40%" },
            { "-300.0", "-30%" },
            { "-200.0", "-20%" },
            { "-100.0", "-10%" },
            { "0.0", "Default" },
            { "1000.0", "+100%" },
            { "2000.0", "+200%" },
            { "3000.0", "+300%" },
            { "4000.0", "+400%" },
            { "5000.0", "+500%" },
            { "6000.0", "+600%" },
            { "7000.0", "+700%" },
            { "8000.0", "+800%" },
            { "9000.0", "+900%" },
            { "10000.0", "+1000%" },
            { "max", "Max (fastest possible)" },
            { NULL, NULL },
         },
         "0.0"
      },
      {
         "puae_cpu_multiplier",
         "CPU > CPU Cycle-exact Speed",
         "CPU Cycle-exact Speed",
         "Applies only with 'Cycle-exact'.",
         NULL,
         "cpu",
         {
            { "0", "Default" },
            { "1", "3.546895 MHz" },
            { "2", "7.093790 MHz (A500)" },
            { "4", "14.187580 MHz (A1200)" },
            { "8", "28.375160 MHz" },
            { "10", "35.468950 MHz" },
            { "12", "42.562740 MHz" },
            { "16", "56.750320 MHz" },
            { NULL, NULL },
         },
         "0"
      },
      {
         "puae_cpu_data_cache",
         "CPU > 68030 Data Cache",
         "68030 Data Cache",
         "Emulates the 68030 data cache, needed by a few programs that rely on it. Slower. Only with 68030 and 'CPU Compatibility' more compatible or cycle-exact.\nCore restart required.",
         NULL,
         "cpu",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_chipmem_size",
         "Memory > Chip RAM",
         "Chip RAM",
         "'Automatic' defaults to the current preset model.\nCore reset required.",
         NULL,
         "memory",
         {
            { "auto", "Automatic" },
            { "1", "0.5M" },
            { "2", "1M" },
            { "3", "1.5M" },
            { "4", "2M" },
            { NULL, NULL },
         },
         "auto"
      },
      {
         "puae_bogomem_size",
         "Memory > Slow RAM",
         "Slow RAM",
         "'Automatic' defaults to the current preset model.\nCore reset required.",
         NULL,
         "memory",
         {
            { "auto", "Automatic" },
            { "0", "None" },
            { "2", "0.5M" },
            { "4", "1M" },
            { "6", "1.5M" },
            { "7", "1.8M" },
            { NULL, NULL },
         },
         "auto"
      },
      {
         "puae_fastmem_size",
         "Memory > Z2 Fast RAM",
         "Z2 Fast RAM",
         "'Automatic' defaults to the current preset model.\nCore reset required.",
         NULL,
         "memory",
         {
            { "auto", "Automatic" },
            { "0", "None" },
            { "1", "1M" },
            { "2", "2M" },
            { "4", "4M" },
            { "8", "8M" },
            { NULL, NULL },
         },
         "auto"
      },
      {
         "puae_z3mem_size",
         "Memory > Z3 Fast RAM",
         "Z3 Fast RAM",
         "'Automatic' defaults to the current preset model.\nCore reset required.",
         NULL,
         "memory",
         {
            { "auto", "Automatic" },
            { "0", "None" },
            { "1", "1M" },
            { "2", "2M" },
            { "4", "4M" },
            { "8", "8M" },
            { "16", "16M" },
            { "32", "32M" },
            { "64", "64M" },
            { "128", "128M" },
            { "256", "256M" },
            { "512", "512M" },
            { NULL, NULL },
         },
         "auto"
      },
      {
         "puae_cpuboard",
         "Expansions > Accelerator Board",
         "Accelerator Board",
         "CPU board for the trapdoor or CPU slot, with its CPU, memory and boot ROM. Needs the board ROM in the system directory (any name, found by checksum). The CPU follows the board; on 68040/68060 boards 'CPU Model' chooses between the two (default: the board CPU).\nCore restart required.",
         NULL,
         "expansion",
         {
            { "none", "disabled" },
            { "GVPA530", "GVP A530 (A500, 68030)" },
            { "e-matrix530", "M-Tec E-Matrix 530 (A500 KS2.0+, 68030)" },
            { "tqm", "Hardital TQM (A500, 68030)" },
            { "golem030", "Kupke Golem 030 (A500, 68030)" },
            { "typhoon2", "DCE Typhoon MK2 (A500 KS2.0+, 68030)" },
            { "Apollo630", "Apollo 630 (A600, 68040)" },
            { "Blizzard1230II", "Blizzard 1230 I/II (A1200, 68030)" },
            { "Blizzard1230III", "Blizzard 1230 III (A1200, 68030)" },
            { "Blizzard1230IV", "Blizzard 1230 IV (A1200, 68030)" },
            { "Blizzard1260", "Blizzard 1240/1260 (A1200, 68040/68060)" },
            { "A1230SI", "GVP A1230 Turbo+ (A1200, 68030)" },
            { "A1230SII", "GVP A1230 Turbo+ Series II (A1200, 68030)" },
            { "Apollo", "Apollo 1240/1260 (A1200, 68040/68060)" },
            { "DKB12x0", "DKB 1230/1240/Cobra (A1200, 68030)" },
            { "Magnum40", "CSA Magnum 40 (A2000, 68040)" },
            { "GVPGFORCE030", "GVP G-Force 030 (A2000, 68030)" },
            { "GVPGFORCE040", "GVP G-Force 040 (A2000, 68040/68060)" },
            { "A3001SI", "GVP A3001 Series I (A2000, 68030)" },
            { "A2630", "Commodore A2620/A2630 (A2000, 68020/68030)" },
            { "Blizzard2060", "Blizzard 2060 (A2000, 68040/68060)" },
            { "TekMagic", "TekMagic 2040/2060 (A2000, 68040/68060)" },
            { "Zeus", "PP&S Zeus 040 (A2000, 68040/68060)" },
            { "CyberStormMK1", "CyberStorm MK I (A3000/A4000, 68040/68060)" },
            { "CyberStormMK2", "CyberStorm MK II (A3000/A4000, 68040/68060)" },
            { "CyberStormMK3", "CyberStorm MK III (A3000/A4000, 68040/68060)" },
            { "Falcon040", "MacroSystem Falcon 040 (A3000/A4000, 68040/68060)" },
            { "WarpEngineA4000", "MacroSystem Warp Engine (A4000, 68040/68060)" },
            { "quikpak", "QuikPak 4060 (A4000, 68060)" },
            { "sx32pro", "DCE SX32 Pro (CD32, 68030)" },
            { NULL, NULL },
         },
         "none"
      },
      {
         "puae_cpuboard_mem",
         "Expansions > Accelerator Memory",
         "Accelerator Memory",
         "Fast RAM on the accelerator board, limited to what the board supports.\nCore restart required.",
         NULL,
         "expansion",
         {
            { "auto", "Automatic" },
            { "0", "None" },
            { "4", "4MB" },
            { "8", "8MB" },
            { "16", "16MB" },
            { "32", "32MB" },
            { "64", "64MB" },
            { "128", "128MB" },
            { "256", "256MB" },
            { NULL, NULL },
         },
         "auto"
      },
      {
         "puae_cart_file",
         "Expansions > Cartridge",
         "Cartridge",
         "Freezer cartridges (Action Replay, Nordic Power, X-Power, Super IV...) found in 'system/uae/' or 'system/uae_data/', recognized by content. 'HRTMon' is built in.\nCore restart required.",
         NULL,
         "expansion",
         {
            /* Options filled dynamically later */
            { NULL, NULL },
         },
         NULL
      },
      {
         "puae_rtg_board",
         "Graphics Card > Board",
         "Board",
         "'UAE' boards are the Picasso96 'uaegfx' card: fastest, need Picasso96 (or P96) installed in the Amiga with the uaegfx driver. The others emulate the real boards and need their own drivers. Zorro III needs a 32-bit CPU (68020 with 32-bit addressing, 68030 or higher), not the A1200's 68EC020. Picasso IV needs its ROM in 'system/uae/' or 'system/uae_data/'.\nCore restart required.",
         NULL,
         "rtg",
         {
            { "disabled", NULL },
            { "ZorroIII", "UAE Zorro III (uaegfx)" },
            { "ZorroII", "UAE Zorro II (uaegfx)" },
            { "PicassoII", "Picasso II [Zorro II]" },
            { "PicassoII+", "Picasso II+ [Zorro II]" },
            { "PicassoIV_Z2", "Picasso IV [Zorro II]" },
            { "PicassoIV_Z3", "Picasso IV [Zorro III]" },
            { "CV64_Z3", "CyberVision 64 [Zorro III]" },
            { "CV643D_Z2", "CyberVision 64/3D [Zorro II]" },
            { "CV643D_Z3", "CyberVision 64/3D [Zorro III]" },
            { "Piccolo_Z2", "Piccolo [Zorro II]" },
            { "Piccolo_Z3", "Piccolo [Zorro III]" },
            { "PiccoloSD64_Z2", "Piccolo SD64 [Zorro II]" },
            { "PiccoloSD64_Z3", "Piccolo SD64 [Zorro III]" },
            { "Spectrum28/24_Z2", "Spectrum 28/24 [Zorro II]" },
            { "Spectrum28/24_Z3", "Spectrum 28/24 [Zorro III]" },
            { "Retina_Z2", "Retina [Zorro II]" },
            { "Retina_Z3", "Retina [Zorro III]" },
            { "RainbowIII", "Rainbow III [Zorro III]" },
            { "MerlinZ2", "Merlin [Zorro II]" },
            { "MerlinZ3", "Merlin [Zorro III]" },
            { "GraffityZ2", "Graffity [Zorro II]" },
            { "GraffityZ3", "Graffity [Zorro III]" },
            { "Visiona", "Visiona [Zorro II]" },
            { "Domino", "Domino [Zorro II]" },
            { "OmnibusET4000", "oMniBus ET4000AX [Zorro II]" },
            { "OmnibusET4000W32", "oMniBus ET4000W32 [Zorro II]" },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_rtg_vram",
         "Graphics Card > VRAM",
         "VRAM",
         "Graphics card memory. 'Automatic': 32 MB for UAE Zorro III, 4 MB for UAE Zorro II, the most the real board had for the others.\nCore restart required.",
         NULL,
         "rtg",
         {
            { "auto", "Automatic" },
            { "1", "1 MB" },
            { "2", "2 MB" },
            { "4", "4 MB" },
            { "8", "8 MB" },
            { "16", "16 MB" },
            { "32", "32 MB" },
            { "64", "64 MB" },
            { "128", "128 MB" },
            { "256", "256 MB" },
            { NULL, NULL },
         },
         "auto"
      },
      {
         "puae_cd32_fmv",
         "CD32 FMV > Cartridge",
         "Cartridge",
         "Full Motion Video cartridge for the CD32 (Video CD, CD-i Digital Video). Needs its ROM in 'system/uae/' or 'system/uae_data/', recognized by content. 'Automatic' uses the first one found. CD32 only.\nCore restart required.",
         NULL,
         "fmv",
         {
            /* Options filled dynamically later */
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_keyboard_type",
         "Expansions > Keyboard Type",
         "Keyboard Type",
         "'UAE' sends keys straight to the Amiga. The other types emulate the keyboard microcontroller, including its timing and key rollover, and need its ROM dump in the system directory: 6570-036 (2KB, CRC32 4a3fc332), 68HC05 (8KB, 2a77eec4) or D8039HLC (2KB, c23d280e or fb92a773). Without the ROM 'UAE' is used. 'Automatic' picks the keyboard of the model.\nCore restart required.",
         NULL,
         "expansion",
         {
            { "uae", "UAE" },
            { "auto", "Automatic" },
            { "a500_6570-036", "A500 (6570-036)" },
            { "a600_6570-036", "A600 (6570-036)" },
            { "a1000_6570-036", "A1000 (6570-036)" },
            { "ax000_6570-036", "A2000/A3000/A4000 (6570-036)" },
            { "a1200_6805", "A1200 (68HC05)" },
            { "a2000_8039", "A2000 (D8039HLC)" },
            { NULL, NULL },
         },
         "uae"
      },
      {
         "puae_keyboard_rollover",
         "Expansions > Keyboard Rollover",
         "Keyboard Rollover",
         "With a microcontroller 'Keyboard Type': 'N-key' reports every key held, 'Original' emulates the key matrix without diodes, including its ghost keys.",
         NULL,
         "expansion",
         {
            { "nkro", "N-key" },
            { "matrix", "Original" },
            { NULL, NULL },
         },
         "nkro"
      },
      {
         "puae_immediate_blits",
         "Chipset > Immediate/Waiting Blits",
         "Immediate/Waiting Blits",
         "- 'Immediate Blitter': Faster but less compatible blitter emulation.\n- 'Wait for Blitter': Compatibility hack for programs that don't wait for the blitter correctly.",
         NULL,
         "chipset",
         {
            { "false", "disabled" },
            { "immediate", "Immediate Blitter" },
            { "waiting", "Wait for Blitter" },
            { NULL, NULL },
         },
         "waiting"
      },
      {
         "puae_collision_level",
         "Chipset > Collision Level",
         "Collision Level",
         "'Sprites and Playfields' is recommended.",
         NULL,
         "chipset",
         {
            { "none", "None" },
            { "sprites", "Sprites only" },
            { "playfields", "Sprites and Playfields" },
            { "full", "Full" },
            { NULL, NULL },
         },
         "playfields"
      },
      {
         "puae_display_optimizations",
         "Chipset > Display Optimizations",
         "Display Optimizations",
         "Shortcuts of the WinUAE 6 display emulation, such as reusing lines that did not change since the previous frame. 'Full' is the fastest, 'Partial' and 'None' trade speed for accuracy in the rare programs that show display glitches. The difference is largest when 'CPU Compatibility' is not cycle-exact.",
         NULL,
         "chipset",
         {
            { "full", "Full" },
            { "partial", "Partial" },
            { "none", "None" },
            { NULL, NULL },
         },
         "full"
      },
      {
         "puae_autoloadfastforward",
         "Media > Automatic Load Fast-Forward",
         "Automatic Load Fast-Forward",
         "Toggle frontend fast-forward during media access if there is no audio output. Mutes 'Floppy Sound Emulation'.",
         NULL,
         "media",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { "fd", "Floppy disks only" },
            { "hd", "Hard drives only" },
            { "cd", "Compact discs only" },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_floppy_speed",
         "Media > Floppy Speed",
         "Floppy Speed",
         "Default speed is 300RPM. 'Turbo' removes disk rotation emulation.",
         NULL,
         "media",
         {
            { "100", "Default" },
            { "200", "2x" },
            { "400", "4x" },
            { "800", "8x" },
            { "0", "Turbo" },
            { NULL, NULL },
         },
         "100"
      },
      {
         "puae_floppy_multidrive",
         "Media > Floppy MultiDrive",
         "Floppy MultiDrive",
         "Insert each disk in different drives. Can be forced with '(MD)' file path tag. Maximum is 4 disks due to external drive limit! Not all games support external drives!\nCore reset required.",
         NULL,
         "media",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "enabled"
      },
      {
         "puae_floppy_write_protection",
         "Media > Floppy Write Protection",
         "Floppy Write Protection",
         "Set all drives read only. Changing this while emulation is running ejects and reinserts all disks. IPF images are always read-only!",
         NULL,
         "media",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_floppy_write_redirect",
         "Media > Floppy Write Redirect",
         "Floppy Write Redirect",
         "Writes to a substitute disk under 'saves' instead of original disks. Works also with IPF images.",
         NULL,
         "media",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_use_boot_hd",
         "Media > Global Boot HD",
         "Global Boot HD",
         "Attach a hard disk for Workbench, not for WHDLoad! Enabling forces HD interface model. CD launch enables AmigaCD mode. HDF size change will not alter existing HDFs.\nCore restart required.",
         NULL,
         "media",
         {
            { "disabled", NULL },
            { "files", "Files" },
            { "hdf20", "HDF 20MB" },
            { "hdf40", "HDF 40MB" },
            { "hdf80", "HDF 80MB" },
            { "hdf128", "HDF 128MB" },
            { "hdf256", "HDF 256MB" },
            { "hdf512", "HDF 512MB" },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_harddrive_write_protect",
         "Media > Hard Drive Write Protection",
         "Hard Drive Write Protection",
         "Mounts every hard drive image and directory read-only.\nCore restart required.",
         NULL,
         "media",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_use_whdload",
         "Media > WHDLoad Support",
         "WHDLoad Support",
         "Enable pre-installed WHDLoad content launching with helper directory filesystems for booting and saving.\nCore reset required.",
         NULL,
         "media",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "enabled"
      },
      {
         "puae_use_whdload_theme",
         "Media > WHDLoad Theme",
         "WHDLoad Theme",
         "AmigaOS 'system-configuration' color prefs in WHDLoad helper image. Available only with 'Files' mode.\nCore reset required.\n- 'Default' = Black/White/DarkGray/LightGray\n- 'Native' = Gray/Black/White/LightBlue",
         NULL,
         "media",
         {
            { "default", "Default" },
            { "native", "Native" },
            { NULL, NULL },
         },
         "default"
      },
      {
         "puae_use_whdload_prefs",
         "Media > WHDLoad Splash Screen",
         "WHDLoad Splash Screen",
         "Space/Enter/Fire works as WHDLoad Start-button. Core reset required.\nOverride with buttons while booting:\n- 'Config': Hold 2nd fire / Blue\n- 'Splash': Hold LMB\n- 'Config + Splash': Hold RMB\n- ReadMe + MkCustom: Hold Red+Blue",
         NULL,
         "media",
         {
            { "disabled", NULL },
            { "config", "Config (Show only if available)" },
            { "splash", "Splash (Show briefly)" },
            { "both", "Config + Splash (Wait for user input)" },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_use_whdload_buttonwait",
         "Media > WHDLoad ButtonWait",
         "WHDLoad ButtonWait",
         "Wait for a button press on internal loading sections if the slave supports it.\nCore reset required.",
         NULL,
         "media",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "enabled"
      },
      {
         "puae_use_whdload_nowritecache",
         "Media > WHDLoad NoWriteCache",
         "WHDLoad NoWriteCache",
         "Write cache requires running the core a few frames after closing content to trigger WHDLoad quit and flush cache to disk.\nQuitKey = '$2b' = '#' = 'LCtrl + Backslash'.\nCore reset required.",
         NULL,
         "media",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_cd_speed",
         "Media > CD Speed",
         "CD Speed",
         "Transfer rate in CD32 is 300KB/s (double-speed), CDTV is 150KB/s (single-speed). 'Turbo' removes seek delay emulation.",
         NULL,
         "media",
         {
            { "100", "Default" },
            { "0", "Turbo" },
            { NULL, NULL },
         },
         "100"
      },
      {
         "puae_cd_startup_delayed_insert",
         "Media > CD Startup Delayed Insert",
         "CD Startup Delayed Insert",
         "Some games fail to load if CD32/CDTV is powered on with CD inserted. 'ON' inserts CD during boot animation.",
         NULL,
         "media",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_shared_nvram",
         "Media > CD32/CDTV Shared NVRAM",
         "CD32/CDTV Shared NVRAM",
         "'OFF' saves separate files per content. Starting without content uses the shared file. CD32 and CDTV use separate shared files.\nCore restart required.",
         NULL,
         "media",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_video_options_display",
         "Show Video Options",
         NULL,
         "",
         NULL,
         NULL,
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_video_allow_hz_change",
         "Video > Allow Hz Change",
         "Allow Hz Change",
         "Let Amiga decide the exact refresh rate when interlace mode or PAL/NTSC changes. 'Locked' changes only when video standard changes.\nCore reset required.",
         NULL,
         "video",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { "locked", "Locked PAL/NTSC" },
            { NULL, NULL },
         },
         "locked"
      },
      {
         "puae_video_aspect",
         "Video > Pixel Aspect Ratio",
         "Pixel Aspect Ratio",
         "'Automatic' follows video standard, 'Automatic NTSC' prefers NTSC ratio when cropped height is under PAL height.\nHotkey toggling disables this option until core restart.\n- 'PAL': 26/25 = 1.04\n- 'NTSC': 43/50 = 0.86",
         NULL,
         "video",
         {
            { "auto", "Automatic" },
            { "NTSC auto", "Automatic NTSC" },
            { "PAL", NULL },
            { "NTSC", NULL },
            { "1:1", NULL },
            { NULL, NULL },
         },
         "auto"
      },
      {
         "puae_video_resolution",
         "Video > Resolution",
         "Resolution",
         "Output width:\n- 'Automatic' uses 'High' at minimum.\n- 'Automatic (Low)' allows 'Low'.\n- 'Automatic (Super-High)' sets max size already at startup.",
         NULL,
         "video",
         {
            { "auto", "Automatic" },
            { "auto-lores", "Automatic (Low)" },
            { "auto-superhires", "Automatic (Super-High)" },
            { "lores", "Low 360px" },
            { "hires", "High 720px" },
            { "superhires", "Super-High 1440px" },
            { NULL, NULL },
         },
         "auto"
      },
      {
         "puae_video_vresolution",
         "Video > Line Mode",
         "Line Mode",
         "Output height:\n- 'Automatic' defaults to 'Single Line' and switches to 'Double Line' on interlaced screens.",
         NULL,
         "video",
         {
            { "auto", "Automatic" },
            { "single", "Single Line" },
            { "double", "Double Line" },
            { NULL, NULL },
         },
         "auto"
      },
      {
         "puae_crop",
         "Video > Crop",
         "Crop",
         "Remove borders according to 'Crop Mode'.",
         NULL,
         "video",
         {
            { "disabled", NULL },
            { "minimum", "Minimum" },
            { "smaller", "Smaller" },
            { "small", "Small" },
            { "medium", "Medium" },
            { "large", "Large" },
            { "larger", "Larger" },
            { "maximum", "Maximum" },
            { "auto", "Automatic" },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_crop_delay",
         "Video > Automatic Crop Delay",
         "Automatic Crop Delay",
         "Patient or instant geometry change.",
         NULL,
         "video",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "enabled"
      },
      {
         "puae_crop_mode",
         "Video > Crop Mode",
         "Crop Mode",
         "'Horizontal + Vertical' & 'Automatic' removes borders completely.",
         NULL,
         "video",
         {
            { "both", "Horizontal + Vertical" },
            { "horizontal", "Horizontal" },
            { "vertical", "Vertical" },
            { "16:9", "16:9" },
            { "16:10", "16:10" },
            { "4:3", "4:3" },
            { "5:4", "5:4" },
            { NULL, NULL },
         },
         "both"
      },
      {
         "puae_vertical_pos",
         "Video > Vertical Position",
         "Vertical Position",
         "'Automatic' keeps only cropped screens centered. 'Forced' centers regardless of available crop area. Positive values move upward and negative values move downward.",
         NULL,
         "video",
         {
            /* Options filled dynamically later */
            { NULL, NULL },
         },
         "auto"
      },
      {
         "puae_horizontal_pos",
         "Video > Horizontal Position",
         "Horizontal Position",
         "'Automatic' keeps screen centered. Positive values move right and negative values move left.",
         NULL,
         "video",
         {
            /* Options filled dynamically later */
            { NULL, NULL },
         },
         "auto"
      },
      {
         "puae_video_interlace_mode",
         "Video > Interlaced Line Mode",
         "Interlaced Line Mode",
         "How interlaced screens are shown when 'Line Mode' is double:\n- 'Double, fields': both fields woven together, updated every field.\n- 'Double, frames': both fields woven together, updated only on complete frames.\n- 'Double, fields+': each field alone, the lines of the other field black, like an interlaced monitor (best at 50/60 Hz, with a CRT shader).",
         NULL,
         "video",
         {
            { "fields", "Double, fields" },
            { "frames", "Double, frames" },
            { "fields_plus", "Double, fields+" },
            { NULL, NULL },
         },
         "fields"
      },
      {
         "puae_video_progressive_mode",
         "Video > Progressive Line Mode",
         "Progressive Line Mode",
         "How non-interlaced screens are shown when 'Line Mode' is double:\n- 'Double': every line drawn twice.\n- 'Scanlines': every second line black.\n- 'Double, fields': like an interlaced monitor showing a progressive picture, alternating the lines every frame.\n- 'Double, fields+': same, with the other lines always black.",
         NULL,
         "video",
         {
            { "double", "Double" },
            { "scanlines", "Scanlines" },
            { "fields", "Double, fields" },
            { "fields_plus", "Double, fields+" },
            { NULL, NULL },
         },
         "double"
      },
      {
         "puae_gfx_lores_filtered",
         "Video > Filtered Low Resolution",
         "Filtered Low Resolution",
         "When a screen has a higher resolution than the output 'Resolution' (e.g. hires shown at lores, superhires at hires), blends the pixels that do not fit instead of skipping them.",
         NULL,
         "video",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_gfx_flickerfixer",
         "Video > Remove Interlace Artifacts",
         "Remove Interlace Artifacts",
         "Best suited for still screens, Workbench etc.",
         NULL,
         "video",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_gfx_framerate",
         "Video > Frameskip",
         "Frameskip",
         "Not compatible with 'Cycle-exact'.",
         NULL,
         "video",
         {
            { "disabled", NULL },
            { "1", NULL },
            { "2", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_gfx_gamma",
         "Video > Color Gamma",
         "Color Gamma",
         "Adjust color gamma.",
         NULL,
         "video",
         {
            { "-500", "+0.5" },
            { "-400", "+0.4" },
            { "-300", "+0.3" },
            { "-200", "+0.2" },
            { "-100", "+0.1" },
            { "0", "disabled" },
            { "100", "-0.1" },
            { "200", "-0.2" },
            { "300", "-0.3" },
            { "400", "-0.4" },
            { "500", "-0.5" },
            { NULL, NULL },
         },
         "0"
      },
      {
         "puae_gfx_colors",
         "Video > Color Depth",
         "Color Depth",
         "Full restart required.",
         NULL,
         "video",
         {
            { "16bit", "16-bit (RGB565)" },
            { "24bit", "24-bit (XRGB8888)" },
            { NULL, NULL },
         },
#if defined(VITA) || defined(__SWITCH__) || defined(DINGUX) || defined(ANDROID)
         "16bit"
#else
         "24bit"
#endif
      },
      {
         "puae_audio_options_display",
         "Show Audio Options",
         NULL,
         "",
         NULL,
         NULL,
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_sound_stereo_separation",
         "Audio > Stereo Separation",
         "Stereo Separation",
         "Paula sound chip channel panning. Does not affect CD audio.",
         NULL,
         "audio",
         {
            { "0%", NULL },
            { "10%", NULL },
            { "20%", NULL },
            { "30%", NULL },
            { "40%", NULL },
            { "50%", NULL },
            { "60%", NULL },
            { "70%", NULL },
            { "80%", NULL },
            { "90%", NULL },
            { "100%", NULL },
            { NULL, NULL },
         },
         "100%"
      },
      {
         "puae_sound_stereo_delay",
         "Audio > Stereo Delay",
         "Stereo Delay",
         "Mixes each channel with the other one as it was a moment earlier, for a wider, headphone friendly stereo image. Only with 'Stereo Separation' below 100%. Does not affect CD audio.",
         NULL,
         "audio",
         {
            { "disabled", NULL },
            { "1", NULL },
            { "2", NULL },
            { "3", NULL },
            { "4", NULL },
            { "5", NULL },
            { "6", NULL },
            { "7", NULL },
            { "8", NULL },
            { "9", NULL },
            { "10", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_sound_stereo_swap",
         "Audio > Swap Stereo Channels",
         "Swap Stereo Channels",
         "Swaps the left and right Paula channels. Does not affect CD audio.",
         NULL,
         "audio",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_sound_interpol",
         "Audio > Interpolation",
         "Interpolation",
         "Paula sound chip interpolation type.",
         NULL,
         "audio",
         {
            { "none", "None" },
            { "anti", "Anti" },
            { "sinc", "Sinc" },
            { "rh", "RH" },
            { "crux", "Crux" },
            { NULL, NULL },
         },
         "anti"
      },
      {
         "puae_sound_filter",
         "Audio > Filter",
         "Filter",
         "'Emulated' allows states between ON/OFF.",
         NULL,
         "audio",
         {
            { "emulated", "Emulated" },
            { "off", "Off" },
            { "on", "On" },
            { NULL, NULL },
         },
         "emulated"
      },
      {
         "puae_sound_filter_type",
         "Audio > Filter Type",
         "Filter Type",
         "'Automatic' picks the filter type for the hardware.",
         NULL,
         "audio",
         {
            { "auto", "Automatic" },
            { "standard", "A500" },
            { "enhanced", "A1200" },
            { NULL, NULL },
         },
         "auto",
      },
      {
         "puae_floppy_sound",
         "Audio > Floppy Sound Emulation",
         "Floppy Sound Emulation",
         "Floppy volume in percent.",
         NULL,
         "audio",
         {
            { "100", "disabled" },
            { "95", "5%" },
            { "90", "10%" },
            { "85", "15%" },
            { "80", "20%" },
            { "75", "25%" },
            { "70", "30%" },
            { "65", "35%" },
            { "60", "40%" },
            { "55", "45%" },
            { "50", "50%" },
            { "45", "55%" },
            { "40", "60%" },
            { "35", "65%" },
            { "30", "70%" },
            { "25", "75%" },
            { "20", "80%" },
            { "15", "85%" },
            { "10", "90%" },
            { "5", "95%" },
            { "0", "100%" },
            { NULL, NULL },
         },
         "80"
      },
      {
         "puae_floppy_sound_empty_mute",
         "Audio > Floppy Sound Mute Ejected",
         "Floppy Sound Mute Ejected",
         "Mute drive head clicking when floppy is not inserted.",
         NULL,
         "audio",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "enabled"
      },
      {
         "puae_floppy_sound_type",
         "Audio > Floppy Sound Type",
         "Floppy Sound Type",
         "External files go in 'system/uae/' or 'system/uae_data/'.",
         NULL,
         "audio",
         {
            { "internal", "Internal" },
            { "A500", "External: A500" },
            { "LOUD", "External: LOUD" },
            { NULL, NULL },
         },
         "internal"
      },
      {
         "puae_sound_volume_cd",
         "Audio > CD Audio Volume",
         "CD Audio Volume",
         "CD volume in percent.",
         NULL,
         "audio",
         {
            { "0%", NULL },
            { "5%", NULL },
            { "10%", NULL },
            { "15%", NULL },
            { "20%", NULL },
            { "25%", NULL },
            { "30%", NULL },
            { "35%", NULL },
            { "40%", NULL },
            { "45%", NULL },
            { "50%", NULL },
            { "55%", NULL },
            { "60%", NULL },
            { "65%", NULL },
            { "70%", NULL },
            { "75%", NULL },
            { "80%", NULL },
            { "85%", NULL },
            { "90%", NULL },
            { "95%", NULL },
            { "100%", NULL },
            { NULL, NULL },
         },
         "100%"
      },
      {
         "puae_analogmouse",
         "Input > Analog Stick Mouse",
         "Analog Stick Mouse",
         "Default mouse control stick when remappings are empty.",
         NULL,
         "input",
         {
            { "disabled", NULL },
            { "left", "Left Analog" },
            { "right", "Right Analog" },
            { "both", "Both Analogs" },
            { NULL, NULL },
         },
         "both"
      },
      {
         "puae_analogmouse_deadzone",
         "Input > Analog Stick Mouse Deadzone",
         "Analog Stick Mouse Deadzone",
         "Required distance from stick center to register input.",
         NULL,
         "input",
         {
            { "0", "0%" },
            { "5", "5%" },
            { "10", "10%" },
            { "15", "15%" },
            { "20", "20%" },
            { "25", "25%" },
            { "30", "30%" },
            { "35", "35%" },
            { "40", "40%" },
            { "45", "45%" },
            { "50", "50%" },
            { NULL, NULL },
         },
         "20"
      },
      {
         "puae_analogmouse_speed",
         "Input > Left Analog Stick Mouse Speed",
         "Left Analog Stick Mouse Speed",
         "Mouse speed for left analog stick.",
         NULL,
         "input",
         ANALOG_STICK_SPEED_OPTIONS,
         "1.0"
      },
      {
         "puae_analogmouse_speed_right",
         "Input > Right Analog Stick Mouse Speed",
         "Right Analog Stick Mouse Speed",
         "Mouse speed for right analog stick.",
         NULL,
         "input",
         ANALOG_STICK_SPEED_OPTIONS,
         "1.0"
      },
      {
         "puae_dpadmouse_speed",
         "Input > D-Pad Mouse Speed",
         "D-Pad Mouse Speed",
         "Mouse speed for directional pad.",
         NULL,
         "input",
         {
            { "0", "disabled" },
            { "1", "16%" },
            { "2", "33%" },
            { "3", "50%" },
            { "4", "66%" },
            { "5", "83%" },
            { "6", "100%" },
            { "7", "116%" },
            { "8", "133%" },
            { "9", "150%" },
            { "10", "166%" },
            { "11", "183%" },
            { "12", "200%" },
            { "13", "216%" },
            { "14", "233%" },
            { "15", "250%" },
            { "16", "266%" },
            { "17", "283%" },
            { "18", "300%" },
            { NULL, NULL },
         },
         "6"
      },
      {
         "puae_mouse_speed",
         "Input > Mouse Speed",
         "Mouse Speed",
         "Global mouse speed.",
         NULL,
         "input",
         {
            /* Options filled dynamically later */
            { NULL, NULL },
         },
         "100"
      },
      {
         "puae_physicalmouse",
         "Input > Physical Mouse",
         "Physical Mouse",
         "'Double' requirements: raw/udev input driver and proper mouse index per port.\nDoes not affect RetroPad emulated mice.",
         NULL,
         "input",
         {
            { "disabled", "disabled" },
            { "enabled", "enabled" },
            { "double", "Double" },
            { NULL, NULL },
         },
         "enabled"
      },
      {
         "puae_physical_keyboard_pass_through",
         "Input > Keyboard Pass-through",
         "Keyboard Pass-through",
         "'ON' passes all physical keyboard events to the core. 'OFF' prevents RetroPad keys from generating keyboard events.\nNOTE: This is a legacy option for old frontends that do not block keyboard events when using RetroPad, so it does nothing with current RetroArch.",
         NULL,
         "input",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_keyrah_keypad_mappings",
         "Input > Keyrah Keypad Mappings",
         "Keyrah Keypad Mappings",
         "Hardcoded keypad to joyport mappings for Keyrah hardware.",
         NULL,
         "input",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_turbo_fire",
         "RetroPad > Turbo Fire",
         "Turbo Fire",
         "Hotkey toggling disables this option until core restart.",
         NULL,
         "input",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_turbo_fire_button",
         "RetroPad > Turbo Button",
         "Turbo Button",
         "Replace the mapped button with turbo fire button.",
         NULL,
         "input",
         {
            { "B", "RetroPad B" },
            { "A", "RetroPad A" },
            { "Y", "RetroPad Y" },
            { "X", "RetroPad X" },
            { "L", "RetroPad L" },
            { "R", "RetroPad R" },
            { "L2", "RetroPad L2" },
            { "R2", "RetroPad R2" },
            { NULL, NULL },
         },
         "B"
      },
      {
         "puae_turbo_pulse",
         "RetroPad > Turbo Pulse",
         "Turbo Pulse",
         "Frames in a button cycle.\n- '2' = 1 frame down, 1 frame up\n- '4' = 2 frames down, 2 frames up\n- '6' = 3 frames down, 3 frames up\netc.",
         NULL,
         "input",
         {
            { "2", "2 frames" },
            { "4", "4 frames" },
            { "6", "6 frames" },
            { "8", "8 frames" },
            { "10", "10 frames" },
            { "12", "12 frames" },
            { NULL, NULL },
         },
         "6"
      },
      {
         "puae_joyport",
         "RetroPad > Joystick/Mouse",
         "Joystick/Mouse",
         "Change D-Pad control between joyports. Hotkey toggling disables this option until core restart.",
         NULL,
         "input",
         {
            { "joystick", "Joystick (Port 1)" },
            { "mouse", "Mouse (Port 2)" },
            { NULL, NULL },
         },
         "Joystick"
      },
      {
         "puae_joyport_order",
         "RetroPad > Joystick Port Order",
         "Joystick Port Order",
         "Plug RetroPads in different ports. Useful for 4-player adapters.",
         NULL,
         "input",
         {
            { "1234", "1-2-3-4" },
            { "2143", "2-1-4-3" },
            { "3412", "3-4-1-2" },
            { "4321", "4-3-2-1" },
            { NULL, NULL },
         },
         "1234"
      },
      {
         "puae_retropad_options",
         "RetroPad > Face Button Options",
         "RetroPad Face Button Options",
         "Rotate face buttons clockwise and/or make 2nd fire press up.",
         NULL,
         "input",
         {
            { "disabled", "B = Fire, A = 2nd fire" },
            { "jump", "B = Fire, A = Up" },
            { "rotate", "Y = Fire, B = 2nd fire" },
            { "rotate_jump", "Y = Fire, B = Up" },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_cd32pad_options",
         "CD32 Pad > Face Button Options",
         "CD32 Pad Face Button Options",
         "Rotate face buttons clockwise and/or make blue button press up.",
         NULL,
         "input",
         {
            { "disabled", "B = Red, A = Blue" },
            { "jump", "B = Red, A = Up" },
            { "rotate", "Y = Red, B = Blue" },
            { "rotate_jump", "Y = Red, B = Up" },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_mapping_options_display",
         "Show Mapping Options",
         NULL,
         "",
         NULL,
         NULL,
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "enabled"
      },
      /* Hotkeys */
      {
         "puae_mapper_vkbd",
         "Hotkey > Toggle Virtual Keyboard",
         "Toggle Virtual Keyboard",
         "Press the mapped key to toggle the virtual keyboard.",
         NULL,
         "hotkey",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_statusbar",
         "Hotkey > Toggle Statusbar",
         "Toggle Statusbar",
         "Press the mapped key to toggle the statusbar.",
         NULL,
         "hotkey",
         {{ NULL, NULL }},
         "RETROK_F12"
      },
      {
         "puae_mapper_mouse_toggle",
         "Hotkey > Switch Joystick/Mouse",
         "Switch Joystick/Mouse",
         "Press the mapped key to switch between joystick and mouse control.",
         NULL,
         "hotkey",
         {{ NULL, NULL }},
         "RETROK_RCTRL"
      },
      {
         "puae_mapper_turbo_fire_toggle",
         "Hotkey > Toggle Turbo Fire",
         "Toggle Turbo Fire",
         "Press the mapped key to toggle turbo fire.",
         NULL,
         "hotkey",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_save_disk_toggle",
         "Hotkey > Toggle Save Disk",
         "Toggle Save Disk",
         "Press the mapped key to create/insert/eject a save disk.",
         NULL,
         "hotkey",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_aspect_ratio_toggle",
         "Hotkey > Toggle Aspect Ratio",
         "Toggle Aspect Ratio",
         "Press the mapped key to toggle between PAL/NTSC pixel aspect ratio.",
         NULL,
         "hotkey",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_crop_toggle",
         "Hotkey > Toggle Crop",
         "Toggle Crop",
         "Press the mapped key to toggle crop.",
         NULL,
         "hotkey",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_reset",
         "Hotkey > Reset",
         "Reset",
         "Press the mapped key to trigger soft reset (Ctrl-Amiga-Amiga).",
         NULL,
         "hotkey",
         {{ NULL, NULL }},
         "---"
      },
      /* Button mappings */
      {
         "puae_mapper_up",
         "RetroPad > Up",
         "Up",
         "Unmapped defaults to joystick up.",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_down",
         "RetroPad > Down",
         "Down",
         "Unmapped defaults to joystick down.",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_left",
         "RetroPad > Left",
         "Left",
         "Unmapped defaults to joystick left.",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_right",
         "RetroPad > Right",
         "Right",
         "Unmapped defaults to joystick right.",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_b",
         "RetroPad > B",
         "B",
         "Unmapped defaults to fire button.\nVKBD: Press selected key.",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_a",
         "RetroPad > A",
         "A",
         "Unmapped defaults to 2nd fire button.\nVKBD: Toggle transparency. Remapping to non-keyboard keys overrides VKBD function!",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_y",
         "RetroPad > Y",
         "Y",
         "VKBD: Toggle 'CapsLock'. Remapping to non-keyboard keys overrides VKBD function!",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_x",
         "RetroPad > X",
         "X",
         "VKBD: Press 'Space'. Remapping to non-keyboard keys overrides VKBD function!",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         "RETROK_SPACE"
      },
      {
         "puae_mapper_select",
         "RetroPad > Select",
         "Select",
         "VKBD comes up with RetroPad Select by default.",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         "TOGGLE_VKBD"
      },
      {
         "puae_mapper_start",
         "RetroPad > Start",
         "Start",
         "VKBD: Press 'Return'. Remapping to non-keyboard keys overrides VKBD function!",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_l",
         "RetroPad > L",
         "L",
         "",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         ""
      },
      {
         "puae_mapper_r",
         "RetroPad > R",
         "R",
         "",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         ""
      },
      {
         "puae_mapper_l2",
         "RetroPad > L2",
         "L2",
         "",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         "MOUSE_LEFT_BUTTON"
      },
      {
         "puae_mapper_r2",
         "RetroPad > R2",
         "R2",
         "",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         "MOUSE_RIGHT_BUTTON"
      },
      {
         "puae_mapper_l3",
         "RetroPad > L3",
         "L3",
         "",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_r3",
         "RetroPad > R3",
         "R3",
         "",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         "---"
      },
      /* Left Stick */
      {
         "puae_mapper_lu",
         "RetroPad > Left Analog > Up",
         "Left Analog > Up",
         "",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_ld",
         "RetroPad > Left Analog > Down",
         "Left Analog > Down",
         "",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_ll",
         "RetroPad > Left Analog > Left",
         "Left Analog > Left",
         "",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_lr",
         "RetroPad > Left Analog > Right",
         "Left Analog > Right",
         "",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         "---"
      },
      /* Right Stick */
      {
         "puae_mapper_ru",
         "RetroPad > Right Analog > Up",
         "Right Analog > Up",
         "",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_rd",
         "RetroPad > Right Analog > Down",
         "Right Analog > Down",
         "",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_rl",
         "RetroPad > Right Analog > Left",
         "Right Analog > Left",
         "",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_rr",
         "RetroPad > Right Analog > Right",
         "Right Analog > Right",
         "",
         NULL,
         "retropad",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_vkbd_theme",
         "OSD > Virtual KBD Theme",
         "Virtual KBD Theme",
         "The keyboard comes up with RetroPad Select by default.",
         NULL,
         "osd",
         {
            { "auto", "Automatic (shadow)" },
            { "auto_outline", "Automatic (outline)" },
            { "beige", "Beige (shadow)" },
            { "beige_outline", "Beige (outline)" },
            { "cd32", "CD32 (shadow)" },
            { "cd32_outline", "CD32 (outline)" },
            { "light", "Light (shadow)" },
            { "light_outline", "Light (outline)" },
            { "dark", "Dark (shadow)" },
            { "dark_outline", "Dark (outline)" },
            { NULL, NULL },
         },
         "auto"
      },
      {
         "puae_vkbd_transparency",
         "OSD > Virtual KBD Transparency",
         "Virtual KBD Transparency",
         "Keyboard transparency can be toggled with RetroPad A.",
         NULL,
         "osd",
         {
            { "0%",   NULL },
            { "25%",  NULL },
            { "50%",  NULL },
            { "75%",  NULL },
            { "100%", NULL },
            { NULL, NULL },
         },
         "25%"
      },
      {
         "puae_vkbd_dimming",
         "OSD > Virtual KBD Dimming",
         "Virtual KBD Dimming",
         "Dimming level of the surrounding area.",
         NULL,
         "osd",
         {
            { "0%",   NULL },
            { "25%",  NULL },
            { "50%",  NULL },
            { "75%",  NULL },
            { "100%", NULL },
            { NULL, NULL },
         },
         "25%"
      },
      {
         "puae_statusbar",
         "OSD > Statusbar Mode",
         "Statusbar Mode",
         "- 'Full': Joyports + Messages + LEDs\n- 'Basic': Messages + LEDs\n- 'Minimal': LED colors only",
         NULL,
         "osd",
         {
            { "bottom", "Bottom Full" },
            { "bottom_minimal", "Bottom Full Minimal" },
            { "bottom_basic", "Bottom Basic" },
            { "bottom_basic_minimal", "Bottom Basic Minimal" },
            { "top", "Top Full" },
            { "top_minimal", "Top Full Minimal" },
            { "top_basic", "Top Basic" },
            { "top_basic_minimal", "Top Basic Minimal" },
            { NULL, NULL },
         },
         "bottom"
      },
      {
         "puae_statusbar_startup",
         "OSD > Statusbar Startup",
         "Statusbar Startup",
         "Show statusbar on startup.",
         NULL,
         "osd",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_statusbar_messages",
         "OSD > Statusbar Messages",
         "Statusbar Messages",
         "Show messages when statusbar is hidden.",
         NULL,
         "osd",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_joyport_pointer_color",
         "OSD > Light Pen/Gun Pointer Color",
         "Light Pen/Gun Pointer Color",
         "Crosshair color for light pens and guns.",
         NULL,
         "osd",
         {
            { "disabled", NULL },
            { "black", "Black" },
            { "white", "White" },
            { "red", "Red" },
            { "green", "Green" },
            { "blue", "Blue" },
            { "yellow", "Yellow" },
            { "purple", "Purple" },
            { NULL, NULL },
         },
         "blue"
      },
      { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
   };

   free_puae_core_options();

   /* Fill in the values for all the mappers */
   int i = 0;
   int j = 0;
   int hotkey = 0;
   int hotkeys_skipped = 0;
   /* Count special hotkeys */
   while (retro_keys[j].value[0] && j < RETRO_NUM_CORE_OPTION_VALUES_MAX - 1)
   {
      if (retro_keys[j].id < 0)
         hotkeys_skipped++;
      ++j;
   }
   while (option_defs_us[i].key)
   {
      if (strstr(option_defs_us[i].key, "puae_mapper_"))
      {
         /* Show different key list for hotkeys (special negatives removed) */
         if (  strstr(option_defs_us[i].key, "puae_mapper_vkbd")
            || strstr(option_defs_us[i].key, "puae_mapper_statusbar")
            || strstr(option_defs_us[i].key, "puae_mapper_mouse_toggle")
            || strstr(option_defs_us[i].key, "puae_mapper_reset")
            || strstr(option_defs_us[i].key, "puae_mapper_aspect_ratio_toggle")
            || strstr(option_defs_us[i].key, "puae_mapper_crop_toggle")
            || strstr(option_defs_us[i].key, "puae_mapper_turbo_fire_toggle")
            || strstr(option_defs_us[i].key, "puae_mapper_save_disk_toggle"))
            hotkey = 1;
         else
            hotkey = 0;

         j = 0;
         if (hotkey)
         {
            while (retro_keys[j].value[0] && j < RETRO_NUM_CORE_OPTION_VALUES_MAX - 1)
            {
               if (j == 0) /* "---" unmapped */
               {
                  option_defs_us[i].values[j].value = retro_keys[j].value;
                  option_defs_us[i].values[j].label = retro_keys[j].label;
               }
               else
               {
                  option_defs_us[i].values[j].value = retro_keys[j + hotkeys_skipped + 1].value;
                  option_defs_us[i].values[j].label = retro_keys[j + hotkeys_skipped + 1].label;
               }
               ++j;
            }
         }
         else
         {
            while (retro_keys[j].value[0] && j < RETRO_NUM_CORE_OPTION_VALUES_MAX - 1)
            {
               option_defs_us[i].values[j].value = retro_keys[j].value;
               option_defs_us[i].values[j].label = retro_keys[j].label;
               ++j;
            }
         }
         option_defs_us[i].values[j].value = NULL;
         option_defs_us[i].values[j].label = NULL;
      }
      else if (!strcmp(option_defs_us[i].key, "puae_mouse_speed"))
      {
         j = 0;

         CORE_OPTION_VALUE_RANGE(10, 1000, 10, "%");

         option_defs_us[i].values[j].value = NULL;
         option_defs_us[i].values[j].label = NULL;
      }
      else if (!strcmp(option_defs_us[i].key, "puae_vertical_pos"))
      {
         j = 0;

         option_defs_us[i].values[j].value = "auto";
         option_defs_us[i].values[j].label = "Automatic";
         ++j;

         option_defs_us[i].values[j].value = "autoforce";
         option_defs_us[i].values[j].label = "Automatic (Forced)";
         ++j;

         option_defs_us[i].values[j].value = "0";
         option_defs_us[i].values[j].label = "Default";
         ++j;

         CORE_OPTION_VALUE_RANGE(1, 70, 1, NULL);
         CORE_OPTION_VALUE_RANGE(-20, -1, 1, NULL);

         option_defs_us[i].values[j].value = NULL;
         option_defs_us[i].values[j].label = NULL;
      }
      else if (!strcmp(option_defs_us[i].key, "puae_horizontal_pos"))
      {
         j = 0;

         option_defs_us[i].values[j].value = "auto";
         option_defs_us[i].values[j].label = "Automatic";
         ++j;

         option_defs_us[i].values[j].value = "0";
         option_defs_us[i].values[j].label = "Default";
         ++j;

         CORE_OPTION_VALUE_RANGE(1, 40, 1, NULL);
         CORE_OPTION_VALUE_RANGE(-40, -1, 1, NULL);

         option_defs_us[i].values[j].value = NULL;
         option_defs_us[i].values[j].label = NULL;
      }
      else if (!strcmp(option_defs_us[i].key, "puae_kickstart"))
      {
         j = 0;
         option_defs_us[i].values[j].value = "auto";
         option_defs_us[i].values[j].label = "Automatic";
         ++j;

         option_defs_us[i].values[j].value = "aros";
         option_defs_us[i].values[j].label = "AROS";
         ++j;

         /* Scan system directory for Kickstarts */
         if (path_is_directory(retro_system_directory))
         {
            RDIR *dir;
            dir = retro_opendir(retro_system_directory);

            while (retro_readdir(dir) && j < RETRO_NUM_CORE_OPTION_VALUES_MAX - 1)
            {
               const char *name = retro_dirent_get_name(dir);

               if (  strstartswith(name, "kick")
                  || strstartswith(name, "amiga-os")
                  || strstartswith(name, "KS ROM")
                  )
               {
                  char value[RETRO_PATH_MAX] = {0};
                  snprintf(value, sizeof(value), "%s", name);

                  puae_kickstarts[j].value = strdup(value);

                  option_defs_us[i].values[j].value = puae_kickstarts[j].value;
                  ++j;
               }

               puae_kickstarts[j].value = NULL;
               puae_kickstarts[j].label = NULL;
            }
            retro_closedir(dir);
         }

         option_defs_us[i].values[j].value = NULL;
         option_defs_us[i].values[j].label = NULL;
      }
      else if (!strcmp(option_defs_us[i].key, "puae_cart_file"))
      {
         j = 0;
         option_defs_us[i].values[j].value = "none";
         option_defs_us[i].values[j].label = "disabled";
         ++j;

         option_defs_us[i].values[j].value = ":hrtmon";
         option_defs_us[i].values[j].label = "HRTMon (built-in)";
         ++j;

         /* Freezer cartridges only (Action Replay, Nordic Power, X-Power,
          * Super IV...), identified by content: the folder also holds
          * Kickstarts, accelerator, SCSI and other board ROMs */
         puae_scan_roms(&option_defs_us[i], &j, puae_carts, ROMTYPE_FREEZER);

         option_defs_us[i].values[j].value = NULL;
         option_defs_us[i].values[j].label = NULL;
      }
      else if (!strcmp(option_defs_us[i].key, "puae_cd32_fmv"))
      {
         j = 0;
         option_defs_us[i].values[j].value = "disabled";
         option_defs_us[i].values[j].label = NULL;
         ++j;

         option_defs_us[i].values[j].value = "auto";
         option_defs_us[i].values[j].label = "Automatic";
         ++j;

         puae_scan_roms(&option_defs_us[i], &j, puae_fmvs, ROMTYPE_CD32CART);
         puae_find_rom(ROMTYPE_PICASSOIV, puae_picassoiv_rom, sizeof(puae_picassoiv_rom));

         option_defs_us[i].values[j].value = NULL;
         option_defs_us[i].values[j].label = NULL;
      }
      ++i;
   }

   struct retro_core_options_v2 options_us = {
      option_cats_us,
      option_defs_us
   };

   unsigned version = 0;
   if (!environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version))
      version = 0;

   if (version >= 2)
   {
#ifndef HAVE_NO_LANGEXTRA
      struct retro_core_options_v2_intl core_options_intl;

      core_options_intl.us    = &options_us;
      core_options_intl.local = NULL;

      if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
          (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH))
         core_options_intl.local = options_intl[language];

      libretro_supports_option_categories = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL,
            &core_options_intl);
#else
      libretro_supports_option_categories = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2,
            &options_us);
#endif
   }
   else
   {
      size_t i, j;
      size_t option_index              = 0;
      size_t num_options               = 0;
      struct retro_core_option_definition
            *option_v1_defs_us         = NULL;
#ifndef HAVE_NO_LANGEXTRA
      size_t num_options_intl          = 0;
      struct retro_core_option_v2_definition
            *option_defs_intl          = NULL;
      struct retro_core_option_definition
            *option_v1_defs_intl       = NULL;
      struct retro_core_options_intl
            core_options_v1_intl;
#endif
      struct retro_variable *variables = NULL;
      char **values_buf                = NULL;

      /* Determine total number of options */
      while (true)
      {
         if (option_defs_us[num_options].key)
            num_options++;
         else
            break;
      }

      if (version >= 1)
      {
         /* Allocate US array */
         option_v1_defs_us = (struct retro_core_option_definition *)
               calloc(num_options + 1, sizeof(struct retro_core_option_definition));

         /* Copy parameters from option_defs_us array */
         for (i = 0; i < num_options; i++)
         {
            struct retro_core_option_v2_definition *option_def_us = &option_defs_us[i];
            struct retro_core_option_value *option_values         = option_def_us->values;
            struct retro_core_option_definition *option_v1_def_us = &option_v1_defs_us[i];
            struct retro_core_option_value *option_v1_values      = option_v1_def_us->values;

            option_v1_def_us->key           = option_def_us->key;
            option_v1_def_us->desc          = option_def_us->desc;
            option_v1_def_us->info          = option_def_us->info;
            option_v1_def_us->default_value = option_def_us->default_value;

            /* Values must be copied individually... */
            while (option_values->value)
            {
               option_v1_values->value = option_values->value;
               option_v1_values->label = option_values->label;

               option_values++;
               option_v1_values++;
            }
         }

#ifndef HAVE_NO_LANGEXTRA
         if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
             (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH) &&
             options_intl[language])
            option_defs_intl = options_intl[language]->definitions;

         if (option_defs_intl)
         {
            /* Determine number of intl options */
            while (true)
            {
               if (option_defs_intl[num_options_intl].key)
                  num_options_intl++;
               else
                  break;
            }

            /* Allocate intl array */
            option_v1_defs_intl = (struct retro_core_option_definition *)
                  calloc(num_options_intl + 1, sizeof(struct retro_core_option_definition));

            /* Copy parameters from option_defs_intl array */
            for (i = 0; i < num_options_intl; i++)
            {
               struct retro_core_option_v2_definition *option_def_intl = &option_defs_intl[i];
               struct retro_core_option_value *option_values           = option_def_intl->values;
               struct retro_core_option_definition *option_v1_def_intl = &option_v1_defs_intl[i];
               struct retro_core_option_value *option_v1_values        = option_v1_def_intl->values;

               option_v1_def_intl->key           = option_def_intl->key;
               option_v1_def_intl->desc          = option_def_intl->desc;
               option_v1_def_intl->info          = option_def_intl->info;
               option_v1_def_intl->default_value = option_def_intl->default_value;

               /* Values must be copied individually... */
               while (option_values->value)
               {
                  option_v1_values->value = option_values->value;
                  option_v1_values->label = option_values->label;

                  option_values++;
                  option_v1_values++;
               }
            }
         }

         core_options_v1_intl.us    = option_v1_defs_us;
         core_options_v1_intl.local = option_v1_defs_intl;

         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL, &core_options_v1_intl);
#else
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, option_v1_defs_us);
#endif
      }
      else
      {
         /* Allocate arrays */
         variables  = (struct retro_variable *)calloc(num_options + 1,
               sizeof(struct retro_variable));
         values_buf = (char **)calloc(num_options, sizeof(char *));

         if (!variables || !values_buf)
            goto error;

         /* Copy parameters from option_defs_us array */
         for (i = 0; i < num_options; i++)
         {
            const char *key                        = option_defs_us[i].key;
            const char *desc                       = option_defs_us[i].desc;
            const char *default_value              = option_defs_us[i].default_value;
            struct retro_core_option_value *values = option_defs_us[i].values;
            size_t buf_len                         = 3;
            size_t default_index                   = 0;

            values_buf[i] = NULL;

            if (desc)
            {
               size_t num_values = 0;

               /* Determine number of values */
               while (true)
               {
                  if (values[num_values].value)
                  {
                     /* Check if this is the default value */
                     if (default_value)
                        if (strcmp(values[num_values].value, default_value) == 0)
                           default_index = num_values;

                     buf_len += strlen(values[num_values].value);
                     num_values++;
                  }
                  else
                     break;
               }

               /* Build values string */
               if (num_values > 0)
               {
                  buf_len += num_values - 1;
                  buf_len += strlen(desc);

                  values_buf[i] = (char *)calloc(buf_len, sizeof(char));
                  if (!values_buf[i])
                     goto error;

                  strcpy(values_buf[i], desc);
                  strcat(values_buf[i], "; ");

                  /* Default value goes first */
                  strcat(values_buf[i], values[default_index].value);

                  /* Add remaining values */
                  for (j = 0; j < num_values; j++)
                  {
                     if (j != default_index)
                     {
                        strcat(values_buf[i], "|");
                        strcat(values_buf[i], values[j].value);
                     }
                  }
               }
            }

            variables[option_index].key   = key;
            variables[option_index].value = values_buf[i];
            option_index++;
         }

         /* Set variables */
         environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
      }

error:
      /* Clean up */

      if (option_v1_defs_us)
      {
         free(option_v1_defs_us);
         option_v1_defs_us = NULL;
      }

#ifndef HAVE_NO_LANGEXTRA
      if (option_v1_defs_intl)
      {
         free(option_v1_defs_intl);
         option_v1_defs_intl = NULL;
      }
#endif

      if (values_buf)
      {
         for (i = 0; i < num_options; i++)
         {
            if (values_buf[i])
            {
               free(values_buf[i]);
               values_buf[i] = NULL;
            }
         }

         free(values_buf);
         values_buf = NULL;
      }

      if (variables)
      {
         free(variables);
         variables = NULL;
      }
   }
}

static const struct retro_controller_description joyport_controllers[] =
{
   { "Automatic", RETRO_DEVICE_JOYPAD },
   { "RetroPad", RETRO_DEVICE_PUAE_JOYPAD },
   { "CD32 Pad", RETRO_DEVICE_PUAE_CD32PAD },
   { "Analog Joystick", RETRO_DEVICE_PUAE_ANALOG },
   { "Arcadia", RETRO_DEVICE_PUAE_ARCADIA },
   { "Trojan Phazer Lightgun", RETRO_DEVICE_PUAE_LIGHTGUN },
   { "Lightpen", RETRO_DEVICE_PUAE_LIGHTPEN },
   { "Joystick", RETRO_DEVICE_PUAE_JOYSTICK },
   { "Keyboard", RETRO_DEVICE_PUAE_KEYBOARD },
   { "None", RETRO_DEVICE_NONE },
   { NULL, 0 }
};

static const struct retro_controller_description parport_controllers[] =
{
   { "Joystick", RETRO_DEVICE_PUAE_JOYSTICK },
   { "Keyboard", RETRO_DEVICE_PUAE_KEYBOARD },
   { "None", RETRO_DEVICE_NONE },
   { NULL, 0 }
};

static const struct retro_controller_description nonport_controllers[] =
{
   { "Keyboard", RETRO_DEVICE_PUAE_KEYBOARD },
   { "None", RETRO_DEVICE_NONE },
   { NULL, 0 }
};

static void retro_set_inputs(void)
{
   unsigned i;

   const struct retro_controller_info ports[] =
   {
      { joyport_controllers, sizeof(joyport_controllers) / sizeof(joyport_controllers[0]) },
      { joyport_controllers, sizeof(joyport_controllers) / sizeof(joyport_controllers[0]) },
      { parport_controllers, sizeof(parport_controllers) / sizeof(parport_controllers[0]) },
      { parport_controllers, sizeof(parport_controllers) / sizeof(parport_controllers[0]) },
      { nonport_controllers, sizeof(nonport_controllers) / sizeof(nonport_controllers[0]) },
      { nonport_controllers, sizeof(nonport_controllers) / sizeof(nonport_controllers[0]) },
      { NULL, 0 }
   };
   
   environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);

   #define RETRO_DESCRIPTOR_BLOCK(_user)                                                                        \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up" },                                          \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down" },                                      \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left" },                                      \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },                                    \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B / Fire / Red" },                               \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A / 2nd fire / Blue" },                          \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Y / Green" },                                    \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "X / Yellow" },                                   \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },                                  \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start / Play" },                             \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L / Rewind" },                                   \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R / Forward" },                                  \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "L2" },                                          \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "R2" },                                          \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, "L3" },                                          \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3, "R3" },                                          \
   { _user, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },   \
   { _user, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },   \
   { _user, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" }, \
   { _user, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" }

   static struct retro_input_descriptor input_descriptors[] =
   {
      RETRO_DESCRIPTOR_BLOCK(0),
      RETRO_DESCRIPTOR_BLOCK(1),
      RETRO_DESCRIPTOR_BLOCK(2),
      RETRO_DESCRIPTOR_BLOCK(3),
      RETRO_DESCRIPTOR_BLOCK(4),
      RETRO_DESCRIPTOR_BLOCK(5),
      {0},
   };
   #undef RETRO_DESCRIPTOR_BLOCK

   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, &input_descriptors);

   /* Reset analog device types to RetroPad */
   for (i = 0; i < RETRO_DEVICES; i++)
      if (retro_devices[i] == RETRO_DEVICE_ANALOG)
         retro_devices[i] = RETRO_DEVICE_JOYPAD;
}

static void retro_set_options_display(void)
{
   struct retro_core_option_display option_display;

   option_display.visible = (crop_id == CROP_AUTO);
   option_display.key = "puae_crop_delay";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

   /*** Options display ***/
   if (libretro_supports_option_categories)
   {
      option_display.visible = false;

      option_display.key = "puae_mapping_options_display";
      environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

      option_display.key = "puae_video_options_display";
      environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

      option_display.key = "puae_audio_options_display";
      environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

      option_display.key = "puae_model_options_display";
      environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
      return;
   }

   /* Model options */
   option_display.visible = opt_model_options_display;

   option_display.key = "puae_model_fd";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_model_hd";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_model_cd";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

   /* Video options */
   option_display.visible = opt_video_options_display;

   option_display.key = "puae_video_resolution";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_video_vresolution";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_video_allow_hz_change";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_video_standard";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_video_aspect";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_crop";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_crop_mode";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_crop_delay";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_vertical_pos";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_horizontal_pos";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_collision_level";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_immediate_blits";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_display_optimizations";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_keyboard_type";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_keyboard_rollover";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_chipset";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_rtc";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_cpu_data_cache";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_cpuboard";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_cpuboard_mem";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_harddrive_write_protect";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_gfx_flickerfixer";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_video_interlace_mode";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_video_progressive_mode";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_gfx_lores_filtered";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_gfx_framerate";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_gfx_colors";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_gfx_gamma";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_statusbar";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_statusbar_startup";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_statusbar_messages";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_vkbd_theme";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_vkbd_transparency";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_vkbd_dimming";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

   /* Audio options */
   option_display.visible = opt_audio_options_display;

   option_display.key = "puae_sound_stereo_separation";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_sound_stereo_delay";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_sound_stereo_swap";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_sound_interpol";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_sound_filter";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_sound_filter_type";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_sound_volume_cd";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_floppy_sound";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_floppy_sound_empty_mute";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_floppy_sound_type";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

   /* Mapping options */
   option_display.visible = opt_mapping_options_display;

   option_display.key = "puae_mapper_up";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_down";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_left";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_right";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_select";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_start";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_b";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_a";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_y";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_x";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_l";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_r";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_l2";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_r2";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_l3";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_r3";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_lu";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_ld";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_ll";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_lr";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_ru";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_rd";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_rl";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_rr";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_vkbd";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_statusbar";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_mouse_toggle";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_reset";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_aspect_ratio_toggle";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_crop_toggle";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_turbo_fire_toggle";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_save_disk_toggle";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
}

static void update_variables(void);
static bool retro_update_display(void)
{
   /* Core options */
   bool updated = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      update_variables();

   return updated;
}

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;

   /* Must set these here for the dynamic cartridge option */
   retro_set_paths();

   retro_set_core_options();
   retro_set_inputs();

   bool support_no_game = true;
   environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &support_no_game);

   struct retro_led_interface led_interface;
   if (environ_cb(RETRO_ENVIRONMENT_GET_LED_INTERFACE, &led_interface))
      if (led_interface.set_led_state && !led_state_cb)
         led_state_cb = led_interface.set_led_state;

#ifdef USE_LIBRETRO_VFS
   struct retro_vfs_interface_info vfs_iface_info;
   vfs_iface_info.required_interface_version = 2;
   vfs_iface_info.iface                      = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs_iface_info))
   {
      filestream_vfs_init(&vfs_iface_info);
      dirent_vfs_init(&vfs_iface_info);
      path_vfs_init(&vfs_iface_info);
   }
#endif
}

void set_variable(const char* key, const char* value)
{
   struct retro_variable var = {0};

   var.key   = strdup(key);
   var.value = strdup(value);
   if (environ_cb(RETRO_ENVIRONMENT_SET_VARIABLE, &var))
      log_cb(RETRO_LOG_INFO, "SET_VARIABLE: %s = \"%s\"\n", var.key, var.value);
}

char* get_variable(const char* key)
{
   struct retro_variable var = {0};

   var.key   = key;
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      return strdup(var.value);

   return NULL;
}

static void retro_set_geometry(unsigned video_config, bool init)
{
   unsigned w = PUAE_VIDEO_WIDTH;
   unsigned h = PUAE_VIDEO_HEIGHT_PAL;

   switch (video_config)
   {
      case PUAE_VIDEO_PAL_LO:
         w = PUAE_VIDEO_WIDTH / 2;
         h = PUAE_VIDEO_HEIGHT_PAL / 2;
         break;
      case PUAE_VIDEO_PAL_HI:
         w = PUAE_VIDEO_WIDTH;
         h = PUAE_VIDEO_HEIGHT_PAL / 2;
         break;
      case PUAE_VIDEO_PAL_HI_DL:
         w = PUAE_VIDEO_WIDTH;
         h = PUAE_VIDEO_HEIGHT_PAL;
         break;
      case PUAE_VIDEO_PAL_SUHI:
         w = PUAE_VIDEO_WIDTH * 2;
         h = PUAE_VIDEO_HEIGHT_PAL / 2;
         break;
      case PUAE_VIDEO_PAL_SUHI_DL:
         w = PUAE_VIDEO_WIDTH * 2;
         h = PUAE_VIDEO_HEIGHT_PAL;
         break;

      case PUAE_VIDEO_NTSC_LO:
         w = PUAE_VIDEO_WIDTH / 2;
         h = PUAE_VIDEO_HEIGHT_NTSC / 2;
         break;
      case PUAE_VIDEO_NTSC_HI:
         w = PUAE_VIDEO_WIDTH;
         h = PUAE_VIDEO_HEIGHT_NTSC / 2;
         break;
      case PUAE_VIDEO_NTSC_HI_DL:
         w = PUAE_VIDEO_WIDTH;
         h = PUAE_VIDEO_HEIGHT_NTSC;
         break;
      case PUAE_VIDEO_NTSC_SUHI:
         w = PUAE_VIDEO_WIDTH * 2;
         h = PUAE_VIDEO_HEIGHT_NTSC / 2;
         break;
      case PUAE_VIDEO_NTSC_SUHI_DL:
         w = PUAE_VIDEO_WIDTH * 2;
         h = PUAE_VIDEO_HEIGHT_NTSC;
         break;
   }

   if (init)
   {
      defaultw = w;
      defaulth = h;
   }
   else
   {
      retrow = w;
      retroh = h;
   }
}

#define GET_VAR(x) \
   var.key   = "puae_" x; \
   var.value = NULL; \
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)

static void update_variables(void)
{
   struct retro_variable var = {0};

   uae_model_config[0] = '\0';
   uae_config[0]       = '\0';

   GET_VAR("model")
   {
      strlcpy(opt_model, var.value, sizeof(opt_model));

      if (libretro_runloop_active)
      {
         changed_prefs.chipset_mask = (preset_opt.chipset == 0 ? 0
            : preset_opt.chipset == 1 ? CSMASK_ECS_AGNUS
            : preset_opt.chipset == 2 ? CSMASK_ECS_DENISE
            : preset_opt.chipset == 3 ? CSMASK_ECS_DENISE | CSMASK_ECS_AGNUS
            : preset_opt.chipset == 5 ? CSMASK_A1000
            : CSMASK_AGA | CSMASK_ECS_DENISE | CSMASK_ECS_AGNUS);

         changed_prefs.cs_compatible = currprefs.cs_compatible = preset_opt.cs_compatible;
         built_in_chipset_prefs(&changed_prefs);

         changed_prefs.address_space_24 = preset_opt.address_space_24;
      }
   }

   GET_VAR("model_fd")
   {
      strlcpy(opt_model_fd, var.value, sizeof(opt_model_fd));
   }

   GET_VAR("model_hd")
   {
      strlcpy(opt_model_hd, var.value, sizeof(opt_model_hd));
   }

   GET_VAR("model_cd")
   {
      strlcpy(opt_model_cd, var.value, sizeof(opt_model_cd));
   }

   GET_VAR("kickstart")
   {
      strlcpy(opt_kickstart, var.value, sizeof(opt_kickstart));

      if (libretro_runloop_active)
      {
         if (!strcmp(opt_kickstart, "aros"))
            strcpy(changed_prefs.romfile, "");
         else
            strcpy(changed_prefs.romfile, uae_kickstart);
      }
   }

   GET_VAR("cart_file")
   {
      char cart_full[RETRO_PATH_MAX] = {0};

      if (!strcmp(var.value, ":hrtmon"))
         strcat(uae_config, "cart_internal=hrtmon\n");
      else if (strcmp(var.value, "none"))
      {
         snprintf(cart_full, sizeof(cart_full), "%s%s%s",
               retro_system_data_directory, DIR_SEP_STR, var.value);
         strcat(uae_config, "cart_file=");
         strcat(uae_config, cart_full);
         strcat(uae_config, "\n");
      }
   }

   GET_VAR("rtg_board")
   {
      if (strcmp(var.value, "disabled"))
      {
         char rtg_line[RETRO_PATH_MAX + 64] = {0};
         struct retro_variable vram = { "puae_rtg_vram", NULL };
         int size = 0;

         if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &vram) && vram.value && strcmp(vram.value, "auto"))
            size = atoi(vram.value);
         if (!size)
         {
            if (!strcmp(var.value, "ZorroIII"))
               size = 32;
            else if (!strcmp(var.value, "ZorroII"))
               size = 4;
            else
            {
               /* The most the real board had */
               struct rtgboardconfig rbc = {0};
               int j, id;
               for (j = 0; (id = gfxboard_get_id_from_index(j)) >= 0; j++)
               {
                  const TCHAR *name = gfxboard_get_configname(id);
                  if (name && !strcasecmp(name, var.value))
                  {
                     rbc.rtgmem_type = id;
                     size = gfxboard_get_vram_max(&rbc) >> 20;
                     break;
                  }
               }
               if (!size)
                  size = 4;
            }
         }
         snprintf(rtg_line, sizeof(rtg_line), "gfxcard_type=%s\ngfxcard_size=%d\n", var.value, size);
         strcat(uae_config, rtg_line);

         /* Picasso IV: its flash ROM, found by content */
         if (strstr(var.value, "PicassoIV") && puae_picassoiv_rom[0])
         {
            snprintf(rtg_line, sizeof(rtg_line), "picassoiv_rom_file=%s\n", puae_picassoiv_rom);
            strcat(uae_config, rtg_line);
         }
      }
   }

   GET_VAR("cd32_fmv")
   {
      const char *rom = var.value;

      /* 'auto': the first FMV ROM the option scan found */
      if (!strcmp(rom, "auto"))
         rom = puae_fmvs[2].value;

      if (strcmp(var.value, "disabled") && rom)
      {
         char fmv_full[RETRO_PATH_MAX] = {0};
         snprintf(fmv_full, sizeof(fmv_full), "%s%s%s",
               retro_system_data_directory, DIR_SEP_STR, rom);
         strcat(uae_config, "cd32fmv_rom_file=");
         strcat(uae_config, fmv_full);
         strcat(uae_config, "\n");
      }
   }

   GET_VAR("video_standard")
   {
      if (strstr(var.value, "auto")) opt_region_auto = true;
      else                           opt_region_auto = false;

      /* video_config change only at start */
      if (video_config_old == 0 && forced_video < 0)
      {
         if (strstr(var.value, "PAL"))
         {
            video_config |= PUAE_VIDEO_PAL;
            video_config &= ~PUAE_VIDEO_NTSC;
            strcat(uae_config, "ntsc=false\n");
            real_ntsc = false;
         }
         else
         {
            video_config |= PUAE_VIDEO_NTSC;
            video_config &= ~PUAE_VIDEO_PAL;
            strcat(uae_config, "ntsc=true\n");
            real_ntsc = true;
         }
      }
      else if (!opt_region_auto)
      {
         if (strstr(var.value, "PAL")) changed_prefs.ntscmode = 0;
         else                          changed_prefs.ntscmode = 1;
      }
      else if (opt_region_auto)
      {
         if (forced_video > -1)
            changed_prefs.ntscmode = !forced_video;
         else
            changed_prefs.ntscmode = !(strstr(var.value, "PAL"));
      }
   }

   GET_VAR("video_aspect")
   {
      int video_config_aspect_prev = video_config_aspect;

      if      (!strcmp(var.value, "PAL"))       video_config_aspect = PUAE_VIDEO_PAL;
      else if (!strcmp(var.value, "NTSC"))      video_config_aspect = PUAE_VIDEO_NTSC;
      else if (!strcmp(var.value, "NTSC auto")) video_config_aspect = PUAE_VIDEO_NTSCAUTO;
      else if (!strcmp(var.value, "1:1"))       video_config_aspect = PUAE_VIDEO_1x1;
      else                                      video_config_aspect = 0;

      /* Revert if aspect ratio is locked */
      if (opt_aspect_ratio_locked)
         video_config_aspect = video_config_aspect_prev;
   }

   GET_VAR("video_allow_hz_change")
   {
      if      (!strcmp(var.value, "disabled")) video_config_allow_hz_change = 0;
      else if (!strcmp(var.value, "enabled"))  video_config_allow_hz_change = 1;
      else if (!strcmp(var.value, "locked"))   video_config_allow_hz_change = 2;

      switch (video_config_allow_hz_change)
      {
         case 0:
         case 1:
            strcat(uae_config, "displaydata_pal=-1,pal\n");
            strcat(uae_config, "displaydata_ntsc=-1,ntsc\n");
            break;
         case 2:
            strcat(uae_config, "displaydata_pal=50.000000,locked,pal\n");
            strcat(uae_config, "displaydata_ntsc=59.940000,locked,ntsc\n");
            break;
      }
   }

   GET_VAR("video_resolution")
   {
      opt_video_resolution_auto = RESOLUTION_AUTO_NONE;

      if (!strcmp(var.value, "lores"))
      {
         video_config &= ~PUAE_VIDEO_HIRES;
         video_config &= ~PUAE_VIDEO_SUPERHIRES;
         retro_max_diwlastword = retro_max_diwlastword_hires / 2;
         width_multiplier = 1;
         if (libretro_runloop_active)
            changed_prefs.gfx_resolution = RES_LORES;
      }
      else if (!strcmp(var.value, "hires"))
      {
         video_config &= ~PUAE_VIDEO_SUPERHIRES;
         video_config |= PUAE_VIDEO_HIRES;
         retro_max_diwlastword = retro_max_diwlastword_hires;
         width_multiplier = 2;
         if (libretro_runloop_active)
            changed_prefs.gfx_resolution = RES_HIRES;
      }
      else if (!strcmp(var.value, "superhires"))
      {
         video_config &= ~PUAE_VIDEO_HIRES;
         video_config |= PUAE_VIDEO_SUPERHIRES;
         retro_max_diwlastword = retro_max_diwlastword_hires * 2;
         width_multiplier = 4;
         if (libretro_runloop_active)
            changed_prefs.gfx_resolution = RES_SUPERHIRES;
      }
      else if (!strcmp(var.value, "auto") || !strcmp(var.value, "auto-lores") || !strcmp(var.value, "auto-superhires"))
      {
         opt_video_resolution_auto = RESOLUTION_AUTO_HIRES;
         if (!strcmp(var.value, "auto-lores"))
            opt_video_resolution_auto = RESOLUTION_AUTO_LORES;
         else if (!strcmp(var.value, "auto-superhires"))
            opt_video_resolution_auto = RESOLUTION_AUTO_SUPERHIRES;

         if (video_config_old & PUAE_VIDEO_SUPERHIRES)
         {
            video_config &= ~PUAE_VIDEO_HIRES;
            video_config |= PUAE_VIDEO_SUPERHIRES;
            retro_max_diwlastword = retro_max_diwlastword_hires * 2;
            width_multiplier = 4;
            if (libretro_runloop_active)
               changed_prefs.gfx_resolution = RES_SUPERHIRES;
         }
         else
         {
            video_config &= ~PUAE_VIDEO_SUPERHIRES;
            video_config |= PUAE_VIDEO_HIRES;
            retro_max_diwlastword = retro_max_diwlastword_hires;
            width_multiplier = 2;
            if (libretro_runloop_active)
               changed_prefs.gfx_resolution = RES_HIRES;
         }

         /* Prevent resetting to hires if already at lores in auto-lores */
         if (libretro_runloop_active)
         {
            if (     opt_video_resolution_auto == RESOLUTION_AUTO_LORES
                  && detected_screen_resolution == RES_LORES)
            {
               video_config &= ~PUAE_VIDEO_HIRES;
               video_config &= ~PUAE_VIDEO_SUPERHIRES;
               retro_max_diwlastword = retro_max_diwlastword_hires / 2;
               width_multiplier = 1;
               changed_prefs.gfx_resolution = RES_LORES;
            }
         }
      }

      /* Resolution change needs init_custom() to be done after reset_drawing() is done */
      if (libretro_runloop_active && video_config != video_config_old)
         request_init_custom_timer = 2;
   }

   GET_VAR("video_vresolution")
   {
      opt_video_vresolution_auto = false;

      if (!strcmp(var.value, "double"))
      {
         video_config |= PUAE_VIDEO_DOUBLELINE;
         if (libretro_runloop_active)
            changed_prefs.gfx_vresolution = VRES_DOUBLE;
      }
      else if (!strcmp(var.value, "single"))
      {
         video_config &= ~PUAE_VIDEO_DOUBLELINE;
         if (libretro_runloop_active)
            changed_prefs.gfx_vresolution = VRES_NONDOUBLE;
      }
      else if (!strcmp(var.value, "auto"))
      {
         opt_video_vresolution_auto = true;

         if (libretro_runloop_active)
         {
            if (video_config_old & PUAE_VIDEO_DOUBLELINE)
            {
               video_config |= PUAE_VIDEO_DOUBLELINE;
               changed_prefs.gfx_vresolution = VRES_DOUBLE;
            }
            else
            {
               video_config &= ~PUAE_VIDEO_DOUBLELINE;
               changed_prefs.gfx_vresolution = VRES_NONDOUBLE;
            }
         }
      }

      /* Lores can not be double lined */
      if (retro_max_diwlastword < retro_max_diwlastword_hires)
      {
         video_config &= ~PUAE_VIDEO_DOUBLELINE;
         changed_prefs.gfx_vresolution = VRES_NONDOUBLE;
      }

      /* Resolution change needs init_custom() to be done after reset_drawing() is done */
      if (libretro_runloop_active && video_config != video_config_old)
         request_init_custom_timer = 2;
   }

   GET_VAR("statusbar")
   {
      opt_statusbar = 0;

      if (strstr(var.value, "top"))
      {
         opt_statusbar |= STATUSBAR_TOP;
         opt_statusbar_position = -1;
      }
      else
      {
         opt_statusbar |= STATUSBAR_BOTTOM;
         opt_statusbar_position = 0;
      }

      if (strstr(var.value, "basic"))
         opt_statusbar |= STATUSBAR_BASIC;

      if (strstr(var.value, "minimal"))
         opt_statusbar |= STATUSBAR_MINIMAL;

      opt_statusbar_position_old = opt_statusbar_position;
   }

   GET_VAR("statusbar_startup")
   {
      if (!libretro_runloop_active)
      {
         if (!strcmp(var.value, "enabled"))
            retro_statusbar = true;
         else
            retro_statusbar = false;
      }
   }

   GET_VAR("statusbar_messages")
   {
      if (!strcmp(var.value, "enabled"))
         opt_statusbar |= STATUSBAR_MESSAGES;
   }

   GET_VAR("vkbd_theme")
   {
      if      (strstr(var.value, "auto"))    opt_vkbd_theme = 0;
      else if (strstr(var.value, "beige"))   opt_vkbd_theme = 1;
      else if (strstr(var.value, "cd32"))    opt_vkbd_theme = 2;
      else if (strstr(var.value, "dark"))    opt_vkbd_theme = 3;
      else if (strstr(var.value, "light"))   opt_vkbd_theme = 4;

      if      (strstr(var.value, "outline")) opt_vkbd_theme |= 0x80;
   }

   GET_VAR("vkbd_transparency")
   {
      if      (!strcmp(var.value, "0%"))   opt_vkbd_alpha = GRAPH_ALPHA_100;
      else if (!strcmp(var.value, "25%"))  opt_vkbd_alpha = GRAPH_ALPHA_75;
      else if (!strcmp(var.value, "50%"))  opt_vkbd_alpha = GRAPH_ALPHA_50;
      else if (!strcmp(var.value, "75%"))  opt_vkbd_alpha = GRAPH_ALPHA_25;
      else if (!strcmp(var.value, "100%")) opt_vkbd_alpha = GRAPH_ALPHA_0;
   }

   GET_VAR("vkbd_dimming")
   {
      if      (!strcmp(var.value, "0%"))   opt_vkbd_dim_alpha = GRAPH_ALPHA_0;
      else if (!strcmp(var.value, "25%"))  opt_vkbd_dim_alpha = GRAPH_ALPHA_25;
      else if (!strcmp(var.value, "50%"))  opt_vkbd_dim_alpha = GRAPH_ALPHA_50;
      else if (!strcmp(var.value, "75%"))  opt_vkbd_dim_alpha = GRAPH_ALPHA_75;
      else if (!strcmp(var.value, "100%")) opt_vkbd_dim_alpha = GRAPH_ALPHA_100;
   }

   GET_VAR("chipmem_size")
   {
      override_opt.chipmem_size = -1;
      if (!strstr(var.value, "auto"))
         override_opt.chipmem_size = atoi(var.value);

      if (!strstr(var.value, "auto"))
      {
         strcat(uae_config, "chipmem_size=");
         strcat(uae_config, var.value);
         strcat(uae_config, "\n");
      }

      if (libretro_runloop_active)
      {
         if (!strstr(var.value, "auto"))
            changed_prefs.chipmem.size = override_opt.chipmem_size * 0x80000;
         else
            changed_prefs.chipmem.size = preset_opt.chipmem_size * 0x80000;
      }
   }

   GET_VAR("bogomem_size")
   {
      override_opt.bogomem_size = -1;
      if (!strstr(var.value, "auto"))
         override_opt.bogomem_size = atoi(var.value);

      if (!strstr(var.value, "auto"))
      {
         strcat(uae_config, "bogomem_size=");
         strcat(uae_config, var.value);
         strcat(uae_config, "\n");
      }

      if (libretro_runloop_active)
      {
         if (!strstr(var.value, "auto"))
            changed_prefs.bogomem.size = override_opt.bogomem_size * 0x40000;
         else
            changed_prefs.bogomem.size = preset_opt.bogomem_size * 0x40000;
      }
   }

   GET_VAR("fastmem_size")
   {
      override_opt.fastmem_size = -1;
      if (!strstr(var.value, "auto"))
         override_opt.fastmem_size = atoi(var.value);

      if (!strstr(var.value, "auto"))
      {
         strcat(uae_config, "fastmem_size=");
         strcat(uae_config, var.value);
         strcat(uae_config, "\n");
      }

      if (libretro_runloop_active)
      {
         if (!strstr(var.value, "auto"))
            changed_prefs.fastmem[0].size = override_opt.fastmem_size * 1024 * 1024;
         else
            changed_prefs.fastmem[0].size = preset_opt.fastmem_size * 1024 * 1024;
      }
   }

   GET_VAR("z3mem_size")
   {
      override_opt.z3mem_size = -1;
      if (!strstr(var.value, "auto"))
         override_opt.z3mem_size = atoi(var.value);

      if (!strstr(var.value, "auto"))
      {
         strcat(uae_config, "z3mem_size=");
         strcat(uae_config, var.value);
         strcat(uae_config, "\n");

         strcat(uae_config, "cpu_24bit_addressing=false\n");
      }

      if (libretro_runloop_active)
      {
         if (!strstr(var.value, "auto"))
         {
            changed_prefs.z3fastmem[0].size = override_opt.z3mem_size * 1024 * 1024;
            changed_prefs.address_space_24  = false;
         }
         else
         {
            changed_prefs.z3fastmem[0].size = preset_opt.z3mem_size * 1024 * 1024;
            changed_prefs.address_space_24  = true;
         }
      }
   }

   GET_VAR("cpu_model")
   {
      override_opt.cpu_model = -1;
      if (!strstr(var.value, "auto"))
         override_opt.cpu_model = atoi(var.value);

      if (!strstr(var.value, "auto"))
      {
         strcat(uae_config, "cpu_model=");
         strcat(uae_config, var.value);
         strcat(uae_config, "\n");

         if (override_opt.cpu_model >= 68030)
         {
            strcat(uae_config, "mmu_model=");
            strcat(uae_config, var.value);
            strcat(uae_config, "\n");

            strcat(uae_config, "cpu_24bit_addressing=false\n");
         }
      }

      if (libretro_runloop_active)
      {
         if (!strstr(var.value, "auto"))
            changed_prefs.cpu_model = override_opt.cpu_model;
         else
            changed_prefs.cpu_model = preset_opt.cpu_model;

         if (changed_prefs.cpu_model >= 68030)
         {
            changed_prefs.mmu_model = changed_prefs.cpu_model;
            changed_prefs.address_space_24 = false;
         }
         else
         {
            changed_prefs.mmu_model = 0;
            changed_prefs.address_space_24 = (changed_prefs.cpu_model < 68020) ? true : false;
         }
      }
   }

   GET_VAR("fpu_model")
   {
      override_opt.fpu_model = -1;
      if (!strcmp(var.value, "cpu") && override_opt.cpu_model >= 68030)
         override_opt.fpu_model = override_opt.cpu_model;
      else if (!strstr(var.value, "auto"))
         override_opt.fpu_model = atoi(var.value);

      /* CPU internal FPU force */
      if (override_opt.fpu_model && override_opt.cpu_model >= 68040)
         override_opt.fpu_model = override_opt.cpu_model;
      else if (override_opt.fpu_model && override_opt.fpu_model != 68881 && override_opt.cpu_model == 68030)
         override_opt.fpu_model = 68882;

      if (override_opt.fpu_model > -1)
      {
         char valbuf[8];
         snprintf(valbuf, sizeof(valbuf), "%d", override_opt.fpu_model);

         strcat(uae_config, "fpu_model=");
         strcat(uae_config, valbuf);
         strcat(uae_config, "\n");
      }

      if (libretro_runloop_active)
      {
         if (!strstr(var.value, "auto"))
            changed_prefs.fpu_model = override_opt.fpu_model;
         else
            changed_prefs.fpu_model = preset_opt.fpu_model;
      }
   }

   GET_VAR("cpu_compatibility")
   {
      if (!strcmp(var.value, "normal"))
      {
         strcat(uae_config, "cpu_compatible=false\n");
         strcat(uae_config, "cpu_cycle_exact=false\n");
         strcat(uae_config, "cpu_memory_cycle_exact=false\n");
         strcat(uae_config, "blitter_cycle_exact=false\n");
      }
      else if (!strcmp(var.value, "compatible"))
      {
         strcat(uae_config, "cpu_compatible=true\n");
         strcat(uae_config, "cpu_cycle_exact=false\n");
         strcat(uae_config, "cpu_memory_cycle_exact=false\n");
         strcat(uae_config, "blitter_cycle_exact=false\n");
      }
      else if (!strcmp(var.value, "memory"))
      {
         strcat(uae_config, "cpu_compatible=true\n");
         strcat(uae_config, "cpu_cycle_exact=false\n");
         strcat(uae_config, "cpu_memory_cycle_exact=true\n");
         strcat(uae_config, "blitter_cycle_exact=true\n");
      }
      else if (!strcmp(var.value, "exact"))
      {
         strcat(uae_config, "cpu_compatible=true\n");
         strcat(uae_config, "cpu_cycle_exact=true\n");
         strcat(uae_config, "cpu_memory_cycle_exact=true\n");
         strcat(uae_config, "blitter_cycle_exact=true\n");
      }

      if (libretro_runloop_active && !cpu_cycle_exact_force)
      {
         if (!strcmp(var.value, "normal"))
         {
            changed_prefs.cpu_compatible = 0;
            changed_prefs.cpu_cycle_exact = 0;
            changed_prefs.cpu_memory_cycle_exact = 0;
            changed_prefs.blitter_cycle_exact = 0;
         }
         else if (!strcmp(var.value, "compatible"))
         {
            changed_prefs.cpu_compatible = 1;
            changed_prefs.cpu_cycle_exact = 0;
            changed_prefs.cpu_memory_cycle_exact = 0;
            changed_prefs.blitter_cycle_exact = 0;
         }
         else if (!strcmp(var.value, "memory"))
         {
            changed_prefs.cpu_compatible = 1;
            changed_prefs.cpu_cycle_exact = 0;
            changed_prefs.cpu_memory_cycle_exact = 1;
            changed_prefs.blitter_cycle_exact = 1;
         }
         else if (!strcmp(var.value, "exact"))
         {
            changed_prefs.cpu_compatible = 1;
            changed_prefs.cpu_cycle_exact = 1;
            changed_prefs.cpu_memory_cycle_exact = 1;
            changed_prefs.blitter_cycle_exact = 1;
         }
      }
   }

   GET_VAR("cpu_throttle")
   {
      /* "max": WinUAE "fastest possible", the CPU gets all the host time
       * left in each frame (not with cycle-exact). It needs the frontend
       * clock: without it the CPU time budget of a frame would never end. */
      bool max = !strcmp(var.value, "max") && perf_cb.get_time_usec;

      const char *throttle = strcmp(var.value, "max") ? var.value : "0.0";

      /* Warn (once per combination) when the speed setting has no effect:
       * "Max" needs a non cycle-exact memory model, any throttle needs
       * a CPU that is not fully cycle-exact. */
      {
         static int warned = 0;
         struct retro_variable cv = { "puae_cpu_compatibility", NULL };
         bool ce  = environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &cv) && cv.value && !strcmp(cv.value, "exact");
         bool mce = ce || (cv.value && !strcmp(cv.value, "memory"));
         int state = (!strcmp(var.value, "max") && mce) ? 1
                   : (ce && atof(throttle) != 0.0) ? 2 : 0;
         if (state && state != warned)
         {
            snprintf(retro_message_msg, sizeof(retro_message_msg),
                  "CPU Speed '%s' has no effect with CPU Compatibility '%s': use 'Normal' or 'More compatible'.",
                  strcmp(var.value, "max") ? throttle : "Max",
                  ce ? "Cycle-exact" : "Memory cycle-exact");
            retro_message = true;
         }
         warned = state;
      }

      strcat(uae_config, max ? "cpu_speed=max\ncpu_throttle=0.0\n" : "cpu_speed=real\ncpu_throttle=");
      if (!max)
      {
         strcat(uae_config, throttle);
         strcat(uae_config, "\n");
      }

      if (libretro_runloop_active)
      {
         changed_prefs.m68k_speed          = max ? -1 : 0;
         changed_prefs.m68k_speed_throttle = max ? 0 : atof(throttle);
      }
   }

   GET_VAR("cpu_multiplier")
   {
      strcat(uae_config, "cpu_multiplier=");
      strcat(uae_config, var.value);
      strcat(uae_config, "\n");

      if (libretro_runloop_active)
         changed_prefs.cpu_clock_multiplier = atoi(var.value) * 256;
   }

   GET_VAR("sound_stereo_separation")
   {
      unsigned char val = atoi(var.value) / 10;
      char valbuf[4];
      snprintf(valbuf, sizeof(valbuf), "%d", val);

      strcat(uae_config, "sound_stereo_separation=");
      strcat(uae_config, valbuf);
      strcat(uae_config, "\n");

      if (libretro_runloop_active)
         changed_prefs.sound_stereo_separation = val;
   }

   GET_VAR("sound_stereo_delay")
   {
      int val = strcmp(var.value, "disabled") ? atoi(var.value) : 0;
      char valbuf[32];
      snprintf(valbuf, sizeof(valbuf), "sound_stereo_mixing_delay=%d\n", val);
      strcat(uae_config, valbuf);

      if (libretro_runloop_active)
         changed_prefs.sound_mixed_stereo_delay = val;
   }

   GET_VAR("sound_stereo_swap")
   {
      bool val = !strcmp(var.value, "enabled");
      strcat(uae_config, val ? "sound_stereo_swap_paula=true\n" : "sound_stereo_swap_paula=false\n");

      if (libretro_runloop_active)
         changed_prefs.sound_stereo_swap_paula = val;
   }

   GET_VAR("sound_interpol")
   {
      strcat(uae_config, "sound_interpol=");
      strcat(uae_config, var.value);
      strcat(uae_config, "\n");

      if (libretro_runloop_active)
      {
         if      (!strcmp(var.value, "none")) changed_prefs.sound_interpol = 0;
         else if (!strcmp(var.value, "anti")) changed_prefs.sound_interpol = 1;
         else if (!strcmp(var.value, "sinc")) changed_prefs.sound_interpol = 2;
         else if (!strcmp(var.value, "rh"))   changed_prefs.sound_interpol = 3;
         else if (!strcmp(var.value, "crux")) changed_prefs.sound_interpol = 4;
      }
   }

   GET_VAR("sound_filter")
   {
      strcat(uae_config, "sound_filter=");
      strcat(uae_config, var.value);
      strcat(uae_config, "\n");
      
      if (libretro_runloop_active)
      {
         if      (!strcmp(var.value, "emulated")) changed_prefs.sound_filter = FILTER_SOUND_EMUL;
         else if (!strcmp(var.value, "off"))      changed_prefs.sound_filter = FILTER_SOUND_OFF;
         else if (!strcmp(var.value, "on"))       changed_prefs.sound_filter = FILTER_SOUND_ON;
      }
   }

   GET_VAR("sound_filter_type")
   {
      if (strcmp(var.value, "auto"))
      {
         automatic_sound_filter_type_update_timer = 0;
         strcat(uae_config, "sound_filter_type=");
         strcat(uae_config, var.value);
         strcat(uae_config, "\n");
      }

      if (libretro_runloop_active)
      {
         if      (!strcmp(var.value, "standard")) changed_prefs.sound_filter_type = FILTER_SOUND_TYPE_A500;
         else if (!strcmp(var.value, "enhanced")) changed_prefs.sound_filter_type = FILTER_SOUND_TYPE_A1200;
         else if (!strcmp(var.value, "auto"))     automatic_sound_filter_type_update_timer = SOUND_FILTER_TYPE_UPDATE_TIMER;
      }
   }

   GET_VAR("sound_volume_cd")
   {
      /* 100 is mute, 0 is max */
      int val = 100 - atoi(var.value);
      char valbuf[4];
      snprintf(valbuf, 4, "%d", val);

      strcat(uae_config, "sound_volume_cd=");
      strcat(uae_config, valbuf);
      strcat(uae_config, "\n");

      if (libretro_runloop_active)
         changed_prefs.sound_volume_cd = val;
   }

   GET_VAR("autoloadfastforward")
   {
      if      (!strcmp(var.value, "disabled")) opt_autoloadfastforward = 0;
      else if (!strcmp(var.value, "fd"))       opt_autoloadfastforward = AUTOLOADFASTFORWARD_FD;
      else if (!strcmp(var.value, "hd"))       opt_autoloadfastforward = AUTOLOADFASTFORWARD_HD;
      else if (!strcmp(var.value, "cd"))       opt_autoloadfastforward = AUTOLOADFASTFORWARD_CD;
      else                                     opt_autoloadfastforward = AUTOLOADFASTFORWARD_FD |
                                                                         AUTOLOADFASTFORWARD_HD |
                                                                         AUTOLOADFASTFORWARD_CD;
   }

   GET_VAR("cd_speed")
   {
      strcat(uae_config, "cd_speed=");
      strcat(uae_config, var.value);
      strcat(uae_config, "\n");

      if (libretro_runloop_active)
         changed_prefs.cd_speed = atoi(var.value);
   }

   GET_VAR("floppy_speed")
   {
      strcat(uae_config, "floppy_speed=");
      strcat(uae_config, var.value);
      strcat(uae_config, "\n");

      if (libretro_runloop_active)
         changed_prefs.floppy_speed = atoi(var.value);
   }

   GET_VAR("floppy_multidrive")
   {
      if (!strcmp(var.value, "disabled")) opt_floppy_multidrive = false;
      else                                opt_floppy_multidrive = true;
   }

   GET_VAR("floppy_write_protection")
   {
      int val = 0;

      if (!strcmp(var.value, "enabled")) val = 1;

      if (val == 1)
         strcat(uae_config, "floppy_write_protect=true\n");

      if (libretro_runloop_active)
      {
         changed_prefs.floppy_read_only = val;
         if (changed_prefs.floppy_read_only != currprefs.floppy_read_only)
         {
            currprefs.floppy_read_only = changed_prefs.floppy_read_only;
            for (unsigned i = 0; i < MAX_FLOPPY_DRIVES; i++)
            {
               if (      string_is_empty(changed_prefs.floppyslots[i].df)
                     && !string_is_empty(dc->files[i]))
                  strcpy(changed_prefs.floppyslots[i].df, dc->files[i]);

               if (!string_is_empty(changed_prefs.floppyslots[i].df))
                  DISK_reinsert(i);

               if (!opt_floppy_multidrive)
                  break;
            }
         }
      }
   }

   GET_VAR("floppy_write_redirect")
   {
      bool opt_floppy_write_redirect_prev = opt_floppy_write_redirect;

      if (!strcmp(var.value, "disabled")) opt_floppy_write_redirect = false;
      else                                opt_floppy_write_redirect = true;

      if (libretro_runloop_active)
      {
         if (opt_floppy_write_redirect_prev != opt_floppy_write_redirect)
         {
            changed_prefs.floppy_read_only = 0;
            currprefs.floppy_read_only     = changed_prefs.floppy_read_only;

            if (!opt_floppy_write_redirect)
               floppy_close_redirect(-1);

            for (unsigned i = 0; i < MAX_FLOPPY_DRIVES; i++)
            {
               if (      string_is_empty(changed_prefs.floppyslots[i].df)
                     && !string_is_empty(dc->files[i]))
                  strcpy(changed_prefs.floppyslots[i].df, dc->files[i]);

               if (!string_is_empty(changed_prefs.floppyslots[i].df))
                  DISK_reinsert(i);

               if (!opt_floppy_multidrive)
                  break;
            }

            if (opt_floppy_write_redirect)
               floppy_open_redirect(-1);
         }
      }
   }

   GET_VAR("floppy_sound")
   {
      char val[4];
      strlcpy(val, var.value, sizeof(val));

      /* Mute with automatic fast-forwarding */
      if (opt_autoloadfastforward & AUTOLOADFASTFORWARD_FD)
         strlcpy(val, "100", sizeof(val));

      /* Sound is enabled by default if files are found, so this needs to be set always */
      /* 100 is mute, 0 is max */
      strcat(uae_config, "floppy_volume=");
      strcat(uae_config, val);
      strcat(uae_config, "\n");

      /* Setting volume in realtime will crash on first pass */
      if (libretro_runloop_active)
         for (unsigned i = 0; i < MAX_FLOPPY_DRIVES; i++)
            changed_prefs.dfxclickvolume_disk[i] = atoi(val);
   }

   GET_VAR("floppy_sound_empty_mute")
   {
      if (!strcmp(var.value, "disabled")) opt_floppy_sound_empty_mute = false;
      else                                opt_floppy_sound_empty_mute = true;
   }

   GET_VAR("floppy_sound_type")
   {
      if (!strcmp(var.value, "internal"))
      {
         strcat(uae_config, "floppy0sound=1\n");
         strcat(uae_config, "floppy1sound=1\n");
         strcat(uae_config, "floppy2sound=1\n");
         strcat(uae_config, "floppy3sound=1\n");
      }
      else
      {
         strcat(uae_config, "floppy0sound=-1\n");
         strcat(uae_config, "floppy1sound=-1\n");
         strcat(uae_config, "floppy2sound=-1\n");
         strcat(uae_config, "floppy3sound=-1\n");

         strcat(uae_config, "floppy0soundext=");
         strcat(uae_config, var.value);
         strcat(uae_config, "\n");
         strcat(uae_config, "floppy1soundext=");
         strcat(uae_config, var.value);
         strcat(uae_config, "\n");
         strcat(uae_config, "floppy2soundext=");
         strcat(uae_config, var.value);
         strcat(uae_config, "\n");
         strcat(uae_config, "floppy3soundext=");
         strcat(uae_config, var.value);
         strcat(uae_config, "\n");
      }

      if (libretro_runloop_active)
      {
         for (unsigned i = 0; i < MAX_FLOPPY_DRIVES; i++)
         {
            if (!strcmp(var.value, "internal"))
            {
               changed_prefs.floppyslots[i].dfxclick = 1;
            }
            else
            {
               changed_prefs.floppyslots[i].dfxclick = -1;
               strcpy(changed_prefs.floppyslots[i].dfxclickexternal, var.value);
            }
         }
      }
   }

   GET_VAR("mouse_speed")
   {
      strcat(uae_config, "input.mouse_speed=");
      strcat(uae_config, var.value);
      strcat(uae_config, "\n");

      if (libretro_runloop_active)
      {
         int val = 0;
         val = atoi(var.value);
         changed_prefs.input_mouse_speed = val;
      }
   }

   GET_VAR("immediate_blits")
   {
      if (!strcmp(var.value, "false"))
      {
         strcat(uae_config, "immediate_blits=false\n");
         strcat(uae_config, "waiting_blits=disabled\n");
      }
      else if (!strcmp(var.value, "immediate"))
      {
         strcat(uae_config, "immediate_blits=true\n");
         strcat(uae_config, "waiting_blits=disabled\n");
      }
      else if (!strcmp(var.value, "waiting"))
      {
         strcat(uae_config, "immediate_blits=false\n");
         strcat(uae_config, "waiting_blits=automatic\n");
      }

      if (libretro_runloop_active)
      {
         if (!strcmp(var.value, "false"))
         {
            changed_prefs.immediate_blits = 0;
            changed_prefs.waiting_blits = 0;
         }
         else if (!strcmp(var.value, "immediate"))
         {
            changed_prefs.immediate_blits = 1;
            changed_prefs.waiting_blits = 0;
         }
         else if (!strcmp(var.value, "waiting"))
         {
            changed_prefs.immediate_blits = 0;
            changed_prefs.waiting_blits = 1;
         }
      }
   }

   GET_VAR("collision_level")
   {
      strcat(uae_config, "collision_level=");
      strcat(uae_config, var.value);
      strcat(uae_config, "\n");

      if (libretro_runloop_active)
      {
         if      (!strcmp(var.value, "none"))       changed_prefs.collision_level = 0;
         else if (!strcmp(var.value, "sprites"))    changed_prefs.collision_level = 1;
         else if (!strcmp(var.value, "playfields")) changed_prefs.collision_level = 2;
         else if (!strcmp(var.value, "full"))       changed_prefs.collision_level = 3;
      }
   }

   GET_VAR("keyboard_rollover")
   {
      bool val = strcmp(var.value, "matrix") != 0;
      strcat(uae_config, val ? "keyboard_nkro=true\n" : "keyboard_nkro=false\n");

      if (libretro_runloop_active)
         changed_prefs.keyboard_nkro = val;
   }

   /* Chipset and RTC overrides: written after the model configuration, and
    * applied again while running, because the "model" option above restores
    * the model defaults (built_in_chipset_prefs) whenever options are read. */
   GET_VAR("chipset")
   {
      if (strcmp(var.value, "auto"))
      {
         strcat(uae_config, "chipset=");
         strcat(uae_config, var.value);
         strcat(uae_config, "\n");

         if (libretro_runloop_active)
            changed_prefs.chipset_mask =
                 !strcmp(var.value, "ecs_agnus")  ? CSMASK_ECS_AGNUS
               : !strcmp(var.value, "ecs_denise") ? CSMASK_ECS_DENISE
               : !strcmp(var.value, "ecs")        ? CSMASK_ECS_AGNUS | CSMASK_ECS_DENISE
               : !strcmp(var.value, "aga")        ? CSMASK_AGA | CSMASK_ECS_AGNUS | CSMASK_ECS_DENISE
               : 0;
      }
   }

   GET_VAR("rtc")
   {
      extern int puae_rtc_override;
      puae_rtc_override = -1;
      if (strcmp(var.value, "auto"))
      {
         strcat(uae_config, "rtc=");
         strcat(uae_config, var.value);
         strcat(uae_config, "\n");

         puae_rtc_override =
                 !strcmp(var.value, "MSM6242B")       ? 1
               : !strcmp(var.value, "RP5C01A")        ? 2
               : !strcmp(var.value, "MSM6242B_A2000") ? 3
               : 0;
         if (libretro_runloop_active)
            changed_prefs.cs_rtc = puae_rtc_override;
      }
   }

   GET_VAR("cpuboard")
   {
      strlcpy(opt_cpuboard, var.value, sizeof(opt_cpuboard));
   }

   GET_VAR("cpuboard_mem")
   {
      opt_cpuboard_mem = strcmp(var.value, "auto") ? atoi(var.value) : -1;
   }

   GET_VAR("cpu_data_cache")
   {
      strcat(uae_config, strcmp(var.value, "enabled") ? "cpu_data_cache=false\n" : "cpu_data_cache=true\n");
   }

   GET_VAR("harddrive_write_protect")
   {
      strcat(uae_config, strcmp(var.value, "enabled") ? "harddrive_write_protect=false\n" : "harddrive_write_protect=true\n");
   }

   GET_VAR("keyboard_type")
   {
      /* Applied with the model configuration, see retro_config_keyboard() */
      strlcpy(opt_keyboard_type, var.value, sizeof(opt_keyboard_type));
   }

   GET_VAR("display_optimizations")
   {
      strcat(uae_config, "display_optimizations=");
      strcat(uae_config, var.value);
      strcat(uae_config, "\n");

      if (libretro_runloop_active)
      {
         if      (!strcmp(var.value, "full"))    changed_prefs.cs_optimizations = DISPLAY_OPTIMIZATIONS_FULL;
         else if (!strcmp(var.value, "partial")) changed_prefs.cs_optimizations = DISPLAY_OPTIMIZATIONS_PARTIAL;
         else if (!strcmp(var.value, "none"))    changed_prefs.cs_optimizations = DISPLAY_OPTIMIZATIONS_NONE;
      }
   }

   GET_VAR("gfx_framerate")
   {
      int val = 0;
      if      (!strcmp(var.value, "disabled")) val = 1;
      else if (!strcmp(var.value, "1"))        val = 2;
      else if (!strcmp(var.value, "2"))        val = 3;

      if (val > 1)
      {
         char valbuf[4];
         snprintf(valbuf, 4, "%d", val);
         strcat(uae_config, "gfx_framerate=");
         strcat(uae_config, valbuf);
         strcat(uae_config, "\n");
      }

      if (libretro_runloop_active && !strstr(uae_custom_config, "gfx_framerate="))
         changed_prefs.gfx_framerate = val;
   }

   GET_VAR("gfx_colors")
   {
      /* Only allow screenmode change after restart */
      if (!pix_bytes_initialized)
      {
         if      (!strcmp(var.value, "16bit")) pix_bytes = 2;
         else if (!strcmp(var.value, "24bit")) pix_bytes = 4;
      }
   }

   GET_VAR("gfx_gamma")
   {
      int val = atoi(var.value);

      if (val)
      {
         char valbuf[5];
         snprintf(valbuf, 5, "%d", val);
         strcat(uae_config, "gfx_gamma=");
         strcat(uae_config, valbuf);
         strcat(uae_config, "\n");
      }

      if (libretro_runloop_active)
      {
         if   (!strcmp(var.value, "disabled")) changed_prefs.gfx_gamma = 0;
         else                                  changed_prefs.gfx_gamma = val;
      }
   }

   GET_VAR("gfx_lores_filtered")
   {
      int val = !strcmp(var.value, "enabled");
      strcat(uae_config, val ? "gfx_lores_mode=filtered\n" : "gfx_lores_mode=normal\n");

      if (libretro_runloop_active)
         changed_prefs.gfx_lores_mode = val;
   }

   GET_VAR("video_interlace_mode")
   {
      extern int puae_iscanlines;
      if      (!strcmp(var.value, "frames"))      puae_iscanlines = 0;
      else if (!strcmp(var.value, "fields_plus")) puae_iscanlines = 2;
      else                                        puae_iscanlines = 1;

      if (libretro_runloop_active)
         changed_prefs.gfx_iscanlines = puae_iscanlines;
   }

   GET_VAR("video_progressive_mode")
   {
      extern int puae_pscanlines;
      if      (!strcmp(var.value, "scanlines"))   puae_pscanlines = 1;
      else if (!strcmp(var.value, "fields"))      puae_pscanlines = 2;
      else if (!strcmp(var.value, "fields_plus")) puae_pscanlines = 3;
      else                                        puae_pscanlines = 0;

      if (libretro_runloop_active)
         changed_prefs.gfx_pscanlines = puae_pscanlines;
   }

   GET_VAR("gfx_flickerfixer")
   {
      if (!strcmp(var.value, "disabled"))
         strcat(uae_config, "gfx_flickerfixer=false\n");
      else
         strcat(uae_config, "gfx_flickerfixer=true\n");

      if (libretro_runloop_active)
      {
         if (!strcmp(var.value, "disabled")) changed_prefs.gfx_scandoubler = 0;
         else                                changed_prefs.gfx_scandoubler = 1;

         /* Change requires init_custom() */
         if (changed_prefs.gfx_scandoubler != currprefs.gfx_scandoubler)
            request_init_custom_timer = 2;
      }
   }

   GET_VAR("crop")
   {
      if      (!strcmp(var.value, "disabled")) crop_id = CROP_NONE;
      else if (!strcmp(var.value, "minimum"))  crop_id = CROP_MINIMUM;
      else if (!strcmp(var.value, "smaller"))  crop_id = CROP_SMALLER;
      else if (!strcmp(var.value, "small"))    crop_id = CROP_SMALL;
      else if (!strcmp(var.value, "medium"))   crop_id = CROP_MEDIUM;
      else if (!strcmp(var.value, "large"))    crop_id = CROP_LARGE;
      else if (!strcmp(var.value, "larger"))   crop_id = CROP_LARGER;
      else if (!strcmp(var.value, "maximum"))  crop_id = CROP_MAXIMUM;
      else if (!strcmp(var.value, "auto"))     crop_id = CROP_AUTO;

      opt_crop_id = crop_id;
   }

   GET_VAR("crop_mode")
   {
      if      (!strcmp(var.value, "both"))       crop_mode_id = CROP_MODE_BOTH;
      else if (!strcmp(var.value, "vertical"))   crop_mode_id = CROP_MODE_VERTICAL;
      else if (!strcmp(var.value, "horizontal")) crop_mode_id = CROP_MODE_HORIZONTAL;
      else if (!strcmp(var.value, "16:9"))       crop_mode_id = CROP_MODE_16_9;
      else if (!strcmp(var.value, "16:10"))      crop_mode_id = CROP_MODE_16_10;
      else if (!strcmp(var.value, "4:3"))        crop_mode_id = CROP_MODE_4_3;
      else if (!strcmp(var.value, "5:4"))        crop_mode_id = CROP_MODE_5_4;
   }

   GET_VAR("crop_delay")
   {
      if (!strcmp(var.value, "disabled")) crop_delay = false;
      else                                crop_delay = true;
   }

   GET_VAR("vertical_pos")
   {
      opt_vertical_offset = 0;
      opt_vertical_offset_auto = false;
      if (!strcmp(var.value, "auto"))
         opt_vertical_offset_auto = true;
      else if (!strcmp(var.value, "autoforce"))
      {
         opt_vertical_offset_auto = true;
         opt_vertical_offset = -1;
      }
      else
      {
         int new_vertical_offset = atoi(var.value);
         if (new_vertical_offset >= -retro_minfirstline && new_vertical_offset <= 70)
            opt_vertical_offset = new_vertical_offset;
      }
   }

   GET_VAR("horizontal_pos")
   {
      opt_horizontal_offset = 0;
      if (!strcmp(var.value, "auto"))
         opt_horizontal_offset_auto = true;
      else
      {
         opt_horizontal_offset_auto = false;
         int new_horizontal_offset = atoi(var.value);
         if (new_horizontal_offset >= -40 && new_horizontal_offset <= 40)
            opt_horizontal_offset = -new_horizontal_offset;
      }
   }

   GET_VAR("use_whdload")
   {
      if      (!strcmp(var.value, "disabled")) opt_use_whdload = 0;
      else if (!strcmp(var.value, "enabled"))  opt_use_whdload = 1;
   }

   GET_VAR("use_whdload_theme")
   {
      if      (!strcmp(var.value, "default"))  opt_use_whdload_theme = 0;
      else if (!strcmp(var.value, "native"))   opt_use_whdload_theme = 1;
   }

   GET_VAR("use_whdload_prefs")
   {
      if      (!strcmp(var.value, "disabled")) opt_use_whdload_prefs = 0;
      else if (!strcmp(var.value, "config"))   opt_use_whdload_prefs = 1;
      else if (!strcmp(var.value, "splash"))   opt_use_whdload_prefs = 2;
      else if (!strcmp(var.value, "both"))     opt_use_whdload_prefs = 3;
   }

   GET_VAR("use_whdload_buttonwait")
   {
      if (!strcmp(var.value, "disabled")) opt_use_whdload_buttonwait = false;
      else                                opt_use_whdload_buttonwait = true;
   }

   GET_VAR("use_whdload_nowritecache")
   {
      if (!strcmp(var.value, "disabled")) opt_use_whdload_nowritecache = false;
      else                                opt_use_whdload_nowritecache = true;
   }

   GET_VAR("cd_startup_delayed_insert")
   {
      if (!strcmp(var.value, "disabled")) opt_cd_startup_delayed_insert = false;
      else                                opt_cd_startup_delayed_insert = true;
   }

   GET_VAR("shared_nvram")
   {
      if (!strcmp(var.value, "disabled")) opt_shared_nvram = false;
      else                                opt_shared_nvram = true;
   }

   GET_VAR("use_boot_hd")
   {
      if      (!strcmp(var.value, "disabled")) opt_use_boot_hd = 0;
      else if (!strcmp(var.value, "files"))    opt_use_boot_hd = 1;
      else if (!strcmp(var.value, "hdf20"))    opt_use_boot_hd = 2;
      else if (!strcmp(var.value, "hdf40"))    opt_use_boot_hd = 3;
      else if (!strcmp(var.value, "hdf80"))    opt_use_boot_hd = 4;
      else if (!strcmp(var.value, "hdf128"))   opt_use_boot_hd = 5;
      else if (!strcmp(var.value, "hdf256"))   opt_use_boot_hd = 6;
      else if (!strcmp(var.value, "hdf512"))   opt_use_boot_hd = 7;
   }

   GET_VAR("joyport_pointer_color")
   {
      if      (!strcmp(var.value, "disabled")) opt_joyport_pointer_color = -1;
      else if (!strcmp(var.value, "black"))    opt_joyport_pointer_color = 0;
      else if (!strcmp(var.value, "white"))    opt_joyport_pointer_color = 1;
      else if (!strcmp(var.value, "red"))      opt_joyport_pointer_color = 2;
      else if (!strcmp(var.value, "green"))    opt_joyport_pointer_color = 3;
      else if (!strcmp(var.value, "blue"))     opt_joyport_pointer_color = 4;
      else if (!strcmp(var.value, "yellow"))   opt_joyport_pointer_color = 5;
      else if (!strcmp(var.value, "cyan"))     opt_joyport_pointer_color = 6;
      else if (!strcmp(var.value, "purple"))   opt_joyport_pointer_color = 7;
   }

   GET_VAR("analogmouse")
   {
      if      (!strcmp(var.value, "disabled")) opt_analogmouse = 0;
      else if (!strcmp(var.value, "left"))     opt_analogmouse = 1;
      else if (!strcmp(var.value, "right"))    opt_analogmouse = 2;
      else if (!strcmp(var.value, "both"))     opt_analogmouse = 3;
   }

   GET_VAR("analogmouse_deadzone")
   {
      opt_analogmouse_deadzone = atoi(var.value);
   }

   GET_VAR("analogmouse_speed")
   {
      opt_analogmouse_speed_left = atof(var.value);
   }

   GET_VAR("analogmouse_speed_right")
   {
      opt_analogmouse_speed_right = atof(var.value);
   }

   GET_VAR("dpadmouse_speed")
   {
      opt_dpadmouse_speed = atoi(var.value);
   }

   GET_VAR("physicalmouse")
   {
      if      (!strcmp(var.value, "disabled")) opt_physicalmouse = 0;
      else if (!strcmp(var.value, "enabled"))  opt_physicalmouse = 1;
      else if (!strcmp(var.value, "double"))   opt_physicalmouse = 2;
   }

   GET_VAR("keyrah_keypad_mappings")
   {
      if (!strcmp(var.value, "disabled")) opt_keyrah_keypad = false;
      else                                opt_keyrah_keypad = true;
   }

   GET_VAR("physical_keyboard_pass_through")
   {
      if (!strcmp(var.value, "disabled")) opt_keyboard_pass_through = false;
      else                                opt_keyboard_pass_through = true;
   }

   GET_VAR("model_options_display")
   {
      if (!strcmp(var.value, "disabled")) opt_model_options_display = 0;
      else                                opt_model_options_display = 1;
   }

   GET_VAR("mapping_options_display")
   {
      if (!strcmp(var.value, "disabled")) opt_mapping_options_display = 0;
      else                                opt_mapping_options_display = 1;
   }

   GET_VAR("video_options_display")
   {
      if (!strcmp(var.value, "disabled")) opt_video_options_display = 0;
      else                                opt_video_options_display = 1;
   }

   GET_VAR("audio_options_display")
   {
      if (!strcmp(var.value, "disabled")) opt_audio_options_display = 0;
      else                                opt_audio_options_display = 1;
   }

   GET_VAR("joyport")
   {
      if (!mousemode_locked)
      {
         if      (!strcmp(var.value, "joystick")) retro_mousemode = false;
         else if (!strcmp(var.value, "mouse"))    retro_mousemode = true;
      }
   }

   GET_VAR("joyport_order")
   {
      snprintf(opt_joyport_order, sizeof(opt_joyport_order), "%s", var.value);
   }

   GET_VAR("retropad_options")
   {
      if      (!strcmp(var.value, "disabled"))    opt_retropad_options = RETROPAD_OPTIONS_DISABLED;
      else if (!strcmp(var.value, "rotate"))      opt_retropad_options = RETROPAD_OPTIONS_ROTATE;
      else if (!strcmp(var.value, "jump"))        opt_retropad_options = RETROPAD_OPTIONS_JUMP;
      else if (!strcmp(var.value, "rotate_jump")) opt_retropad_options = RETROPAD_OPTIONS_ROTATE_JUMP;
   }

   GET_VAR("cd32pad_options")
   {
      if      (!strcmp(var.value, "disabled"))    opt_cd32pad_options = RETROPAD_OPTIONS_DISABLED;
      else if (!strcmp(var.value, "rotate"))      opt_cd32pad_options = RETROPAD_OPTIONS_ROTATE;
      else if (!strcmp(var.value, "jump"))        opt_cd32pad_options = RETROPAD_OPTIONS_JUMP;
      else if (!strcmp(var.value, "rotate_jump")) opt_cd32pad_options = RETROPAD_OPTIONS_ROTATE_JUMP;
   }

   GET_VAR("turbo_fire")
   {
      if (!turbo_fire_locked)
      {
         if (!strcmp(var.value, "disabled")) retro_turbo_fire = false;
         else                                retro_turbo_fire = true;
      }
   }

   GET_VAR("turbo_fire_button")
   {
      if      (!strcmp(var.value, "B"))  turbo_fire_button = RETRO_DEVICE_ID_JOYPAD_B;
      else if (!strcmp(var.value, "A"))  turbo_fire_button = RETRO_DEVICE_ID_JOYPAD_A;
      else if (!strcmp(var.value, "Y"))  turbo_fire_button = RETRO_DEVICE_ID_JOYPAD_Y;
      else if (!strcmp(var.value, "X"))  turbo_fire_button = RETRO_DEVICE_ID_JOYPAD_X;
      else if (!strcmp(var.value, "L"))  turbo_fire_button = RETRO_DEVICE_ID_JOYPAD_L;
      else if (!strcmp(var.value, "R"))  turbo_fire_button = RETRO_DEVICE_ID_JOYPAD_R;
      else if (!strcmp(var.value, "L2")) turbo_fire_button = RETRO_DEVICE_ID_JOYPAD_L2;
      else if (!strcmp(var.value, "R2")) turbo_fire_button = RETRO_DEVICE_ID_JOYPAD_R2;
   }

   GET_VAR("turbo_pulse")
   {
      turbo_pulse = atoi(var.value);
   }

   /* Mapper */
   GET_VAR("mapper_up")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_UP] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_down")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_DOWN] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_left")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_LEFT] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_right")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_RIGHT] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_select")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_SELECT] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_start")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_START] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_b")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_B] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_a")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_A] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_y")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_Y] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_x")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_X] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_l")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_L] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_r")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_R] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_l2")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_L2] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_r2")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_R2] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_l3")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_L3] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_r3")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_R3] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_lr")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_LR] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_ll")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_LL] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_ld")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_LD] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_lu")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_LU] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_rr")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_RR] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_rl")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_RL] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_rd")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_RD] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_ru")
   {
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_RU] = retro_keymap_id(var.value);
   }

   /* Mapper hotkeys */
   GET_VAR("mapper_vkbd")
   {
      mapper_keys[RETRO_MAPPER_VKBD] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_statusbar")
   {
      mapper_keys[RETRO_MAPPER_STATUSBAR] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_mouse_toggle")
   {
      mapper_keys[RETRO_MAPPER_JOYMOUSE] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_reset")
   {
      mapper_keys[RETRO_MAPPER_RESET] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_aspect_ratio_toggle")
   {
      mapper_keys[RETRO_MAPPER_ASPECT_RATIO] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_crop_toggle")
   {
      mapper_keys[RETRO_MAPPER_CROP] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_turbo_fire_toggle")
   {
      mapper_keys[RETRO_MAPPER_TURBO_FIRE] = retro_keymap_id(var.value);
   }

   GET_VAR("mapper_save_disk_toggle")
   {
      mapper_keys[RETRO_MAPPER_SAVE_DISK] = retro_keymap_id(var.value);
   }

   /* Accelerator board, last: it sets the CPU */
   retro_config_cpuboard();

   /* Setting resolution */
   switch (video_config)
   {
      case PUAE_VIDEO_PAL_LO:
      case PUAE_VIDEO_NTSC_LO:
         strcat(uae_config, "gfx_resolution=lores\n");
         strcat(uae_config, "gfx_linemode=none\n");
         break;
      case PUAE_VIDEO_PAL_HI:
      case PUAE_VIDEO_NTSC_HI:
         strcat(uae_config, "gfx_resolution=hires\n");
         strcat(uae_config, "gfx_linemode=none\n");
         break;
      case PUAE_VIDEO_PAL_HI_DL:
      case PUAE_VIDEO_NTSC_HI_DL:
         strcat(uae_config, "gfx_resolution=hires\n");
         strcat(uae_config, "gfx_linemode=double\n");
         break;
      case PUAE_VIDEO_PAL_SUHI:
      case PUAE_VIDEO_NTSC_SUHI:
         strcat(uae_config, "gfx_resolution=superhires\n");
         strcat(uae_config, "gfx_linemode=none\n");
         break;
      case PUAE_VIDEO_PAL_SUHI_DL:
      case PUAE_VIDEO_NTSC_SUHI_DL:
         strcat(uae_config, "gfx_resolution=superhires\n");
         strcat(uae_config, "gfx_linemode=double\n");
         break;
   }

   if (!libretro_runloop_active || request_init_custom_timer)
      retro_set_geometry(video_config, true);
   else
      retro_set_geometry(video_config, false);

   /* Always update av_info geometry */
   request_update_av_info = true;

   /* Only fix invalid combinations when running */
   if (libretro_runloop_active)
      fixup_prefs(&changed_prefs, false);

   /* Always trigger changed prefs */
   target_fixup_options(&changed_prefs);
   set_config_changed();
   device_check_config();

   /* Hide/show core options */
   retro_set_options_display();
}

/*****************************************************************************/
/* Disk Control */
bool retro_disk_set_eject_state(bool ejected)
{
   if (dc)
   {
      if (dc->eject_state == ejected)
         return true;
      else
         dc->eject_state = ejected;

      if (!dc->files[dc->index])
         return false;

      switch (dc->types[dc->index])
      {
         case DC_IMAGE_TYPE_HD:
         case DC_IMAGE_TYPE_WHDLOAD:
            /* No-op success for hard drives */
            return true;
         default:
            break;
      }

      if (path_is_valid(dc->files[dc->index]))
          display_current_image(((!dc->eject_state) ? dc->labels[dc->index] : ""), !dc->eject_state);

      if (dc->eject_state)
      {
         switch (dc->types[dc->index])
         {
            case DC_IMAGE_TYPE_FLOPPY:
            case DC_IMAGE_TYPE_ARCHIVE:
               floppy_close_redirect(0);
               changed_prefs.floppyslots[0].df[0] = 0;
               disk_eject(0);
               break;
            case DC_IMAGE_TYPE_CD:
               changed_prefs.cdslots[0].name[0] = 0;
               break;
            default:
               break;
         }
      }
      else if (path_is_valid(dc->files[dc->index]))
      {
         switch (dc->types[dc->index])
         {
            case DC_IMAGE_TYPE_FLOPPY:
            case DC_IMAGE_TYPE_ARCHIVE:
               /* Need to remove duplicates from external drives */
               for (unsigned i = 1; i < MAX_FLOPPY_DRIVES; i++)
                  if (!strcmp(currprefs.floppyslots[i].df, dc->files[dc->index]))
                  {
                     changed_prefs.floppyslots[i].df[0] = 0;
                     disk_eject(i);
                  }

               strcpy(changed_prefs.floppyslots[0].df, dc->files[dc->index]);
               floppy_open_redirect(0);
               DISK_reinsert(0);
               break;
            case DC_IMAGE_TYPE_CD:
               strcpy(changed_prefs.cdslots[0].name, dc->files[dc->index]);
               break;
            default:
               break;
         }
      }
   }

   return true;
}

static bool retro_disk_get_eject_state(void)
{
   if (dc)
      return dc->eject_state;

   return true;
}

static unsigned retro_disk_get_image_index(void)
{
   if (dc)
      return dc->index;

   return 0;
}

bool retro_disk_set_image_index(unsigned index)
{
   if (dc)
   {
      if (index == dc->index)
         return true;

      if (dc->replace)
      {
         dc->replace = false;
         index = 0;
      }

      if (index < dc->count && dc->files[index])
      {
         dc->index = index;

         switch (dc->types[dc->index])
         {
            case DC_IMAGE_TYPE_FLOPPY:
               log_cb(RETRO_LOG_INFO, "Disk (%d) inserted in drive DF0: \"%s\".\n", dc->index + 1, dc->files[dc->index]);
               break;
            case DC_IMAGE_TYPE_CD:
               log_cb(RETRO_LOG_INFO, "CD (%d) inserted in drive CD0: \"%s\".\n", dc->index + 1, dc->files[dc->index]);
               break;
            default:
               return true;
               break;
         }

         display_current_image(dc->labels[dc->index], false);
         return true;
      }
   }

   return false;
}

static unsigned retro_disk_get_num_images(void)
{
   if (dc)
      return dc->count;

   return 0;
}

static bool retro_disk_replace_image_index(unsigned index, const struct retro_game_info *info)
{
   if (dc)
   {
      if (info != NULL)
         dc_replace_file(dc, index, info->path);
      else
         dc_remove_file(dc, index);
      return true;
   }

   return false;
}

static bool retro_disk_add_image_index(void)
{
   if (dc)
   {
      if (dc->count <= DC_MAX_SIZE)
      {
         dc->files[dc->count]  = NULL;
         dc->labels[dc->count] = NULL;
         dc->types[dc->count]  = DC_IMAGE_TYPE_NONE;
         dc->count++;
         return true;
      }
   }

   return false;
}

static bool retro_disk_get_image_path(unsigned index, char *path, size_t len)
{
   if (len < 1)
      return false;

   if (dc)
   {
      if (index < dc->count)
      {
         if (!string_is_empty(dc->files[index]))
         {
            strlcpy(path, dc->files[index], len);
            return true;
         }
      }
   }

   return false;
}

static bool retro_disk_get_image_label(unsigned index, char *label, size_t len)
{
   if (len < 1)
      return false;

   if (dc)
   {
      if (index < dc->count)
      {
         if (!string_is_empty(dc->labels[index]))
         {
            strlcpy(label, dc->labels[index], len);
            return true;
         }
      }
   }

   return false;
}

static struct retro_disk_control_callback disk_interface = {
   retro_disk_set_eject_state,
   retro_disk_get_eject_state,
   retro_disk_get_image_index,
   retro_disk_set_image_index,
   retro_disk_get_num_images,
   retro_disk_replace_image_index,
   retro_disk_add_image_index,
};

static struct retro_disk_control_ext_callback disk_interface_ext = {
   retro_disk_set_eject_state,
   retro_disk_get_eject_state,
   retro_disk_get_image_index,
   retro_disk_set_image_index,
   retro_disk_get_num_images,
   retro_disk_replace_image_index,
   retro_disk_add_image_index,
   NULL, /* set_initial_image */
   retro_disk_get_image_path,
   retro_disk_get_image_label,
};

/*****************************************************************************/

static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
   (void)level;
   va_list va;
   va_start(va, fmt);
   vfprintf(stderr, fmt, va);
   va_end(va);
}

void retro_init(void)
{
   struct retro_log_callback log;
   log_cb = fallback_log;
   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;

   if (!environ_cb(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf_cb))
      perf_cb.get_time_usec = NULL;

   /* Clean ZIP temp */
   if (!string_is_empty(retro_temp_directory) && path_is_directory(retro_temp_directory))
      remove_recurse(retro_temp_directory);

   /* Disk Control interface */
   dc = dc_create();
   unsigned dci_version = 0;
   if (environ_cb(RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION, &dci_version) && (dci_version >= 1))
      environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE, &disk_interface_ext);
   else
      environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, &disk_interface);

   /* Keyboard callback */
   static struct retro_keyboard_callback keyboard_callback = {retro_keyboard_event};
   environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &keyboard_callback);

   /* Core option display callback */
   struct retro_core_options_update_display_callback update_display_callback = {retro_update_display};
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK, &update_display_callback);

   /* > Ensure save state de-serialization file
    *   is closed/NULL
    *   (redundant safety check, possibly required
    *   for static builds...) */
   if (retro_deserialize_file)
   {
      zfile_fclose(retro_deserialize_file);
      retro_deserialize_file = NULL;
   }

   if (environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL))
      libretro_supports_bitmasks = true;

   if (environ_cb(RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE, NULL))
      libretro_supports_ff_override = true;

   bool achievements = true;
   environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS, &achievements);

   memset(retro_bmp, 0, sizeof(retro_bmp));
   init_output_audio_buffer(2048);

   libretro_runloop_active = false;
   update_variables();
}

void retro_deinit(void)
{
   if (save_state_file_size)
      leave_program();

   /* Clean ZIP temp */
   if (!string_is_empty(retro_temp_directory) && path_is_directory(retro_temp_directory))
      remove_recurse(retro_temp_directory);

   /* Clean Disc Control context */
   if (dc)
      dc_free(dc);

   /* Clean dynamic core option info */
   free_puae_core_options();

   /* Free buffers used by libretro-graph */
   libretro_graph_free();

   /* Free audio buffer */
   free_output_audio_buffer();

   /* 'Reset' troublesome static variables */
   pix_bytes_initialized = false;
   libretro_supports_bitmasks = false;
   libretro_supports_ff_override = false;
   libretro_supports_option_categories = false;
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   if (port < RETRO_DEVICES)
   {
      retro_devices[port] = device;

      if (port < 2)
      {
         int uae_port = (port == 0) ? 1 : 0;

         cd32_pad_enabled[uae_port]    = 0;
         arcadia_pad_enabled[uae_port] = 0;

         if (  (device == RETRO_DEVICE_PUAE_CD32PAD) ||
               (device == RETRO_DEVICE_JOYPAD && (strstr(full_path, "CD32") || strstr(uae_preset, "CD32"))))
            cd32_pad_enabled[uae_port] = 1;
         else
         if (  (device == RETRO_DEVICE_PUAE_ARCADIA) ||
               (device == RETRO_DEVICE_JOYPAD && strstr(full_path, "_Arcadia")))
            arcadia_pad_enabled[uae_port] = 1;

         changed_prefs.jports[port].jd[0].mode    = 0;
         changed_prefs.jports[port].jd[0].submode = 0;

         if (device == RETRO_DEVICE_PUAE_LIGHTGUN)
         {
            changed_prefs.jports[port].jd[0].mode    = 8;
            changed_prefs.jports[port].jd[0].submode = 1;
         }
         else
         if (device == RETRO_DEVICE_PUAE_LIGHTPEN)
         {
            changed_prefs.jports[port].jd[0].mode    = 8;
            changed_prefs.jports[port].jd[0].submode = 0;
         }
      }

      /* After startup input_get_default_joystick will need to be refreshed for cd32<>joystick change to work.
         Doing updateconfig straight from boot will crash, hence inputdevice_finalized */
      if (inputdevice_finalized)
         inputdevice_updateconfig(NULL, &currprefs);
   }
}

void retro_get_system_info(struct retro_system_info *info)
{
#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif
   memset(info, 0, sizeof(*info));
   info->library_name     = "PUAE";
   info->library_version  = PACKAGE_VERSION "" GIT_VERSION;
   info->need_fullpath    = true;
   info->block_extract    = true;
   info->valid_extensions = "adf|adz|dms|fdi|raw|ipf|hdf|hdz|lha|slave|info|cue|ccd|nrg|mds|iso|chd|uae|m3u|zip|7z|/";
}

float retro_get_aspect_ratio(unsigned int width, unsigned int height, bool pixel_aspect)
{
   double ar  = 1;
   double par = 1;

   if (     (video_config_geometry & PUAE_VIDEO_NTSC || video_config_aspect == PUAE_VIDEO_NTSC)
         || (video_config_aspect == PUAE_VIDEO_NTSCAUTO && retroh_crop <= PUAE_VIDEO_HEIGHT_NTSC / ((video_config & PUAE_VIDEO_DOUBLELINE) && !retro_doublescan ? 1 : 2)))
      par = 28512.0f / 33173.0f; /* 43:50 */
   else if (video_config_geometry & PUAE_VIDEO_PAL || video_config_aspect == PUAE_VIDEO_PAL)
      par = 9600000.0f / 9221927.0f; /* 26:25 */

   if (video_config_aspect == PUAE_VIDEO_1x1 || video_productivity)
      par = 1;

   ar = ((double)width / (double)height) * par;

   if (video_productivity)
   {
      if (retro_av_info_is_lace && !retro_doublescan)
         ar *= 2.0f;

      if (!retro_av_info_is_lace && !retro_doublescan && (video_config_geometry & PUAE_VIDEO_DOUBLELINE))
         ar *= 2.0f;

      if (retro_av_info_is_lace && retro_doublescan && (video_config_geometry & PUAE_VIDEO_QUADLINE))
         ar *= 2.0f;

      if (retro_doublescan || (retrow == PUAE_VIDEO_WIDTH_S72 || retrow == PUAE_VIDEO_WIDTH_S72 * 2))
         ar *= 2.0f;

      if (video_config_geometry & PUAE_VIDEO_HIRES)
         ar /= 2.0f;
      else if (video_config_geometry & PUAE_VIDEO_SUPERHIRES)
         ar /= 4.0f;

      /* Set productivity modes to 4:3 if not forced to 1:1 */
      if (video_config_aspect != PUAE_VIDEO_1x1)
      {
         par = (4.0f / 3.0f);
         ar  = par;
      }
   }
   else if (video_config_geometry & PUAE_VIDEO_DOUBLELINE)
   {
      if (video_config_geometry & PUAE_VIDEO_HIRES)
         ;
      else if (video_config_geometry & PUAE_VIDEO_SUPERHIRES)
         ar /= 2.0f;
   }
   else
   {
      if (video_config_geometry & PUAE_VIDEO_HIRES)
         ar /= 2.0f;
      else if (video_config_geometry & PUAE_VIDEO_SUPERHIRES)
         ar /= 4.0f;
   }

   if (pixel_aspect)
      return par;
   return ar;
}

static float retro_default_refresh(void)
{
   /* Use TV standards with "Locked" mode */
   if (video_config_allow_hz_change == 2)
      return (retro_get_region() == RETRO_REGION_NTSC) ? 59.94f : 50.00f;
   return (retro_get_region() == RETRO_REGION_NTSC) ? PUAE_VIDEO_HZ_NTSC : PUAE_VIDEO_HZ_PAL;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   /* Prevent video reinits on geometry increase */
   if (opt_video_resolution_auto == RESOLUTION_AUTO_SUPERHIRES)
      retrow_max = EMULATOR_DEF_WIDTH * 2;

   info->geometry.base_width   = retrow;
   info->geometry.base_height  = retroh;
   info->geometry.max_width    = retrow_max;
   info->geometry.max_height   = retroh_max;
   info->geometry.aspect_ratio = retro_get_aspect_ratio(retrow, retroh, false);

   if (!retro_refresh)
      retro_refresh            = retro_default_refresh();

   info->timing.fps            = retro_refresh;
   info->timing.sample_rate    = 44100.0;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
   audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

#define RETRO_AUDIO_BATCH

void retro_audio_queue(const int16_t *data, int32_t samples)
{
   if ((samples < 1) || !libretro_runloop_active)
      return;

#ifdef RETRO_AUDIO_BATCH
   if (output_audio_buffer.capacity - output_audio_buffer.size < samples)
      ensure_output_audio_buffer_capacity((output_audio_buffer.capacity + samples) * 1.5);
   memcpy(output_audio_buffer.data + output_audio_buffer.size, data, samples * sizeof(*output_audio_buffer.data));
   output_audio_buffer.size += samples;
#else
   for (int x = 0; x < samples; x += 2) audio_cb(data[x], data[x + 1]);
#endif
}



static void retro_config_append(const char *row, ...)
{
   char output[4096];
   va_list ap;

   if (row == NULL)
      return;

   va_start(ap, row);
   vsprintf(output, row, ap);
   va_end(ap);

   strlcat(uae_full_config, output, sizeof(uae_full_config));

   /* Cycle-exact force */
   if (strstr(output, "cycle_exact=true") && output[0] == 'c')
      cpu_cycle_exact_force = true;

   /* Parse Kickstart for alternate namings */
   if (strstr(output, "kickstart_rom_file=") && output[0] == 'k')
   {
      char *token = strtok(output, "=");
      while (token != NULL)
      {
         snprintf(uae_kickstart, sizeof(uae_kickstart), "%s", trimwhitespace(token));
         token = strtok(NULL, "=");
      }
   }

   /* Parse diskimage & floppy rows */
   if ((strstr(output, "diskimage") && output[0] == 'd') ||
       (strstr(output, "floppy") && output[0] == 'f'))
   {
      char disk_image[RETRO_PATH_MAX] = {0};
      char disk_image_label[RETRO_PATH_MAX] = {0};

      char *token = strtok(output, "=");
      while (token != NULL)
      {
         snprintf(disk_image, sizeof(disk_image), "%s", token);
         token = strtok(NULL, "=");
      }
      strtok(disk_image, "\n");
      if (!string_is_empty(disk_image) && path_is_valid(disk_image))
      {
         /* Add the file to Disk Control */
         fill_pathname(disk_image_label, path_basename(disk_image), "", sizeof(disk_image_label));
         dc_add_file(dc, disk_image, disk_image_label);
      }
   }

   /* Parse and replace presets */
   if (string_count_occurrences_single_character(output, '\n') == 1)
   {
      if (strstr(output, "chipmem_size=") && output[0] == 'c')
      {
         char *token = strtok(output, "=");
         while (token != NULL)
         {
            preset_opt.chipmem_size = atoi(token);
            token = strtok(NULL, "=");
         }
      }

      if (strstr(output, "bogomem_size=") && output[0] == 'b')
      {
         char *token = strtok(output, "=");
         while (token != NULL)
         {
            preset_opt.bogomem_size = atoi(token);
            token = strtok(NULL, "=");
         }
      }

      if (strstr(output, "fastmem_size=") && output[0] == 'f')
      {
         char *token = strtok(output, "=");
         while (token != NULL)
         {
            preset_opt.fastmem_size = atoi(token);
            token = strtok(NULL, "=");
         }
      }

      if (strstr(output, "z3mem_size=") && output[0] == 'z')
      {
         char *token = strtok(output, "=");
         while (token != NULL)
         {
            preset_opt.z3mem_size = atoi(token);
            token = strtok(NULL, "=");
         }
      }

      if (strstr(output, "cpu_model=") && output[0] == 'c')
      {
         char *token = strtok(output, "=");
         while (token != NULL)
         {
            preset_opt.cpu_model = atoi(token);
            token = strtok(NULL, "=");
         }
      }

      if (strstr(output, "fpu_model=") && output[0] == 'f')
      {
         char *token = strtok(output, "=");
         while (token != NULL)
         {
            preset_opt.fpu_model = atoi(token);
            token = strtok(NULL, "=");
         }
      }

      if (strstr(output, "chipset=") && output[0] == 'c')
      {
         char value[255];
         char *token = strtok(output, "=");
         while (token != NULL)
         {
            snprintf(value, sizeof(value), "%s", trimwhitespace(token));
            token = strtok(NULL, "=");
         }
         cfgfile_strval ("chipset", value, _T("chipset"), &preset_opt.chipset, csmode, 0);
      }

      if (strstr(output, "chipset_compatible=") && output[0] == 'c')
      {
         char value[255];
         char *token = strtok(output, "=");
         while (token != NULL)
         {
            snprintf(value, sizeof(value), "%s", trimwhitespace(token));
            token = strtok(NULL, "=");
         }
         cfgfile_strval ("chipset_compatible", value, _T("chipset_compatible"), &preset_opt.cs_compatible, cscompa, 0);
      }
   }
}

static void retro_config_force_region(void)
{
   /* If region was specified in the path */
   if (strstr(full_path, "NTSC") || strstr(full_path, "(USA)"))
   {
      log_cb(RETRO_LOG_INFO, "Forcing NTSC mode.\n");
      retro_config_append("ntsc=true\n");
      real_ntsc = true;
      forced_video = RETRO_REGION_NTSC;
   }
   else if (strstr(full_path, "PAL") || strstr(full_path, "(Europe)")
         || strstr(full_path, "(France)") || strstr(full_path, "(Germany)")
         || strstr(full_path, "(Italy)") || strstr(full_path, "(Spain)")
         || strstr(full_path, "(Finland)") || strstr(full_path, "(Denmark)")
         || strstr(full_path, "(Sweden)"))
   {
      log_cb(RETRO_LOG_INFO, "Forcing PAL mode.\n");
      retro_config_append("ntsc=false\n");
      real_ntsc = false;
      forced_video = RETRO_REGION_PAL;
   }
}

static void retro_config_boot_hd(void)
{
   char *tmp_str       = NULL;
   char boothd_size[5] = {0};
   char volume[10]     = {0};
   char label[10]      = {0};

   int m3u = 0;
   if (strendswith(full_path, "m3u"))
      m3u = dc_inspect_m3u(full_path);

   snprintf(label, sizeof(label), "%s", "BootHD");
   snprintf(volume, sizeof(volume), "%s", "DH0");
   /* Many HD installers have DH0: hardcoded as destination,
    * and WHDLoad + HDF launching require the use of DH0:,
    * therefore use 'BootHD:' as volume name */
   if (dc_get_image_type(full_path) == DC_IMAGE_TYPE_WHDLOAD ||
       dc_get_image_type(full_path) == DC_IMAGE_TYPE_HD ||
       m3u & (1 << DC_IMAGE_TYPE_HD) ||
       m3u & (1 << DC_IMAGE_TYPE_WHDLOAD))
      snprintf(volume, sizeof(volume), "%s", label);

   snprintf(boothd_size, sizeof(boothd_size), "%dM", 0);
   if (opt_use_boot_hd > 1)
   {
      switch (opt_use_boot_hd)
      {
         case 2:
            snprintf(boothd_size, sizeof(boothd_size), "%dM", 20);
            break;
         case 3:
            snprintf(boothd_size, sizeof(boothd_size), "%dM", 40);
            break;
         case 4:
            snprintf(boothd_size, sizeof(boothd_size), "%dM", 80);
            break;
         case 5:
            snprintf(boothd_size, sizeof(boothd_size), "%dM", 128);
            break;
         case 6:
            snprintf(boothd_size, sizeof(boothd_size), "%dM", 256);
            break;
         case 7:
            snprintf(boothd_size, sizeof(boothd_size), "%dM", 512);
            break;
      }
   }

   /* HDF mode */
   if (opt_use_boot_hd > 1)
   {
      /* Init Boot HD */
      char boothd_hdf[RETRO_PATH_MAX];
      path_join(boothd_hdf, retro_save_directory, LIBRETRO_PUAE_PREFIX ".hdf");
      if (!path_is_valid(boothd_hdf))
      {
         log_cb(RETRO_LOG_INFO, "Boot HD image file \"%s\" not found, attempting to create...\n", boothd_hdf);
         if (make_hdf(boothd_hdf, boothd_size, label))
            log_cb(RETRO_LOG_ERROR, "Unable to create Boot HD image: \"%s\"\n", boothd_hdf);
      }
      if (path_is_valid(boothd_hdf))
      {
         tmp_str = string_replace_substring(boothd_hdf, strlen(boothd_hdf), "\\", strlen("\\"), "\\\\", strlen("\\\\"));

         /* Detect RDB */
         bool hdf_rdb = is_hdf_rdb(tmp_str);

         if (hdf_rdb)
            retro_config_append("hardfile2=rw,%s:\"%s\",0,0,0,512,0,,uae0\n", volume, tmp_str);
         else
         {
            /* Guesstimate surfaces */
            unsigned surfaces = 1;
            if ((fsize(tmp_str) / 1024) >= (1024 * 1024))
               surfaces = 16;

            retro_config_append("hardfile2=rw,%s:\"%s\",32,%d,2,512,0,,uae0\n", volume, tmp_str, surfaces);
         }
      }
   }
   /* Directory mode */
   else if (opt_use_boot_hd == 1)
   {
      char boothd_path[RETRO_PATH_MAX];
      path_join(boothd_path, retro_save_directory, label);

      if (!path_is_directory(boothd_path))
      {
         log_cb(RETRO_LOG_INFO, "Boot HD image directory \"%s\" not found, attempting to create...\n", boothd_path);
         path_mkdir(boothd_path);
      }
      if (path_is_directory(boothd_path))
      {
         tmp_str = string_replace_substring(boothd_path, strlen(boothd_path), "\\", strlen("\\"), "\\\\", strlen("\\\\"));
         retro_config_append("filesystem2=rw,%s:%s:\"%s\",0\n", volume, label, tmp_str);
      }
      else
         log_cb(RETRO_LOG_ERROR, "Unable to create Boot HD directory: \"%s\".\n", boothd_path);
   }

   if (tmp_str)
      free(tmp_str);
   tmp_str = NULL;
}

/* ROM scanner: like WinUAE's ROM scan, every file of the system directory
 * and of system/uae (plus its subfolders) that the WinUAE ROM database knows
 * is added to the ROM list, so ROMs are found by contents whatever their file
 * name: Kickstarts (see uae_kickstarts[].romids), extended ROMs, accelerator
 * and expansion board ROMs. Done once per session. */
static void retro_scan_roms_dir(const char *dirpath, int depth)
{
   RDIR *dir;

   if (!path_is_directory(dirpath) || !(dir = retro_opendir(dirpath)))
      return;
   while (retro_readdir(dir))
   {
      char path[RETRO_PATH_MAX];
      const char *name = retro_dirent_get_name(dir);
      uint64_t size;
      uae_u8 *buf;
      FILE *fp;

      if (!name || name[0] == '.')
         continue;
      path_join(path, dirpath, name);
      if (path_is_directory(path))
      {
         if (depth > 0)
            retro_scan_roms_dir(path, depth - 1);
         continue;
      }
      /* 2KB keyboard ROMs up to 1MB CD32 ROMs, AMIROMTYPE1 header included */
      size = fsize(path);
      if (size < 2048 || size > 1048576 + 11)
         continue;
      if (!(buf = (uae_u8*)malloc((size_t)size)))
         continue;
      if ((fp = fopen(path, "rb")))
      {
         if (fread(buf, 1, (size_t)size, fp) == (size_t)size)
         {
            struct romdata *rd = getromdatabydata(buf, (int)size);
            /* Split ROMs (_even/_odd halves) need both files: not supported */
            if (rd && !(rd->type & (ROMTYPE_EVEN | ROMTYPE_ODD)))
               romlist_add(path, rd);
         }
         fclose(fp);
      }
      free(buf);
   }
   retro_closedir(dir);
}

static void retro_scan_roms(void)
{
   static bool scanned = false;
   char uae_dir[RETRO_PATH_MAX];

   if (scanned || string_is_empty(retro_system_directory))
      return;
   scanned = true;
   path_join(uae_dir, retro_system_directory, "uae");
   retro_scan_roms_dir(retro_system_directory, 0);
   retro_scan_roms_dir(uae_dir, 1);
   log_cb(RETRO_LOG_INFO, "ROM scan: %d known ROM(s) found.\n", romlist_count());
}

/* Kickstart table entry of a file name, or -1 */
static int retro_kickstart_index(const char *name)
{
   unsigned i;
   for (i = 0; i < sizeof(uae_kickstarts) / sizeof(uae_kickstarts[0]); i++)
      if (uae_kickstarts[i].normal && !strcmp(uae_kickstarts[i].normal, name))
         return (int)i;
   return -1;
}

/* Kickstart found by contents: the first of romids[] present in the ROM list */
static bool retro_kickstart_by_id(const char *name, char *out, size_t out_size)
{
   int i = retro_kickstart_index(name), j, ids[2];

   if (i < 0)
      return false;
   retro_scan_roms();
   for (j = 0; j < 4 && uae_kickstarts[i].romids[j] > 0; j++)
   {
      struct romlist *rl;
      ids[0] = uae_kickstarts[i].romids[j];
      ids[1] = -1;
      if ((rl = getromlistbyids(ids, NULL)) && path_is_valid(rl->path))
      {
         strlcpy(out, rl->path, out_size);
         return true;
      }
   }
   return false;
}

static void retro_config_kickstart(void)
{
   char kickstart[RETRO_PATH_MAX];
   bool valid = false;

   /* Wrong place for logging model but still the best place */
   log_cb(RETRO_LOG_INFO, "Model: \"%s\".\n", uae_preset);

   /* Forced Kickstart */
   if (strcmp(opt_kickstart, "auto"))
      strlcpy(uae_kickstart, opt_kickstart, sizeof(uae_kickstart));

   /* Fix path if not already valid */
   if (!path_is_valid(uae_kickstart))
   {
      char tmp[RETRO_PATH_MAX];
      snprintf(tmp, sizeof(tmp), "%s", path_basename(uae_kickstart));
      strlcpy(uae_kickstart, tmp, sizeof(uae_kickstart));

      /* Final absolute path */
      path_join(kickstart, retro_system_directory, uae_kickstart);
   }
   else
      strlcpy(kickstart, uae_kickstart, sizeof(kickstart));

   /* No path validations for AROS */
   if (!strcmp(opt_kickstart, "aros"))
      valid = true;
   else
      valid = path_is_valid(kickstart);

   if (!valid)
   {
      /* Search for Amiga Forever + TOSEC naming conventions */
      int i = retro_kickstart_index(uae_kickstart);

      if (i >= 0 && !string_is_empty(uae_kickstarts[i].aforever))
      {
         path_join(kickstart, retro_system_directory, uae_kickstarts[i].aforever);
         valid = path_is_valid(kickstart);
      }
      if (!valid && i >= 0)
      {
         if (!string_is_empty(uae_kickstarts[i].tosec_mod))
         {
            path_join(kickstart, retro_system_directory, uae_kickstarts[i].tosec_mod);
            valid = path_is_valid(kickstart);
         }
         if (!valid)
         {
            if (!string_is_empty(uae_kickstarts[i].tosec))
            {
               path_join(kickstart, retro_system_directory, uae_kickstarts[i].tosec);
               valid = path_is_valid(kickstart);
            }
         }
      }
   }

   /* Search by contents (any file name) */
   if (!valid && strcmp(opt_kickstart, "aros"))
      valid = retro_kickstart_by_id(uae_kickstart, kickstart, sizeof(kickstart));

   /* Fallback to original filename */
   if (!valid)
      strlcpy(kickstart, uae_kickstart, sizeof(kickstart));

   /* Final append + logging */
   if (valid)
   {
      log_cb(RETRO_LOG_INFO, "Kickstart: \"%s\".\n", path_basename(kickstart));
      if (strcmp(opt_kickstart, "aros"))
         retro_config_append("kickstart_rom_file=%s\n", kickstart);
   }
   else
   {
      log_cb(RETRO_LOG_ERROR, "Kickstart ROM \"%s\" not found!\n", path_basename(kickstart));
      snprintf(retro_message_msg, sizeof(retro_message_msg), "Kickstart ROM '%s' not found!", path_basename(kickstart));
      retro_message = true;
   }

   /* Replace full path for override */
   strlcpy(uae_kickstart, kickstart, sizeof(uae_kickstart));

   /* Extended KS + NVRAM */
   if (!string_is_empty(uae_kickstart_ext))
   {
      char kickstart_ext[RETRO_PATH_MAX];
      path_join(kickstart_ext, retro_system_directory, uae_kickstart_ext);

      /* Decide if CD32 ROM is combined based on filesize */
      const uint64_t ks_size = fsize(uae_kickstart);

      /* Verify extended ROM if external */
      if (ks_size <= ROM_SIZE_512)
      {
         valid = path_is_valid(kickstart_ext);

         if (!valid)
         {
            /* Search for Amiga Forever + TOSEC naming conventions */
            int i = retro_kickstart_index(uae_kickstart_ext);

            if (i >= 0 && !string_is_empty(uae_kickstarts[i].aforever))
            {
               path_join(kickstart_ext, retro_system_directory, uae_kickstarts[i].aforever);
               valid = path_is_valid(kickstart_ext);
            }
            if (!valid && i >= 0)
            {
               if (!string_is_empty(uae_kickstarts[i].tosec_mod))
               {
                  path_join(kickstart_ext, retro_system_directory, uae_kickstarts[i].tosec_mod);
                  valid = path_is_valid(kickstart_ext);
               }
               if (!valid)
               {
                  if (!string_is_empty(uae_kickstarts[i].tosec))
                  {
                     path_join(kickstart_ext, retro_system_directory, uae_kickstarts[i].tosec);
                     valid = path_is_valid(kickstart_ext);
                  }
               }
            }
         }

         /* Search by contents (any file name) */
         if (!valid)
            valid = retro_kickstart_by_id(uae_kickstart_ext, kickstart_ext, sizeof(kickstart_ext));

         /* Fallback to original filename */
         if (!valid)
            strlcpy(kickstart_ext, uae_kickstart_ext, sizeof(kickstart_ext));

         /* Final append + logging */
         if (valid)
         {
            log_cb(RETRO_LOG_INFO, "+Extended: \"%s\"\n", path_basename(kickstart_ext));
            retro_config_append("kickstart_ext_rom_file=%s\n", kickstart_ext);
         }
         else
         {
            log_cb(RETRO_LOG_ERROR, "Kickstart extended ROM \"%s\" not found!\n", path_basename(kickstart_ext));
            snprintf(retro_message_msg, sizeof(retro_message_msg), "Kickstart extended ROM \"%s\" not found!", path_basename(kickstart_ext));
            retro_message = true;
         }
      }

      /* NVRAM */
      char flash_filename[RETRO_PATH_MAX];
      char flash_filepath[RETRO_PATH_MAX];
      /* Shared */
      if (opt_shared_nvram || string_is_empty(full_path))
      {
         snprintf(flash_filename, sizeof(flash_filename), "%s.nvr", LIBRETRO_PUAE_PREFIX);
         /* CDTV suffix (CDTV-CR has a 512K Kickstart) */
         if (ks_size == ROM_SIZE_256 || strstr(uae_preset, "CDTV"))
            snprintf(flash_filename, sizeof(flash_filename), "%s_cdtv.nvr", LIBRETRO_PUAE_PREFIX);
      }
      /* Per game */
      else
      {
         char flash_filebase[RETRO_PATH_MAX];
         snprintf(flash_filebase, sizeof(flash_filebase), "%s", path_basename(full_path));
         path_remove_extension(flash_filebase);
         snprintf(flash_filename, sizeof(flash_filename), "%s.nvr", flash_filebase);
      }
      path_join(flash_filepath, retro_save_directory, flash_filename);
      log_cb(RETRO_LOG_INFO, "NVRAM: \"%s\".\n", flash_filepath);
      retro_config_append("flash_file=%s\n", flash_filepath);
   }

   /* Boards whose boot ROM works only with some Kickstarts (tested with
    * xsysinfo): E-Matrix 530 and Typhoon MK2 reset in a loop with 1.x.
    * Not offered: Fusion Forty and Harms 3000 (black screen with 3.x,
    * spontaneous resets with 1.3), T-Rex II and Twelve Gauge (CPU halt). */
   {
      static const struct { const char *board; int ksmin, ksmax; const char *hint; } ksreq[] = {
         { "e-matrix530", 2, 99, "Kickstart 2.0+ (A500+ model)" },
         { "typhoon2",    2, 99, "Kickstart 2.0+ (A500+ model)" },
      };
      unsigned k;
      for (k = 0; k < sizeof(ksreq) / sizeof(ksreq[0]); k++)
      {
         struct romdata *rd;
         int ver;
         if (strcmp(opt_cpuboard, ksreq[k].board) || !path_is_valid(uae_kickstart))
            continue;
         retro_scan_roms();
         rd = getromdatabypath(uae_kickstart);
         ver = (rd && rd->ver > 0) ? rd->ver : (fsize(uae_kickstart) <= ROM_SIZE_256 ? 1 : 3);
         if (ver < ksreq[k].ksmin || ver > ksreq[k].ksmax)
         {
            log_cb(RETRO_LOG_WARN, "Accelerator board \"%s\" needs %s.\n", opt_cpuboard, ksreq[k].hint);
            snprintf(retro_message_msg, sizeof(retro_message_msg),
                  "This accelerator needs %s", ksreq[k].hint);
            retro_message = true;
         }
      }
   }

   retro_config_keyboard();
}

/* Keyboard microcontroller emulation (WinUAE 6 "keyboard_type").
 * The MCU types need the dump of their ROM, which libretro has no ROM scanner
 * for: look for it by size and CRC32 in the system directory. */
extern uae_u32 get_crc32(void *p, int size);

static bool retro_find_kbmcu_rom(uae_u32 crc, uae_u32 crc_alt, int size, char *out, size_t out_size)
{
   const char *dirs[2];
   unsigned d;
   char uae_dir[RETRO_PATH_MAX];

   path_join(uae_dir, retro_system_directory, "uae");
   dirs[0] = retro_system_directory;
   dirs[1] = uae_dir;

   for (d = 0; d < 2; d++)
   {
      RDIR *dir;


      if (!path_is_directory(dirs[d]))
         continue;
      dir = retro_opendir(dirs[d]);
      if (!dir)
         continue;
      while (retro_readdir(dir))
      {
         char path[RETRO_PATH_MAX];
         uae_u8 *buf;
         FILE *f;
         bool match = false;

         path_join(path, dirs[d], retro_dirent_get_name(dir));
         if (path_is_directory(path) || fsize(path) != (uint64_t)size)
            continue;
         if (!(buf = (uae_u8*)malloc(size)))
            continue;
         if ((f = fopen(path, "rb")))
         {
            if (fread(buf, 1, size, f) == (size_t)size)
            {
               uae_u32 c = get_crc32(buf, size);
               match = c == crc || (crc_alt && c == crc_alt);
            }
            fclose(f);
         }
         free(buf);
         if (match)
         {
            strlcpy(out, path, out_size);
            retro_closedir(dir);
            return true;
         }
      }
      retro_closedir(dir);
   }
   return false;
}

static void retro_config_keyboard(void)
{
   static const struct
   {
      const char *type;
      uae_u32 crc;
      uae_u32 crc_alt; /* other known good dump, or 0 */
      int size;
      const char *mcu;
   } kbmcu[] = {
      { "a500_6570-036",  0x4a3fc332, 0, 2048, "6570-036" },
      { "a600_6570-036",  0x4a3fc332, 0, 2048, "6570-036" },
      { "a1000_6570-036", 0x4a3fc332, 0, 2048, "6570-036" },
      { "ax000_6570-036", 0x4a3fc332, 0, 2048, "6570-036" },
      { "a1200_6805",     0x2a77eec4, 0, 8192, "68HC05" },
      /* fb92a773: another D8039HLC dump, works in WinUAE when selected by path */
      { "a2000_8039",     0xc23d280e, 0xfb92a773, 2048, "D8039HLC" },
   };
   const char *type = opt_keyboard_type;
   char rom[RETRO_PATH_MAX];
   unsigned i;

   if (!strcmp(type, "auto"))
   {
      /* CD32 and CDTV keep the default: their keyboards are optional */
      if (strstr(uae_preset, "CD"))
         type = "uae";
      else if (strstr(uae_preset, "A1200"))
         type = "a1200_6805";
      else if (strstr(uae_preset, "A600"))
         type = "a600_6570-036";
      else if (strstr(uae_preset, "A2000"))
         type = "a2000_8039";
      else if (strstr(uae_preset, "A40") || strstr(uae_preset, "A30"))
         type = "ax000_6570-036";
      else
         type = "a500_6570-036";
   }

   for (i = 0; i < sizeof(kbmcu) / sizeof(kbmcu[0]); i++)
      if (!strcmp(type, kbmcu[i].type))
         break;
   if (i == sizeof(kbmcu) / sizeof(kbmcu[0]))
      return; /* "uae": the default of the emulation */

   if (!retro_find_kbmcu_rom(kbmcu[i].crc, kbmcu[i].crc_alt, kbmcu[i].size, rom, sizeof(rom)))
   {
      log_cb(RETRO_LOG_WARN, "Keyboard MCU ROM %s (CRC32 %08x) not found, using UAE keyboard.\n",
            kbmcu[i].mcu, kbmcu[i].crc);
      snprintf(retro_message_msg, sizeof(retro_message_msg),
            "Keyboard MCU ROM %s not found, using UAE keyboard", kbmcu[i].mcu);
      retro_message = true;
      return;
   }

   log_cb(RETRO_LOG_INFO, "Keyboard: %s, MCU ROM \"%s\".\n", type, path_basename(rom));
   retro_config_append("keyboard_type=%s\n", type);
   retro_config_append("keyboard_rom_file=%s\n", rom);
}

/* Accelerator boards (WinUAE "cpuboard_type"). The board ROM is looked up by
 * its contents (CRC32 or SHA1 of the WinUAE ROM database), in the system
 * directory and system/uae, since libretro has no ROM scanner. */
static bool retro_find_board_rom(uae_u32 romtype, char *out, size_t out_size)
{
   const char *dirs[2];
   unsigned d;
   char uae_dir[RETRO_PATH_MAX];

   /* Scanned ROM list (system dir, system/uae and its subfolders): highest
    * version first, then the order of the WinUAE database, which lists the
    * newest revision first when there is no version (A2620/A2630 -07 before
    * -01, the early revisions being A2620 + Kickstart 1.3 only) */
   {
      struct romlist *rl;
      struct romdata *best = NULL;
      int n, k, b = -1;

      retro_scan_roms();
      rl = romlist_getit();
      n = romlist_count();
      for (k = 0; k < n; k++)
      {
         struct romdata *rd = rl[k].rd, *db = getromdatabyid(rd->id);
         if ((rd->type & ROMTYPE_MASK) != (romtype & ROMTYPE_MASK) || !path_is_valid(rl[k].path))
            continue;
         if (!db)
            db = rd;
         if (!best
               || db->ver > best->ver
               || (db->ver == best->ver && db->rev > best->rev)
               || (db->ver == best->ver && db->rev == best->rev && db < best))
         {
            best = db;
            b = k;
         }
      }
      if (b >= 0)
      {
         strlcpy(out, rl[b].path, out_size);
         return true;
      }
   }

   path_join(uae_dir, retro_system_directory, "uae");
   dirs[0] = retro_system_directory;
   dirs[1] = uae_dir;

   for (d = 0; d < 2; d++)
   {
      RDIR *dir;


      if (!path_is_directory(dirs[d]) || !(dir = retro_opendir(dirs[d])))
         continue;
      while (retro_readdir(dir))
      {
         char path[RETRO_PATH_MAX];
         uint64_t size;
         uae_u8 *buf;
         FILE *f;
         bool match = false;

         path_join(path, dirs[d], retro_dirent_get_name(dir));
         if (path_is_directory(path))
            continue;
         /* Board ROMs are 8KB to 512KB */
         size = fsize(path);
         if (size < 8192 || size > 524288 || (size & (size - 1)))
            continue;
         if (!(buf = (uae_u8*)malloc((size_t)size)))
            continue;
         if ((f = fopen(path, "rb")))
         {
            if (fread(buf, 1, (size_t)size, f) == (size_t)size)
            {
               struct romdata *rd = getromdatabydata(buf, (int)size);
               match = rd && (rd->type & ROMTYPE_MASK) == (romtype & ROMTYPE_MASK);
            }
            fclose(f);
         }
         free(buf);
         if (match)
         {
            strlcpy(out, path, out_size);
            retro_closedir(dir);
            return true;
         }
      }
      retro_closedir(dir);
   }

   /* ROMs without checksums in the database (flash images such as the
    * CyberStorm MK II) are known only by their default file name */
   {
      int id;
      for (id = 1; id < 1000; id++)
      {
         struct romdata *rd = getromdatabyid(id);
         if (!rd || !rd->defaultfilename || rd->crc32
               || (rd->type & ROMTYPE_MASK) != (romtype & ROMTYPE_MASK))
            continue;
         for (d = 0; d < 2; d++)
         {
            char path[RETRO_PATH_MAX];
            path_join(path, dirs[d], rd->defaultfilename);
            if (path_is_valid(path) && fsize(path) == rd->size)
            {
               strlcpy(out, path, out_size);
               return true;
            }
         }
      }
   }
   return false;
}

static void retro_config_cpuboard(void)
{
   const struct cpuboardsubtype *cbst = NULL;
   char rom[RETRO_PATH_MAX] = {0};
   char row[RETRO_PATH_MAX + 64];
   int cpu, mem, ctype;
   unsigned i, j;

   extern int puae_cpuboard_cpu;
   puae_cpuboard_cpu = 0;
   if (!strcmp(opt_cpuboard, "none"))
      return;

   for (i = 0; cpuboards[i].name && !cbst; i++)
      for (j = 0; cpuboards[i].subtypes && cpuboards[i].subtypes[j].name; j++)
         if (!strcmp(cpuboards[i].subtypes[j].configname, opt_cpuboard))
         {
            cbst = &cpuboards[i].subtypes[j];
            break;
         }
   if (!cbst)
   {
      log_cb(RETRO_LOG_WARN, "Accelerator board \"%s\" is not available in this build.\n", opt_cpuboard);
      return;
   }

   /* Boot ROM: mandatory unless the board works without one */
   if (cbst->romtype && !retro_find_board_rom(cbst->romtype, rom, sizeof(rom)))
   {
      if (!(cbst->romtype & ROMTYPE_NONE))
      {
         log_cb(RETRO_LOG_WARN, "Accelerator board \"%s\": ROM not found, board disabled.\n", cbst->name);
         snprintf(retro_message_msg, sizeof(retro_message_msg),
               "%s ROM not found, accelerator disabled", cbst->name);
         retro_message = true;
         return;
      }
   }

   /* Memory: 32MB or the board maximum when not set, clamped to the maximum */
   mem = opt_cpuboard_mem < 0 ? 32 : opt_cpuboard_mem;
   if (mem * 1024 * 1024 > cbst->maxmemory)
      mem = cbst->maxmemory / (1024 * 1024);

   /* CPU of the board, or the 'CPU Model' option when the board takes it
    * (68040/68060 boards: either; 68020/68030 boards: at least the board CPU) */
   /* Hardital TQM: a 68030 board that WinUAE leaves on the machine CPU */
   ctype = cbst->cputype;
   if (!ctype && !strcmp(cbst->configname, "tqm"))
      ctype = 3;
   cpu = 68000 + ctype * 10;
   if (ctype >= 4 && (override_opt.cpu_model == 68040 || override_opt.cpu_model == 68060))
      cpu = override_opt.cpu_model;
   else if (ctype >= 2 && ctype < 4 && override_opt.cpu_model >= cpu && override_opt.cpu_model <= 68030)
      cpu = override_opt.cpu_model;
   /* CyberStorm MK I: its only ROM is the 68060 support ROM (with a 68040 the
    * board runs without ROM, and a 68040 hangs on the 68060 ROM) */
   if ((cbst->romtype & ROMTYPE_MASK) == (ROMTYPE_CB_CSMK1 & ROMTYPE_MASK))
   {
      if (override_opt.cpu_model == 68040)
         rom[0] = 0;
      else if (rom[0])
         cpu = 68060;
   }
   puae_cpuboard_cpu = ctype >= 2 ? cpu : 0;

   log_cb(RETRO_LOG_INFO, "Accelerator: %s, %dMB, CPU %d, ROM \"%s\".\n",
         cbst->name, mem, cpu, rom[0] ? path_basename(rom) : "-");

   snprintf(row, sizeof(row), "cpuboard_type=%s\ncpuboardmem1_size=%d\n", cbst->configname, mem);
   strcat(uae_config, row);
   if (rom[0])
   {
      snprintf(row, sizeof(row), "cpuboard_rom_file=%s\n", rom);
      strcat(uae_config, row);
   }
   if (ctype >= 2)
   {
      snprintf(row, sizeof(row), "cpu_model=%d\ncpu_24bit_addressing=false\n", cpu);
      strcat(uae_config, row);
      if (cpu >= 68040)
      {
         snprintf(row, sizeof(row), "fpu_model=%d\n", cpu);
         strcat(uae_config, row);
      }
   }
}

static void retro_config_harddrives(void)
{
   char *tmp_str = NULL;
   int8_t i      = 0;
   uint8_t unit  = 0;

   if (!dc->count)
      return;

   for (i = 0; i < dc->count; i++)
   {
      char tmp_str_name[RETRO_PATH_MAX];
      char tmp_str_path[RETRO_PATH_MAX];

      if (     dc_get_image_type(dc->files[i]) == DC_IMAGE_TYPE_FLOPPY
            || dc_get_image_type(dc->files[i]) == DC_IMAGE_TYPE_CD)
         continue;

      tmp_str = utf8_to_local_string_alloc(dc->files[i]);
      snprintf(tmp_str_name, sizeof(tmp_str_name), "%s", path_basename(tmp_str));
      path_remove_extension(tmp_str_name);

      /* Deduce mount path for launch extensions */
      if (strendswith(dc->files[i], "slave") || strendswith(dc->files[i], "info"))
      {
         path_parent_dir(tmp_str, strlen(tmp_str));
         snprintf(tmp_str_path, sizeof(tmp_str_path), "%s%s", tmp_str, tmp_str_name);

         if (!path_is_directory(tmp_str_path))
            path_parent_dir(tmp_str_path, strlen(tmp_str_path));

         if (tmp_str_path[strlen(tmp_str_path)-1] == DIR_SEP_CHR)
            tmp_str_path[strlen(tmp_str_path)-1] = '\0';

         tmp_str = strdup(tmp_str_path);
      }

#ifdef WIN32
      tmp_str = string_replace_substring(tmp_str, strlen(tmp_str), "\\", strlen("\\"), "\\\\", strlen("\\\\"));
#endif

      /* LHAs read-only */
      if (strendswith(dc->files[i], "lha"))
         retro_config_append("filesystem2=ro,DH%d:%s:\"%s\",0\n", unit, tmp_str_name, tmp_str);
      /* Directories writable */
      else if (path_is_directory(dc->files[i]) || strendswith(dc->files[i], "slave") || strendswith(dc->files[i], "info"))
         retro_config_append("filesystem2=rw,DH%d:%s:\"%s\",0\n", unit, tmp_str_name, tmp_str);
      /* Hardfiles */
      else if (dc_get_image_type(dc->files[i]) == DC_IMAGE_TYPE_HD)
      {
         /* Detect RDB */
         bool hdf_rdb = is_hdf_rdb(tmp_str);

         if (hdf_rdb)
            retro_config_append("hardfile2=rw,DH%d:\"%s\",0,0,0,512,0,,uae0\n", unit, tmp_str);
         else
         {
            /* Guesstimate surfaces */
            unsigned surfaces = 1;
            if ((fsize(tmp_str) / 1024) >= (1024 * 1024))
               surfaces = 16;

            retro_config_append("hardfile2=rw,DH%d:\"%s\",32,%d,2,512,0,,uae0\n", unit, tmp_str, surfaces);
         }
      }

      log_cb(RETRO_LOG_INFO, "HD (%d) inserted in drive DH%d: \"%s\".\n", unit + 1, unit, dc->files[i]);
      unit++;

      if (tmp_str)
         free(tmp_str);
      tmp_str = NULL;
   }

   for (i = dc->count - 1; i > -1; i--)
   {
      /* Remove hard drives from Disc Control list */
      if (     dc_get_image_type(dc->files[i]) == DC_IMAGE_TYPE_HD
            || dc_get_image_type(dc->files[i]) == DC_IMAGE_TYPE_WHDLOAD)
         dc_remove_file(dc, i);
   }
}

#define KS_NUM 4

static void whdload_kscopy(void)
{
   char ks_src[RETRO_PATH_MAX] = {0};
   char ks_dst[RETRO_PATH_MAX] = {0};
   unsigned x                  = 0;

   const char kickstart[KS_NUM][32] =
   {
      "kick33180.A500",
      "kick34005.A500",
      "kick40063.A600",
      "kick40068.A1200"
   };

   const unsigned int ks_size[KS_NUM] =
   {
      ROM_SIZE_256,
      ROM_SIZE_256,
      ROM_SIZE_512,
      ROM_SIZE_512
   };

   for (x = 0; x < KS_NUM; x++)
   {
      bool valid        = false;
      uint8_t key_extra = 0;

      snprintf(ks_src, sizeof(ks_src), "%s%s%s",
            retro_system_directory, DIR_SEP_STR, kickstart[x]);

      snprintf(ks_dst, sizeof(ks_dst), "%s%sWHDLoad%sDevs%sKickstarts%s%s",
            retro_save_directory, DIR_SEP_STR, DIR_SEP_STR, DIR_SEP_STR, DIR_SEP_STR, kickstart[x]);

      valid = path_is_valid(ks_src);
      if (!valid)
      {
         /* Search for Amiga Forever + TOSEC naming conventions */
         unsigned i = 0;
         for (i = 0; i < sizeof(uae_kickstarts); i++)
         {
            if (!strcmp(uae_kickstarts[i].normal, kickstart[x]))
               break;
         }

         path_join(ks_src, retro_system_directory, uae_kickstarts[i].aforever);
         valid = path_is_valid(ks_src);

         /* Special Amiga Forever encryption bonus */
         if (valid)
            key_extra = 11;

         if (!valid)
         {
            if (!string_is_empty(uae_kickstarts[i].tosec_mod))
            {
               path_join(ks_src, retro_system_directory, uae_kickstarts[i].tosec_mod);
               valid = path_is_valid(ks_src);
            }
            if (!valid)
            {
               if (!string_is_empty(uae_kickstarts[i].tosec))
               {
                  path_join(ks_src, retro_system_directory, uae_kickstarts[i].tosec);
                  valid = path_is_valid(ks_src);
               }
            }
         }
      }

      if (valid && !path_is_valid(ks_dst))
      {
         const uint64_t ks_src_size = fsize(ks_src);

         /* Allow size exception */
         if (     ks_src_size != ks_size[x]
               && ks_src_size - key_extra != ks_size[x])
            log_cb(RETRO_LOG_INFO, "WHDLoad not installing Kickstart \"%s\" due to incorrect size, %d != %d.\n", path_basename(ks_src), ks_src_size, ks_size[x]);
         else if (fcopy(ks_src, ks_dst) < 0)
            log_cb(RETRO_LOG_INFO, "WHDLoad failed to install \"%s\" to \"%s\".\n", path_basename(ks_src), ks_dst);
         else
            log_cb(RETRO_LOG_INFO, "WHDLoad installed \"%s\" to \"%s\".\n", path_basename(ks_src), ks_dst);
      }
   }
}

static void whdload_prefs_copy(void)
{
   char src[RETRO_PATH_MAX] = {0};
   char dst[RETRO_PATH_MAX] = {0};
   char filename[32]        = {0};

   /* rom.key only when not found */
   snprintf(filename, sizeof(filename), "%s", "rom.key");

   snprintf(src, sizeof(src), "%s%s%s",
         retro_system_directory, DIR_SEP_STR, filename);
   snprintf(dst, sizeof(dst), "%s%sWHDLoad%sDevs%sKickstarts%s%s",
         retro_save_directory, DIR_SEP_STR, DIR_SEP_STR, DIR_SEP_STR, DIR_SEP_STR, filename);

   if (!path_is_valid(dst) && path_is_valid(src))
   {
      if (fcopy(src, dst) < 0)
         log_cb(RETRO_LOG_INFO, "WHDLoad failed to install \"%s\".\n", filename);
      else
         log_cb(RETRO_LOG_INFO, "WHDLoad installed \"%s\".\n", filename);
   }

   /* WHDLoad.key only when not found */
   snprintf(filename, sizeof(filename), "%s", "WHDLoad.key");

   snprintf(src, sizeof(src), "%s%s%s",
         retro_system_directory, DIR_SEP_STR, filename);
   snprintf(dst, sizeof(dst), "%s%sWHDLoad%sL%s%s",
         retro_save_directory, DIR_SEP_STR, DIR_SEP_STR, DIR_SEP_STR, filename);

   if (!path_is_valid(dst) && path_is_valid(src))
   {
      if (fcopy(src, dst) < 0)
         log_cb(RETRO_LOG_INFO, "WHDLoad failed to install \"%s\".\n", filename);
      else
         log_cb(RETRO_LOG_INFO, "WHDLoad installed \"%s\".\n", filename);
   }

   /* WHDLoad.prefs always */
   snprintf(filename, sizeof(filename), "%s", "WHDLoad.prefs");

   snprintf(src, sizeof(src), "%s%s%s",
         retro_system_directory, DIR_SEP_STR, filename);
   snprintf(dst, sizeof(dst), "%s%sWHDLoad%sS%s%s",
         retro_save_directory, DIR_SEP_STR, DIR_SEP_STR, DIR_SEP_STR, filename);

   if (path_is_valid(src))
   {
      if (path_is_valid(dst))
      {
         /* No need to do anything without changes */
         if (!fcmp(src, dst))
            return;

         retro_remove(dst);
      }

      if (fcopy(src, dst) < 0)
         log_cb(RETRO_LOG_INFO, "WHDLoad failed to update \"%s\".\n", filename);
      else
         log_cb(RETRO_LOG_INFO, "WHDLoad updated \"%s\".\n", filename);
   }
}

static void whdload_quitkey(void)
{
   int i;
   if (     opt_use_whdload_nowritecache
         || string_is_empty(currprefs.mountconfig[0].ci.devname))
      return;

   log_cb(RETRO_LOG_INFO, "WHDLoad QuitKey triggered...\n");
   libretro_runloop_active = false;

   retro_key_down(RETROK_HASH);
   for (i = 0; i < 2; i++)
      m68k_go(1, 1);

   retro_key_up(RETROK_HASH);
   for (i = 0; i < retro_refresh * 2; i++)
      m68k_go(1, 1);

   libretro_runloop_active = true;
}

static char* emu_config_string(const char *mode, int config)
{
   if (!strcmp(mode, "model"))
   {
      switch (config)
      {
         case EMU_CONFIG_A500:      return "A500";
         case EMU_CONFIG_A500OG:    return "A500OG";
         case EMU_CONFIG_A500ECS:   return "A500ECS";
         case EMU_CONFIG_A500PLUS:  return "A500PLUS";
         case EMU_CONFIG_A600:      return "A600";
         case EMU_CONFIG_A600OG:    return "A600OG";
         case EMU_CONFIG_A1200:     return "A1200";
         case EMU_CONFIG_A1200OG:   return "A1200OG";
         case EMU_CONFIG_A2000:     return "A2000";
         case EMU_CONFIG_A2000OG:   return "A2000OG";
         case EMU_CONFIG_A4030:     return "A4030";
         case EMU_CONFIG_A4040:     return "A4040";
         case EMU_CONFIG_CDTV:      return "CDTV";
         case EMU_CONFIG_CD32:      return "CD32";
         case EMU_CONFIG_CD32FR:    return "CD32FR";
         case EMU_CONFIG_A1000:     return "A1000";
         case EMU_CONFIG_A3000:     return "A3000";
         case EMU_CONFIG_A4060:     return "A4060";
         case EMU_CONFIG_A4000T:    return "A4000T";
         case EMU_CONFIG_CDTVCR:    return "CDTVCR";
      }
   }
   else if (!strcmp(mode, "kickstart"))
   {
      switch (config)
      {
         case EMU_CONFIG_A500:      return uae_kickstarts[A500_KS13_ROM].normal;
         case EMU_CONFIG_A500OG:    return uae_kickstarts[A500_KS13_ROM].normal;
         case EMU_CONFIG_A500ECS:   return uae_kickstarts[A500_KS13_ROM].normal;
         case EMU_CONFIG_A500PLUS:  return uae_kickstarts[A500_KS204_ROM].normal;
         case EMU_CONFIG_A600:      return uae_kickstarts[A600_KS31_ROM].normal;
         case EMU_CONFIG_A600OG:    return uae_kickstarts[A600_KS31_ROM].normal;
         case EMU_CONFIG_A1200:     return uae_kickstarts[A1200_KS31_ROM].normal;
         case EMU_CONFIG_A1200OG:   return uae_kickstarts[A1200_KS31_ROM].normal;
         case EMU_CONFIG_A2000:     return uae_kickstarts[A600_KS31_ROM].normal;
         case EMU_CONFIG_A2000OG:   return uae_kickstarts[A500_KS13_ROM].normal;
         case EMU_CONFIG_A4030:     return uae_kickstarts[A4000_KS31_ROM].normal;
         case EMU_CONFIG_A4040:     return uae_kickstarts[A4000_KS31_ROM].normal;
         case EMU_CONFIG_CDTV:      return uae_kickstarts[A500_KS13_ROM].normal;
         case EMU_CONFIG_CD32:      return uae_kickstarts[CD32_ROM].normal;
         case EMU_CONFIG_CD32FR:    return uae_kickstarts[CD32_ROM].normal;
         case EMU_CONFIG_A1000:     return uae_kickstarts[A500_KS13_ROM].normal;
         case EMU_CONFIG_A3000:     return uae_kickstarts[A3000_KS31_ROM].normal;
         case EMU_CONFIG_A4060:     return uae_kickstarts[A4000_KS31_ROM].normal;
         case EMU_CONFIG_A4000T:    return uae_kickstarts[A4000T_KS31_ROM].normal;
         case EMU_CONFIG_CDTVCR:    return uae_kickstarts[A600_KS205_ROM].normal;
      }
   }
   else if (!strcmp(mode, "kickstart_ext"))
   {
      switch (config)
      {
         case EMU_CONFIG_CDTV:      return uae_kickstarts[CDTV_ROM].normal;
         case EMU_CONFIG_CD32:      return uae_kickstarts[CD32_ROM_EXT].normal;
         case EMU_CONFIG_CD32FR:    return uae_kickstarts[CD32_ROM_EXT].normal;
         case EMU_CONFIG_CDTVCR:    return uae_kickstarts[CDTVCR_ROM_EXT].normal;
         default:                   return "";
      }
   }
   return "";
}

static int emu_config_int(const char *model)
{
   if      (!strcmp(model, "A500"))      return EMU_CONFIG_A500;
   else if (!strcmp(model, "A500OG"))    return EMU_CONFIG_A500OG;
   else if (!strcmp(model, "A500ECS"))   return EMU_CONFIG_A500ECS;
   else if (!strcmp(model, "A500PLUS"))  return EMU_CONFIG_A500PLUS;
   else if (!strcmp(model, "A600"))      return EMU_CONFIG_A600;
   else if (!strcmp(model, "A600OG"))    return EMU_CONFIG_A600OG;
   else if (!strcmp(model, "A1200"))     return EMU_CONFIG_A1200;
   else if (!strcmp(model, "A1200OG"))   return EMU_CONFIG_A1200OG;
   else if (!strcmp(model, "A2000"))     return EMU_CONFIG_A2000;
   else if (!strcmp(model, "A2000OG"))   return EMU_CONFIG_A2000OG;
   else if (!strcmp(model, "A4030"))     return EMU_CONFIG_A4030;
   else if (!strcmp(model, "A4040"))     return EMU_CONFIG_A4040;
   else if (!strcmp(model, "CDTV"))      return EMU_CONFIG_CDTV;
   else if (!strcmp(model, "CD32"))      return EMU_CONFIG_CD32;
   else if (!strcmp(model, "CD32FR"))    return EMU_CONFIG_CD32FR;
   else if (!strcmp(model, "A1000"))     return EMU_CONFIG_A1000;
   else if (!strcmp(model, "A3000"))     return EMU_CONFIG_A3000;
   else if (!strcmp(model, "A4060"))     return EMU_CONFIG_A4060;
   else if (!strcmp(model, "A4000T"))    return EMU_CONFIG_A4000T;
   else if (!strcmp(model, "CDTVCR"))    return EMU_CONFIG_CDTVCR;
   else return -1;
}

static char* emu_config(int config)
{
   char custom_config_path[RETRO_PATH_MAX] = {0};
   char custom_config_file[RETRO_PATH_MAX] = {0};
   char temp[255];

   /* Reset Kickstart */
   uae_kickstart[0] = '\0';

   snprintf(custom_config_file, sizeof(custom_config_file),
            "%s_%s.%s", LIBRETRO_PUAE_PREFIX, emu_config_string("model", config), "uae");
   path_join(custom_config_path, retro_save_directory, custom_config_file);
   if (path_is_valid(custom_config_path))
   {
      log_cb(RETRO_LOG_INFO, "Appending model preset: \"%s\"...\n", custom_config_path);

      FILE *custom_config_fp;
      char filebuf[RETRO_PATH_MAX];
      if ((custom_config_fp = fopen(custom_config_path, "r")))
      {
         while (fgets(filebuf, sizeof(filebuf), custom_config_fp))
         {
            strlcat(uae_custom_config, filebuf, sizeof(uae_custom_config));
            /* Parse for alternate Kickstart */
            if (strstr(filebuf, "kickstart_rom_file=") && filebuf[0] == 'k')
            {
               char *token = strtok(filebuf, "=");
               while (token != NULL)
               {
                  snprintf(uae_kickstart, sizeof(uae_kickstart), "%s", trimwhitespace(token));
                  token = strtok(NULL, "=");
               }
            }
         }
         fclose(custom_config_fp);
      }
   }

   /* chipmem_size (default 1): 1 = 0.5MB, 2 = 1MB, 4 = 2MB
    * bogomem_size (default 0): 2 = 0.5MB, 4 = 1MB, 6 = 1.5MB, 7 = 1.8MB
    * fastmem_size (default 0): 1 = 1.0MB, ...
    */
   uae_preset_config[0] = '\0';

   preset_opt.cpu_model     = 68000;
   preset_opt.fpu_model     = 0;
   preset_opt.mmu_model     = 0;
   preset_opt.chipmem_size  = 1;
   preset_opt.bogomem_size  = 0;
   preset_opt.fastmem_size  = 0;
   preset_opt.z3mem_size    = 0;
   preset_opt.chipset       = 0;
   preset_opt.cs_compatible = CP_A500;
   preset_opt.address_space_24 = 1;

   switch (config)
   {
      default:
      case EMU_CONFIG_A500:
         preset_opt.chipmem_size  = 1;
         preset_opt.bogomem_size  = 2;
         preset_opt.fastmem_size  = 0;
         preset_opt.chipset       = 0;
         preset_opt.cs_compatible = CP_A500;
         break;

      case EMU_CONFIG_A500OG:
         preset_opt.chipmem_size  = 1;
         preset_opt.bogomem_size  = 0;
         preset_opt.fastmem_size  = 0;
         preset_opt.chipset       = 0;
         preset_opt.cs_compatible = CP_A500;
         break;

      case EMU_CONFIG_A500ECS:
         preset_opt.chipmem_size  = 2;
         preset_opt.bogomem_size  = 0;
         preset_opt.fastmem_size  = 0;
         preset_opt.chipset       = 1;
         preset_opt.cs_compatible = CP_A500;
         break;

      case EMU_CONFIG_A500PLUS:
         preset_opt.chipmem_size  = 2;
         preset_opt.bogomem_size  = 0;
         preset_opt.fastmem_size  = 0;
         preset_opt.chipset       = 3;
         preset_opt.cs_compatible = CP_A500P;
         break;

      case EMU_CONFIG_A600:
         preset_opt.chipmem_size  = 4;
         preset_opt.bogomem_size  = 0;
         preset_opt.fastmem_size  = 8;
         preset_opt.chipset       = 3;
         preset_opt.cs_compatible = CP_A600;
         break;

      case EMU_CONFIG_A600OG:
         preset_opt.chipmem_size  = 4;
         preset_opt.bogomem_size  = 0;
         preset_opt.fastmem_size  = 0;
         preset_opt.chipset       = 3;
         preset_opt.cs_compatible = CP_A600;
         break;

      case EMU_CONFIG_A1200:
         preset_opt.cpu_model     = 68020;
         preset_opt.chipmem_size  = 4;
         preset_opt.bogomem_size  = 0;
         preset_opt.fastmem_size  = 8;
         preset_opt.chipset       = 4;
         preset_opt.cs_compatible = CP_A1200;
         break;

      case EMU_CONFIG_A1200OG:
         preset_opt.cpu_model     = 68020;
         preset_opt.chipmem_size  = 4;
         preset_opt.bogomem_size  = 0;
         preset_opt.fastmem_size  = 0;
         preset_opt.chipset       = 4;
         preset_opt.cs_compatible = CP_A1200;
         break;

      case EMU_CONFIG_A2000:
         preset_opt.chipmem_size  = 2;
         preset_opt.bogomem_size  = 0;
         preset_opt.fastmem_size  = 0;
         preset_opt.chipset       = 1;
         preset_opt.cs_compatible = CP_A2000;
         break;

      case EMU_CONFIG_A2000OG:
         preset_opt.chipmem_size  = 1;
         preset_opt.bogomem_size  = 2;
         preset_opt.fastmem_size  = 0;
         preset_opt.chipset       = 0;
         preset_opt.cs_compatible = CP_A2000;
         break;

      case EMU_CONFIG_A4030:
         preset_opt.cpu_model     = 68030;
         preset_opt.fpu_model     = 68882;
         preset_opt.mmu_model     = 68030;
         preset_opt.chipmem_size  = 4;
         preset_opt.bogomem_size  = 0;
         preset_opt.fastmem_size  = 8;
         preset_opt.chipset       = 4;
         preset_opt.cs_compatible = CP_A4000;
         preset_opt.address_space_24 = 0;
         break;

      case EMU_CONFIG_A4040:
         preset_opt.cpu_model     = 68040;
         preset_opt.fpu_model     = 68040;
         preset_opt.mmu_model     = 68040;
         preset_opt.chipmem_size  = 4;
         preset_opt.bogomem_size  = 0;
         preset_opt.fastmem_size  = 8;
         preset_opt.chipset       = 4;
         preset_opt.cs_compatible = CP_A4000;
         preset_opt.address_space_24 = 0;
         break;

      case EMU_CONFIG_CDTV:
         preset_opt.chipmem_size  = 2;
         preset_opt.bogomem_size  = 0;
         preset_opt.fastmem_size  = 0;
         preset_opt.chipset       = 1;
         preset_opt.cs_compatible = CP_CDTV;

         strcat(uae_preset_config,
         "floppy0type=-1\n"
         );
         break;

      case EMU_CONFIG_CD32:
         preset_opt.cpu_model     = 68020;
         preset_opt.chipmem_size  = 4;
         preset_opt.bogomem_size  = 0;
         preset_opt.fastmem_size  = 0;
         preset_opt.chipset       = 4;
         preset_opt.cs_compatible = CP_CD32;

         strcat(uae_preset_config,
         "floppy0type=-1\n"
         );
         break;

      case EMU_CONFIG_CD32FR:
         preset_opt.cpu_model     = 68020;
         preset_opt.chipmem_size  = 4;
         preset_opt.bogomem_size  = 0;
         preset_opt.fastmem_size  = 8;
         preset_opt.chipset       = 4;
         preset_opt.cs_compatible = CP_CD32;

         strcat(uae_preset_config,
         "floppy0type=-1\n"
         );
         break;

      /* Models of the WinUAE quickstart (cfgfile.c bip_*) */
      case EMU_CONFIG_A1000:
         preset_opt.chipmem_size  = 1;
         preset_opt.bogomem_size  = 0;
         preset_opt.fastmem_size  = 0;
         preset_opt.chipset       = 5;
         preset_opt.cs_compatible = CP_A1000;
         break;

      case EMU_CONFIG_A3000:
         preset_opt.cpu_model     = 68030;
         preset_opt.fpu_model     = 68882;
         preset_opt.mmu_model     = 68030;
         preset_opt.chipmem_size  = 4;
         preset_opt.bogomem_size  = 0;
         preset_opt.fastmem_size  = 8;
         preset_opt.chipset       = 3;
         preset_opt.cs_compatible = CP_A3000;
         preset_opt.address_space_24 = 0;
         break;

      case EMU_CONFIG_A4060:
         preset_opt.cpu_model     = 68060;
         preset_opt.fpu_model     = 68060;
         preset_opt.mmu_model     = 68060;
         preset_opt.chipmem_size  = 4;
         preset_opt.bogomem_size  = 0;
         preset_opt.fastmem_size  = 8;
         preset_opt.chipset       = 4;
         preset_opt.cs_compatible = CP_A4000;
         preset_opt.address_space_24 = 0;
         break;

      case EMU_CONFIG_A4000T:
         preset_opt.cpu_model     = 68040;
         preset_opt.fpu_model     = 68040;
         preset_opt.mmu_model     = 68040;
         preset_opt.chipmem_size  = 4;
         preset_opt.bogomem_size  = 0;
         preset_opt.fastmem_size  = 8;
         preset_opt.chipset       = 4;
         preset_opt.cs_compatible = CP_A4000T;
         preset_opt.address_space_24 = 0;
         break;

      case EMU_CONFIG_CDTVCR:
         preset_opt.chipmem_size  = 2;
         preset_opt.bogomem_size  = 0;
         preset_opt.fastmem_size  = 0;
         preset_opt.chipset       = 3;
         preset_opt.cs_compatible = CP_CDTVCR;

         strcat(uae_preset_config,
         "floppy0type=-1\n"
         );
         break;
   }

   snprintf(temp, sizeof(temp), "%d", preset_opt.cpu_model);
   strcat(uae_preset_config, "cpu_model=");
   strcat(uae_preset_config, temp);
   strcat(uae_preset_config, "\n");

   snprintf(temp, sizeof(temp), "%d", preset_opt.fpu_model);
   strcat(uae_preset_config, "fpu_model=");
   strcat(uae_preset_config, temp);
   strcat(uae_preset_config, "\n");

   snprintf(temp, sizeof(temp), "%d", preset_opt.mmu_model);
   strcat(uae_preset_config, "mmu_model=");
   strcat(uae_preset_config, temp);
   strcat(uae_preset_config, "\n");

   snprintf(temp, sizeof(temp), "%d", preset_opt.chipmem_size);
   strcat(uae_preset_config, "chipmem_size=");
   strcat(uae_preset_config, temp);
   strcat(uae_preset_config, "\n");

   snprintf(temp, sizeof(temp), "%d", preset_opt.bogomem_size);
   strcat(uae_preset_config, "bogomem_size=");
   strcat(uae_preset_config, temp);
   strcat(uae_preset_config, "\n");

   snprintf(temp, sizeof(temp), "%d", preset_opt.fastmem_size);
   strcat(uae_preset_config, "fastmem_size=");
   strcat(uae_preset_config, temp);
   strcat(uae_preset_config, "\n");

   strcat(uae_preset_config, "cpu_24bit_addressing=");
   strcat(uae_preset_config, (preset_opt.address_space_24 ? "true" : "false"));
   strcat(uae_preset_config, "\n");

   snprintf(temp, sizeof(temp), "%s", csmode[preset_opt.chipset]);
   strcat(uae_preset_config, "chipset=");
   strcat(uae_preset_config, temp);
   strcat(uae_preset_config, "\n");

   snprintf(temp, sizeof(temp), "%s", cscompa[preset_opt.cs_compatible]);
   strcat(uae_preset_config, "chipset_compatible=");
   strcat(uae_preset_config, temp);
   strcat(uae_preset_config, "\n");

   strlcat(uae_preset_config, uae_custom_config, sizeof(uae_preset_config));
   return uae_preset_config;
}

static void retro_config_preset(const char *model)
{
   int model_int = emu_config_int(model);
   strlcpy(uae_model_config, emu_config(model_int), sizeof(uae_model_config));
   strlcpy(uae_preset, emu_config_string("model", model_int), sizeof(uae_preset));

   /* Allow Kickstart overrides in custom presets */
   if (string_is_empty(uae_kickstart))
   {
      strlcpy(uae_kickstart, emu_config_string("kickstart", model_int), sizeof(uae_kickstart));
      strlcpy(uae_kickstart_ext, emu_config_string("kickstart_ext", model_int), sizeof(uae_kickstart_ext));
   }
}

static bool retro_create_config(void)
{
   char *tmp_str      = NULL;
   uae_full_config[0] = '\0';

   /* Model preset */
   if (!strcmp(opt_model, "auto"))
   {
      if (opt_use_boot_hd)
         retro_config_preset(opt_model_hd);
      else
         retro_config_preset(opt_model_fd);
   }
   else
      retro_config_preset(opt_model);

   /* "Browsed" file in ZIP */
   char browsed_file[RETRO_PATH_MAX] = {0};
   if (!string_is_empty(full_path) && (strstr(full_path, ".zip#") || strstr(full_path, ".7z#")))
   {
      char *token = strtok(full_path, "#");
      while (token != NULL)
      {
         snprintf(browsed_file, sizeof(browsed_file), "%s", token);
         token = strtok(NULL, "#");
      }
   }

   if (!string_is_empty(full_path) && (path_is_valid(full_path) || path_is_directory(full_path)))
   {
      /* Extract ZIP for examination */
      if (strendswith(full_path, ".zip")
       || strendswith(full_path, ".7z")
       || strendswith(full_path, ".rp9"))
      {
         char zip_basename[RETRO_PATH_MAX] = {0};
         snprintf(zip_basename, sizeof(zip_basename), "%s", path_basename(full_path));
         path_remove_extension(zip_basename);

         path_mkdir(retro_temp_directory);
         if (strendswith(full_path, ".zip") || strendswith(full_path, ".rp9"))
            zip_uncompress(full_path, retro_temp_directory, NULL);
         else if (strendswith(full_path, ".7z"))
            sevenzip_uncompress(full_path, retro_temp_directory, NULL);

         /* Default to directory mode */
         int zip_mode = 0;
         snprintf(full_path, sizeof(full_path), "%s", retro_temp_directory);

         FILE *zip_m3u;
         char zip_m3u_list[DC_MAX_SIZE][RETRO_PATH_MAX] = {0};
         char zip_m3u_path[RETRO_PATH_MAX] = {0};
         int zip_m3u_num = 0;

         char *retro_temp_local   = utf8_to_local_string_alloc(retro_temp_directory);
         char *zip_basename_local = utf8_to_local_string_alloc(zip_basename);

         snprintf(zip_m3u_path, sizeof(zip_m3u_path), "%s%s%s.m3u",
               retro_temp_local,
               DIR_SEP_STR,
               zip_basename_local);

         free(retro_temp_local);
         retro_temp_local = NULL;
         free(zip_basename_local);
         zip_basename_local = NULL;

         RDIR *zip_dir;
         zip_dir = retro_opendir(retro_temp_directory);
         char *zip_lastfile = {0};
         while (retro_readdir(zip_dir))
         {
            const char *name = retro_dirent_get_name(zip_dir);

            if (name[0] == '.' || strendswith(name, "m3u") || zip_mode > 1 || browsed_file[0] != '\0')
               continue;

            zip_lastfile = utf8_to_local_string_alloc(name);

            /* Multi file mode, generate playlist */
            if (dc_get_image_type(zip_lastfile) == DC_IMAGE_TYPE_FLOPPY ||
                dc_get_image_type(zip_lastfile) == DC_IMAGE_TYPE_CD ||
                dc_get_image_type(zip_lastfile) == DC_IMAGE_TYPE_HD)
            {
               zip_mode = 1;
               zip_m3u_num++;
               snprintf(zip_m3u_list[zip_m3u_num-1], RETRO_PATH_MAX, "%s", zip_lastfile);
            }
            else if (dc_get_image_type(zip_lastfile) == DC_IMAGE_TYPE_WHDLOAD)
            {
               /* Only accept infos if slave or dir exists,
                * in order to get proper content path */
               char zip_lastfile_path[RETRO_PATH_MAX] = {0};
               snprintf(zip_lastfile_path, sizeof(zip_lastfile_path), "%s%s%s",
                     retro_temp_directory, DIR_SEP_STR, zip_lastfile);
               if (strendswith(zip_lastfile_path, "info"))
               {
                  path_remove_extension(zip_lastfile_path);
                  if (!path_is_directory(zip_lastfile_path))
                     snprintf(zip_lastfile_path, sizeof(zip_lastfile_path), "%s.slave",
                           zip_lastfile_path);
               }
               if (path_is_valid(zip_lastfile_path) || path_is_directory(zip_lastfile_path))
               {
                  zip_mode = 2;
                  snprintf(full_path, sizeof(full_path), "%s%s%s", retro_temp_directory, DIR_SEP_STR, zip_lastfile);
               }
            }
         }

         /* Final check for single directories */
         if (zip_mode == 0)
         {
            char zip_lastfile_path[RETRO_PATH_MAX] = {0};
            snprintf(zip_lastfile_path, sizeof(zip_lastfile_path), "%s%s%s",
                  retro_temp_directory, DIR_SEP_STR, zip_lastfile);
            if (path_is_directory(zip_lastfile_path))
               snprintf(full_path, sizeof(full_path), "%s", zip_lastfile_path);
         }

         retro_closedir(zip_dir);
         if (zip_lastfile)
            free(zip_lastfile);
         zip_lastfile = NULL;

         switch (zip_mode)
         {
            case 0: /* Extracted path */
            case 2: /* Single image */
               if (browsed_file[0] != '\0')
                  snprintf(full_path, sizeof(full_path), "%s%s%s", retro_temp_directory, DIR_SEP_STR, browsed_file);
               break;
            case 1: /* Generated playlist */
               qsort(zip_m3u_list, zip_m3u_num, RETRO_PATH_MAX, qstrcmp);
               zip_m3u = fopen(zip_m3u_path, "w");
               if (zip_m3u)
               {
                  for (unsigned l = 0; l < zip_m3u_num; l++)
                     fprintf(zip_m3u, "%s\n", zip_m3u_list[l]);
                  fclose(zip_m3u);
               }
               snprintf(full_path, sizeof(full_path), "%s", zip_m3u_path);
               log_cb(RETRO_LOG_INFO, "->M3U: %s\n", zip_m3u_path);
               break;
         }
      }

      /* Inspect M3U */
      unsigned m3u = 0;
      if (strendswith(full_path, "m3u"))
         m3u = dc_inspect_m3u(full_path);

      /* Floppy disk, hard drive, WHDLoad or playlist */
      if (dc_get_image_type(full_path) == DC_IMAGE_TYPE_FLOPPY
       || dc_get_image_type(full_path) == DC_IMAGE_TYPE_HD
       || dc_get_image_type(full_path) == DC_IMAGE_TYPE_WHDLOAD
       || m3u & (1 << DC_IMAGE_TYPE_FLOPPY)
       || m3u & (1 << DC_IMAGE_TYPE_ARCHIVE)
       || m3u & (1 << DC_IMAGE_TYPE_HD)
       || m3u & (1 << DC_IMAGE_TYPE_WHDLOAD))
      {
         /* Check if model is specified in the path on 'Automatic' */
         if (!strcmp(opt_model, "auto"))
         {
            if (strstr(full_path, "(A4060)") || strstr(full_path, "(060)"))
            {
               log_cb(RETRO_LOG_INFO, "Found \"(A4060)\" or \"(060)\" in: \"%s\".\n", full_path);
               retro_config_preset("A4060");
            }
            else if (strstr(full_path, "(A4000T)"))
            {
               log_cb(RETRO_LOG_INFO, "Found \"(A4000T)\" in: \"%s\".\n", full_path);
               retro_config_preset("A4000T");
            }
            else if (strstr(full_path, "(A3000)"))
            {
               log_cb(RETRO_LOG_INFO, "Found \"(A3000)\" in: \"%s\".\n", full_path);
               retro_config_preset("A3000");
            }
            else if (strstr(full_path, "(A1000)"))
            {
               log_cb(RETRO_LOG_INFO, "Found \"(A1000)\" in: \"%s\".\n", full_path);
               retro_config_preset("A1000");
            }
            else if (strstr(full_path, "(A4030)") || strstr(full_path, "(030)"))
            {
               log_cb(RETRO_LOG_INFO, "Found \"(A4030)\" or \"(030)\" in: \"%s\".\n", full_path);
               retro_config_preset("A4030");
            }
            else if (strstr(full_path, "(A4040)") || strstr(full_path, "(040)"))
            {
               log_cb(RETRO_LOG_INFO, "Found \"(A4040)\" or \"(040)\" in: \"%s\".\n", full_path);
               retro_config_preset("A4040");
            }
            else if (strstr(full_path, "(A1200OG)") || strstr(full_path, "(A1200NF)"))
            {
               log_cb(RETRO_LOG_INFO, "Found \"(A1200OG)\" or \"(A1200NF)\" in: \"%s\".\n", full_path);
               retro_config_preset("A1200OG");
            }
            else if (strstr(full_path, "(A1200)"))
            {
               log_cb(RETRO_LOG_INFO, "Found \"(A1200)\" in: \"%s\".\n", full_path);
               retro_config_preset("A1200");
            }
            else if (strstr(full_path, "(A600OG)") || strstr(full_path, "(A600NF)"))
            {
               log_cb(RETRO_LOG_INFO, "Found \"(A600OG)\" or \"(A600NF)\" in: \"%s\".\n", full_path);
               retro_config_preset("A600OG");
            }
            else if (strstr(full_path, "(A600)"))
            {
               log_cb(RETRO_LOG_INFO, "Found \"(A600)\" in: \"%s\".\n", full_path);
               retro_config_preset("A600");
            }
            else if (strstr(full_path, "(A500+)") || strstr(full_path, "(A500PLUS)"))
            {
               log_cb(RETRO_LOG_INFO, "Found \"(A500+)\" or \"(A500PLUS)\" in: \"%s\".\n", full_path);
               retro_config_preset("A500PLUS");
            }
            else if (strstr(full_path, "(A500ECS)"))
            {
               log_cb(RETRO_LOG_INFO, "Found \"(A500ECS)\" in: \"%s\".\n", full_path);
               retro_config_preset("A500ECS");
            }
            else if (strstr(full_path, "(A500OG)") || strstr(full_path, "(512K)") || strstr(full_path, "(512KB)"))
            {
               log_cb(RETRO_LOG_INFO, "Found \"(A500OG)\" or \"(512K)\" or \"(512KB)\" in: \"%s\".\n", full_path);
               retro_config_preset("A500OG");
            }
            else if (strstr(full_path, "(A500)"))
            {
               log_cb(RETRO_LOG_INFO, "Found \"(A500)\" in: \"%s\".\n", full_path);
               retro_config_preset("A500");
            }
            else if (strstr(full_path, "AGA") || strstr(full_path, "AmigaCD") || strstr(full_path, "CD32"))
            {
               log_cb(RETRO_LOG_INFO, "Found \"AGA\" or \"AmigaCD\" or \"CD32\" in: \"%s\".\n", full_path);
               /* Change to A1200 only if not already with AGA, and prevent overriding non-fast RAM preset */
               if (strstr(opt_model_hd, "A1200"))
                  retro_config_preset(opt_model_hd);
               else if (!strstr(uae_preset, "A1200") && !strstr(uae_preset, "A40"))
                  retro_config_preset("A1200");
            }
            else if (strstr(full_path, "ECS"))
            {
               log_cb(RETRO_LOG_INFO, "Found \"ECS\" in: \"%s\".\n", full_path);
               /* Change to A600 only if not already with ECS */
               if (!strstr(uae_preset, "A500PLUS"))
                  retro_config_preset("A600");
            }
            else if (strstr(full_path, "OCS"))
            {
               log_cb(RETRO_LOG_INFO, "Found \"OCS\" in: \"%s\".\n", full_path);
               /* Change to A500 only if not already with OCS */
               if (!strstr(uae_preset, "A500"))
                  retro_config_preset("A500");
            }
            else
            {
               /* No model specified */
               log_cb(RETRO_LOG_INFO, "No model specified in: \"%s\".\n", full_path);

               /* Hard disks must default to a machine with HD interface */
               if (!opt_use_boot_hd &&
                   (dc_get_image_type(full_path) == DC_IMAGE_TYPE_HD ||
                    dc_get_image_type(full_path) == DC_IMAGE_TYPE_WHDLOAD ||
                    m3u & (1 << DC_IMAGE_TYPE_CD) ||
                    m3u & (1 << DC_IMAGE_TYPE_HD) ||
                    m3u & (1 << DC_IMAGE_TYPE_WHDLOAD)))
                  retro_config_preset(opt_model_hd);
            }
         }

         /* Write model preset */
         retro_config_append(uae_model_config);

         /* Verify and write Kickstart */
         retro_config_kickstart();

         /* Bootable HD exception */
         if (opt_use_boot_hd)
            retro_config_boot_hd();

         /* Hard drive or WHDLoad image */
         if (dc_get_image_type(full_path) == DC_IMAGE_TYPE_HD
          || dc_get_image_type(full_path) == DC_IMAGE_TYPE_WHDLOAD
          || m3u == (1 << DC_IMAGE_TYPE_HD)
          || m3u == (1 << DC_IMAGE_TYPE_WHDLOAD))
         {
            /* M3U playlist */
            if (strendswith(full_path, "m3u"))
            {
               /* Parse the M3U file */
               dc_parse_m3u(dc, full_path, retro_save_directory);

               /* Some debugging */
               log_cb(RETRO_LOG_INFO, "M3U parsed, %d file(s) found\n", dc->count);
               for (unsigned i = 0; i < dc->count; i++)
                  log_cb(RETRO_LOG_DEBUG, "File %d: %s\n", i + 1, dc->files[i]);
            }
            /* Single file */
            else
            {
               /* Add the file to disk control context */
               char hd_image_label[RETRO_PATH_MAX];
               hd_image_label[0] = '\0';

               if (!string_is_empty(full_path))
                  fill_pathname(
                        hd_image_label, path_basename(full_path), "", sizeof(hd_image_label));

               /* Must reset disk control struct here,
                * otherwise duplicate entries will be
                * added when calling retro_reset() */
               dc_reset(dc);
               dc_add_file(dc, full_path, hd_image_label);
            }

            /* Init only existing disks */
            if (dc->count)
            {
               /* Init first disk */
               dc->index = 0;
               /* Don't consider hard drives as "inserted" since they can't be "ejected" */
               dc->eject_state = true;
               display_current_image(dc->labels[0], true);
            }

            /* WHDLoad support */
            if (opt_use_whdload)
            {
               /* Manipulate WHDLoad.prefs */
               int WHDLoad_ConfigDelay = 0;
               int WHDLoad_SplashDelay = 0;
               int WHDLoad_WriteDelay  = 0;

               switch (opt_use_whdload_prefs)
               {
                  case 1:
                     WHDLoad_ConfigDelay = -1;
                     break;
                  case 2:
                     WHDLoad_SplashDelay = 200;
                     break;
                  case 3:
                     WHDLoad_ConfigDelay = -1;
                     WHDLoad_SplashDelay = -1;
                     break;
               }

               FILE *whdload_prefs_fp;
               char whdload_prefs_path[RETRO_PATH_MAX];
               path_join(whdload_prefs_path, retro_system_directory, "WHDLoad.prefs");

               if (!path_is_valid(whdload_prefs_path))
               {
                  log_cb(RETRO_LOG_INFO, "WHDLoad.prefs \"%s\" not found, attempting to create...\n", whdload_prefs_path);

                  char whdload_prefs_gz_path[RETRO_PATH_MAX];
                  path_join(whdload_prefs_gz_path, retro_system_directory, "WHDLoad.prefs.gz");

                  FILE *whdload_prefs_gz_fp;
                  if ((whdload_prefs_gz_fp = fopen(whdload_prefs_gz_path, "wb")))
                  {
                     /* Write GZ */
                     fwrite(whdload_WHDLoad_prefs_gz, whdload_WHDLoad_prefs_gz_len, 1, whdload_prefs_gz_fp);
                     fclose(whdload_prefs_gz_fp);

                     /* Extract GZ */
                     gz_uncompress(whdload_prefs_gz_path, whdload_prefs_path);
                     retro_remove(whdload_prefs_gz_path);
                  }
                  else
                     log_cb(RETRO_LOG_ERROR, "Unable to create WHDLoad.prefs: \"%s\".\n", whdload_prefs_path);
               }

               FILE *whdload_prefs_new_fp;
               char whdload_prefs_new_path[RETRO_PATH_MAX];
               path_join(whdload_prefs_new_path, retro_system_directory, "WHDLoad.prefs_new");

               char whdload_prefs_backup_path[RETRO_PATH_MAX];
               path_join(whdload_prefs_backup_path, retro_system_directory, "WHDLoad.prefs_backup");

               char whdload_buf[256]      = {0};
               char whdload_buf_row[256]  = {0};
               char whdload_buf_new[4096] = {0};
               if ((whdload_prefs_fp = fopen(whdload_prefs_path, "r")))
               {
                  bool whdload_prefs_changes  = false;
                  bool whdload_quitkey_set    = false;
                  const char *whdload_quitkey = "$2b"; /* AK_NUMBERSIGN = RETROK_HASH */

                  while (fgets(whdload_buf, sizeof(whdload_buf), whdload_prefs_fp))
                  {
                     if (opt_use_whdload_buttonwait
                           && strstr(whdload_buf, "ButtonWait") && whdload_buf[0] == ';')
                        snprintf(whdload_buf_row, sizeof(whdload_buf_row), "%s", whdload_buf + 1);
                     else if (!opt_use_whdload_buttonwait
                           && strstr(whdload_buf, "ButtonWait") && whdload_buf[0] == 'B')
                        snprintf(whdload_buf_row, sizeof(whdload_buf_row), ";%s", whdload_buf);
                     else if (opt_use_whdload_nowritecache
                           && strstr(whdload_buf, "NoWriteCache") && whdload_buf[0] == ';')
                        snprintf(whdload_buf_row, sizeof(whdload_buf_row), "%s", whdload_buf + 1);
                     else if (!opt_use_whdload_nowritecache
                           && strstr(whdload_buf, "NoWriteCache") && whdload_buf[0] == 'N')
                        snprintf(whdload_buf_row, sizeof(whdload_buf_row), ";%s", whdload_buf);
                     else if (!opt_use_whdload_nowritecache
                           && strstr(whdload_buf, "QuitKey=") && whdload_buf[0] == 'Q')
                     {
                        /* WHDLoad save data quit requires having a standard quit key */
                        snprintf(whdload_buf_row, sizeof(whdload_buf_row), "QuitKey=%s\n", whdload_quitkey);
                        whdload_quitkey_set = true;
                     }
                     else if (strstr(whdload_buf, "ConfigDelay=") && whdload_buf[0] == 'C')
                        snprintf(whdload_buf_row, sizeof(whdload_buf_row), "ConfigDelay=%d\n", WHDLoad_ConfigDelay);
                     else if (strstr(whdload_buf, "SplashDelay=") && whdload_buf[0] == 'S')
                        snprintf(whdload_buf_row, sizeof(whdload_buf_row), "SplashDelay=%d\n", WHDLoad_SplashDelay);
                     else if (strstr(whdload_buf, "WriteDelay=") && whdload_buf[0] == 'W')
                        snprintf(whdload_buf_row, sizeof(whdload_buf_row), "WriteDelay=%d\n", WHDLoad_WriteDelay);
                     else
                        snprintf(whdload_buf_row, sizeof(whdload_buf_row), "%s", whdload_buf);

                     if (memcmp(whdload_buf, whdload_buf_row, sizeof(whdload_buf)))
                        whdload_prefs_changes = true;

                     strlcat(whdload_buf_new, whdload_buf_row, sizeof(whdload_buf_new));
                  }

                  /* retro_unload triggers QuitKey (RETROK_HASH) and runs a few frames so that data gets saved */
                  if (!opt_use_whdload_nowritecache && !whdload_quitkey_set)
                  {
                     snprintf(whdload_buf_row, sizeof(whdload_buf_row), "QuitKey=%s\n", whdload_quitkey);
                     strlcat(whdload_buf_new, whdload_buf_row, sizeof(whdload_buf_new));
                     whdload_prefs_changes = true;
                  }

                  fclose(whdload_prefs_fp);

                  if (whdload_prefs_changes)
                  {
                     if ((whdload_prefs_new_fp = fopen(whdload_prefs_new_path, "w")))
                     {
                        fprintf(whdload_prefs_new_fp, "%s", whdload_buf_new);
                        fclose(whdload_prefs_new_fp);
                     }
                     else
                        log_cb(RETRO_LOG_ERROR, "Unable to create new WHDLoad.prefs: \"%s\".\n", whdload_prefs_new_path);

                     /* Remove old backup prefs */
                     retro_remove(whdload_prefs_backup_path);

                     /* Backup old and update new prefs */
                     rename(whdload_prefs_path, whdload_prefs_backup_path);
                     rename(whdload_prefs_new_path, whdload_prefs_path);
                  }
               }

               char whdload_path[RETRO_PATH_MAX];
               path_join(whdload_path, retro_save_directory, "WHDLoad");

               char whdload_c_path[RETRO_PATH_MAX];
               path_join(whdload_c_path, retro_save_directory, "WHDLoad/C");

               /* Verify WHDLoad */
               if (!path_is_directory(whdload_path) || (path_is_directory(whdload_path) && !path_is_directory(whdload_c_path)))
               {
                  log_cb(RETRO_LOG_INFO, "WHDLoad image directory \"%s\" not found, attempting to create...\n", whdload_path);
                  path_mkdir(whdload_path);

                  char whdload_files_zip_path[RETRO_PATH_MAX];
                  path_join(whdload_files_zip_path, retro_save_directory, "WHDLoad_files.zip");

                  FILE *whdload_files_zip_fp;
                  if ((whdload_files_zip_fp = fopen(whdload_files_zip_path, "wb")))
                  {
                     /* Write ZIP */
                     fwrite(whdload_WHDLoad_files_zip, whdload_WHDLoad_files_zip_len, 1, whdload_files_zip_fp);
                     fclose(whdload_files_zip_fp);

                     /* Extract ZIP */
                     zip_uncompress(whdload_files_zip_path, whdload_path, NULL);
                     retro_remove(whdload_files_zip_path);
                  }

                  if (!path_is_directory(whdload_c_path))
                     log_cb(RETRO_LOG_ERROR, "Unable to create WHDLoad image directory: \"%s\".\n", whdload_path);
               }
               /* Attach directory */
               if (path_is_directory(whdload_path) && path_is_directory(whdload_c_path))
               {
#ifdef WIN32
                  tmp_str = string_replace_substring(whdload_path, strlen(whdload_path), "\\", strlen("\\"), "\\\\", strlen("\\\\"));
#else
                  tmp_str = strdup(whdload_path);
#endif
                  retro_config_append("filesystem2=rw,WHDLoad:WHDLoad:\"%s\",0\n", tmp_str);
                  free(tmp_str);
                  tmp_str = NULL;

                  /* Check and copy Kickstarts */
                  whdload_kscopy();

                  /* Copy required files host-wise with file mode */
                  if (path_is_valid(whdload_prefs_path))
                     whdload_prefs_copy();

                  /* Rename theme prefs if necessary */
                  char whdload_theme_path[RETRO_PATH_MAX];
                  path_join(whdload_theme_path, retro_save_directory, "WHDLoad/Devs/system-configuration");
                  char whdload_theme_default_path[RETRO_PATH_MAX];
                  path_join(whdload_theme_default_path, retro_save_directory, "WHDLoad/Devs/system-configuration-default");
                  char whdload_theme_native_path[RETRO_PATH_MAX];
                  path_join(whdload_theme_native_path, retro_save_directory, "WHDLoad/Devs/system-configuration-native");

                  /* Default (black) */
                  if (!opt_use_whdload_theme)
                  {
                     if (path_is_valid(whdload_theme_path) && path_is_valid(whdload_theme_default_path))
                     {
                        if (fcmp(whdload_theme_path, whdload_theme_default_path))
                        {
                           if (fcopy(whdload_theme_default_path, whdload_theme_path) < 0)
                              log_cb(RETRO_LOG_INFO, "WHDLoad failed to change theme to \"Default\".\n");
                           else
                              log_cb(RETRO_LOG_INFO, "WHDLoad changed theme to \"Default\".\n");
                        }
                     }
                     else if (!path_is_valid(whdload_theme_default_path) && !path_is_valid(whdload_theme_native_path))
                        ; /* No-op fallback */
                     else
                        log_cb(RETRO_LOG_INFO, "WHDLoad theme \"Default\" not found. Delete \"WHDLoad\" directory to reinstall!\n");
                  }
                  /* Native (gray) */
                  else
                  {
                     if (path_is_valid(whdload_theme_path) && path_is_valid(whdload_theme_native_path))
                     {
                        if (fcmp(whdload_theme_path, whdload_theme_native_path))
                        {
                           if (fcopy(whdload_theme_native_path, whdload_theme_path) < 0)
                              log_cb(RETRO_LOG_INFO, "WHDLoad failed to change theme to \"Native\".\n");
                           else
                              log_cb(RETRO_LOG_INFO, "WHDLoad changed theme to \"Native\".\n");
                        }
                     }
                     else
                        log_cb(RETRO_LOG_INFO, "WHDLoad theme \"Native\" not found. Delete \"WHDLoad\" directory to reinstall!\n");
                  }
               }

               /* Verify WHDSaves */
               char whdsaves_path[RETRO_PATH_MAX];
               path_join(whdsaves_path, retro_save_directory, "WHDSaves");
               if (!path_is_directory(whdsaves_path))
                  path_mkdir(whdsaves_path);
               /* Attach directory */
               if (path_is_directory(whdsaves_path))
               {
#ifdef WIN32
                  tmp_str = string_replace_substring(whdsaves_path, strlen(whdsaves_path), "\\", strlen("\\"), "\\\\", strlen("\\\\"));
#else
                  tmp_str = strdup(whdsaves_path);
#endif
                  retro_config_append("filesystem2=rw,WHDSaves:WHDSaves:\"%s\",0\n", tmp_str);
                  free(tmp_str);
                  tmp_str = NULL;
               }
               else
                  log_cb(RETRO_LOG_ERROR, "Unable to create WHDSaves image directory: \"%s\".\n", whdsaves_path);
            }

            /* Attach hard drive(s) */
            retro_config_harddrives();
         }
         else
         {
            /* M3U playlist */
            if (strendswith(full_path, "m3u"))
            {
               /* Parse the M3U file */
               dc_parse_m3u(dc, full_path, retro_save_directory);

               /* Some debugging */
               log_cb(RETRO_LOG_INFO, "M3U parsed, %d file(s) found\n", dc->count);
               for (unsigned i = 0; i < dc->count; i++)
                  log_cb(RETRO_LOG_DEBUG, "File %d: %s\n", i + 1, dc->files[i]);
            }
            /* Single file */
            else
            {
               /* Add the file to disk control context */
               char disk_image_label[RETRO_PATH_MAX];
               disk_image_label[0] = '\0';

               if (!string_is_empty(full_path))
                  fill_pathname(
                        disk_image_label, path_basename(full_path), "", sizeof(disk_image_label));

               /* Must reset disk control struct here,
                * otherwise duplicate entries will be
                * added when calling retro_reset() */
               dc_reset(dc);
               dc_add_file(dc, full_path, disk_image_label);
            }

            /* Init only existing disks */
            if (dc->count)
            {
               /* Init first disk */
               dc->index = 0;
               dc->eject_state = false;
               if (dc->types[dc->index] == DC_IMAGE_TYPE_FLOPPY)
               {
                  log_cb(RETRO_LOG_INFO, "Disk (%d) inserted in drive DF0: \"%s\".\n", dc->index + 1, dc->files[dc->index]);
                  tmp_str = utf8_to_local_string_alloc(dc->files[0]);
                  retro_config_append("floppy0=%s\n", tmp_str);
                  free(tmp_str);
                  tmp_str = NULL;
               }

               /* Append rest of the disks to the config if M3U is a MultiDrive-M3U */
               if (strstr(full_path, "(MD)") != NULL || opt_floppy_multidrive)
               {
                  uint8_t i;
                  uint8_t floppy = 1;

                  for (i = 1; i < dc->count; i++)
                  {
                     if (floppy < MAX_FLOPPY_DRIVES)
                     {
                        if (strstr(dc->labels[i], M3U_SAVEDISK_LABEL))
                           continue;

                        if (dc->types[i] != DC_IMAGE_TYPE_FLOPPY)
                           continue;

                        log_cb(RETRO_LOG_INFO, "Disk (%d) inserted in drive DF%d: \"%s\".\n", floppy + 1, floppy, dc->files[i]);
                        tmp_str = utf8_to_local_string_alloc(dc->files[i]);
                        retro_config_append("floppy%d=%s\n", floppy, tmp_str);
                        /* By default only DF0: is enabled, so floppyXtype needs to be set on the extra drives */
                        retro_config_append("floppy%dtype=%d\n", floppy, 0); /* 0 = DRV_35_DD */

                        if (libretro_runloop_active)
                        {
                           changed_prefs.floppyslots[i].dfxtype = 0;
                           strcpy(changed_prefs.floppyslots[i].df, tmp_str);
                        }

                        free(tmp_str);
                        tmp_str = NULL;

                        floppy++;
                     }
                     else
                     {
                        log_cb(RETRO_LOG_WARN, "Too many disks for MultiDrive!\n");
                        break;
                     }
                  }
               }
               else if (libretro_runloop_active)
               {
                  uint8_t i;
                  for (i = 1; i < MAX_FLOPPY_DRIVES; i++)
                  {
                     changed_prefs.floppyslots[i].dfxtype = DRV_NONE;
                     changed_prefs.floppyslots[i].df[0]   = 0;
                  }
               }

               if (m3u & (1 << DC_IMAGE_TYPE_CD))
               {
                  uint8_t i;
                  uint8_t drive = 0;
                  /* Attach optical disc */
                  for (i = 0; i < dc->count && !drive; i++)
                  {
                     if (dc->types[i] != DC_IMAGE_TYPE_CD)
                        continue;

                     log_cb(RETRO_LOG_INFO, "CD (%d) inserted in drive CD0: \"%s\".\n", drive + 1, dc->files[i]);
                     retro_config_append("cdimage0=%s,%s\n", dc->files[i], (opt_cd_startup_delayed_insert ? "delay" : "")); /* ","-suffix needed if filename contains "," */
                     drive++;
                  }

                  if (drive)
                     retro_config_append("scsi=true\n");
               }
            }

            /* Attach hard drive(s) */
            if (m3u & (1 << DC_IMAGE_TYPE_HD))
               retro_config_harddrives();

            /* Show current index image */
            if (dc->count)
               display_current_image(dc->labels[dc->index], true);

            /* Scan for save disk 0, append if exists */
            if (dc->count)
            {
               bool file_check = dc_save_disk_toggle(dc, true, false);
               if (file_check)
                  dc_save_disk_toggle(dc, false, false);
            }
         }

         /* Write common config */
         retro_config_append(uae_config);
      }
      /* CD image */
      else if (dc_get_image_type(full_path) == DC_IMAGE_TYPE_CD
            || m3u == (1 << DC_IMAGE_TYPE_CD))
      {
         /* Check if model is specified in the path on 'Automatic' */
         if (!strcmp(opt_model, "auto"))
         {
            if (strstr(full_path, "(CD32FR)") || strstr(full_path, "FastRAM"))
            {
               log_cb(RETRO_LOG_INFO, "Found \"(CD32FR)\" or \"FastRAM\" in: \"%s\".\n", full_path);
               retro_config_preset("CD32FR");
            }
            else if (strstr(full_path, "(CD32)") || strstr(full_path, "(CD32NF)"))
            {
               log_cb(RETRO_LOG_INFO, "Found \"(CD32)\" or \"(CD32NF)\" in: \"%s\".\n", full_path);
               retro_config_preset("CD32");
            }
            else if (strstr(full_path, "CDTV-CR") || strstr(full_path, "CDTVCR"))
            {
               log_cb(RETRO_LOG_INFO, "Found \"CDTV-CR\" in: \"%s\".\n", full_path);
               retro_config_preset("CDTVCR");
            }
            else if (strstr(full_path, "CDTV"))
            {
               log_cb(RETRO_LOG_INFO, "Found \"CDTV\" in: \"%s\".\n", full_path);
               retro_config_preset("CDTV");
            }
            else
            {
               /* Default CD model */
               if (opt_use_boot_hd)
                  retro_config_preset(opt_model_hd);
               else
                  retro_config_preset(opt_model_cd);

               /* No model specified */
               log_cb(RETRO_LOG_INFO, "No model specified in: \"%s\".\n", full_path);
            }
         }

         /* Write model preset */
         retro_config_append(uae_model_config);

         /* Verify and write Kickstart */
         retro_config_kickstart();

         /* Bootable HD exception */
         if (opt_use_boot_hd)
         {
            retro_config_boot_hd();
            retro_config_append("scsi=true\n");
         }

         /* M3U playlist */
         if (strendswith(full_path, "m3u"))
         {
            /* Parse the M3U file */
            dc_parse_m3u(dc, full_path, retro_save_directory);

            /* Some debugging */
            log_cb(RETRO_LOG_INFO, "M3U parsed, %d file(s) found\n", dc->count);
            for (unsigned i = 0; i < dc->count; i++)
               log_cb(RETRO_LOG_DEBUG, "File %d: %s\n", i + 1, dc->files[i]);
         }
         /* Single file */
         else
         {
            /* Add the file to disk control context */
            char cd_image_label[RETRO_PATH_MAX];
            cd_image_label[0] = '\0';

            if (!string_is_empty(full_path))
               fill_pathname(
                     cd_image_label, path_basename(full_path), "", sizeof(cd_image_label));

            /* Must reset disk control struct here,
             * otherwise duplicate entries will be
             * added when calling retro_reset() */
            dc_reset(dc);
            dc_add_file(dc, full_path, cd_image_label);
         }

         /* Init only existing disks */
         if (dc->count)
         {
            /* Init first disk */
            dc->index = 0;
            dc->eject_state = false;
            display_current_image(dc->labels[dc->index], true);
            log_cb(RETRO_LOG_INFO, "CD (%d) inserted in drive CD0: \"%s\".\n", dc->index + 1, dc->files[dc->index]);
            retro_config_append("cdimage0=%s,%s\n", dc->files[0], (opt_cd_startup_delayed_insert ? "delay" : "")); /* ","-suffix needed if filename contains "," */
         }

         /* Write common config */
         retro_config_append(uae_config);
      }
      /* UAE config file */
      else if (strendswith(full_path, "uae"))
      {
         /* Write model preset */
         retro_config_append(uae_model_config);

         /* Write common config */
         retro_config_append(uae_config);

         /* Must reset disk control struct here,
          * otherwise duplicate entries will be
          * added when calling retro_reset() */
         dc_reset(dc);

         /* Iterate parsed file and append all rows to the temporary config */
         FILE *configfile_custom;
         char filebuf[RETRO_PATH_MAX];
         if ((configfile_custom = fopen(full_path, "r")))
         {
            while (fgets(filebuf, sizeof(filebuf), configfile_custom))
            {
               /* Skip input rows */
               if ((strstr(filebuf, "input.") && filebuf[0] == 'i')
                || (strstr(filebuf, "joyport") && filebuf[0] == 'j'))
                  continue;

               /* Skip Kickstart row if Kickstart is not automatic */
               if (strcmp(opt_kickstart, "auto"))
                  if (strstr(filebuf, "kickstart_rom_file=") && filebuf[0] == 'k')
                     continue;

               /* Skip Kickstart & model rows if model is not automatic */
               if (strcmp(opt_model, "auto"))
               {
                  if ((strstr(filebuf, "kickstart_rom_file=") && filebuf[0] == 'k')
                   || (strstr(filebuf, "cpu_model=") && filebuf[0] == 'c')
                   || (strstr(filebuf, "fpu_model=") && filebuf[0] == 'f')
                   || (strstr(filebuf, "chipset=") && filebuf[0] == 'c')
                   || (strstr(filebuf, "chipset_compatible=") && filebuf[0] == 'c')
                   || (strstr(filebuf, "chipmem_size=") && filebuf[0] == 'c')
                   || (strstr(filebuf, "bogomem_size=") && filebuf[0] == 'b')
                   || (strstr(filebuf, "fastmem_size=") && filebuf[0] == 'f')
                   || (strstr(filebuf, "z3mem_size=") && filebuf[0] == 'z'))
                     continue;
               }

               /* Skip overrides always */
               if (override_opt.cpu_model > -1 && strstr(filebuf, "cpu_model=") && filebuf[0] == 'c')
                  continue;
               if (override_opt.fpu_model > -1 && strstr(filebuf, "fpu_model=") && filebuf[0] == 'f')
                  continue;
               if (override_opt.chipmem_size > -1 && strstr(filebuf, "chipmem_size=") && filebuf[0] == 'c')
                  continue;
               if (override_opt.bogomem_size > -1 && strstr(filebuf, "bogomem_size=") && filebuf[0] == 'b')
                  continue;
               if (override_opt.fastmem_size > -1 && strstr(filebuf, "fastmem_size=") && filebuf[0] == 'f')
                  continue;
               if (override_opt.z3mem_size > -1 && strstr(filebuf, "z3mem_size=") && filebuf[0] == 'z')
                  continue;

               /* Append */
               retro_config_append(filebuf);
            }
            
            fclose(configfile_custom);
         }

         /* Init only existing disks */
         if (dc->count)
         {
            /* Init first disk */
            dc->index = 0;
            dc->eject_state = false;
            display_current_image(dc->labels[dc->index], true);
            log_cb(RETRO_LOG_INFO, "Disk (%d) inserted in drive DF0: \"%s\".\n", dc->index + 1, dc->files[dc->index]);
         }

         /* Verify and write Kickstart */
         retro_config_kickstart();
      }
      /* Unknown extensions */
      else
      {
         /* Write model preset */
         retro_config_append(uae_model_config);

         /* Verify and write Kickstart */
         retro_config_kickstart();

         /* Write common config */
         retro_config_append(uae_config);

         /* Unsupported file format */
         log_cb(RETRO_LOG_ERROR, "Unsupported file format: \"%s\".\n", full_path);
         snprintf(retro_message_msg, sizeof(retro_message_msg), "Unsupported file format: \"%s\"", path_get_extension(full_path));
         retro_message = true;
      }
   }
   /* Empty content */
   else
   {
      /* Write model preset */
      retro_config_append(uae_model_config);

      /* Verify and write Kickstart */
      retro_config_kickstart();

      /* Bootable HD exception, not for CD systems */
      if (opt_use_boot_hd)
         if (strcmp(opt_model, "CD32") && strcmp(opt_model, "CD32FR") && strcmp(opt_model, "CDTV") && strcmp(opt_model, "CDTVCR"))
            retro_config_boot_hd();

      /* Write common config */
      retro_config_append(uae_config);
   }

   /* Iterate global config file and append all rows to the temporary config */
   char configfile_path[RETRO_PATH_MAX];
   path_join(configfile_path, retro_save_directory, LIBRETRO_PUAE_PREFIX "_global.uae");
   if (path_is_valid(configfile_path))
   {
      log_cb(RETRO_LOG_INFO, "Appending global configuration: \"%s\".\n", configfile_path);

      FILE *configfile;
      char filebuf[RETRO_PATH_MAX];
      if ((configfile = fopen(configfile_path, "r")))
      {
         while (fgets(filebuf, sizeof(filebuf), configfile))
            retro_config_append(filebuf);
         fclose(configfile);

         /* Verify and write Kickstart */
         retro_config_kickstart();
      }
   }

   /* Append content-specific config */
   char content_path[RETRO_PATH_MAX];
   tmp_str = utf8_to_local_string_alloc(info_path);
   path_basedir(tmp_str);
   strlcpy(content_path, tmp_str, sizeof(content_path));
   free(tmp_str);

   tmp_str = utf8_to_local_string_alloc(full_path);
   path_remove_extension(tmp_str);

   /* First search content path, then 'saves' (path_basedir leaves ending slash) */
   snprintf(configfile_path, sizeof(configfile_path), "%s%s%s",
         content_path, path_basename(tmp_str), ".uae");

   if (!path_is_valid(configfile_path))
   {
      tmp_str = utf8_to_local_string_alloc(full_path);
      path_remove_extension(tmp_str);

      snprintf(configfile_path, sizeof(configfile_path), "%s%s%s%s",
            retro_save_directory, DIR_SEP_STR, path_basename(tmp_str), ".uae");
   }

   if (path_is_valid(configfile_path))
   {
      log_cb(RETRO_LOG_INFO, "Appending content configuration: \"%s\".\n", configfile_path);

      FILE *configfile;
      char filebuf[RETRO_PATH_MAX];
      if ((configfile = fopen(configfile_path, "r")))
      {
         while (fgets(filebuf, sizeof(filebuf), configfile))
            retro_config_append(filebuf);
         fclose(configfile);

         /* Verify and write Kickstart */
         retro_config_kickstart();
      }
   }

   /* Scan region tags only with automatic region */
   if (opt_region_auto)
      retro_config_force_region();

   /* Forced Cycle-exact */
   if (strstr(full_path, "(CE)"))
   {
      retro_config_append("cycle_exact=true\n");
      cpu_cycle_exact_force = true;
   }

   if (cpu_cycle_exact_force)
      log_cb(RETRO_LOG_INFO, "Forcing Cycle-exact.\n");

   /* Scan for specific rows and print the final config in debug log for copypaste purposes */
   log_cb(RETRO_LOG_DEBUG, "Generated config:\n");
   log_cb(RETRO_LOG_DEBUG, "-----------------\n");

   char *token;
   char uae_full_config_temp[UAE_CONFIG_SIZE];
   strlcpy(uae_full_config_temp, uae_full_config, sizeof(uae_full_config_temp));
   for (token = strtok(uae_full_config_temp, "\n"); token; token = strtok(NULL, "\n"))
   {
      log_cb(RETRO_LOG_DEBUG, "%s\n", token);

      if (strstr(token, "ntsc=true") && token[0] == 'n')
         real_ntsc = true;
      if (strstr(token, "ntsc=false") && token[0] == 'n')
         real_ntsc = false;
   }

   if (real_ntsc && video_config & PUAE_VIDEO_PAL)
      forced_video = RETRO_REGION_NTSC;

   if (!real_ntsc && video_config & PUAE_VIDEO_NTSC)
      forced_video = RETRO_REGION_PAL;

   free(tmp_str);
   tmp_str = NULL;

   return true;
}

void retro_reset(void)
{
   request_reset_hard = true;
}

static void retro_reset_hard(void)
{
   request_reset_hard = false;

   /* Ensure WHDLoad saves are written with write cache enabled */
   whdload_quitkey();

   video_config_old = (forced_video < 0) ? 0 : video_config_old;
   fake_ntsc = false;
   locked_video_horizontal = false;

   /* Restart the automatic centering/crop detection as on a fresh start:
    * the history of the previous machine shifted the picture of the new
    * one (A500 -> CDTV started 25 lines too high) */
   retro_thisframe_counter                = 0;
   retro_thisframe_first_drawn_line_old   = -1;
   retro_thisframe_first_drawn_line_start = -1;
   retro_thisframe_last_drawn_line_old    = -1;
   retro_thisframe_last_drawn_line_start  = -1;
   retro_thisframe_y_adjust_old           = -1;
   retro_min_diwstart_old                 = -1;
   retro_max_diwstop_old                  = -1;
   retro_diwstartstop_counter             = 0;
   retro_visible_left_border_old          = 0;
   retro_thisframe_first_drawn_line       = -1;
   retro_thisframe_last_drawn_line        = -1;
   retro_min_diwstart                     = 30000; /* RETRO_TOTAL_NONE */
   retro_thisframe_y_adjust               = 0;

   /* Preset override circle:
    * - config preset models require core options
    * - core option overrides require config presets */
   update_variables();
   retro_create_config();
   update_variables();

   /* Restart the emulation from scratch with the new configuration, as
    * loading the core does (uae_restart -> libretro_do_restart ->
    * real_main2). A hard reset applies a changed configuration only
    * piece by piece: memory_reset() copied the ROM and RAM sizes back from
    * changed_prefs (A500 -> A1200 ran with Kickstart 1.3), and chipset
    * changes arrived only after custom_reset() had run with the old
    * chipset (A1200 -> A500 started as NTSC). */
   uae_restart(&currprefs, 0, NULL);
}

static void retro_reset_soft(void)
{
   request_reset_soft = false;
   fake_ntsc = false;
   uae_reset(0, 0); /* hardreset, keyboardreset */
}

/* Vertical centering */
static void update_video_center_vertical(void)
{
   int retroh_crop_normal     = (video_config & PUAE_VIDEO_DOUBLELINE) && !retro_doublescan ? retroh_crop / 2 : retroh_crop;
   int retro_thisframe_y_adjust_new = retro_thisframe_y_adjust_old;
   int retro_thisframe_y_adjust_cur = retro_thisframe_y_adjust;
   int first_drawn_limit      = 150;
   int last_drawn_limit       = 150;

   /* Reset default top border except with doublescan use first drawn line */
   retro_thisframe_y_adjust = (video_productivity) ? retro_thisframe_first_drawn_line : retro_minfirstline;

   /* Need proper values for calculations */
   if (!opt_vertical_offset_auto)
      retro_thisframe_y_adjust_new = retro_thisframe_y_adjust + opt_vertical_offset;
   else if ( retro_thisframe_first_drawn_line       != retro_thisframe_last_drawn_line
         && (retro_thisframe_first_drawn_line > 0   && retro_thisframe_last_drawn_line > 0)
         && (retro_thisframe_first_drawn_line < first_drawn_limit || retro_thisframe_last_drawn_line > last_drawn_limit)
      )
      retro_thisframe_y_adjust_new = (retro_thisframe_last_drawn_line - retro_thisframe_first_drawn_line - retroh_crop_normal) / 2 + retro_thisframe_first_drawn_line;
   else if (retro_thisframe_first_drawn_line == -1 && retro_thisframe_last_drawn_line == -1 && retro_thisframe_y_adjust_old != 0)
      retro_thisframe_y_adjust_new = retro_thisframe_y_adjust_old;

   /* Sensible limits */
   retro_thisframe_y_adjust_new = (retro_thisframe_y_adjust_new < 0) ? 0 : retro_thisframe_y_adjust_new;
   retro_thisframe_y_adjust_new = (retro_thisframe_y_adjust_new > (retro_minfirstline + 70)) ? (retro_minfirstline + 70) : retro_thisframe_y_adjust_new;

   /* Remember the previous value */
   retro_thisframe_y_adjust_old = retro_thisframe_y_adjust_new;

   /* Corrections if top border has stuff in it.
    * Allow centering of all cropped sizes and also
    * prevent centering in Agony intro.. */
   if (     opt_vertical_offset_auto
         && retro_thisframe_y_adjust_new < retro_thisframe_y_adjust && retro_thisframe_y_adjust_new > 0)
   {
      int diff = retro_thisframe_y_adjust - retro_thisframe_y_adjust_new;
      if (     retro_thisframe_last_drawn_line - retro_thisframe_first_drawn_line < first_drawn_limit
            || !opt_vertical_offset)
         diff = 0;

      retro_thisframe_y_adjust     -= diff;
      retro_thisframe_y_adjust_new -= diff;
   }
   else if (opt_vertical_offset_auto
         && retro_thisframe_y_adjust_new > retro_thisframe_y_adjust
         && retro_thisframe_first_drawn_line > retroh_crop_normal / 2
         && retroh_crop_normal > EMULATOR_MIN_HEIGHT)
      retro_thisframe_y_adjust_new = retro_thisframe_last_drawn_line - retroh_crop_normal + retro_thisframe_y_adjust;

   /* Disallow centering if trying to crop NTSC aspect in full PAL mode */
   if (retroh == retroh_crop && !video_productivity)
      retro_thisframe_y_adjust = retro_thisframe_y_adjust_new = retro_minfirstline;

   /* Manual offset override */
   if (!opt_vertical_offset_auto)
   {
      retro_thisframe_y_adjust += opt_vertical_offset;
      if (retro_thisframe_y_adjust < retro_minfirstline)
         retro_thisframe_y_adjust_new += retro_thisframe_y_adjust - retro_minfirstline;
   }

   /* Offset adjustments */
   retro_thisframe_y_adjust_new -= retro_thisframe_y_adjust;
   retro_thisframe_y_adjust_new = (retro_thisframe_y_adjust_new < 0) ? 0 : retro_thisframe_y_adjust_new;

   /* Change value always due to the possible change of interlace */
   retroy_crop = retro_thisframe_y_adjust_new * ((video_config & PUAE_VIDEO_DOUBLELINE) && !retro_doublescan ? 2 : 1);

#if 0
   printf("FIRSTDRAWN:%6d LASTDRAWN:%6d   yadjust:%3d old:%3d cur:%3d croph:%d cropy:%d\n", retro_thisframe_first_drawn_line, retro_thisframe_last_drawn_line, retro_thisframe_y_adjust, retro_thisframe_y_adjust_old, retro_thisframe_y_adjust_cur, retroh_crop, retroy_crop);
#endif

   /* The top border is applied when the window is cut out of the raster
    * (retro_video_blit), the emulation does not need to know about it */
   (void)retro_thisframe_y_adjust_cur;

   /* Counter reset */
   retro_thisframe_counter = 0;
}

/* Horizontal centering */
static void update_video_center_horizontal(void)
{
   int default_left_border     = (retro_max_diwlastword - retrow);
   int retro_visible_left_border_new = retro_max_diwlastword - retrow + ((retrow - retrow_crop) / 2);
   int retro_visible_left_border_cur = retro_visible_left_border;

   /* Horizontal centering thresholds */
   int min_diwstart_limit = 162 * width_multiplier;
   int max_diwstop_limit  = 300 * width_multiplier;

   /* Always reset default left border */
   retro_visible_left_border = default_left_border;

   /* Need proper values for calculations */
   if (locked_video_horizontal)
      ; /* no-op */
   else if (!opt_horizontal_offset_auto)
      retro_visible_left_border_new = retro_visible_left_border + (opt_horizontal_offset * width_multiplier);
   else if (retro_min_diwstart != retro_max_diwstop
         && retro_min_diwstart > 0
         && retro_max_diwstop  > 0
         && retro_min_diwstart < min_diwstart_limit
         && retro_max_diwstop  > max_diwstop_limit
         && (retro_max_diwstop - retro_min_diwstart) <= (retrow + (4 * width_multiplier)))
      retro_visible_left_border_new = (retro_max_diwstop - retro_min_diwstart - retrow_crop) / 2 + retro_min_diwstart;
   else if (retro_min_diwstart == MAX_STOP && retro_max_diwstop == 0 && retro_visible_left_border != 0)
      retro_visible_left_border_new = retro_visible_left_border;

   /* CD32 boot special */
   if (opt_horizontal_offset_auto && retro_max_diwstop < retro_min_diwstart)
      retro_visible_left_border_new = default_left_border;

   /* Sensible limits */
   retro_visible_left_border_new = (retro_visible_left_border_new < 0) ? 0 : retro_visible_left_border_new;
   retro_visible_left_border_new = (retro_visible_left_border_new / width_multiplier > 150) ? (150 * width_multiplier) : retro_visible_left_border_new;

   /* Remember the previous value */
   retro_visible_left_border_old = retro_visible_left_border;

   /* Essential correction if default left border has stuff left of it */
   if (retro_visible_left_border_new < retro_visible_left_border)
   {
      int diff = retro_visible_left_border - retro_visible_left_border_new;
      retro_visible_left_border     -= diff;
      retro_visible_left_border_new -= diff;
   }

   /* Special Super72 modes */
   if (video_productivity && (retrow == PUAE_VIDEO_WIDTH_S72 || retrow == PUAE_VIDEO_WIDTH_S72 * 2))
      retro_visible_left_border = retro_min_diwstart;

   /* Special DBL modes */
   if (retro_doublescan)
      retro_visible_left_border = retro_min_diwstart;

   /* Manual offset override */
   if (!opt_horizontal_offset_auto)
      retro_visible_left_border = default_left_border + (opt_horizontal_offset * width_multiplier);

   /* Offset adjustments */
   retro_visible_left_border_new -= retro_visible_left_border;
   retro_visible_left_border_new = (retro_visible_left_border_new < 0) ? 0 : retro_visible_left_border_new;

   /* Change value only if altered */
   if (retrox_crop != retro_visible_left_border_new)
      retrox_crop = retro_visible_left_border_new;

#if 0
   printf("DIWSTART  :%6d DIWSTOP  :%6d   lborder:%3d old:%3d cur:%3d cropw:%3d cropx:%d\n", retro_min_diwstart, retro_max_diwstop, retro_visible_left_border, retro_visible_left_border_old, retro_visible_left_border_cur, (retro_max_diwstop - retro_min_diwstart), retrox_crop);
#endif

   /* The left border is applied when the window is cut out of the raster
    * (retro_video_blit), the emulation does not need to know about it */
   (void)retro_visible_left_border_cur;

   /* Counter reset */
   retro_diwstartstop_counter = 0;
}

static bool update_vresolution(bool update)
{
   /* Full motion video is shown in double line mode */
   int tmp_interlace_seen = retro_av_info_is_lace || retro_doublescan
#ifdef WITH_MPEG2
       || cd32_fmv_active
#endif
       ;

   /* Lores force to single line */
   if (!(video_config & PUAE_VIDEO_HIRES) && !(video_config & PUAE_VIDEO_SUPERHIRES))
      tmp_interlace_seen = 0;

   switch (tmp_interlace_seen)
   {
      case 1:
         if (!(video_config & PUAE_VIDEO_DOUBLELINE))
         {
            if (!update)
               return true;

            video_config |= PUAE_VIDEO_DOUBLELINE;
            changed_prefs.gfx_vresolution = VRES_DOUBLE;
            defaulth = retroh = (video_config & PUAE_VIDEO_NTSC) ? PUAE_VIDEO_HEIGHT_NTSC : PUAE_VIDEO_HEIGHT_PAL;
            return true;
         }
         break;
      case 0:
         if ((video_config & PUAE_VIDEO_DOUBLELINE))
         {
            if (!update)
               return true;

            video_config &= ~PUAE_VIDEO_DOUBLELINE;
            changed_prefs.gfx_vresolution = VRES_NONDOUBLE;
            defaulth = retroh = ((video_config & PUAE_VIDEO_NTSC) ? PUAE_VIDEO_HEIGHT_NTSC : PUAE_VIDEO_HEIGHT_PAL) / 2;
            return true;
         }
         break;
   }
   return false;
}

static void update_audiovideo(void)
{
   /* Statusbar message timer */
   if (statusbar_message_timer > 0)
      statusbar_message_timer--;

   /* Update audio settings */
   if (automatic_sound_filter_type_update_timer > 0)
   {
      automatic_sound_filter_type_update_timer--;
      if (automatic_sound_filter_type_update_timer == 0)
      {
         set_config_changed();

         if (currprefs.chipset_mask & CSMASK_AGA)
            changed_prefs.sound_filter_type = FILTER_SOUND_TYPE_A1200;
         else
            changed_prefs.sound_filter_type = FILTER_SOUND_TYPE_A500;
      }
   }

   /* Automatic video resolution */
   if (opt_video_resolution_auto && retro_min_diwstart != MAX_STOP)
   {
      int current_resolution   = detected_screen_resolution;
      bool request_init_custom = false;
      static uint8_t hsytrue_count = 0;

#if 0
      printf("BPLCON0: %x, %d, %d-%d\n", bplcon0, current_resolution, retro_min_diwstart, retro_max_diwstop);
#endif
      if (opt_video_resolution_auto == RESOLUTION_AUTO_SUPERHIRES)
         opt_video_resolution_auto = RESOLUTION_AUTO_HIRES;

      if (current_resolution == RES_SUPERHIRES && retro_doublescan)
         current_resolution = RES_HIRES;

      /* Super Skidmarks force SuperHires */
      if (     current_resolution == RES_HIRES
            && bplcon0 == 0xC201
            && (retro_min_diwstart == 322 || retro_min_diwstart == 322 * 2)
            && (retro_max_diwstop  == 898 || retro_max_diwstop  == 898 * 2)
         )
         current_resolution = RES_SUPERHIRES;
      /* Super Stardust prevent Lores */
      else if (opt_video_resolution_auto == RESOLUTION_AUTO_LORES
            && current_resolution == RES_LORES
            && (retro_min_diwstart == 121 || retro_min_diwstart == 121 * 2 || retro_min_diwstart == 128 || retro_min_diwstart == 128 * 2 || retro_min_diwstart == 129 || retro_min_diwstart == 129 * 2)
            && (retro_max_diwstop  == 448 || retro_max_diwstop  == 448 * 2 || retro_max_diwstop  == 449 || retro_max_diwstop  == 449 * 2 || retro_max_diwstop  == 457 || retro_max_diwstop  == 457 * 2)
         )
      {
         if (hsytrue_count)
            hsytrue_count--;
         if (bplcon0 & BEAMCON0_HSYTRUE)
         {
            current_resolution = RES_HIRES;
            hsytrue_count = 2;
         }
         else if (!(bplcon0 & BEAMCON0_HSYTRUE) && hsytrue_count)
            current_resolution = -1;
      }
      /* Lores force Hires in 'auto' */
      else if (opt_video_resolution_auto == RESOLUTION_AUTO_HIRES && current_resolution == 0)
         current_resolution = RES_HIRES;

      /* Ignore Lores spikes in Lemmings */
      if (     opt_video_resolution_auto == RESOLUTION_AUTO_LORES
            && changed_prefs.gfx_resolution == RES_HIRES
            && current_resolution == RES_LORES
            && bplcon0 == (BEAMCON0_HARDDIS | BEAMCON0_VARVSYEN)
         )
         current_resolution = -1;

      /* Ignore always */
      if (bplcon0 & BEAMCON0_CSCBEN)
         current_resolution = -1;

      switch (current_resolution)
      {
         case RES_HIRES:
            if (!(video_config & PUAE_VIDEO_HIRES))
            {
               changed_prefs.gfx_resolution = RES_HIRES;
               video_config |= PUAE_VIDEO_HIRES;
               video_config &= ~PUAE_VIDEO_SUPERHIRES;
               defaultw = retrow = PUAE_VIDEO_WIDTH;
               retro_max_diwlastword = retro_max_diwlastword_hires;
               request_init_custom = true;
            }
            break;
         case RES_SUPERHIRES:
            if (!(video_config & PUAE_VIDEO_SUPERHIRES))
            {
               changed_prefs.gfx_resolution = RES_SUPERHIRES;
               video_config |= PUAE_VIDEO_SUPERHIRES;
               video_config &= ~PUAE_VIDEO_HIRES;
               defaultw = retrow = PUAE_VIDEO_WIDTH * 2;
               retro_max_diwlastword = retro_max_diwlastword_hires * 2;
               request_init_custom = true;
            }
            break;
         case RES_LORES:
            if ((video_config & PUAE_VIDEO_HIRES) || (video_config & PUAE_VIDEO_SUPERHIRES))
            {
               changed_prefs.gfx_resolution = RES_LORES;
               video_config &= ~PUAE_VIDEO_HIRES;
               video_config &= ~PUAE_VIDEO_SUPERHIRES;
               defaultw = retrow = PUAE_VIDEO_WIDTH / 2;
               retro_max_diwlastword = retro_max_diwlastword_hires / 2;
               request_init_custom = true;
            }
            break;
         default:
            break;
      }

      /* Horizontal centering calculation needs to be forced due to
       * retro_max_diwlastword change which is crucial for retro_visible_left_border */
      if (request_init_custom)
      {
         request_init_custom_timer = 2;
         retro_min_diwstart_old    = -1;
         retro_max_diwstop_old     = -1;
         set_config_changed();
      }
   }

   /* Automatic video vresolution (Line Mode) */
   if (opt_video_vresolution_auto && !request_init_custom_timer)
   {
      /* This is for the core option to be able refresh realtime,
       * do not update anything yet, but do it later in update_av_info  */
      if (update_vresolution(false))
         request_update_av_info = true;
   }

   /* Update av_info */
   if (request_init_custom_timer > 0)
      request_update_av_info = true;

   /* Reuse old values if more valid than current,
    * since otherwise interlace toggle interferes with crop */
   if (opt_vertical_offset_auto && crop_id != 0)
   {
      if (retro_thisframe_first_drawn_line == -1 && retro_thisframe_first_drawn_line_old != -1)
         retro_thisframe_first_drawn_line = retro_thisframe_first_drawn_line_old;
      if (retro_thisframe_last_drawn_line == -1 && retro_thisframe_last_drawn_line_old != -1)
         retro_thisframe_last_drawn_line = retro_thisframe_last_drawn_line_old;

      if (retro_thisframe_last_drawn_line < retro_thisframe_first_drawn_line && retro_thisframe_last_drawn_line_old != -1)
         retro_thisframe_last_drawn_line = retro_thisframe_last_drawn_line_old;
   }

   /* Automatic vertical offset */
   if (opt_vertical_offset_auto && crop_id != 0 && (
         (retro_thisframe_first_drawn_line != retro_thisframe_last_drawn_line) ||
         (retro_thisframe_first_drawn_line == -1 && retro_thisframe_last_drawn_line == -1))
      )
   {
      int min_height = 4;
      int retro_thisframe_height = MAX(retro_thisframe_last_drawn_line - retro_thisframe_first_drawn_line + 1, 200);
      int retro_thisframe_first_drawn_line_delta = abs(retro_thisframe_first_drawn_line_old - retro_thisframe_first_drawn_line);
      int retro_thisframe_last_drawn_line_delta  = abs(retro_thisframe_last_drawn_line_old - retro_thisframe_last_drawn_line);

#if 0
      printf("thisframe first:%3d old:%3d start:%3d delta:%3d last:%3d old:%3d start:%3d delta:%3d - h:%3d\n",
            retro_thisframe_first_drawn_line, retro_thisframe_first_drawn_line_old, retro_thisframe_first_drawn_line_start, retro_thisframe_first_drawn_line_delta,
            retro_thisframe_last_drawn_line, retro_thisframe_last_drawn_line_old, retro_thisframe_last_drawn_line_start, retro_thisframe_last_drawn_line_delta,
            retro_thisframe_height);
#endif

      /* For some odd reason KS2+ Kickstarts start with bogus last_drawn_line,
       * therefore reset to safe values and skip the rest for the frame
       * to prevent stuck vertical position */
      if (retro_thisframe_first_drawn_line == retro_thisframe_first_drawn_line_old
       && retro_thisframe_first_drawn_line == retro_thisframe_last_drawn_line_old
       && retro_thisframe_first_drawn_line == -1
       && retro_thisframe_last_drawn_line < 50)
      {
         retro_thisframe_first_drawn_line = (changed_prefs.ntscmode) ? NTSC_KS2_CROP_SAFE_FIRST_LINE : PAL_KS2_CROP_SAFE_FIRST_LINE;
         retro_thisframe_last_drawn_line  = (changed_prefs.ntscmode) ? NTSC_KS2_CROP_SAFE_LAST_LINE : PAL_KS2_CROP_SAFE_LAST_LINE;

         retro_thisframe_first_drawn_line_start = retro_thisframe_first_drawn_line_old = retro_thisframe_first_drawn_line;
         retro_thisframe_last_drawn_line_start  = retro_thisframe_last_drawn_line_old  = retro_thisframe_last_drawn_line;
      }

      if (( retro_thisframe_first_drawn_line != retro_thisframe_first_drawn_line_old
         || retro_thisframe_last_drawn_line  != retro_thisframe_last_drawn_line_old)
         && retro_thisframe_first_drawn_line != -1
         && retro_thisframe_last_drawn_line  != -1
         && retro_thisframe_last_drawn_line - retro_thisframe_first_drawn_line > min_height
         && (retro_thisframe_first_drawn_line_delta > ((video_config & PUAE_VIDEO_DOUBLELINE) && retro_av_info_is_lace) ? 1 : 0
          || retro_thisframe_last_drawn_line_delta  > ((video_config & PUAE_VIDEO_DOUBLELINE) && retro_av_info_is_lace) ? 1 : 0)
      )
      {
#if 0
         printf("frmstr %d, first:%3d old:%3d start:%3d delta:%3d last:%3d old:%3d start:%3d delta:%3d\n", retro_thisframe_counter,
               retro_thisframe_first_drawn_line, retro_thisframe_first_drawn_line_old, retro_thisframe_first_drawn_line_start, retro_thisframe_first_drawn_line_delta,
               retro_thisframe_last_drawn_line, retro_thisframe_last_drawn_line_old, retro_thisframe_last_drawn_line_start, retro_thisframe_last_drawn_line_delta);
#endif
         if (retro_thisframe_first_drawn_line_start == -1 && retro_thisframe_last_drawn_line_start == -1)
            request_update_av_info = true;

         if ((retro_thisframe_first_drawn_line_start == retro_thisframe_first_drawn_line
           && retro_thisframe_last_drawn_line_start  == retro_thisframe_last_drawn_line)
          || (retro_thisframe_first_drawn_line_old == retro_thisframe_first_drawn_line
           && retro_thisframe_last_drawn_line_old  == retro_thisframe_last_drawn_line))
            retro_thisframe_counter = 0;

         if (!retro_thisframe_counter)
         {
            if (retro_thisframe_first_drawn_line_delta > ((video_config & PUAE_VIDEO_DOUBLELINE) && retro_av_info_is_lace) ? 1 : 0)
               retro_thisframe_first_drawn_line_start = (retro_thisframe_first_drawn_line_old > 0) ? retro_thisframe_first_drawn_line_old : retro_thisframe_first_drawn_line;
            if (retro_thisframe_last_drawn_line_delta > ((video_config & PUAE_VIDEO_DOUBLELINE) && retro_av_info_is_lace) ? 1 : 0)
               retro_thisframe_last_drawn_line_start  = (retro_thisframe_last_drawn_line_old > 0) ? retro_thisframe_last_drawn_line_old : retro_thisframe_last_drawn_line;
         }

         if (retro_thisframe_first_drawn_line_start != retro_thisframe_first_drawn_line
          || retro_thisframe_last_drawn_line_start  != retro_thisframe_last_drawn_line)
            retro_thisframe_counter = 1;

         /* Immediate mode, but not when interlaced */
         if (!crop_delay && !retro_av_info_is_lace)
            request_update_av_info = true;

         /* Immediate centering if big enough jump */
         if ((retro_thisframe_first_drawn_line_delta > 15)
          || (retro_thisframe_last_drawn_line_delta  > 15 && retro_thisframe_last_drawn_line_delta < 115))
            request_update_av_info = true;

         /* Immediate centering if height remains the same or grows big enough */
         if (retro_thisframe_height == retroh_crop || (!retro_thisframe_counter && retro_thisframe_height - retroh_crop > 15))
            request_update_av_info = true;

         /* Hasten the result with big enough difference in last line (last line for CD32 no disc) */
         if (retro_thisframe_last_drawn_line_delta > 47 && retro_thisframe_last_drawn_line_delta < 189)
            retro_thisframe_counter++;

         /* Do not consider too big first line deltas as necessary changes.
          * Fixes cases like Fantastic Dizzy and Lollypop screen transition */
         if (retro_thisframe_first_drawn_line_delta > 165 && retro_thisframe_last_drawn_line_delta < 2
          && retro_thisframe_first_drawn_line > retro_thisframe_first_drawn_line_old)
         {
            retro_thisframe_counter = 0;
            request_update_av_info  = false;
         }

         /* Clear counter when immediate */
         if (request_update_av_info)
            retro_thisframe_counter = 0;

         retro_thisframe_first_drawn_line_old = retro_thisframe_first_drawn_line;
         retro_thisframe_last_drawn_line_old  = retro_thisframe_last_drawn_line;
      }
      else if (retro_thisframe_counter > 0
            && retro_thisframe_first_drawn_line != -1
            && retro_thisframe_last_drawn_line  != -1
            && retro_thisframe_first_drawn_line == retro_thisframe_first_drawn_line_old
            && retro_thisframe_last_drawn_line  == retro_thisframe_last_drawn_line_old)
      {
         int counter_max = 4;
         if (abs(retro_thisframe_first_drawn_line_start - retro_thisframe_first_drawn_line) < 2
          || abs(retro_thisframe_last_drawn_line_start - retro_thisframe_last_drawn_line) < 2)
            counter_max *= 2;

#if 0
         printf("frmcnt %d, first:%3d old:%3d start:%3d delta:%3d last:%3d old:%3d start:%3d delta:%3d\n", retro_thisframe_counter,
               retro_thisframe_first_drawn_line, retro_thisframe_first_drawn_line_old, retro_thisframe_first_drawn_line_start, retro_thisframe_first_drawn_line_delta,
               retro_thisframe_last_drawn_line, retro_thisframe_last_drawn_line_old, retro_thisframe_last_drawn_line_start, retro_thisframe_last_drawn_line_delta);
#endif
         if (retro_thisframe_counter > 0)
            retro_thisframe_counter++;

         /* Prevent geometry change but allow vertical centering if the values return to the starting point during counting */
         if (retro_thisframe_first_drawn_line == retro_thisframe_first_drawn_line_start
          && retro_thisframe_last_drawn_line  == retro_thisframe_last_drawn_line_start)
            retro_av_info_change_geometry = false;

         /* Reset counter if the first drawn line changes while the last line stays the same.
          * To prevent Lollypop earthquake effect trigger, and allow Superfrog statusbar startup animation */
         if (retro_thisframe_first_drawn_line != retro_thisframe_first_drawn_line_start
          && abs(retro_thisframe_first_drawn_line_start - retro_thisframe_first_drawn_line) > 1
          && abs(retro_thisframe_first_drawn_line_start - retro_thisframe_first_drawn_line) < 5
          && abs(retro_thisframe_last_drawn_line_start - retro_thisframe_last_drawn_line) < 2)
            retro_thisframe_counter = 1;

         /* Super Obliteration slide animation jump preventation */
         if (retro_thisframe_last_drawn_line > 280
          && retro_thisframe_last_drawn_line - retro_thisframe_first_drawn_line < (min_height * 2))
            retro_thisframe_counter = 1;

         if (retro_thisframe_counter > counter_max)
            request_update_av_info = true;
      }

      if (request_update_av_info)
      {
         retro_thisframe_first_drawn_line_start = retro_thisframe_first_drawn_line;
         retro_thisframe_last_drawn_line_start  = retro_thisframe_last_drawn_line;
      }
   }
   else if (opt_vertical_offset_auto && crop_id != 0 && retro_thisframe_first_drawn_line == retro_thisframe_last_drawn_line
         && retro_thisframe_first_drawn_line != -1)
   {
      /* Another odd case of bogus initial drawn_lines with A1200 without Fast RAM */
      retro_thisframe_first_drawn_line = (changed_prefs.ntscmode) ? NTSC_KS2_CROP_SAFE_FIRST_LINE : PAL_KS2_CROP_SAFE_FIRST_LINE;
      retro_thisframe_last_drawn_line  = (changed_prefs.ntscmode) ? NTSC_KS2_CROP_SAFE_LAST_LINE : PAL_KS2_CROP_SAFE_LAST_LINE;
   }

   /* Automatic horizontal offset */
   if (opt_horizontal_offset_auto)
   {
      if (retro_min_diwstart == MAX_STOP
       && retro_max_diwstop  == 0
       && retro_min_diwstart_old != -1
       && retro_max_diwstop_old  != -1)
      {
         retro_min_diwstart = retro_min_diwstart_old;
         retro_max_diwstop  = retro_max_diwstop_old;
      }

      if ( (retro_min_diwstart != retro_min_diwstart_old
         || retro_max_diwstop  != retro_max_diwstop_old)
         && retro_min_diwstart != MAX_STOP
         && retro_max_diwstop  != 0
         && !locked_video_horizontal)
      {
#if 0
         printf("diwcnt %d, start:%3d old:%3d stop:%3d old:%3d width:%3d, tfdl:%3d tldl:%3d\n",
               retro_diwstartstop_counter, retro_min_diwstart, retro_min_diwstart_old,
               retro_max_diwstop, retro_max_diwstop_old, retro_max_diwstop - retro_min_diwstart,
               retro_thisframe_first_drawn_line, retro_thisframe_last_drawn_line);
#endif
         retro_diwstartstop_counter = 0;

#if 1
         /* Game specific hacks */

         /* North & South really is an exhausting mess due to the horizontal shift, and as a bonus every single
          * version behaves differently, and WHDLoad version even differs depending on Cycle-exact and Fast-Forward.. */
         /* North & South PAL floppy */
         if (     retro_max_diwstop - retro_min_diwstart == (329 * width_multiplier)
               && retro_thisframe_first_drawn_line == 44 && retro_thisframe_last_drawn_line == 299
               && retro_min_diwstart == (129 * width_multiplier) && retro_min_diwstart_old == retro_min_diwstart
               && retro_max_diwstop  == (458 * width_multiplier) && retro_max_diwstop_old  == (449 * width_multiplier))
         {
            locked_video_horizontal = true;
            log_cb(RETRO_LOG_INFO, "Horizontal centering hack for \"%s\" active.\n", "North & South PAL floppy");
         }
         /* North & South PAL WHDLoad */
         else if (retro_max_diwstop - retro_min_diwstart == (329 * width_multiplier)
               && (retro_thisframe_last_drawn_line == 243 || retro_thisframe_last_drawn_line >= 298)
               && retro_min_diwstart == (129 * width_multiplier) && retro_min_diwstart_old == retro_min_diwstart
               && retro_max_diwstop  == (458 * width_multiplier) && retro_max_diwstop_old  == (449 * width_multiplier))
         {
            locked_video_horizontal = true;
            log_cb(RETRO_LOG_INFO, "Horizontal centering hack for \"%s\" active.\n", "North & South PAL WHDLoad");
         }
         /* North & South NTSC floppy */
         else if (retro_max_diwstop - retro_min_diwstart == (329 * width_multiplier)
               && (retro_thisframe_first_drawn_line ==  44 || retro_thisframe_first_drawn_line ==  43)
               && (retro_thisframe_last_drawn_line  == 243 || retro_thisframe_last_drawn_line  >= 261)
               && retro_min_diwstart == (129 * width_multiplier) && retro_min_diwstart_old == retro_min_diwstart
               && retro_max_diwstop  == (458 * width_multiplier) && retro_max_diwstop_old  == (449 * width_multiplier))
         {
            locked_video_horizontal = true;
            log_cb(RETRO_LOG_INFO, "Horizontal centering hack for \"%s\" active.\n", "North & South NTSC floppy");
         }
         /* North & South NTSC WHDLoad */
         else if (retro_max_diwstop - retro_min_diwstart == (329 * width_multiplier)
               && (retro_thisframe_last_drawn_line == 243 || retro_thisframe_last_drawn_line >= 261)
               && retro_min_diwstart == (129 * width_multiplier) && (retro_min_diwstart_old == retro_min_diwstart || retro_min_diwstart_old == (127 * width_multiplier))
               && (retro_max_diwstop == (449 * width_multiplier) || retro_max_diwstop == (458 * width_multiplier)) && (retro_max_diwstop_old == (447 * width_multiplier) || retro_max_diwstop_old == (449 * width_multiplier)))
         {
            locked_video_horizontal = true;
            log_cb(RETRO_LOG_INFO, "Horizontal centering hack for \"%s\" active.\n", "North & South NTSC WHDLoad");
         }
         /* Chase HQ WHDLoad */
         else if (retro_thisframe_first_drawn_line == 50 && retro_thisframe_last_drawn_line == 249
               && retro_min_diwstart == (137 * width_multiplier) && retro_min_diwstart_old == (129 * width_multiplier)
               && retro_max_diwstop  == (457 * width_multiplier) && retro_max_diwstop_old  == (449 * width_multiplier))
         {
            locked_video_horizontal = true;
            log_cb(RETRO_LOG_INFO, "Horizontal centering hack for \"%s\" active.\n", "Chase HQ WHDLoad");
         }
         /* Test Drive */
         else if (retro_thisframe_first_drawn_line ==  44 && retro_thisframe_last_drawn_line  == 243
               && retro_min_diwstart == (128 * width_multiplier) && retro_min_diwstart_old == (129 * width_multiplier)
               && retro_max_diwstop  == (448 * width_multiplier) && retro_max_diwstop_old  == (449 * width_multiplier))
         {
            locked_video_horizontal = true;
            log_cb(RETRO_LOG_INFO, "Horizontal centering hack for \"%s\" active.\n", "Test Drive");
         }
         /* Test Drive II */
         else if (retro_thisframe_first_drawn_line == 160 && retro_thisframe_last_drawn_line  == 243
               && retro_min_diwstart == (128 * width_multiplier) && retro_min_diwstart_old == (129 * width_multiplier)
               && retro_max_diwstop  == (448 * width_multiplier) && retro_max_diwstop_old  == (449 * width_multiplier))
         {
            locked_video_horizontal = true;
            log_cb(RETRO_LOG_INFO, "Horizontal centering hack for \"%s\" active.\n", "Test Drive II");
         }
         /* Toki */
         else if (retro_thisframe_first_drawn_line == 60 && retro_thisframe_last_drawn_line == 259
               && retro_min_diwstart == (145 * width_multiplier) && retro_min_diwstart_old == (113 * width_multiplier)
               && retro_max_diwstop  == (401 * width_multiplier) && retro_max_diwstop_old  == (465 * width_multiplier))
         {
            locked_video_horizontal = true;
            log_cb(RETRO_LOG_INFO, "Horizontal centering hack for \"%s\" active.\n", "Toki");
         }
#endif

         /* Immediate mode */
         if (!crop_delay)
            request_update_av_info = true;
         else
            retro_diwstartstop_counter = 1;

         retro_min_diwstart_old = retro_min_diwstart;
         retro_max_diwstop_old  = retro_max_diwstop;
      }
      /* Prevent centering of horizontal animations by requiring the change to stabilize */
      else if (retro_diwstartstop_counter > 0
            && retro_min_diwstart == retro_min_diwstart_old
            && retro_max_diwstop  == retro_max_diwstop_old
            && retro_min_diwstart_old > 0
            && retro_max_diwstop_old  > 0)
      {
         retro_diwstartstop_counter++;

         if (retro_diwstartstop_counter > 3)
            request_update_av_info = true;
      }
   }
}

static bool retro_update_av_info(void)
{
   bool av_log                 = false;
   bool isntsc                 = retro_av_info_is_ntsc;
   bool islace                 = retro_av_info_is_lace;
   bool change_timing          = retro_av_info_change_timing;
   bool change_timing_init     = retro_av_info_change_timing;
   bool change_geometry        = retro_av_info_change_geometry;
   float hz                    = currprefs.chipset_refreshrate;

   /* Prevent unnecessary geometry calls */
   unsigned base_width_old     = retrow_crop;
   unsigned base_height_old    = retroh_crop;
   float    aspect_ratio_old   = aspect_ratio;

   static bool fake_ntsc_maybe = false;
   if (fake_ntsc)
      hz = currprefs.cr[CHIPSET_REFRESH_NTSC].rate;

   /* Reset global parameters ready for the next update
    * Except is_lace, because the current state is required */
   request_update_av_info        = false;
   retro_av_info_is_ntsc         = false;
   retro_av_info_change_timing   = false;
   retro_av_info_change_geometry = true;

   if (av_log)
      printf("* Trying to update AV timing:%d to: ntsc:%d hz:%0.4f, from video_config:%d, video_aspect:%d, hz:%0.4f\n", change_timing, isntsc, hz, video_config, video_config_aspect, retro_refresh);

   /* Change PAL/NTSC with a twist, thanks to Dyna Blaster
    *
    * Early Startup switch looks proper:
    *    PAL mode V=49.9201Hz H=15625.0881Hz (227x312+1) IDX=10 (PAL) D=0 RTG=0/0
    *    NTSC mode V=59.8859Hz H=15590.7473Hz (227x262+1) IDX=11 (NTSC) D=0 RTG=0/0
    *    PAL mode V=49.9201Hz H=15625.0881Hz (227x312+1) IDX=10 (PAL) D=0 RTG=0/0
    *
    * Dyna Blaster switch looks unorthodox:
    *    PAL mode V=49.9201Hz H=15625.0881Hz (227x312+1) IDX=10 (PAL) D=0 RTG=0/0
    *    PAL mode V=59.4106Hz H=15625.0881Hz (227x312+1) IDX=10 (PAL) D=0 RTG=0/0
    *    PAL mode V=49.9201Hz H=15625.0881Hz (227x312+1) IDX=10 (PAL) D=0 RTG=0/0
    */

   video_config_old      = video_config;
   video_config_geometry = video_config;

   /* When timing & geometry is changed */
   if (change_timing)
   {
      /* Change to NTSC if not NTSC */
      if (isntsc && (video_config & PUAE_VIDEO_PAL) && !fake_ntsc)
      {
         video_config |= PUAE_VIDEO_NTSC;
         video_config &= ~PUAE_VIDEO_PAL;
         real_ntsc = true;
      }
      /* Change to PAL if not PAL */
      else if (!isntsc && (video_config & PUAE_VIDEO_NTSC) && !fake_ntsc)
      {
         video_config |= PUAE_VIDEO_PAL;
         video_config &= ~PUAE_VIDEO_NTSC;
         real_ntsc = false;
      }

      /* Request init_custom() on change and update temporary video config */
      if (video_config_geometry != video_config)
         request_init_custom_timer = 2;
      video_config_geometry = video_config;
   }

   /* Aspect ratio override changes only the temporary video config. Don't change at the same time with region change. */
   if (!change_timing && video_config_aspect != 0)
   {
      if (video_config_aspect == PUAE_VIDEO_NTSC)
      {
         video_config_geometry |= PUAE_VIDEO_NTSC;
         video_config_geometry &= ~PUAE_VIDEO_PAL;
      }
      else if (video_config_aspect == PUAE_VIDEO_PAL)
      {
         video_config_geometry |= PUAE_VIDEO_PAL;
         video_config_geometry &= ~PUAE_VIDEO_NTSC;
      }
   }
   /* Forced aspect needs to be postponed for the next run to keep the actual dimension intact */
   else if (change_timing && video_config_aspect != 0)
      request_update_av_info = true;

   /* Do nothing if timing has not changed, unless Hz switched without isntsc */
   if (change_timing && video_config_old == video_config)
   {
      /* Dyna Blaster and the like stays at fake NTSC to prevent pointless switching back and forth */
      if (!isntsc && hz > 56 && hz < 60)
      {
         if (!fake_ntsc_maybe)
         {
            request_update_av_info = true;
            retro_av_info_change_timing = true;
            fake_ntsc_maybe = true;
            return false;
         }
         else
         {
            video_config |= PUAE_VIDEO_NTSC;
            video_config &= ~PUAE_VIDEO_PAL;
            video_config_geometry = video_config;
            fake_ntsc = true;
         }
      }
      else
         fake_ntsc_maybe = false;

      /* If still no change */
      if (video_config_old == video_config && retro_refresh == hz)
      {
         /* Allow other calculations but don't alter timing */
         change_timing   = false;
         change_geometry = true;

         if (av_log)
            printf("  * Already at wanted AV\n");
      }
   }

   /* Geometry dimensions */
   retro_set_geometry(video_config_geometry, false);
   video_config &= ~PUAE_VIDEO_QUADLINE;

   /* Interlace detector */
   if (opt_video_vresolution_auto)
   {
      if (update_vresolution(true))
      {
         request_init_custom_timer = 2;
         set_config_changed();
         return false;
      }
   }

   if (retro_doublescan || hz > 70)
   {
      video_productivity = true;
      if (retro_doublescan)
         retrow = PUAE_VIDEO_WIDTH_PROD;
      if (hz > 70) /* EURO36 & SUPER72 */
         retrow = retro_max_diwstop - retro_min_diwstart;
      /* SUPER72 overscan */
      if (retrow == 448)
         retrow = 400;

      switch (maxvpos)
      {
         /* MULTISCAN */
         case 525: case 506: retroh = 480; break;
         case 450: case 426: retroh = 400; break;
         case 343: case 328: retroh = 300; break;
         case 216: case 217: retroh = 200; break;

         /* AGA DBL */
         case 590: case 574: retroh = 512; break;
         case 493: case 478: retroh = 400; break;
      }

      /* Forced doubleline and productivity non-doublescan.. */
      if (!opt_video_vresolution_auto && (video_config & PUAE_VIDEO_DOUBLELINE))
      {
         if ((video_config & PUAE_VIDEO_SUPERHIRES) && retrow < PUAE_VIDEO_WIDTH_PROD * 2 && retrow != PUAE_VIDEO_WIDTH_S72 && retrow != PUAE_VIDEO_WIDTH_S72 * 2)
            retrow *= 2;
         if (!retro_doublescan && retroh < PUAE_VIDEO_WIDTH_S72)
            retroh *= 2;
         else if (retro_doublescan && islace)
         {
            retroh *= 2;
            video_config |= PUAE_VIDEO_QUADLINE;
         }
      }
      else if (islace && (video_config & PUAE_VIDEO_DOUBLELINE))
      {
         retroh *= 2;
         if (retro_doublescan)
            video_config |= PUAE_VIDEO_QUADLINE;
      }

      if (retrow == PUAE_VIDEO_WIDTH_S72 && retroh == PUAE_VIDEO_HEIGHT_S72 * 2)
         video_config |= PUAE_VIDEO_QUADLINE;

      /* Always set actual dimension */
      defaultw = retrow_crop = retrow;
      defaulth = retroh_crop = retroh;

      /* Make sure init_custom() is ready */
      if (request_init_custom_timer && change_timing)
         request_init_custom_timer++;
   }
   else
      video_productivity = false;

   /* SUPER72 switch from SuperHigh-laced to High-nonlaced breaks if init_custom() is pending,
      and DBL switch breaks if returned early.. */
   if (video_productivity && !retro_doublescan && request_init_custom_timer > 1)
      return false;

   /* Width multiplier */
   if (video_config & PUAE_VIDEO_SUPERHIRES)
      width_multiplier = 4;
   else if (video_config & PUAE_VIDEO_HIRES)
      width_multiplier = 2;
   else
      width_multiplier = 1;

   /* Limit fake NTSC maximum height */
   if (fake_ntsc)
      retroh -= 4;

   /* Restore actual canvas height for aspect ratio toggling in real NTSC, otherwise toggling is broken */
   if (!change_timing && video_config_aspect == PUAE_VIDEO_PAL && (fake_ntsc || real_ntsc))
      retroh = defaulth;

   /* When the actual dimensions change and not just the view */
   if (change_timing)
   {
      defaultw = retrow;
      defaulth = retroh;
   }

   /* Disable Hz change if not allowed */
   if (!video_config_allow_hz_change)
      change_timing = false;

   /* Ensure statusbar stays visible at the bottom */
   opt_statusbar_position = opt_statusbar_position_old;

   if (!change_timing)
      if (retroh < defaulth)
         if (opt_statusbar_position >= 0 && (defaulth - retroh) >= opt_statusbar_position)
            opt_statusbar_position = defaulth - retroh;

   /* Aspect offset for crop mode */
   opt_statusbar_position_offset = opt_statusbar_position_old - opt_statusbar_position;

   /* Correction for "impossible" aspect mode */
   if (     crop_id == CROP_AUTO
         && video_config & PUAE_VIDEO_PAL
         && video_config_aspect == PUAE_VIDEO_NTSC)
      opt_statusbar_position_offset += (PUAE_VIDEO_HEIGHT_PAL - PUAE_VIDEO_HEIGHT_NTSC) / ((video_config_geometry & PUAE_VIDEO_DOUBLELINE) ? 1 : 2);

#if 0
   printf("statusbar:%3d old:%3d offset:%3d, defaulth:%d retroh:%d\n", opt_statusbar_position, opt_statusbar_position_old, opt_statusbar_position_offset, defaulth, retroh);
#endif

   /* Horizontal width hack forcing */
   if (locked_video_horizontal)
      retro_max_diwstop = retro_max_diwstop_old;

   /* Auto horizontal center corrections */
   if (opt_horizontal_offset_auto)
   {
      /* Automatic width sense fooling */
      /* Walker */
      if (retro_min_diwstart == (145 * width_multiplier) && retro_max_diwstop == (481 * width_multiplier))
      {
         retro_min_diwstart += (16 * width_multiplier);
         retro_max_diwstop  -= (32 * width_multiplier);
      }
      /* AfterBurner */
      else if (retro_min_diwstart == (97 * width_multiplier) && retro_max_diwstop == (449 * width_multiplier))
         retro_max_diwstop  += (18 * width_multiplier);
   }

   /* Apply crop mode */
   switch (crop_id)
   {
      case CROP_MINIMUM:
         retrow_crop = EMULATOR_MIN_WIDTH;
         retroh_crop = (video_config_geometry & PUAE_VIDEO_NTSC) ? 240 : 270;
         break;
      case CROP_SMALLER:
         retrow_crop = EMULATOR_MIN_WIDTH;
         retroh_crop = (video_config_geometry & PUAE_VIDEO_NTSC) ? 240 : 264;
         break;
      case CROP_SMALL:
         retrow_crop = EMULATOR_MIN_WIDTH;
         retroh_crop = (video_config_geometry & PUAE_VIDEO_NTSC) ? 240 : 256;
         break;
      case CROP_MEDIUM:
         retrow_crop = EMULATOR_MIN_WIDTH;
         retroh_crop = 240;
         break;
      case CROP_LARGE:
         retrow_crop = EMULATOR_MIN_WIDTH;
         retroh_crop = 224;
         break;
      case CROP_LARGER:
         retrow_crop = EMULATOR_MIN_WIDTH;
         retroh_crop = 216;
         break;
      case CROP_MAXIMUM:
         retrow_crop = EMULATOR_MIN_WIDTH;
         retroh_crop = EMULATOR_MIN_HEIGHT;
         break;
      case CROP_AUTO:
         /* Normalize previous width */
         retrow_crop /= width_multiplier;

         if (     retro_min_diwstart != retro_max_diwstop
               && retro_min_diwstart > 0
               && retro_max_diwstop > 0)
            retrow_crop = (retro_max_diwstop / width_multiplier) - (retro_min_diwstart / width_multiplier);
         retrow_crop = (retrow_crop < EMULATOR_MIN_WIDTH) ? EMULATOR_MIN_WIDTH : retrow_crop;

         if (     retro_thisframe_first_drawn_line != retro_thisframe_last_drawn_line
               && retro_thisframe_first_drawn_line > 0
               && retro_thisframe_last_drawn_line > 0)
            retroh_crop = retro_thisframe_last_drawn_line - retro_thisframe_first_drawn_line + 1;
         retroh_crop = (retroh_crop < EMULATOR_MIN_HEIGHT) ? EMULATOR_MIN_HEIGHT : retroh_crop;

         /* Allow full PAL height with NTSC PAR */
         if (defaulth > retroh && retroh_crop > retroh * (video_config & PUAE_VIDEO_DOUBLELINE) && !retro_doublescan ? 2 : 1)
            retroh = defaulth;

         /* Laced doublescan crop limit */
         if (retro_doublescan && islace)
            retroh_crop = retroh;
         break;
      default:
         retrow_crop = retrow;
         retroh_crop = retroh;
         break;
   }

   /* Crop mode preset calculations */
   if (crop_id > 0)
   {
      float crop_dar         = 0;
      float crop_par         = retro_get_aspect_ratio(0, 0, true);
      unsigned retroh_normal = retroh / ((video_config & PUAE_VIDEO_DOUBLELINE) && !retro_doublescan ? 2 : 1);

      switch (crop_mode_id)
      {
         case CROP_MODE_BOTH:
            /* Calculate width in height chunks */
            if (crop_id != CROP_AUTO)
               retrow_crop = (retrow / width_multiplier) - (retroh_normal - retroh_crop) / 2;
            break;
         /* Vertical disables horizontal crop */
         case CROP_MODE_VERTICAL:
            retrow_crop = retrow;
            break;
         /* Horizontal disables vertical crop and uses height chunks for width */
         case CROP_MODE_HORIZONTAL:
            if (crop_id != CROP_AUTO)
               retrow_crop = (retrow / width_multiplier) - (retroh_normal - retroh_crop) / 2;
            retroh_crop = retroh;
            break;
         /* Wide modes prioritize height crop and calculate new width when height runs out */
         case CROP_MODE_16_9:
            crop_dar    = (float)16/9;
            retrow_crop = retrow;
            break;
         case CROP_MODE_16_10:
            crop_dar    = (float)16/10;
            retrow_crop = retrow;
            break;
         /* Narrow modes prioritize width crop and calculate new height when width runs out */
         case CROP_MODE_4_3:
            crop_dar    = (float)4/3;
            retrow_crop = (int)(retroh_crop * crop_dar / crop_par);
            break;
         case CROP_MODE_5_4:
            crop_dar    = (float)5/4;
            retrow_crop = (int)(retroh_crop * crop_dar / crop_par);
            break;
      }

      /* Aspect limits */
      if (crop_dar > 0)
      {
         /* New height with minimum width when width is limited */
         if (retrow_crop < EMULATOR_MIN_WIDTH)
         {
            retrow_crop = EMULATOR_MIN_WIDTH;
            retroh_crop = (int)(retrow_crop / crop_dar * crop_par);
         }

         /* New width when height is limited */
         if (retroh_crop < (int)(retrow_crop / crop_dar * crop_par))
            retrow_crop = (int)(retroh_crop * crop_dar / crop_par);
      }

      /* If previous crop height was in double line */
      if (!video_productivity && retroh_crop > retroh)
         retroh_crop /= 2;

      retroh_crop = (retroh_crop < EMULATOR_MIN_HEIGHT) ? EMULATOR_MIN_HEIGHT : retroh_crop;
      retroh_crop *= (video_config & PUAE_VIDEO_DOUBLELINE) && !retro_doublescan ? 2 : 1;
      if (retroh_crop > retroh)
         retroh_crop = retroh;

      retrow_crop = (retrow_crop < EMULATOR_MIN_WIDTH) ? EMULATOR_MIN_WIDTH : retrow_crop;
      /* Even widths only */
      retrow_crop = (int)(retrow_crop / 2) * 2;
      retrow_crop *= width_multiplier;
      if (retrow_crop > retrow)
         retrow_crop = retrow;
   }

   /* Must do full av_info update if max size grows */
   if (retrow > retrow_max)
   {
      retrow_max = retrow;
      change_timing = true;
   }
   if (retroh > retroh_max)
   {
      retroh_max = retroh;
      change_timing = true;
   }

   /* Offset centerings */
   update_video_center_vertical();
   update_video_center_horizontal();

   /* Fetch default av_info (not current!) */
   struct retro_system_av_info new_av_info;
   retro_get_system_av_info(&new_av_info);

   /* Cropped geometry update */
   if (retrow_crop != retrow || retroh_crop != retroh)
   {
      new_av_info.geometry.base_width   = retrow_crop;
      new_av_info.geometry.base_height  = retroh_crop;
      new_av_info.geometry.aspect_ratio = retro_get_aspect_ratio(retrow_crop, retroh_crop, false);

      /* Ensure statusbar stays visible at the bottom */
      int statusbar_position_offset = retroh - retroh_crop - retroy_crop - opt_statusbar_position_offset;
      if (opt_statusbar_position >= 0)
      {
         opt_statusbar_position = statusbar_position_offset;

         if (opt_statusbar_position < 0)
            opt_statusbar_position = 0;
      }
      else
         opt_statusbar_position = -retroy_crop + 1;

#if 0
      printf("ztatusbar:%3d old:%3d offset:%3d, defaulth:%d retroz:%d\n", opt_statusbar_position, opt_statusbar_position_old, opt_statusbar_position_offset, defaulth, retroh_crop);
#endif
   }

   /* Skip geometry update if there is no change */
   if (base_width_old   == new_av_info.geometry.base_width  &&
       base_height_old  == new_av_info.geometry.base_height &&
       aspect_ratio_old == new_av_info.geometry.aspect_ratio)
      change_geometry = false;

   /* Timing or geometry update */
   if (change_timing)
   {
      set_config_changed();
      new_av_info.timing.fps = retro_refresh = hz;
      environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &new_av_info);
   }
   else if (change_geometry)
      environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &new_av_info);

   /* Timing changes require new SNDRATE calculation */
   if (change_timing || (video_config_allow_hz_change > 1 && change_timing_init))
      compute_vsynctime();

   /* Remember aspect ratio for update skip */
   if (change_timing || change_geometry)
      aspect_ratio = new_av_info.geometry.aspect_ratio;

   retro_bmp_offset = (retrox_crop * (pix_bytes >> 1)) + (retroy_crop * (retrow << (pix_bytes >> 2)));

   /* Logging */
   if (av_log)
   {
      if (change_timing)
         printf("  * Update av_info : %dx%d %0.4fHz, crop: %dx%d, aspect:%0.3f, video_config:%d offset:%dx%d\n",
               retrow, retroh, hz, retrow_crop, retroh_crop, aspect_ratio, video_config_geometry, retrox_crop, retroy_crop);
      else
         printf("  * Update %s: %dx%d, crop: %dx%d, aspect:%0.3f, video_config:%d offset:%dx%d\n",
               (change_geometry) ? "geometry" : "center  ",
               retrow, retroh, retrow_crop, retroh_crop, aspect_ratio, video_config_geometry, retrox_crop, retroy_crop);
   }

   if (islace)
   {
      /* Reset horizontal hacks */
      if (locked_video_horizontal)
         log_cb(RETRO_LOG_INFO, "Horizontal centering hacks cleared.\n");
      locked_video_horizontal = false;
   }

   return true;
}

/* RTG (Picasso96 / graphics board) screen: while the Amiga shows it, the
 * frontend gets the board's framebuffer at its own resolution instead of
 * the chipset picture (retrodep/picasso96_retro.c). Returns true when the
 * frame was presented. */
extern bool retro_rtg_frame(const uae_u32 **buf, int *width, int *height, int *pitch);

static bool retro_rtg_present(void)
{
   static bool rtg_on_old = false;
   static int rtg_w_old = 0, rtg_h_old = 0;
   static uint16_t *rtg_565 = NULL;
   const uae_u32 *rtg = NULL;
   int w = 0, h = 0, pitch = 0;
   bool rtg_on = retro_rtg_frame(&rtg, &w, &h, &pitch);

   if (rtg_on != rtg_on_old || (rtg_on && (w != rtg_w_old || h != rtg_h_old)))
   {
      struct retro_system_av_info av;
      retro_get_system_av_info(&av);
      if (rtg_on)
      {
         av.geometry.base_width   = w;
         av.geometry.base_height  = h;
         if (av.geometry.max_width < (unsigned)w)
            av.geometry.max_width = w;
         if (av.geometry.max_height < (unsigned)h)
            av.geometry.max_height = h;
         av.geometry.aspect_ratio = (float)w / (float)h;
         log_cb(RETRO_LOG_INFO, "RTG screen %dx%d\n", w, h);
      }
      else
      {
         /* Back to the chipset display: recompute its geometry */
         request_update_av_info = true;
         log_cb(RETRO_LOG_INFO, "RTG screen off\n");
      }
      environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av);
      rtg_on_old = rtg_on;
      rtg_w_old  = w;
      rtg_h_old  = h;
   }

   if (!rtg_on)
      return false;

   if (pix_bytes == 4)
      video_cb(rtg, w, h, pitch);
   else
   {
      /* RGB565 frontend */
      int x, y;
      if (!rtg_565)
         rtg_565 = (uint16_t*)malloc(1920 * 1200 * sizeof(uint16_t));
      if (!rtg_565 || w > 1920 || h > 1200)
         return true;
      for (y = 0; y < h; y++)
      {
         const uae_u32 *s = (const uae_u32*)((const uint8_t*)rtg + (size_t)y * pitch);
         uint16_t *d = rtg_565 + (size_t)y * w;
         for (x = 0; x < w; x++)
         {
            uae_u32 c = s[x];
            d[x] = ((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x001f);
         }
      }
      video_cb(rtg_565, w, h, w * sizeof(uint16_t));
   }
   return true;
}

void retro_run(void)
{
   static uint8_t old_frame = 0;
   bool rtg_shown;

   /* Core options */
   bool updated = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      update_variables();

   /* Poll inputs */
   input_poll_cb();
   retro_poll_event();

   /* Check if a restart is required */
   if (restart_pending)
   {
      restart_pending = 0;
      libretro_do_restart(sizeof(uae_argv)/sizeof(*uae_argv), uae_argv);
      /* Re-run emulation first pass */
      libretro_runloop_active = false;
      restart_pending = m68k_go(1, 0);
      /* RAM was reallocated for the new configuration */
      retro_update_memory_info();
   }

   /* Resume emulation for 1 frame (may_quit, resume) */
   libretro_runloop_active = true;
   restart_pending = m68k_go(1, 1);
   retro_now += 1000000 / retro_refresh;

   /* Handle statusbar text, audio filter type & video geometry + resolution */
   update_audiovideo();

   /* RTG screen shown: the chipset geometry waits until it is switched off */
   rtg_shown = retro_rtg_present();

   /* AV info change is requested */
   if (request_update_av_info && !rtg_shown)
      retro_update_av_info();

   /* Fetch the picture from the emulation's raster: the window position
    * and the crop rectangle are final at this point, and the raster still
    * holds the complete frame (reset_drawing() below erases it). Statusbar
    * and virtual keyboard are drawn on top later. */
   retro_video_blit();

   /* If any drawing parameters/offsets have been modified,
    * must call reset_drawing() to ensure that the changes
    * are 'registered' by center_image() in drawing.c
    * > If we don't do this, the wrong parameters may be
    *   used on the next frame, which can lead to out of
    *   bounds video buffer access (memory corruption)
    * > This check must come *after* horizontal/vertical
    *   offset calculation, retro_update_av_info() and
    *   retro_poll_event() */
   if (request_reset_drawing)
   {
      request_reset_drawing = false;
      reset_drawing();

      if (request_init_custom_timer > 1)
         old_frame++;
   }

   /* Dynamic resolution changing requires a frame breather after reset_drawing() */
   if (request_init_custom_timer > 0)
   {
      old_frame++;
      request_init_custom_timer--;
      if (request_init_custom_timer == 0)
         init_custom();
   }

   /* Warning messages */
   if (retro_message)
   {
      struct retro_message msg;
      msg.msg = retro_message_msg;
      msg.frames = 500;
      environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
      retro_message = false;
   }

   /* LED interface */
   if (led_state_cb)
      retro_led_interface();

   /* Automatic loading fast-forward */
   if (opt_autoloadfastforward)
      retro_autoloadfastforwarding();

   /* Virtual keyboard */
   if (retro_vkbd)
      print_vkbd();

   /* Forced statusbar messages */
   if ((!retro_statusbar && opt_statusbar & STATUSBAR_MESSAGES && statusbar_message_timer) || retro_statusbar)
      print_statusbar();

   if (!rtg_shown)
      video_cb((old_frame) ? NULL : retro_bmp + retro_bmp_offset, retrow_crop, retroh_crop, retrow << (pix_bytes >> 1));
   upload_output_audio_buffer();

   if (old_frame)
      old_frame--;

   if (request_reset_soft)
      retro_reset_soft();
   else if (request_reset_hard)
      retro_reset_hard();
}

bool retro_load_game(const struct retro_game_info *info)
{
   /* Pixel format */
   if (!pix_bytes_initialized)
   {
      pix_bytes_initialized = true;
      if (pix_bytes == 2)
      {
         enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
         if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
         {
            log_cb(RETRO_LOG_ERROR, "RGB565 is not supported.\n");
            environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
            return false;
         }
      }
      else if (pix_bytes == 4)
      {
         enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
         if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
         {
            pix_bytes = 2;
            log_cb(RETRO_LOG_INFO, "XRGB8888 is not supported. Trying RGB565.\n");
            fmt = RETRO_PIXEL_FORMAT_RGB565;
            if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
            {
               log_cb(RETRO_LOG_INFO, "RGB565 is not supported.\n");
               environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
               return false;
            }
         }
      }
   }

   /* Content */
   if (info)
   {
      /* path_is_valid() requires raw path */
      strlcpy(full_path, info->path, sizeof(full_path));
      /* Content-dir uae conf requires original path */
      strlcpy(info_path, info->path, sizeof(info_path));
   }

   /* UAE config */
   if (!retro_create_config())
      return false;

   /* Initialise emulation */
   umain(sizeof(uae_argv)/sizeof(*uae_argv), uae_argv);

   /* Run emulation first pass */
   restart_pending = m68k_go(1, 0);

   /* Force check for redirected save disks */
   floppy_open_redirect(-1);

   /* Save states
    * > Ensure that save state file path is empty,
    *   since we use memory based save states */
   savestate_fname[0] = '\0';

   /* Estimate necessary save state size
    * > Headroom must cover more than just RAM: CPU/chipset state, device
    *   state, and - critically for HD/WHDLoad content - the filesystem
    *   FSYP/FSYS chunks and CONF chunk, none of which scale with RAM size.
    *   A flat 128 KB alone (previously the only term here) can be too
    *   small once WHDLoad "Files"/"HDFs" mode adds one or more virtual/
    *   hardfile units with many open locks, silently truncating
    *   retro_serialize() and corrupting the rewind buffer. Restore the
    *   proportional margin on top of the flat floor instead of dropping
    *   the floor entirely.
    * > The 5%-of-RAM margin alone is still too thin on small-RAM configs:
    *   each mounted filesystem unit's FSYS chunk is capped at ~100 KB
    *   (see save_filesys()) and WHDLoad "Files" mode can mount up to
    *   four of them, so account for that directly rather than relying
    *   on RAM size to correlate with mounted unit count. */
   retro_update_memory_info();
   return true;
}

/* Save state size and memory map of the current machine. Called again
 * after a restart (model change + reset), which reallocates the RAM. */
static void retro_update_memory_info(void)
{
   save_state_file_size = currprefs.chipmem.size + currprefs.bogomem.size + currprefs.fastmem[0].size + currprefs.z3fastmem[0].size;
   /* Accelerator board and mainboard RAM (A3000 high/low, 25-bit, Z3 chip).
    * Boards report their RAM as cpuboardmem and also map it as 25-bit or
    * A3000 high memory, so take the larger of the two instead of the sum. */
   {
      size_t board = currprefs.cpuboardmem1.size + currprefs.cpuboardmem2.size;
      size_t mapped = currprefs.mbresmem_high.size + currprefs.mem25bit.size;
      save_state_file_size += (board > mapped ? board : mapped)
            + currprefs.mbresmem_low.size + currprefs.z3chipmem.size;
   }
   save_state_file_size += (size_t)(((float)save_state_file_size * 0.05f) + 0.5f);
   save_state_file_size += 128 * 1000;
   save_state_file_size += (size_t)nr_units() * 100 * 1024;

   struct retro_memory_descriptor memdesc[] = {
      {RETRO_MEMDESC_SYSTEM_RAM, chipmem_bank.baseaddr, 0, 0, 0, 0, chipmem_bank.allocated_size, "CHIP"},
      {RETRO_MEMDESC_SYSTEM_RAM, bogomem_bank.baseaddr, 0, 0, 0, 0, bogomem_bank.allocated_size, "SLOW"},
      {RETRO_MEMDESC_SYSTEM_RAM, fastmem_bank[0].baseaddr, 0, 0, 0, 0, fastmem_bank[0].allocated_size, "FAST"}
   };

   struct retro_memory_map mmap = {
      memdesc,
      sizeof(memdesc) / sizeof(memdesc[0])
   };

   environ_cb(RETRO_ENVIRONMENT_SET_MEMORY_MAPS, &mmap);
}

void retro_unload_game(void)
{
   /* Ensure WHDLoad saves are written with write cache enabled */
   whdload_quitkey();

   /* Gzip savedisks */
   if (dc && dc->count > 1 && !string_is_empty(changed_prefs.floppyslots[0].df))
      dc_save_disk_compress(dc);

   /* Close redirected save disks */
   floppy_close_redirect(-1);

   /* Ensure save state de-serialization file
    * is closed/NULL
    * Note: Have to do this here (not in retro_deinit())
    * since leave_program() calls zfile_exit() */
   if (retro_deserialize_file)
   {
      zfile_fclose(retro_deserialize_file);
      retro_deserialize_file = NULL;
   }

   if (dc)
      dc_reset(dc);

   cpu_cycle_exact_force = false;
   automatic_sound_filter_type_update_timer = 0;
   fake_ntsc = false;
   real_ntsc = false;
   forced_video = -1;
   locked_video_horizontal = false;
   opt_aspect_ratio_locked = false;
}

unsigned retro_get_region(void)
{
   return (video_config & PUAE_VIDEO_NTSC || real_ntsc) ? RETRO_REGION_NTSC : RETRO_REGION_PAL;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
   (void)type;
   (void)info;
   (void)num;
   return false;
}

size_t retro_serialize_size(void)
{
   return save_state_file_size;
}

bool retro_serialize(void *data_, size_t size)
{
   struct zfile *state_file = save_state("libretro", (uae_u64)save_state_file_size);
   bool success = false;

   if (state_file)
   {
      uae_s64 state_file_size = zfile_size(state_file);

      if (size >= state_file_size)
      {
         size_t len = zfile_fread(data_, 1, state_file_size, state_file);

         if (len == state_file_size)
            success = true;
      }
      else
      {
         log_cb(RETRO_LOG_ERROR, "retro_serialize: save state buffer too small (%llu available, %lld required) - state not saved\n",
               (unsigned long long)size, (long long)state_file_size);
      }

      zfile_fclose(state_file);
   }

   return success;
}

bool retro_unserialize(const void *data_, size_t size)
{
   bool success = false;

   /* Cannot restore state while any 'savestate'
    * operation is underway
    * > Actual restore is deferred until m68k_go(),
    *   so we have to use a shared shared state file
    *   object - this cannot be modified until the
    *   restore is complete
    * > Note that this condition should never be
    *   true - if a save state operation is underway
    *   at this point then we are dealing with an
    *   unknown error */
   if (!savestate_state)
   {
      if (retro_deserialize_file)
      {
         zfile_fclose(retro_deserialize_file);
         retro_deserialize_file = NULL;
      }

      retro_deserialize_file = zfile_fopen_empty(NULL, "libretro", size);

      if (retro_deserialize_file)
      {
         size_t len = zfile_fwrite(data_, 1, size, retro_deserialize_file);

         if (len == size)
         {
            unsigned frame_counter = 0;
            unsigned max_frames    = 50;

            zfile_fseek(retro_deserialize_file, 0, SEEK_SET);
            savestate_state = STATE_DORESTORE;

            /* For correct operation of the frontend,
             * the save state restore must be completed
             * by the time this function returns.
             * Since PUAE requires several (2) frames to get
             * itself in order during a restore event, we
             * have to keep emulating frames until the
             * restore is complete...
             * > Note that we set a 'timeout' of 50 frames
             *   here (1s of emulated time at 50Hz) to
             *   prevent lock-ups in the event of unexpected
             *   errors
             * > Temporarily 'deactivate' runloop - this lets
             *   us call m68k_go() without accessing frontend
             *   features - specifically, it disables the audio
             *   callback functionality */
            libretro_runloop_active = false;
            while (savestate_state && (frame_counter < max_frames))
            {
               /* Note that retro_deserialize_file will be
                * closed inside m68k_go() upon successful
                * completion of the restore event */
               restart_pending = m68k_go(1, 1);
               frame_counter++;
            }
            libretro_runloop_active = true;

            /* If the above while loop times out, then
             * everything is completely broken. We cannot
             * handle this here, so just assume the restore
             * completed successfully... */
            request_reset_drawing = true;
            success               = true;
         }
         else
         {
            zfile_fclose(retro_deserialize_file);
            retro_deserialize_file = NULL;
         }
      }
   }

   return success;
}

void *retro_get_memory_data(unsigned id)
{
   if (id == RETRO_MEMORY_SYSTEM_RAM)
      return chipmem_bank.baseaddr;
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   if (id == RETRO_MEMORY_SYSTEM_RAM)
      return chipmem_bank.allocated_size;
   return 0;
}

void retro_cheat_reset(void) {}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;
}
