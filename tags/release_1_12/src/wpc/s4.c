#include <stdarg.h>
#include <time.h>
#include "driver.h"
#include "cpu/m6800/m6800.h"
#include "machine/6821pia.h"
#include "core.h"
#include "sndbrd.h"
#include "wmssnd.h"
#include "s4.h"

#define S4_PIA0 0
#define S4_PIA1 1
#define S4_PIA2 2
#define S4_PIA3 3

#define S4_VBLANKFREQ    60 /* VBLANK frequency */
#define S4_IRQFREQ    1000  /* IRQ Frequency*/

static void s4_exit(void);
static void s4_init(void);

static struct {
  int	 alphapos;
  int    vblankCount;
  UINT32 solenoids;
  core_tSeg segments, pseg;
  int    lampRow, lampColumn;
  int    diagnosticLed;
  int    swCol;
  int    ssEn;
} s4locals;

static data8_t *s4_CMOS;

/*
  6 Digit Structure, Alpha Position, and Layout
  ----------------------------------------------
  (00)(01)(02)(03)(04)(05) (08)(09)(10)(11)(12)(13)

  (16)(17)(18)(19)(20)(21) (24)(25)(26)(27)(28)(29)

	   (14)(15)   (06)(97)
*/
//Structure is: top, left, start, length, type
const core_tLCDLayout s4_disp[] = {
  // Player 1            Player 2
  {0, 0, 0,6,CORE_SEG7}, {0,18, 8,6,CORE_SEG7},
  // Player 3            Player 4
  {2, 0,20,6,CORE_SEG7}, {2,18,28,6,CORE_SEG7},
  // Right Side          Left Side
  {4, 9,14,2,CORE_SEG7}, {4,14, 6,2,CORE_SEG7}, {0}
};

static void s4_nvram(void *file, int write);

static READ_HANDLER(s4_dips_r);
static READ_HANDLER(s4_entersw_r);
static READ_HANDLER(s4_swrow_r);
static WRITE_HANDLER(s4_swcol_w);
static WRITE_HANDLER(s4_alpha_w);
static WRITE_HANDLER(s4_pa_w);
static WRITE_HANDLER(s4_specsol6_w);
static WRITE_HANDLER(s4_specsol4_w);
static WRITE_HANDLER(s4_specsol3_w);
static WRITE_HANDLER(s4_lampcol_w);
static WRITE_HANDLER(s4_lamprow_w);
static WRITE_HANDLER(s4_specsol2_w);
static WRITE_HANDLER(s4_specsol1_w);
static WRITE_HANDLER(s4_sol1_8_w);
static WRITE_HANDLER(s4_sol9_16_w);
static WRITE_HANDLER(s4_specsol5_w);
static WRITE_HANDLER(s4_gameon_w);

static void s4_irqline(int state) {
  if (state) {
    cpu_set_irq_line(S4_CPUNO, M6800_IRQ_LINE, ASSERT_LINE);
    pia_set_input_ca1(S4_PIA0, core_getSw(S4_SWADVANCE));
    pia_set_input_cb1(S4_PIA0, core_getSw(S4_SWUPDN));
  }
  else {
    cpu_set_irq_line(S4_CPUNO, M6800_IRQ_LINE, CLEAR_LINE);
    pia_set_input_ca1(S4_PIA0, 0);
    pia_set_input_cb1(S4_PIA0, 0);
  }
}

static int s4_irq(void) {
  s4_irqline(1);
  timer_set(TIME_IN_CYCLES(32,0),S4_CPUNO,s4_irqline);
  return 0;
}

/*********************/
/*DIPS SWITCHES?     */
/*********************/
/*Dips are returned to a4-7 and are read by pulsing of a0-3 (same as alpapos).
  Dips are matrixed.. Column 0 = Data Switches 1-4, Column 1 = Data Switches 5-8..
  		      Column 2 = Function Switches 1-4, Column 3 = Function Switches 5-8
 */
