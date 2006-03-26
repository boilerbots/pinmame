/************************************************************************************************
 Playmatic (Spain)
 -----------------

 Playmatic is a nightmare to maintain! Plain and simple.
 They did use at least four, if not fife different hardware generations over their years,
 and they're not interchangable at any point or time.
 The earliest games (like "Chance" from 1978) used a rough 400 kHz clock for the ancient
 CDP 1802 CPU (generated by an R/C combo), and had the IRQ hard-wired to zero cross detection.
 The 2nd genaration used a clock chip of 2.95 MHz and an IRQ of about 360 Hz.
 The 3rd generation had the same value, but completely re-wired all output circuits!
 The 4th genaration used a generic 3.58 MHz NTSC quartz.
 Sound started out with 4 simple tones (with fading), and evolved through an CPU-driven
 oscillator circuit on to a complete sound board with another 1802 CPU.

   Hardware:
   ---------
		CPU:	 CDP1802 @ various frequencies (various IRQ freq's as well)
		DISPLAY: 5 rows of 7-segment LED panels, direct segment access
		SOUND:	 - discrete (4 tones, like Zaccaria's 1311)
				 - simple noise and tone genarator with varying frequencies
				 - CDP1802 @ NTSC clock with 2 x AY8910 @ NTSC/2 for later games
 ************************************************************************************************/

#include "driver.h"
#include "core.h"
#include "play.h"
#include "cpu/cdp1802/cdp1802.h"
#include "sound/ay8910.h"

#define PLAYMATIC_VBLANKFREQ   60 /* VBLANK frequency */
#define NTSC_QUARTZ 3579545.0 /* 3.58 MHz quartz */

enum { COLUMN=1, DISPLAY, SOUND, SWITCH, DIAG, LAMP, SOL, UNKNOWN };

/*----------------
/  Local variables
/-----------------*/
static struct {
  int    vblankCount;
  UINT32 solenoids;
  UINT8  sndCmd;
  int    ef[5];
  int    digitSel;
  int    panelSel;
  int    cpuType;
} locals;

static INTERRUPT_GEN(PLAYMATIC_irq) {
  cpu_set_irq_line(PLAYMATIC_CPU, 0, PULSE_LINE);
}

static void PLAYMATIC_zeroCross(int data) {
  static int zc = 0;
  static int state = 0;
  locals.ef[3] = (zc = !zc);
  if (zc) {
    locals.ef[1] = state;
    locals.ef[2] = !state;
    state = !state;
  }
}

/*-------------------------------
/  copy local data to interface
/--------------------------------*/
static INTERRUPT_GEN(PLAYMATIC_vblank) {
  locals.vblankCount++;

  /*-- lamps --*/
  if ((locals.vblankCount % PLAYMATIC_LAMPSMOOTH) == 0)
    memcpy(coreGlobals.lampMatrix, coreGlobals.tmpLampMatrix, sizeof(coreGlobals.tmpLampMatrix));
  /*-- solenoids --*/
  coreGlobals.solenoids = locals.solenoids;
  if ((locals.vblankCount % PLAYMATIC_SOLSMOOTH) == 0)
  	locals.solenoids = 0;

  core_updateSw(TRUE);
}

static SWITCH_UPDATE(PLAYMATIC) {
  if (inports) {
    CORE_SETKEYSW(inports[CORE_COREINPORT], 0xff, 0);
  }
  locals.ef[4] = (coreGlobals.swMatrix[7] & 1) ? 0 : 1; // test button
}

static READ_HANDLER(sw_r) {
  return coreGlobals.swMatrix[offset+1];
}

static WRITE_HANDLER(lamp_w) {
  coreGlobals.tmpLampMatrix[offset] = data;
}

static WRITE_HANDLER(disp_w) {
  coreGlobals.segments[offset].w = data;
}

static WRITE_HANDLER(sol_w) {
  locals.solenoids |= data;
}

static int bitColToNum(int tmp)
{
	int i, data;
	i = data = 0;
	while(tmp)
	{
		if(tmp&1) data=i;
		tmp = tmp>>1;
		i++;
	}
	return data;
}

