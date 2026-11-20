# PUAE LIBRETRO

Based on WinUAE 6.1.0 (beta 23), [tonioni/WinUAE](https://github.com/tonioni/WinUAE) commit `4348e045`, on top of [libretro-uae](https://github.com/libretro/libretro-uae) commit `d366f77`.

The official release is based on WinUAE 5.3.1, git commit `51b7be351f359dc97f967f051da390283bc4b409`.

## WinUAE 6.1 port

The emulator side (`sources/src`) has been replaced with WinUAE 6.1 and the libretro frontend adapted to it.

### Emulation

- WinUAE 6.1 chipset, CPU, blitter and device emulation
- New Denise renderer: Denise draws straight into the host buffer during emulation (the `linetoscr_*.cpp` files are WinUAE's generated line renderers). The core keeps a private 32-bit raster and the frontend cuts its window out of it, so crop and centering behave as before.
- Keyboard microcontroller emulation (6570-036, 68HC05, D8039HLC), see [Keyboard](#keyboard)
- Accelerator boards, see [Accelerator boards](#accelerator-boards)
- WinUAE's C++ constructs converted to C (atomics, `alignas`, `cfgfile_mount`, `<cstdint>`, overloads); the keyboard MCU cores are converted by a script and checked against the C++ originals

### Fixes found during the port

- Save state restore crashed on the first `retro_unserialize` (RGA pipeline refresh slot without a pointer, also upstream)
- Mismatched function signatures between files (heap corruption when saving states, crash on Windows)
- Accelerator board ROMs read as empty from in-memory files (Blizzard 1230 III without RAM; also ACA500, CyberStorm MK3, SCSI kits)
- Use-after-free in the A3000/A4000T SCSI controller on shutdown
- Uninitialized buffer copied in `target_cfgfile_load()` (overflow when restarting)
- Windows builds added the UAE boot ROM and filesystem board to every model; on CDTV/CDTV-CR that skipped the DMAC autoconfig and the machine stayed on a black screen
- 8048 keyboard MCU read past the ROM at PC `0x7FF`; illegal 68HC05 opcodes called a NULL handler
- Automatic crop jumping when sprites touched the display window edge (Pinball Illusions)

### Hardware test suite (amitorture)

The core passes [amitorture](https://github.com/solskogen/amitorture) (commit `af2c0e9`), a bare-metal test suite that checks CPU, chipset, CIA, blitter, copper, Paula and expansion hardware against real Amiga behaviour and shows its results on the Amiga display.

| Disk | Model | Pass | Fail | Skip | Untestable |
|---|---|---|---|---|---|
| 1 core | A1200 | 922 | 0 | 48 | 31 |
| 2 cpu | A1200 | 82 | 0 | 5 | 0 |
| benchmarks | A500 | integer score 1.00 (A500 = 1.00) | | | |
| benchmarks | A1200 + 8 MB fast | integer score 7.23 | | | |

- Disk 1 needs `Chipset > Collision Level` = `Full` (with the default `Sprites and Playfields`, bitplane-to-bitplane collisions are not emulated) and `Video > Resolution` = `Super-High` (AGA 1/4 pixel scrolling is only visible at superhires output width). Both behave as in WinUAE.
- Disk 3 (expansion) fails only the 5 MSM6242B real-time clock cases: as in WinUAE, the clock reads the host time and ignores writes, HOLD, STOP and BUSY.
- "Untestable" cases need amitorture's host runner (screenshots, key and mouse injection), which is not available in RetroArch.
- The disks wait up to 10 minutes for that runner after boot. To run them in RetroArch, re-bake them without the mailbox: `python3 tools/mkadf.py --in amitorture-1-core.adf -o out.adf --param flags=nombox`.

## Default controls

| RetroPad button | Action                        |
|-----------------|-------------------------------|
| D-Pad           | Joystick                      |
| Left Analog     | Mouse                         |
| Right Analog    | Mouse                         |
| B               | Fire button 1 / Red           |
| A               | Fire button 2 / Blue          |
| X               | Space                         |
| L2              | Left mouse button             |
| R2              | Right mouse button            |
| Select (Short)  | Toggle virtual keyboard       |
| Select (Long)   | Toggle statusbar              |
| Select (Hold)   | Fast-Forward                  |

| Keyboard key    | Action                        |
|-----------------|-------------------------------|
| F12             | Toggle statusbar              |
| RControl        | Switch between joystick/mouse |

### Virtual keyboard controls

| Button          | Action                        |
|-----------------|-------------------------------|
| B / Enter       | Keypress                      |
| A               | Toggle transparency           |
| Y (Short)       | Toggle CapsLock               |
| Y (Long)        | Quick map button              |
| Y (Very long)   | Quick clear button            |
| X               | Press Space                   |
| Start           | Press Return                  |

Long press for sticky keys. Stickying the third key will replace the second.

### Joyport control

Some games use mouse instead of joystick. D-Pad can be switched between joystick and mouse control in several ways:

- Use the core option: `Quick Menu > Core Options > RetroPad Joystick/Mouse`
- Bring up the virtual keyboard with `Select` button, and press the key labeled `J/M`
- Press the default keyboard shortcut `Right Control`
- Assign `Switch Joystick/Mouse` to any RetroPad button under `Quick Menu > Core Options`

## Configuration

The following model presets are provided:

| Short   | Long           | Kickstart | Chipset | Memory                          |
|---------|----------------|-----------|---------|---------------------------------|
| A1000   | Amiga 1000     | v1.3      | OCS     | 0.5MB Chip RAM                  |
| A500OG  | Amiga 500      | v1.3      | OCS     | 0.5MB Chip RAM                  |
| A500    | Amiga 500      | v1.3      | OCS     | 0.5MB Chip RAM + 0.5MB Slow RAM |
| A500ECS | Amiga 500      | v1.3      | ECS     | 1MB Chip RAM                    |
| A500+   | Amiga 500+     | v2.04     | ECS     | 1MB Chip RAM                    |
| A600OG  | Amiga 600      | v3.1      | ECS     | 2MB Chip RAM                    |
| A600    | Amiga 600      | v3.1      | ECS     | 2MB Chip RAM + 8MB Fast RAM     |
| A1200OG | Amiga 1200     | v3.1      | AGA     | 2MB Chip RAM                    |
| A1200   | Amiga 1200     | v3.1      | AGA     | 2MB Chip RAM + 8MB Fast RAM     |
| A2000OG | Amiga 2000     | v1.3      | OCS     | 0.5MB Chip RAM + 0.5MB Slow RAM |
| A2000   | Amiga 2000     | v3.1      | ECS     | 1MB Chip RAM                    |
| A3000   | Amiga 3000     | v3.1      | ECS     | 2MB Chip RAM + 8MB Fast RAM     |
| A4030   | Amiga 4000/030 | v3.1      | AGA     | 2MB Chip RAM + 8MB Fast RAM     |
| A4040   | Amiga 4000/040 | v3.1      | AGA     | 2MB Chip RAM + 8MB Fast RAM     |
| A4060   | Amiga 4000/060 | v3.1      | AGA     | 2MB Chip RAM + 8MB Fast RAM     |
| A4000T  | Amiga 4000T    | v3.1      | AGA     | 2MB Chip RAM + 8MB Fast RAM     |
| CDTV    | Amiga CDTV     | v1.3+ext  | ECS     | 1MB Chip RAM                    |
| CDTVCR  | Amiga CDTV-CR  | v2.05+ext | ECS     | 1MB Chip RAM                    |
| CD32    | Amiga CD32     | v3.1+ext  | AGA     | 2MB Chip RAM                    |
| CD32FR  | Amiga CD32     | v3.1+ext  | AGA     | 2MB Chip RAM + 8MB Fast RAM     |

### Kickstart ROMs

The models require matching Kickstart ROMs in RetroArch `system` directory.

Amiga Forever and TOSEC filenames are also accepted.

Encrypted Amiga Forever ROMs require `rom.key` also in `system` directory.

The core has a somewhat compatible built-in AROS Kickstart, which is used as a fallback when the proper Kickstart is not found.

*It is critical to use ROMs with the correct MD5!*

| System     | Version                       | Filename               | Amiga Forever                 | Size      | MD5                              |
|------------|-------------------------------|------------------------|-------------------------------|----------:|----------------------------------|
| A1000      | KS v1.1 rev 31.034 NTSC       | **kick31034.A1000**    | **amiga-os-110-ntsc.rom**     |   262 144 | 0b8442c311caa54fb12ec88eaaa9facf |
| A1000      | KS v1.1 rev 32.034 PAL        | **kick32034.A1000**    | **amiga-os-110-pal.rom**      |   262 144 | 1fa1f93d3d7b51271dd1356b8b2b45a9 |
| A500-A2000 | KS v1.2 rev 33.180            | **kick33180.A500**     | **amiga-os-120.rom**          |   262 144 | 85ad74194e87c08904327de1a9443b7a |
| A500-A2000 | KS v1.3 rev 34.005            | **kick34005.A500**     | **amiga-os-130.rom**          |   262 144 | 82a21c1890cae844b3df741f2762d48d |
| A500+      | KS v2.04 rev 37.175           | **kick37175.A500**     | **amiga-os-204.rom**          |   524 288 | dc10d7bdd1b6f450773dfb558477c230 |
| A600       | KS v2.05 rev 37.350           | **kick37350.A600**     | **amiga-os-205-a600.rom**     |   524 288 | 465646c9b6729f77eea5314d1f057951 |
| A600-A2000 | KS v3.1 rev 40.063            | **kick40063.A600**     | **amiga-os-310-a600.rom**     |   524 288 | e40a5dfb3d017ba8779faba30cbd1c8e |
| A1200      | KS v3.0 rev 39.106            | **kick39106.A1200**    | **amiga-os-300-a1200.rom**    |   524 288 | b7cc148386aa631136f510cd29e42fc3 |
| A1200      | KS v3.1 rev 40.068            | **kick40068.A1200**    | **amiga-os-310-a1200.rom**    |   524 288 | 646773759326fbac3b2311fd8c8793ee |
| A4000      | KS v3.0 rev 39.106            | **kick39106.A4000**    | **amiga-os-300-a4000.rom**    |   524 288 | 9b8bdd5a3fd32c2a5a6f5b1aefc799a5 |
| A4000      | KS v3.1 rev 40.068            | **kick40068.A4000**    | **amiga-os-310-a4000.rom**    |   524 288 | 9bdedde6a4f33555b4a270c8ca53297d |
| CDTV       | CDTV extended ROM v1.0        | **kick34005.CDTV**     | **amiga-os-130-cdtv-ext.rom** |   262 144 | 89da1838a24460e4b93f4f0c5d92d48d |
| A3000      | KS v3.1 rev 40.068            | **kick40068.A3000**    | **amiga-os-310-a3000.rom**    |   524 288 | 413590e50098a056cfec418d3df0212d |
| A4000T     | KS v3.1 rev 40.070            | **kick40070.A4000T**   | **amiga-os-310-a4000t.rom**   |   524 288 | e873c43040b4d7a9c65f37cf2da2158f |
| CDTV-CR    | CDTV-CR extended ROM v3.44    | **kick40001.CDTVCR**   | **amiga-os-cdtv-cr-ext.rom**  |   262 144 | 58454dffbabef6b7666876c1e6870a60 |

The A1000 preset boots Kickstart 1.3 from ROM (not the bootstrap with a Kickstart disk). CDTV-CR uses the A600 Kickstart 2.05 (`kick37350.A600`) with its extended ROM.

ROMs are also found **by content**: when a file with the standard name is missing, every file from 2KB to 1MB in `system` and `system/uae` (one subdirectory deep) is identified with WinUAE's ROM database, whatever its name. The same scan provides the ROMs of accelerator boards and keyboard microcontrollers. ROMs split in two chips (`_even/_odd` or `_hi/_lo`) must be merged first.

CD32 requires either separate ROMs (Kickstart + extended ROM) or the combined ROM:

| System | Version                       | Filename               | Amiga Forever                 | Size      | MD5                              |
|--------|-------------------------------|------------------------|-------------------------------|----------:|----------------------------------|
| CD32   | KS + extended v3.1 rev 40.060 | **kick40060.CD32**     |                               | 1 048 576 | f2f241bf094168cfb9e7805dc2856433 |
|        |                               | OR                     |                               |           |                                  |
| CD32   | KS v3.1 rev 40.060            | **kick40060.CD32**     | **amiga-os-310-cd32.rom**     |   524 288 | 5f8924d013dd57a89cf349f4cdedc6b1 |
| CD32   | Extended ROM rev 40.060       | **kick40060.CD32.ext** | **amiga-os-310-cd32-ext.rom** |   524 288 | bb72565701b1b6faece07d68ea5da639 |

### Resolution and rendering

These parameters control the output resolution of the core (default bolded):

| Name                 | Values                                   |
|----------------------|------------------------------------------|
| Video Standard       | **PAL 50Hz**, NTSC 60Hz                  |
| Video Resolution     | **Automatic**, Low, High, Super-High     |
| Video Line Mode      | **Automatic**, Single Line, Double Line  |
| Aspect Ratio         | **Automatic**, PAL, NTSC, NTSC auto, 1:1 |

With these settings all the standard resolutions are available:

| PAL 50Hz Resolution  | Description            |
|----------------------|------------------------|
| 360x288              | Lores                  |
| 720x288              | Hires Single Line      |
| 720x576              | Hires Double Line      |
| 1440x288             | SuperHires Single Line |
| 1440x576             | SuperHires Double Line |

| NTSC 60Hz Resolution | Description            |
|----------------------|------------------------|
| 360x240              | Lores                  |
| 720x240              | Hires Single Line      |
| 720x480              | Hires Double Line      |
| 1440x240             | SuperHires Single Line |
| 1440x480             | SuperHires Double Line |

When using low resolution mode, rendering will be halved horizontally and forced into "**Single Line**" mode. 
Scaling shaders looks great, but high resolution games and Workbench are badly rendered.

When using high resolution "**Double Line**" mode, rendering will be doubled vertically. 
It is compatible with high resolution games and Workbench, but scaling shaders will look ugly. 
"**Double Line**" shows interlaced fields separately (weave) and is suited for deinterlacing shaders.

When using high resolution "**Single Line**" mode, rendering is presented as is. 
It delivers the best of both worlds, and looks great with high resolution games, Workbench and shaders. 
"**Single Line**" combines interlaced fields into one field (bob), which will make high resolution images blocky and jittery.

- Automatic "Resolution" defaults to "**Hires**" and selects "**SuperHires**" when needed (practically only in Workbench and Super Skidmarks)
- Automatic "Line Mode" defaults to "**Single Line**" and selects "**Double Line**" on interlaced screens

## Core options

The core options are grouped in 12 categories, in menu order: **Machine** (model and presets, Kickstart, chipset, video standard, clock), **CPU**, **Memory**, **Expansions** (accelerator boards, cartridges, keyboard), **Chipset** (accuracy), **Media** (floppy, hard drive, WHDLoad, CD), **Video**, **Audio**, **Input**, **Hotkey Mapping**, **RetroPad Mapping**, **On-Screen Display**. The option keys (`puae_...`) did not change, so saved configurations keep working.

New options from WinUAE 6:

| Option | Values | Notes |
|--------|--------|-------|
| Chipset > Display Optimizations | **Full**, Partial, None | Shortcuts of the new Denise renderer |
| Machine > Chipset | **Automatic**, OCS, ECS Agnus, ECS Denise, Full ECS, AGA | Overrides the chipset of the model |
| Machine > Real-Time Clock | **Automatic**, None, MSM6242B, RP5C01A, MSM6242B (A2000) | |
| CPU > 68030 Data Cache | **disabled**, enabled | |
| Expansions > Keyboard Type | **UAE**, Automatic, A500/A600/A1000/A2000-A4000, A1200, A2000 | Keyboard microcontroller, see [Keyboard](#keyboard) |
| Expansions > Keyboard Rollover | **N-key**, Original | Original emulates the matrix ghosting |
| Media > Hard Drive Write Protection | **disabled**, enabled | Mounts all images and directories read-only |
| Video > Interlaced Line Mode | **Double, fields**, Double, frames, Double, fields+ | Line doubling of interlaced screens |
| Video > Progressive Line Mode | **Double**, Scanlines, Double, fields, Double, fields+ | Line doubling of progressive screens |
| Video > Filtered Low Resolution | **disabled**, enabled | Blends pixels when the screen has a higher resolution than the output |
| Audio > Stereo Delay | **disabled**, 1-10 | Only with stereo separation below 100% |
| Audio > Swap Stereo Channels | **disabled**, enabled | |

### Reset

A hard reset (RetroArch `Restart`) restarts the whole emulation with the current options, the same as loading the core again. Model, accelerator board, memory and chipset changes therefore only need a reset, no core reload.

### CPU speed

`CPU > CPU Speed` has a new value, **Max**: the CPU runs as fast as the PC allows, using 75% of each frame and leaving the rest to RetroArch for video and audio. The result depends on the PC. As an example, an A1200 with a Blizzard 1260 reaches about 120 000-150 000 Dhrystone in xsysinfo, about 140 times an A600.

`CPU Speed` only works with `CPU Compatibility` set to **Normal** or **More compatible**; with Cycle-exact the core shows a warning and ignores it.

## Accelerator boards

`Expansions > Accelerator Board` and `Expansions > Accelerator Memory` (Automatic = 32MB, limited to the board maximum). Core restart required.

| Board | Model | CPU |
|-------|-------|-----|
| GVP A530 | A500 | 68030 |
| M-Tec E-Matrix 530 | A500 (KS 2.0+) | 68030 |
| Hardital TQM | A500 | 68030 |
| Kupke Golem 030 | A500 | 68030 |
| DCE Typhoon MK2 | A500 (KS 2.0+) | 68030 |
| Apollo 630 | A600 | 68040 |
| Blizzard 1230 I/II, III, IV | A1200 | 68030 |
| Blizzard 1240/1260 | A1200 | 68040/68060 |
| GVP A1230 Turbo+ / Series II | A1200 | 68030 |
| Apollo 1240/1260 | A1200 | 68040/68060 |
| DKB 1230/1240/Cobra | A1200 | 68030 |
| CSA Magnum 40 | A2000 | 68040 |
| GVP G-Force 030 | A2000 | 68030 |
| GVP G-Force 040 | A2000 | 68040/68060 |
| GVP A3001 Series I | A2000 | 68030 |
| Commodore A2620/A2630 | A2000 | 68020/68030 |
| Blizzard 2060 | A2000 | 68040/68060 |
| TekMagic 2040/2060 | A2000 | 68040/68060 |
| PP&S Zeus 040 | A2000 | 68040/68060 |
| CyberStorm MK I, MK II, MK III | A3000/A4000 | 68040/68060 |
| MacroSystem Falcon 040 | A3000/A4000 | 68040/68060 |
| MacroSystem Warp Engine | A4000 | 68040/68060 |
| QuikPak 4060 | A4000 | 68060 |
| DCE SX32 Pro | CD32 | 68030 |

- Most boards need their ROM, found by content in `system` or `system/uae`. Without it the board is disabled with a message.
- The board sets the CPU; on 68040/68060 boards `CPU Model` chooses between the two.
- E-Matrix 530 and Typhoon MK2 keep resetting with Kickstart 1.x; the core warns about it.
- As in WinUAE, Hardital TQM and Falcon 040 have no RAM, and the RAM of A2630 and Zeus 040 is not seen.
- Board RAM is included in save states.

## Keyboard

`Expansions > Keyboard Type` selects the emulation of the keyboard microcontroller (timing, rollover, ghosting). It needs the dump of the MCU ROM in `system` (or `system/uae`), found by CRC32 whatever its name:

| Keyboard | MCU | Size | CRC32 |
|----------|-----|-----:|-------|
| A500, A600, A1000, A2000-A4000 | 6570-036 | 2KB | `4a3fc332` |
| A1200 | 68HC05 | 8KB | `2a77eec4` |
| A2000 | D8039HLC | 2KB | `c23d280e` or `fb92a773` |

Without the ROM the default UAE keyboard is used and a message is shown. "Automatic" picks the keyboard of the model (CD32 and CDTV keep the UAE keyboard). Core restart required.

## Model overriding

You can force a specific model if a game needs one (AGA games for instance) either by the "Model" core option or by file path tags.

The "Model" core option at "**Automatic**" will default to A500 when booting floppy disks, A1200 when booting hard drives, and CD32 when booting compact discs.

The whole path (filename and directory) will be searched for the following tags if the model is "**Automatic**":

| Floppy | HD/LHA | CD    | String                                      | Result                                       |
|--------|--------|-------|---------------------------------------------|----------------------------------------------|
| **x**  |        |       | **(A500OG)**, **(512K)**, **(512KB)**       | Amiga 500 (0.5MB Chip RAM)                   |
| **x**  |        |       | **(A500)**, **OCS**                         | Amiga 500 (0.5MB Chip RAM + 0.5MB Slow RAM)  |
| **x**  |        |       | **(A500ECS)**                               | Amiga 500 (1MB Chip RAM)                     |
| **x**  |        |       | **(A500+)**, **(A500PLUS)**                 | Amiga 500+ (1MB Chip RAM)                    |
| **x**  | **x**  |       | **(A600OG)**, **(A600NF)**                  | Amiga 600 (2MB Chip RAM)                     |
| **x**  | **x**  |       | **(A600)**, **ECS**                         | Amiga 600 (2MB Chip RAM + 8MB Fast RAM)      |
| **x**  | **x**  |       | **(A1200OG)**, **(A1200NF)**                | Amiga 1200 (2MB Chip RAM)                    |
| **x**  | **x**  |       | **(A1200)**, **AGA**, **CD32**, **AmigaCD** | Amiga 1200 (2MB Chip RAM + 8MB Fast RAM)     |
| **x**  | **x**  |       | **(A4030)**, **(030)**                      | Amiga 4000/030 (2MB Chip RAM + 8MB Fast RAM) |
| **x**  | **x**  |       | **(A4040)**, **(040)**                      | Amiga 4000/040 (2MB Chip RAM + 8MB Fast RAM) |
| **x**  |        |       | **(A1000)**                                 | Amiga 1000 (0.5MB Chip RAM)                  |
| **x**  | **x**  |       | **(A3000)**                                 | Amiga 3000 (2MB Chip RAM + 8MB Fast RAM)     |
| **x**  | **x**  |       | **(A4060)**, **(060)**                      | Amiga 4000/060 (2MB Chip RAM + 8MB Fast RAM) |
| **x**  | **x**  |       | **(A4000T)**                                | Amiga 4000T (2MB Chip RAM + 8MB Fast RAM)    |
|        |        | **x** | **CDTV-CR**                                 | Amiga CDTV-CR (1MB Chip RAM)                 |
|        |        | **x** | **CDTV**                                    | Amiga CDTV (1MB Chip RAM)                    |
|        |        | **x** | **(CD32)**, **(CD32NF)**                    | Amiga CD32 (2MB Chip RAM)                    |
|        |        | **x** | **(CD32FR)**, **FastRAM**                   | Amiga CD32 (2MB Chip RAM + 8MB Fast RAM)     |
| **x**  | **x**  | **x** | **NTSC**, **(USA)**                         | NTSC 60Hz                                    |
| **x**  | **x**  | **x** | **PAL**, **(Europe)** !                     | PAL 50Hz                                     |
| **x**  |        |       | **(MD)** !!                                 | Insert each disk in different drives         |
| **x**  | **x**  | **x** | **(CE)**                                    | Force CPU Cycle-exact                        |

- (!) Additional tags: **(Denmark)**, **(Finland)**, **(France)**, **(Germany)**, **(Italy)**, **(Spain)**, **(Sweden)**
- (!!) **Maximum 4 disks**

Example: When launching "Alien Breed 2 AGA.hdf" or "AGA/Alien Breed 2.hdf" the model will be Amiga 1200.

Note: **CD32** and **AmigaCD** are a bit misleading, since they have nothing to do with actual CDs. 
They are for automatically selecting the appropriate model with certain WHDLoad slaves and AmigaCD-to-HDF conversions.

## Disk images, WHDLoad and M3U support

Supported formats are:

- **ADF**, **ADZ**, **IPF**, **DMS**, **FDI** for floppy disk images
- **ISO**, **CUE**, **CCD**, **NRG**, **MDS**, **CHD** for compact disc images
- **HDF**, **HDZ**, **LHA** for hard drive images
- **M3U** for multiple image playlist
- **ZIP** for various content (FD, HD, CD, WHDLoad)

### Floppy drive sounds

The core has embedded internal floppy drive samples. External sound samples have to be copied from [https://github.com/libretro/libretro-uae/tree/master/sources/uae_data](https://github.com/libretro/libretro-uae/tree/master/sources/uae_data) into a directory named `uae_data` or `uae` in RetroArch `system` directory.

### IPF support

Most full-price commercial Amiga games had some form of custom disk format and/or copy protection. For this reason, most commercial Amiga games cannot be stored in ADF files unaltered, but there is an alternative called Interchangeable Preservation Format (IPF) which was specifically designed for this purpose.

IPF support is done through CAPSIMG library. To enable it you have to put the dynamic library called `capsimg.dll` (Windows) or `capsimg.so` (Linux, macOS) in RetroArch `system` or executable directory.

Compatible CAPSIMG libraries for Windows, macOS and Linux can be found at [http://www.softpres.org/download](http://www.softpres.org/download) and [https://fs-uae.net/download#plugins](https://fs-uae.net/download#plugins)

Compatible CAPSIMG libraries for Android can be found at [https://github.com/rsn8887/capsimg/releases/latest](https://github.com/rsn8887/capsimg/releases/latest)

Please be aware that there are 32-bits and 64-bits versions of the library. Choose the one corresponding to the RetroArch executable.

#### Android

Android does not allow loading dynamic libraries from `system` directory, but it can load from the executable directory, which requires root access.

Android does allow loading from the core directory, which means you can and have to import/install `capsimg.so` as a core to use CAPSIMG without root access.

### ZIP support

ZIPs are extracted to a temporary directory in `saves`, bypassing the default frontend extraction.
The temporary directory is emptied but not removed on exit. ZIP is not repacked, which means saves and highscores are lost.

This allows:

- Automatic M3U playlist generation of all files
- The use of zipped images in M3Us
- If no compatible images are found, the ZIP will be treated as a directory

### M3U support

When you have a multi disk game, you can use a M3U playlist file to be able to change disks via RetroArch Disc Control interface.

A M3U file is a simple text file with one disk per line ([Wikipedia](https://en.wikipedia.org/wiki/M3U)).

Example:

`Simpsons, The - Bart vs. The Space Mutants.m3u`
```
Simpsons, The - Bart vs. The Space Mutants_Disk1.adf
Simpsons, The - Bart vs. The Space Mutants_Disk2.adf
```

Path can be absolute or relative to the location of the M3U file.

When the game asks for it, you can change the current disk in the RetroArch "Disc Control" menu:

- Eject the current disk with "Eject Disc"
- Select the right disk index with "Current Disc Index"
- Insert the new disk with "Insert Disc"

By default, RetroArch will display the filename (without extension) of each M3U entry when selecting a disk via the `Current Disc Index` drop-down menu. Custom display labels may be set for each disk using the syntax: `DISK_FILE|DISK_LABEL`. For example, the following M3U file:

`Valhalla & the Fortress of Eve.m3u`
```
Valhalla & the Fortress of Eve_Disk1.adf|Game Disk
Valhalla & the Fortress of Eve_Disk2.adf|Data Disk
Valhalla & the Fortress of Eve_Disk3.adf|Level 1 Disk
Valhalla & the Fortress of Eve_Disk4.adf|Level 2 Disk
Valhalla & the Fortress of Eve_Disk5.adf|Level 3 Disk
Valhalla & the Fortress of Eve_Disk6.adf|Level 4 Disk
```

...will be shown in RetroArch's disk selection menu as:

```
1: Game Disk
2: Data Disk
3: Level 1 Disk
4: Level 2 Disk
5: Level 3 Disk
6: Level 4 Disk
```

If `DISK_LABEL` is intentionally left blank (i.e. `DISK_FILE|`) then only the disk index will be displayed.

For games that require a dedicated save disk, one may be generated automatically by entering the following line in an M3U file: `#SAVEDISK:VolumeName`. `VolumeName` is optional and may be omitted. For example, this will create a blank, unlabelled disk image at disk index 5:

`Secret of Monkey Island.m3u`
```
Secret of Monkey Island_Disk 1.adf
Secret of Monkey Island_Disk 2.adf
Secret of Monkey Island_Disk 3.adf
Secret of Monkey Island_Disk 4.adf
#SAVEDISK:
```

Some games require save disks to have a specific label - for example, `It Came from the Desert` will only save to a disk named `DSAVE`:

***Virtual keyboard has a key labeled `SVDS` (CapsLock+STBR) which creates and switches to/from the save disk, which makes this step obsolete.***

`It Came from the Desert.m3u`
```
It Came from the Desert_Disk 1.adf
It Came from the Desert_Disk 2.adf
It Came from the Desert_Disk 3.adf
#SAVEDISK:DSAVE
```

Although one save disk is normally sufficient, an arbitrary number of `#SAVEDISK:VolumeName` lines may be included. Save disks are located in the frontend's save directory, with the following name: `[M3U_FILE_NAME].save[DISK_INDEX].adf`.

Save disks generated by the `#SAVEDISK:` keyword are automatically assigned the label: `Save Disk [SAVE_DISK_INDEX]`.

For games that support multiple disk drives, append "**(MD)**" as in "MultiDrive" to the M3U filename to insert each disk in different drives. Only possible with maximum 4 disks!

### Extra features

- `#SAVEDISK:<label>`
    - Create a save disk in `saves`
- `<disk>.adf|<label>`
    - Set a friendly name (shown in "Disc Control")
- `<disks>.zip#<disk>.adf`
    - Specify a disk inside a ZIP with multiple disks (not needed with single file ZIPs)

M3U playlist supports floppy disks, hard drives (all images are mounted at once) and compact discs.

## LED interface

Libretro LED interface is presented in the following order:

1. Power
2. Floppy drives
3. HD/CD/MD (NVRAM)
4. DF0
5. DF1
6. DF2
7. DF3
8. HD
9. CD/MD (NVRAM)

## WHDLoad

Pre-installed WHDLoad LHA archives can be launched directly without any kind of manual preparing and downloading.
HDF format is not recommended due to potential corruption especially with save states.

- WHDLoad helper files will be generated to `saves`, `WHDLoad.prefs` will be generated to `system`.
- `WHDLoad.prefs` & `WHDLoad.key` & `rom.key` will be copied from `system` to the helper image.
- Kickstarts will be copied automatically to the helper image.
- Saves are redirected to `WHDSaves:`.
- To update the WHDLoad helper image to the current core embedded version simply delete the directory.

- WHDLoad quit key is hardcoded to unused `$2B` = `AK_NUMBERSIGN` = `RETROK_HASH` which does not exist in modern keyboards.
    - Keyboard shortcut: LCtrl + Backslash
    - Virtual keyboard shortcut: Shifted RESET button

#### Overrides at startup

- **(Red)** Hold fire button for launch selector
    - For alternate `.info` launching
- **(Red+Blue)** Hold fire + 2nd fire for `ReadMe` + `MkCustom`
    - For creating default `CUSTOM` parameters

#### `WHDLoad Splash Screen` core option overrides

- **(Blue)** Hold 2nd fire for WHDLoad Config
    - Waits for user input if the slave supports splash screen configurations
- **(LMB)** Hold left mouse button for WHDLoad Splash
    - Briefly shows the splash screen while preloading (default WHDLoad behavior)
- **(RMB)** Hold right mouse button for WHDLoad Config+Splash
    - Always waits for user input at the splash screen

### New WHDLoad features for old users

#### Major changes

- Slave no longer needs to be renamed to game.slave. The first one is selected.
- Kickstarts will be copied automatically from `system` if they do not exist in the helper image.
- `WHDLoad.key` will be copied from `system` if it does not exist in the helper image.
- `WHDLoad.prefs` will be copied from `system` on every run.
- Supports a file named `custom` in the root of the game image for passing specific WHDLoad parameters when the slave does not support the config screen or when it should be the default, for example `Custom1=1`. It always overrides `WHDLoad.prefs`.
    - Use `MkCustom` script for simplest `custom` file handling. Launches after quitting WHDLoad.
    - `MkCustom` will create a slave-based `custom_$SLAVE` in `WHDSaves:`. Essential with readonly images.
- Supports a file named `load` in the root of the game image which overrides startup-sequence, aimed at non-WHDLoad installs.
- If `.slave` is not in the root of the game image, it will also be searched under the first found directory.
- ~~Saves will be redirected to `WHDSaves.hdf` when using legacy HDF mode.~~

#### Minor changes

- ~~Both HDF-files (`WHDLoad.hdf` & `WHDSaves.hdf`) can be located in either RA `system` or `saves`.~~
- "WHDLoad Support" core option does not need to be disabled when launching a non-WHDLoad HDF which has `S/Startup-Sequence`.
- `NTSC` parameter can be used with WHDLoad.
- Included `ClickNot` for suppressing drive clicking when drive sound emulation is enabled.
- Included `MEmacs` for file editing (`custom` & `load`).
- Included `xpkmaster.library`.
- New WHDLoad defaults:
    - `ButtonWait` (Waits for a button press in certain slaves when loading is so fast that you can't enjoy a picture or a tune).
    - `ReadDelay=0` & `WriteDelay=50` (These speed up OS switching on loadings and savings. Saves tend to corrupt HDFs with WriteDelay below 50).

#### Latest changes

- Updated WHDLoad to 20.1 (2026-10-13).
- Legacy HDF mode removed.
- Updated WHDLoad to 20.0 (2026-03-27).
- Updated WHDLoad to 19.2 (2025-10-28).
- Included `SetPatch` for better compatibility.
- Updated WHDLoad to 19.1 (2024-12-07).
- Updated WHDLoad to 18.9 (2023-05-04).
- Downgraded WHDLoad to 18.6 due to a save related bug in 18.7.
- Updated WHDLoad to the latest one (18.7 2021-10-23).
- Support for Retroplay LHA installs.
- Support for Arcadia installs (requires KS 1.2, `kick33180.A500` will be copied automatically).
- Show `ReadMe` before running `MkCustom` after quitting.
- Changed HDF filesystem from OFS to FFS. Prevents image corruption on unclean shutdowns.
- Default launch method changed to `WBRun`.
- Better support for games with multiple icons.
- Hold down fire button at boot for `.info` selector. Selector will be launched always when there is no exact match for `.slave`.
- `WHDLoad.hdf` + `WHDSaves.hdf` + `WHDLoad.prefs` baked in the core. No more separate downloads!
- `WHDLoad:` and `WHDSaves:` volumes defaulted to directory mode.

### Create a HDF image for a game

#### Not recommended or required anymore, because:

- WHDLoad specific LHA archives will work directly as a read-only hard drive image
- Directories will work as a regular hard drive image
- ZIP files will be extracted to a temporary directory and can contain any type of supported content

#### But if you must:

To do this you can use [ADF Opus](http://adfopus.sourceforge.net) or [amitools](https://github.com/cnvogelg/amitools).

Example, to create a HDF file from a zipped WHDLoad game:

- Extract files from the ZIP to a directory
- Pack the directory in a HDF file with:
    - ADFOpus: see [Allan Lindqvist's tutorial](http://lindqvist.synology.me/wordpress/?page_id=182)
    - amitools: `xdftool -f <NAME_OF_HDF> pack <GAME_DIRECTORY> size=<SIZE_OF_HDF>`
	
Note the size of the HDF specified by SIZE_OF_HDF must be greater than size of the directory to store the additional filesystem informations (f.ex a 1.25 ratio).

## Using configuration files

You can pass `.uae` configuration files and they will be appended to the core option configuration.

If `puae_libretro_[model].uae` exists in RetroArch `saves` it will be appended to the model preset section.

If `puae_libretro_global.uae` exists in RetroArch `saves` it will be appended to the configuration.

If `[content].uae` exists in content path or RetroArch `saves` it will be appended to the configuration.

The final generated configuration output is available in debug level log.

***Note that the use of configuration files is no longer encouraged or necessary. The core has been modified to always use the core options as a base, so that all custom configurations will be appended to the created configuration, effectively overriding the core options. The problem with this is that changing any core option while the core is running will reset all duplicate configurations. Therefore only add configurations which will require a restart or do not exist in the core options, if you must use a custom uae. If there is an option missing that is a must have, please make an issue about it.***

Example 1: You want to mount four non-RDB HDF files. You have one bootable 1000 MB file called `System.hdf` created with `surfaces=1`, and three non-bootable 2000 MB files called `WHDGamesA.hdf`, `WHDGamesB.hdf`, `WHDGamesC.hdf` created with `surfaces=2`. The HDF files are located in the folder with absolute path `/emuroms/amiga/hdf/`. For that scenario, you should create a `.uae` text file with the following content:

```
hardfile=read-write,32,1,2,512,/emuroms/amiga/hdf/System.hdf
hardfile=read-write,32,2,2,512,/emuroms/amiga/hdf/WHDGamesA.hdf
hardfile=read-write,32,2,2,512,/emuroms/amiga/hdf/WHDGamesB.hdf
hardfile=read-write,32,2,2,512,/emuroms/amiga/hdf/WHDGamesC.hdf
```

Example 2: You want to mount a directory full of extracted data as a hard drive:

```
filesystem2=rw,DH0:data:/emuroms/amiga/,0
```

Windows tip:

- If paths are enclosed with quotes, Windows needs double backslashes: `filesystem2=rw,DH0:data:"c:\\emuroms\\amiga",0`.

Linux tip:

- Leave the ending slash to the path to make sure UAE sees it as a directory.

If you are using RDB HDF files, please use `0,0,0,512` instead of geometry numbers like `32,1,2,512`. The geometry will then be read from the file. This only works for RDB HDF files.

## Latest features

- WinUAE 6.1 emulator core
- New models: A1000, A3000, A4000/060, A4000T, CDTV-CR
- Accelerator boards for A500, A600, A1200, A2000, A3000, A4000 and CD32
- Keyboard microcontroller emulation
- ROM detection by content
- CPU Speed "Max"
- Core options grouped in 12 categories
- Model and hardware changes applied with a reset
- Lightgun/pen (sonninnos)
- Floppy write redirect (sonninnos)
- Large HDF support (sonninnos)
- Full framerate double line interlace backport (sonninnos)
- Core-based ZIP extraction with automatic M3U generation (sonninnos)
- CDTV support (sonninnos)
- CD turbo speed backport (sonninnos)
- Touch control VKBD (sonninnos)
- File-based WHDLoad.hdf (sonninnos)
- Memory-based savestates (jdgleaver)
- Analog Joystick (sonninnos)
- SuperHires (sonninnos)
- Retroplay WHDLoad LHA support (sonninnos)
- CD32 support (sonninnos)
- WHDLoad.prefs override core option (sonninnos)
- Internal+external floppy drive sounds (sonninnos)
- Single line high resolution mode (sonninnos)
- Savestate support (sonninnos)
- Parallel port four player joystick adapter is emulated with working two, three and four-player controls (sonninnos and rsn8887)
- CD32 controller support (sonninnos)
- Multiple mice support using raw input driver on Windows (sonninnos)
- Autozoom and autocentering options for large game display without borders or distortion (sonninnos)
- Mappable hotkeys to bring up statusbar and on-screen keyboard (sonninnos)
- Analog stick mouse control (sonninnos and rsn8887)
- On screen keyboard with easy joystick/dpad control (sonninnos and rsn8887)
- Implement new core options with some small print explanations (sonninnos)
- Implement hotkeys for status bar and virtual keyboard that can be remapped instead of using hardcoded buttons (sonninnos)
- Allow starting the emulated Amiga without content (sonninnos)
- Add many core options, such as a cycle exact option (sonninnos and rsn8887)
- Allow to load any single, bootable .hdf hard drive file directly via load content (rsn8887)
- Allow disabling the use of system/WHDLoad.hdf (rsn8887)
- Fix quickmenu->disk control options for swapping disks on the fly (rsn8887)
- Change option defaults to more sensible values (sonninnos)
- Add Switch version of the core (rsn8887)
- ...