static READ_HANDLER(s4_dips_r) {
  /* 0x01=0, 0x02=1, 0x04=2, 0x08=3 */
  int dipcol = (s4locals.alphapos & 0x0c) ? ((s4locals.alphapos>>2)+1) :
                                              s4locals.alphapos>>1;
  return (core_getDip(dipcol/2+1)<<(4*(1-(dipcol&0x01)))) & 0xf0;
}

/************/
/* SWITCHES */
/************/
static READ_HANDLER(s4_entersw_r) { return core_getSw(S4_ENTER); }
/* SWITCH MATRIX */
static READ_HANDLER(s4_swrow_r) { return core_getSwCol(s4locals.swCol); }
static WRITE_HANDLER(s4_swcol_w) { s4locals.swCol = data; }

/*****************/
/*DISPLAY ALPHA  */
/*****************/
static WRITE_HANDLER(s4_alpha_w) {
  s4locals.segments[0][s4locals.alphapos].lo |=
    s4locals.pseg[0][s4locals.alphapos].lo = core_bcd2seg[data>>4];
  s4locals.segments[1][s4locals.alphapos].lo |=
    s4locals.pseg[1][s4locals.alphapos].lo = core_bcd2seg[data&0x0f];
}

static WRITE_HANDLER(s4_pa_w) {
  /* DIAG LED IS PA-4 & PA-5*/
  s4locals.diagnosticLed = ((data>>3) & 0x02) | ((data>>5) & 0x01);
  /*Blanking? PA-2*/
  /* Alpha position/Dip */
  s4locals.alphapos = data & 0x0f;
}

/*****************/
/* LAMP HANDLING */
/*****************/
static WRITE_HANDLER(s4_lampcol_w) { core_setLamp(coreGlobals.tmpLampMatrix, s4locals.lampColumn = data, s4locals.lampRow); }
static WRITE_HANDLER(s4_lamprow_w) { core_setLamp(coreGlobals.tmpLampMatrix, s4locals.lampColumn, s4locals.lampRow = ~data); }

/**************************/
/* REGULAR SOLENOIDS #1-8 */
/**************************/
static WRITE_HANDLER(s4_sol1_8_w) {
  coreGlobals.pulsedSolState = (coreGlobals.pulsedSolState & 0xffffff00) | data;
  s4locals.solenoids |= data;
}

/***********************************************************/
/* REGULAR SOLENOIDS #9-16 - USED AS SOUND COMMANDS! (9-14)*/
/***********************************************************/
static WRITE_HANDLER(s4_sol9_16_w) {
  if (!(core_gameData->gen & GEN_S3C)) { sndbrd_0_data_w(0, ~data); data &= 0xe0; }
  coreGlobals.pulsedSolState = (coreGlobals.pulsedSolState & 0xffff00ff) | (data<<8);
  s4locals.solenoids |= (data<<8);
}

/**********************************/
/* SPECIAL SOLENOID CONTROL #1-#6 */
/**********************************/
static void setSSSol(int data, int solNo) {
  int bit = CORE_SOLBIT(CORE_FIRSTSSSOL + solNo);
  if (s4locals.ssEn & (~data & 1)) { coreGlobals.pulsedSolState |= bit;  s4locals.solenoids |= bit; }
  else                               coreGlobals.pulsedSolState &= ~bit;
}
static WRITE_HANDLER(s4_specsol1_w) { setSSSol(data, 0); }
static WRITE_HANDLER(s4_specsol2_w) { setSSSol(data, 1); }
static WRITE_HANDLER(s4_specsol3_w) { setSSSol(data, 2); }
static WRITE_HANDLER(s4_specsol4_w) { setSSSol(data, 3); }
static WRITE_HANDLER(s4_specsol5_w) { setSSSol(data, 4); }
static WRITE_HANDLER(s4_specsol6_w) { setSSSol(data, 5); }