static void dma(int cycles) { /* not used */ }
static void out_n(int data, int n) {
  static int outports[4][8] =
    {{ COLUMN, DISPLAY, SOUND, SWITCH, DIAG, LAMP, SOL, UNKNOWN },
     { COLUMN, DISPLAY, SOUND, SWITCH, DIAG, LAMP, SOL, UNKNOWN },
     { COLUMN, LAMP, SOL, SWITCH, DIAG, DISPLAY, SOUND, UNKNOWN },
     { COLUMN, LAMP, SOL, SWITCH, DIAG, DISPLAY, SOUND, UNKNOWN }};
  const int out = outports[locals.cpuType][n-1];
  switch (out) {
    case COLUMN:
      if (!(data & 0x7f))
        locals.panelSel = 0;
      else
        locals.digitSel = bitColToNum(data & 0x7f);
      coreGlobals.diagnosticLed = data >> 7;
      break;
    case DISPLAY:
      disp_w(8 * (locals.panelSel++) + locals.digitSel, data);
      break;
    case SOUND:
      if (locals.cpuType > 1) {
        cpu_set_irq_line(PLAYMATIC_SCPU, 0, PULSE_LINE);
      }
      break;
    case LAMP:
      lamp_w(0, data);
      break;
    case SOL:
      lamp_w(1, data);
      break;
  }
  logerror("out_n %d:%02x\n", n, data);
}
static int in_n(int n) {
  switch (n) {
    case SWITCH:
      return (UINT8)coreGlobals.swMatrix[locals.digitSel + 1] ^ (UINT8)(rand() % 256);
      break;
    case DIAG:
      return (UINT8)coreGlobals.swMatrix[0] ^ (UINT8)(rand() % 256);
  }
  return 0;
}
static void out_q(int level) { /* connected to RST1 pin of flip flop U2 */ }
static int in_ef(void) { return locals.ef[1] | (locals.ef[2] << 1) | (locals.ef[3] << 2) | (locals.ef[4] << 3); }

static CDP1802_CONFIG play1802_config= { dma, out_n, in_n, out_q, in_ef };

static MACHINE_INIT(PLAYMATIC) {
  memset(&locals, 0, sizeof locals);
}

static MACHINE_INIT(PLAYMATIC2) {
  memset(&locals, 0, sizeof locals);
  locals.cpuType = 1;
}

static MACHINE_INIT(PLAYMATIC3) {
  memset(&locals, 0, sizeof locals);
  locals.cpuType = 2;
}

static MACHINE_INIT(PLAYMATIC4) {
  memset(&locals, 0, sizeof locals);
  locals.cpuType = 3;
}

/*-----------------------------------------------
/ Load/Save static ram
/-------------------------------------------------*/
static MEMORY_READ_START(PLAYMATIC_readmem)
  {0x0000,0x1fff, MRA_ROM},
  {0x2000,0x20ff, MRA_RAM},
  {0xa000,0xafff, MRA_ROM},
MEMORY_END

static MEMORY_WRITE_START(PLAYMATIC_writemem)
  {0x0000,0x00ff, MWA_NOP},
  {0x2000,0x20ff, MWA_RAM, &generic_nvram, &generic_nvram_size},
MEMORY_END

static MEMORY_READ_START(PLAYMATIC_readmem3)
  {0x0000,0x7fff, MRA_ROM},
  {0x8000,0x80ff, MRA_RAM},
MEMORY_END

static MEMORY_WRITE_START(PLAYMATIC_writemem3)
  {0x0000,0x00ff, MWA_NOP},
  {0x8000,0x80ff, MWA_RAM, &generic_nvram, &generic_nvram_size},
MEMORY_END

DISCRETE_SOUND_START(play_tones)
	DISCRETE_INPUT(NODE_01,1,0x000f,0)                         // Input handlers, mostly for enable
	DISCRETE_INPUT(NODE_02,2,0x000f,0)
	DISCRETE_INPUT(NODE_04,4,0x000f,0)
	DISCRETE_INPUT(NODE_08,8,0x000f,0)

	DISCRETE_SAWTOOTHWAVE(NODE_10,NODE_01,523,50000,10000,0,0) // C' note
	DISCRETE_SAWTOOTHWAVE(NODE_20,NODE_02,659,50000,10000,0,0) // E' note
	DISCRETE_SAWTOOTHWAVE(NODE_30,NODE_04,784,50000,10000,0,0) // G' note
	DISCRETE_SAWTOOTHWAVE(NODE_40,NODE_08,988,50000,10000,0,0) // H' note

	DISCRETE_ADDER4(NODE_50,1,NODE_10,NODE_20,NODE_30,NODE_40) // Mix all four sound sources

	DISCRETE_OUTPUT(NODE_50, 50)                               // Take the output from the mixer
DISCRETE_SOUND_END