/*******************/
/* GAME ON SIGNAL? */
/*******************/
WRITE_HANDLER(s4_gameon_w) { s4locals.ssEn = data; }

static const struct pia6821_interface s4_pia[] = {{
/*PIA 1 - Display and other*/
/*PIA I:
(out)	PA0-3: 	16 Digit Strobing.. 1 # activates a digit.
(out)	PA4-5:  Output LED's - When CA2 Output = 1
(in)	PA4-7: 	16 Functional Switches (Dips?)
(out)	PB0-7: 	BCD1&BCD2 Outputs..
(in)	CA1:	Diagnostic Switch - On interrupt: Advance Switch
(in)	CB1:	Diagnostic Switch - On interrupt: Auto/Manual Switch
(in)	CA2:   	Read Master Command Enter Switch
(out)	CB2:   	Special Solenoid #6

	Display: 5 x (7 Segment LEDS) (Player 1-4 & Status)
	BCD1: Feeds Player 1, 2, and Status
	BCD2: Feeds Player 3, 4
	Alpha Pos (L-R): 1,2,3,4,5,6
	Strobing: 1-6 = (P1&3)1-6
		  7,8,16 = (Status)5,6,(2&3)
		  9-14 = (P2&4)1-6
*/
 /* i : A/B,CA1/B1,CA2/B2 */ s4_dips_r, 0, 0,0, s4_entersw_r, 0,
 /* o : A/B,CA2/B2        */ s4_pa_w, s4_alpha_w, 0, s4_specsol6_w,
 /* irq: A/B              */ 0, 0
},{
/*PIA 2 - Switch Matrix*/
/*PIA II:
(in)	PA0-7:	Switch Rows
(in)	PB0-7:	Switch Columns??
(out)	PB0-7:  Switch Column Enable
	CA1:	None
	CB1:	None
(out)	CA2:	Special Solenoid #4
(out)	CB2:	Special Solenoid #3
*/
  /* i : A/B,CA1/B1,CA2/B2 */ s4_swrow_r, 0, 0, 0, 0, 0,
  /* o : A/B,CA2/B2        */ 0, s4_swcol_w, s4_specsol4_w, s4_specsol3_w,
  /* irq: A/B             */  0, 0
},{
/*PIA 3 - Lamp Matrix*/
/*
PIA III:
(out)	PA0-7:	Lamp Rows
(out)	PB0-7:	Lamp Columns
(in)    PB0-7:  Lamp Column (input????!!)
	CA1:	None
	CB1:	None
(out)	CA2:	Special Solenoid #2
(out)	CB2:	Special Solenoid #1
*/
  /* i : A/B,CA1/B1,CA2/B2 */ 0, 0, 0, 0, 0, 0,
  /* o : A/B,CA2/B2        */ s4_lamprow_w, s4_lampcol_w, s4_specsol2_w,s4_specsol1_w,
  /* irq: A/B              */ 0, 0
},{
/*PIA 4 - Standard Solenoids*/
/*
PIA IV:
	PA0-7:	Solenoids 1-8
	PB0-7:	Solenoids 9-16 - Sound Commands!
	CA1:	None
	CB1:	None
	CA2:	Special Solenoid #5
	CB2:	Game On Signal
*/
  /* i : A/B,CA1/B1,CA2/B2 */ 0, 0, 0, 0, 0, 0,
  /* o : A/B,CA2/B2        */ s4_sol1_8_w, s4_sol9_16_w,s4_specsol5_w, s4_gameon_w,
  /* irq: A/B              */ 0, 0
}};

static int s4_vblank(void) {
  /*-------------------------------
  /  copy local data to interface
  /--------------------------------*/
  s4locals.vblankCount += 1;

  /*-- lamps --*/
  if ((s4locals.vblankCount % S4_LAMPSMOOTH) == 0) {
    memcpy(coreGlobals.lampMatrix, coreGlobals.tmpLampMatrix, sizeof(coreGlobals.tmpLampMatrix));
    memset(coreGlobals.tmpLampMatrix, 0, sizeof(coreGlobals.tmpLampMatrix));
  }

  /*-- solenoids --*/
  if ((s4locals.vblankCount % S4_SOLSMOOTH) == 0) {
    coreGlobals.solenoids = s4locals.solenoids;
    if (s4locals.ssEn) {
      int ii;
      coreGlobals.solenoids |= CORE_SOLBIT(CORE_SSFLIPENSOL);
      /*-- special solenoids updated based on switches --*/
      for (ii = 0; ii < 6; ii++)
        if (core_gameData->sxx.ssSw[ii] && core_getSw(core_gameData->sxx.ssSw[ii]))
          coreGlobals.solenoids |= CORE_SOLBIT(CORE_FIRSTSSSOL+ii);
    }
    s4locals.solenoids = coreGlobals.pulsedSolState;
  }
  /*-- display --*/
  if ((s4locals.vblankCount % S4_DISPLAYSMOOTH) == 0) {
    memcpy(coreGlobals.segments, s4locals.segments, sizeof(coreGlobals.segments));
    memcpy(s4locals.segments, s4locals.pseg, sizeof(s4locals.segments));

    /*update leds*/
    coreGlobals.diagnosticLed = s4locals.diagnosticLed;
    s4locals.diagnosticLed = 0;

  }
  core_updateSw(s4locals.ssEn);
  return 0;
}

static void s4_updSw(int *inports) {
  if (inports) {
    coreGlobals.swMatrix[1] = inports[S4_COMINPORT] & 0x00ff;
    coreGlobals.swMatrix[0] = (inports[S4_COMINPORT] & 0xff00)>>8;
  }
  /*-- Diagnostic buttons on CPU board --*/
  cpu_set_nmi_line(0, core_getSw(S4_SWCPUDIAG) ? ASSERT_LINE : CLEAR_LINE);
  sndbrd_0_diag(core_getSw(S4_SWSOUNDDIAG));

  /* Show Status of Auto/Manual Switch */
  core_textOutf(40, 30, BLACK, core_getSw(S4_SWUPDN) ? "Auto  " : "Manual");
}

static const core_tData s4Data = {
  8+16, /* 16 Dips */
  s4_updSw,
  2 | DIAGLED_VERTICAL,
  sndbrd_0_data_w, "s4",
  core_swSeq2m, core_swSeq2m,core_m2swSeq,core_m2swSeq
};

static void s4_init(void) {
  memset(&s4locals, 0, sizeof(s4locals));
  if (core_init(&s4Data)) return;
  pia_config(S4_PIA0, PIA_STANDARD_ORDERING, &s4_pia[0]);
  pia_config(S4_PIA1, PIA_STANDARD_ORDERING, &s4_pia[1]);
  pia_config(S4_PIA2, PIA_STANDARD_ORDERING, &s4_pia[2]);
  pia_config(S4_PIA3, PIA_STANDARD_ORDERING, &s4_pia[3]);
  if (core_gameData->gen & GEN_S3C)
    sndbrd_0_init(SNDBRD_NONE, 0, NULL, NULL, NULL);
  else
    sndbrd_0_init(SNDBRD_S67S, 1, NULL, NULL, NULL);
  pia_reset();
  s4locals.vblankCount = 1;
}

static void s4_exit(void) {
  sndbrd_0_exit(); core_exit();
}

static WRITE_HANDLER(s4_CMOS_w) { s4_CMOS[offset] = data | 0xf0; }

/*-----------------------------------
/  Memory map for Sys 4 CPU board
/------------------------------------*/
static MEMORY_READ_START(s4_readmem)
  { 0x0000, 0x01ff, MRA_RAM},
  { 0x2200, 0x2203, pia_r(S4_PIA3) },		/*Solenoids + Sound Commands*/
  { 0x2400, 0x2403, pia_r(S4_PIA2) }, 		/*Lamps*/
  { 0x2800, 0x2803, pia_r(S4_PIA0) },		/*Display + Other*/
  { 0x3000, 0x3003, pia_r(S4_PIA1) },		/*Switches*/
  { 0x6000, 0x8000, MRA_ROM },
  { 0x8000, 0xffff, MRA_ROM },		/*Doubled ROM region since only 15 address pins used!*/