MACHINE_DRIVER_START(PLAYMATIC)
  MDRV_IMPORT_FROM(PinMAME)
  MDRV_CPU_ADD_TAG("mcpu", CDP1802, 400000)
  MDRV_CPU_MEMORY(PLAYMATIC_readmem, PLAYMATIC_writemem)
  MDRV_CPU_CONFIG(play1802_config)
  MDRV_CPU_PERIODIC_INT(PLAYMATIC_irq, 100)
  MDRV_TIMER_ADD(PLAYMATIC_zeroCross, 100)
  MDRV_CPU_VBLANK_INT(PLAYMATIC_vblank, 1)
  MDRV_CORE_INIT_RESET_STOP(PLAYMATIC,NULL,NULL)
  MDRV_SWITCH_UPDATE(PLAYMATIC)
  MDRV_DIPS(0)
  MDRV_DIAGNOSTIC_LEDH(1)
//  MDRV_NVRAM_HANDLER(generic_0fill)

  MDRV_SOUND_ADD(DISCRETE, play_tones)
MACHINE_DRIVER_END

MACHINE_DRIVER_START(PLAYMATIC2)
  MDRV_IMPORT_FROM(PinMAME)
  MDRV_CPU_ADD_TAG("mcpu", CDP1802, 2950000.0/8.0)
  MDRV_CPU_MEMORY(PLAYMATIC_readmem, PLAYMATIC_writemem)
  MDRV_CPU_CONFIG(play1802_config)
  MDRV_CPU_PERIODIC_INT(PLAYMATIC_irq, 2950000.0/4096.0)
  MDRV_TIMER_ADD(PLAYMATIC_zeroCross, 100)
  MDRV_CPU_VBLANK_INT(PLAYMATIC_vblank, 1)
  MDRV_CORE_INIT_RESET_STOP(PLAYMATIC2,NULL,NULL)
  MDRV_SWITCH_UPDATE(PLAYMATIC)
  MDRV_DIPS(0)
  MDRV_DIAGNOSTIC_LEDH(1)
  MDRV_NVRAM_HANDLER(generic_0fill)

  MDRV_SOUND_ADD(DISCRETE, play_tones)
MACHINE_DRIVER_END

// electronic sound section

static READ_HANDLER(ay8910_0_porta_r)	{ return 0; }
static READ_HANDLER(ay8910_1_porta_r)	{ return 0; }

struct AY8910interface play_ay8910 = {
	2,			/* 2 chips */
	NTSC_QUARTZ/2,	/* 1.79 MHz */
	{ 30, 30 },		/* Volume */
	{ ay8910_0_porta_r, ay8910_1_porta_r }
};

static WRITE_HANDLER(ay0_w) {
	AY8910Write(0,offset % 2,data);
}

static WRITE_HANDLER(ay1_w) {
	AY8910Write(1,offset % 2,data);
}

static MEMORY_READ_START(playsound_readmem)
  {0x0000,0x1fff, MRA_ROM},
  {0x2000,0x2fff, MRA_ROM},
  {0x8000,0x80ff, MRA_RAM},
  {0xa000,0xbfff, MRA_ROM},
MEMORY_END

static MEMORY_WRITE_START(playsound_writemem)
  {0x0000,0x00ff, MWA_NOP},
  {0x4000,0x4fff, ay0_w},
  {0x6000,0x6fff, ay1_w},
  {0x8000,0x80ff, MWA_RAM},
MEMORY_END

MACHINE_DRIVER_START(PLAYMATIC2S)
  MDRV_IMPORT_FROM(PLAYMATIC2)

  MDRV_CPU_ADD_TAG("scpu", CDP1802, 2950000.0/8.0)
  MDRV_CPU_FLAGS(CPU_AUDIO_CPU)
  MDRV_CPU_MEMORY(playsound_readmem, playsound_writemem)
  MDRV_SOUND_ADD(AY8910, play_ay8910)
MACHINE_DRIVER_END

MACHINE_DRIVER_START(PLAYMATIC3S)
  MDRV_IMPORT_FROM(PLAYMATIC2S)
  MDRV_CORE_INIT_RESET_STOP(PLAYMATIC3,NULL,NULL)
  MDRV_CPU_MODIFY("mcpu");
  MDRV_CPU_MEMORY(PLAYMATIC_readmem3, PLAYMATIC_writemem3)
MACHINE_DRIVER_END

MACHINE_DRIVER_START(PLAYMATIC4S)
  MDRV_IMPORT_FROM(PLAYMATIC3S)
  MDRV_CORE_INIT_RESET_STOP(PLAYMATIC4,NULL,NULL)
  MDRV_CPU_REPLACE("mcpu", CDP1802, NTSC_QUARTZ/8.0)
  MDRV_CPU_PERIODIC_INT(PLAYMATIC_irq, NTSC_QUARTZ/4096.0)

  MDRV_CPU_REPLACE("scpu", CDP1802, NTSC_QUARTZ/8.0)
MACHINE_DRIVER_END