MEMORY_END

static MEMORY_WRITE_START(s4_writemem)
  { 0x0000, 0x00ff, MWA_RAM },
  { 0x0100, 0x01ff, s4_CMOS_w, &s4_CMOS },
  { 0x2200, 0x2203, pia_w(S4_PIA3) },		/*Solenoids + Sound Commands*/
  { 0x2400, 0x2403, pia_w(S4_PIA2) }, 		/*Lamps*/
  { 0x2800, 0x2803, pia_w(S4_PIA0) },		/*Display + Other*/
  { 0x3000, 0x3003, pia_w(S4_PIA1) },		/*Switches*/
  { 0x6000, 0x8000, MWA_ROM },
  { 0x8000, 0xffff, MWA_ROM },		/*Doubled ROM region since only 15 address pins used!*/
MEMORY_END

//Special System 3 Machine for games using Chimes instead of Solid State Sound Hardware
const struct MachineDriver machine_driver_s3c = {
  {{  CPU_M6800, 3580000/4, /* 3.58/4 = 900hz */
      s4_readmem, s4_writemem, NULL, NULL,
      s4_vblank, 1, s4_irq, S4_IRQFREQ
  }},
  S4_VBLANKFREQ, DEFAULT_60HZ_VBLANK_DURATION,
  50,
  s4_init, s4_exit,
  CORE_SCREENX, CORE_SCREENY, { 0, CORE_SCREENX-1, 0, CORE_SCREENY-1 },
  0, sizeof(core_palette)/sizeof(core_palette[0][0])/3, 0, core_initpalette,
  VIDEO_TYPE_RASTER, 0,
  NULL, NULL, gen_refresh,
  0,0,0,0, {{0}},
  s4_nvram
};

const struct MachineDriver machine_driver_s4 = {
  {{  CPU_M6800, 3580000/4, /* 3.58/4 = 900hz */
      s4_readmem, s4_writemem, NULL, NULL,
      s4_vblank, 1, s4_irq, S4_IRQFREQ
  }},
  S4_VBLANKFREQ, DEFAULT_60HZ_VBLANK_DURATION,
  50,
  s4_init, s4_exit,
  CORE_SCREENX, CORE_SCREENY, { 0, CORE_SCREENX-1, 0, CORE_SCREENY-1 },
  0, sizeof(core_palette)/sizeof(core_palette[0][0])/3, 0, core_initpalette,
  VIDEO_TYPE_RASTER, 0,
  NULL, NULL, gen_refresh,
  0,0,0,0, {{0}},
  s4_nvram
};

const struct MachineDriver machine_driver_s4s= {
  {{ CPU_M6800, 3580000/4, /* 3.58/4 = 900hz */
     s4_readmem, s4_writemem, NULL, NULL,
     s4_vblank, 1, s4_irq, S4_IRQFREQ
   },S67S_SOUNDCPU
  },
  S4_VBLANKFREQ, DEFAULT_60HZ_VBLANK_DURATION,
  50,
  s4_init, s4_exit,
  CORE_SCREENX, CORE_SCREENY, { 0, CORE_SCREENX-1, 0, CORE_SCREENY-1 },
  0, sizeof(core_palette)/sizeof(core_palette[0][0])/3, 0, core_initpalette,
  VIDEO_TYPE_RASTER, 0,
  NULL, NULL, gen_refresh,
  0,0,0,0, { S67S_SOUND },
  s4_nvram
};

/*-----------------------------------------------
/ Load/Save static ram
/-------------------------------------------------*/
static void s4_nvram(void *file, int write) {
  core_nvram(file, write, s4_CMOS, 0x100, 0xff);
}
