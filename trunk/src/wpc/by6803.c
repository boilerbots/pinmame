/* Bally MPU-6803 Hardware
  6809 vectors:
  RES: FFFE-F
  NMI: FFFC-D (Not Used)
  IRQ: FFF8-9
  ICF: FFF6-7 (Input Capture)~IRQ2
  OCF: FFF4-5 (Output Compare)~IRQ2
  TOF: FFF2-3 (Timer Overflow)~IRQ2 (Not Used)
  SCI: FFF0-1 (Input Capture)~IRQ2  (Not Used)


  Notes:
  It is almost identical to the Bally -35 hardware with the exception of a 6803 CPU and that it addresses the segments directly on the display. 
  But here is what Clive writes: 
  
  You see Bally came up with this neat idea: 
  Take the xero-crossing (which occurs 120 times/sec for a 60Hz AC wave, and 
  generate two 180 degree phase shifted signals - each occuring 60 times/sec. 
  They then used this principle to generate two seperate phase controllers. 
  The first phase controller had phases A and B for the feature lamps, and the 
  second had phases C and D for the solenoids/flashers. 
  The two phase controllers were totally independant of each other 
  (tapped from different secondary windings). 
  This way, Bally were able to wire two feature lamps or two solenoids to the
  same driver circuit and toggle them on different phases. 
  For instance, the Extra ball lamp and the Multi-ball lamp could be wired to the 
  same driver circuit. The EB lamp would be "on" for phase A and "off" for phase B, 
  the Multi-ball lamp would be "off" for phase A but "on" for phase B. 
  The driver transistor was obviously on for both phases. 
  Bally where able to double their lamps and coils/flashers by using the same 
  hardware without resorting to auxillary lamp or solenoid driver boards. 
  Simple...but brilliant. 

  Display:
  9 Segment, 14 Digit X 2 Rows
  Sound Hardware:
  Turbo Cheap Squeak (6809,6821,DAC)
  Sounds Deluxe (68000,6821,PAL,DAC)

*/
#include <stdarg.h>
#include <time.h>
#include "driver.h"
#include "cpu/m6800/m6800.h"
#include "machine/6821pia.h"
#include "core.h"
#include "by6803.h"

#define BY6803_VBLANKFREQ    60 /* VBLANK frequency */
#define BY6803_IRQFREQ      150 /* IRQ (via PIA) frequency*/
#define BY6803_ZCFREQ        85 /* Zero cross frequency (PHASE A)*/

#define mlogerror printf

static struct {
  int a0, a1, b1, ca20, ca21, cb20, cb21;
  int bcd[6];
  int lampadr1, lampadr2;
  UINT32 solenoids;
  core_tSeg segments,pseg;
  int    diagnosticLed;
  int vblankCount;
  int initDone;
  int phase_a;
  void *zctimer;
} locals;

static void by6803_exit(void);
static void by6803_nvram(void *file, int write);

static void piaIrq(int state) {
  //DBGLOG(("IRQ = %d\n",state));
  cpu_set_irq_line(0, M6803_IRQ_LINE, state ? ASSERT_LINE : CLEAR_LINE);
}

static void by6803_dispStrobe(int mask) {
  int digit = locals.a1 & 0xfe;
  int ii,jj;
  //DBGLOG(("digit = %x (%x,%x,%x,%x,%x,%x)\n",digit,locals.bcd[0],locals.bcd[1],locals.bcd[2],locals.bcd[3],locals.bcd[4],locals.bcd[5]));
  for (ii = 0; digit; ii++, digit>>=1)
    if (digit & 0x01) {
      UINT8 dispMask = mask;
      for (jj = 0; dispMask; jj++, dispMask>>=1)
        if (dispMask & 0x01)
          ((int *)locals.segments)[jj*8+ii] |= ((int *)locals.pseg)[jj*8+ii] = core_bcd2seg[locals.bcd[jj]];
    }
}

static void by6803_lampStrobe(int board, int lampadr) {
  if (lampadr != 0x0f) {
    int lampdata = (locals.a0>>4)^0x0f;
    UINT8 *matrix = &coreGlobals.tmpLampMatrix[(lampadr>>3)+8*board];
    int bit = 1<<(lampadr & 0x07);

    //DBGLOG(("adr=%x data=%x\n",lampadr,lampdata));
    while (lampdata) {
      if (lampdata & 0x01) *matrix |= bit;
      lampdata >>= 1; matrix += 2;
    }
  }
}

/* PIA0:A-W  Control what is read from PIA0:B */
static WRITE_HANDLER(pia0a_w) {
  if (!locals.ca20) {
    int bcdLoad = locals.a0 & ~data & 0x0f;
    int ii;

    for (ii = 0; bcdLoad; ii++, bcdLoad>>=1)
      if (bcdLoad & 0x01) locals.bcd[ii] = data>>4;
  }
  locals.a0 = data;
  by6803_lampStrobe(0,locals.lampadr1);
  if (core_gameData->hw.lampCol > 0) by6803_lampStrobe(1,locals.lampadr2);
}
/* PIA1:A-W  0,2-7 Display handling */
/*        W  1     Sound E */
static WRITE_HANDLER(pia1a_w) {
  int tmp = locals.a1;
  locals.a1 = data;

  //by6803_soundCmd(0, (locals.cb21<<5) | ((locals.a1 & 0x02)<<3) | (locals.b1 & 0x0f));

  if (!locals.ca20) {
    if (tmp & ~data & 0x01) { // Positive edge
      locals.bcd[4] = locals.a0>>4;
      by6803_dispStrobe(0x10);
    }
  }
}

/* PIA0:B-R  Switch & Cabinet Returns */
static READ_HANDLER(pia0b_r) {
  return core_getSwCol((locals.a0 & 0x1f) | ((locals.b1 & 0x80)>>2));
}

/* PIA0:CB2-W Lamp Strobe #1, DIPBank3 STROBE */
static WRITE_HANDLER(pia0cb2_w) {
  //DBGLOG(("PIA0:CB2=%d PC=%4x\n",data,cpu_get_pc()));
  if (locals.cb20 & ~data) locals.lampadr1 = locals.a0 & 0x0f;
  locals.cb20 = data;
}
/* PIA1:CA2-W Lamp Strobe #2 */
static WRITE_HANDLER(pia1ca2_w) {
  //DBGLOG(("PIA1:CA2=%d\n",data));
  if (locals.ca21 & ~data) locals.lampadr2 = locals.a0 & 0x0f;
  locals.diagnosticLed = data;
  locals.ca21 = data;
}

/* PIA0:CA2-W Display Strobe */
static WRITE_HANDLER(pia0ca2_w) {
  //DBGLOG(("PIA0:CA2=%d\n",data));
  locals.ca20 = data;
  if (!data) by6803_dispStrobe(0x1f);
}

/* PIA1:B-W Solenoid/Sound output */
static WRITE_HANDLER(pia1b_w) {
  // m_mpac got a 6th display connected to solenoid 20
  if (~locals.b1 & data & 0x80) { locals.bcd[5] = locals.a0>>4; by6803_dispStrobe(0x20); }
  locals.b1 = data;

  //by6803_soundCmd(0, (locals.cb21<<5) | ((locals.a1 & 0x02)<<3) | (data & 0x0f));
  coreGlobals.pulsedSolState = 0;
  if (!locals.cb21)
    locals.solenoids |= coreGlobals.pulsedSolState = (1<<(data & 0x0f)) & 0x7fff;
  data ^= 0xf0;
  coreGlobals.pulsedSolState = (coreGlobals.pulsedSolState & 0xfff0ffff) | ((data & 0xf0)<<12);
  locals.solenoids |= (data & 0xf0)<<12;

  //DBGLOG(("PIA1:bw=%d\n",data));
}

/* PIA1:CB2-W Solenoid/Sound select */
static WRITE_HANDLER(pia1cb2_w) {
  //DBGLOG(("PIA1:CB2=%d\n",data));
  locals.cb21 = data;
  //by6803_soundCmd(0, (locals.cb21<<5) | ((locals.a1 & 0x02)<<3) | (locals.b1 & 0x0f));
}

static int by6803_vblank(void) {
  /*-------------------------------
  /  copy local data to interface
  /--------------------------------*/
  locals.vblankCount += 1;

  /*-- lamps --*/
  if ((locals.vblankCount % BY6803_LAMPSMOOTH) == 0) {
    memcpy(coreGlobals.lampMatrix, coreGlobals.tmpLampMatrix, sizeof(coreGlobals.tmpLampMatrix));
    memset(coreGlobals.tmpLampMatrix, 0, sizeof(coreGlobals.tmpLampMatrix));
  }

  /*-- solenoids --*/
  if ((locals.vblankCount % BY6803_SOLSMOOTH) == 0) {
    coreGlobals.solenoids = locals.solenoids;
    locals.solenoids = coreGlobals.pulsedSolState;
  }
  /*-- display --*/
  if ((locals.vblankCount % BY6803_DISPLAYSMOOTH) == 0) {
    memcpy(coreGlobals.segments, locals.segments, sizeof(coreGlobals.segments));
    memcpy(locals.segments, locals.pseg, sizeof(locals.segments));
    memset(locals.pseg,0,sizeof(locals.pseg));
    /*update leds*/
    coreGlobals.diagnosticLed = locals.diagnosticLed;
    locals.diagnosticLed = 0;
  }
  core_updateSw(core_getSol(19));
  return 0;
}

static void by6803_updSw(int *inports) {
  if (inports) {
    coreGlobals.swMatrix[0] = (inports[BY6803_COMINPORT]>>10) & 0x07;
    coreGlobals.swMatrix[1] = (coreGlobals.swMatrix[1] & (~0x60)) |
                              ((inports[BY6803_COMINPORT]<<5) & 0x60);
    // Adjust Coins, and Slam Tilt Switches for Stern MPU-200 Games!
    if ((core_gameData->gen & GEN_STMPU200))
      coreGlobals.swMatrix[1] = (coreGlobals.swMatrix[1] & (~0x87)) |
                                ((inports[BY6803_COMINPORT]>>2) & 0x87);
    else
      coreGlobals.swMatrix[2] = (coreGlobals.swMatrix[2] & (~0x87)) |
                                ((inports[BY6803_COMINPORT]>>2) & 0x87);
  }
  /*-- Diagnostic buttons on CPU board --*/
  if (core_getSw(BY6803_SWCPUDIAG))  cpu_set_nmi_line(0, PULSE_LINE);
  if (core_getSw(BY6803_SWSOUNDDIAG)) cpu_set_nmi_line(BY6803_SCPU1NO, PULSE_LINE);
  /*-- coin door switches --*/
  pia_set_input_ca1(0, !core_getSw(BY6803_SWSELFTEST));
}

/*
-------
 PIA 0
-------
(in)  PB0-7: Switch Returns/Rows and Cabinet Switch Returns/Rows			(SAME AS BALLY MPU35)
(in)  CA1:   Self Test Switch												(SAME AS BALLY MPU35)
(in)  CB1:   Zero Cross/Phase B Detection									(ZERO CROSS IN BALLY MPU35)
(in)  CA2:   N/A															(SAME AS BALLY MPU35)	
(in)  CB2:   N/A?
(out) PA0-3: Cabinet Switches Strobe (shared below)							(SAME AS BALLY MPU35 + 2 Extra Columns)
(out) PA0-4: Switch Strobe(Columns)											(SAME AS BALLY MPU35)
(out) PA0-3: Lamp Address (Shared with Switch Strobe)						(SAME AS BALLY MPU35)
(out) PA0-1: Display Latch Strobe											(SAME AS BALLY MPU35 - Only 2 Strobes needed instead of 4)
(out) PA4-7: BCD Lamp Data & BCD Display Data								(SAME AS BALLY MPU35)
(out) CA2:   Display Blanking/(Select?)										(SAME AS BALLY MPU35)
(out) CB2:   Lamp Strobe #1 (Drives Main Lamp Driver - Playfield Lamps)		(SAME AS BALLY MPU35)
	  IRQ:	 Wired to Main 6803 CPU IRQ.									(SAME AS BALLY MPU35)
*/
/*
-------
 PIA 1
-------
(out) PA0 = PA10 = SEG DATA H+J
(out) PA1 = J2-8 = SEG DATA A
(out) PA2 = J2-7 = SEG DATA B
(out) PA3 = J2-6 = SEG DATA C
(out) PA4 = J2-5 = SEG DATA D
(out) PA5 = J2-4 = SEG DATA E
(out) PA6 = J2-3 = SEG DATA F
(out) PA7 = J2-2 = SEG DATA G
(out) PB0 = MOMENTARY SOL DATA (4-8 Multiplexed?)
(out) PB1 = "
(out) PB2 = "
(out) PB3 = "
(out) PB4 = CONTINOUS SOL DATA(J4-1) 		(if JW9 installed)
(out) PB5 = "			(J7-4 & J6-7)	(if JW10 installed)
(out) PB6 = "			(J1-1 & J6-9 & J6-8 & J7-7 & J7-8) ?? Flipper Coil Enable?
(out) PB7 = "			(J7-3 & J8-1 & J9-10)
CA1 = N/A
CB1 = N/A
CA2 = N/A
(out)CB2 = SOLENOID BANK SELECT, 0=SOL1-8,1=SOL9-16
IRQ:	 Wired to Main 6803 CPU IRQ.
*/
static struct pia6821_interface piaIntf[] = {{
/* I:  A/B,CA1/B1,CA2/B2 */  0, pia0b_r, 0,0, 0,0,
/* O:  A/B,CA2/B2        */  pia0a_w,0, pia0ca2_w,pia0cb2_w,
/* IRQ: A/B              */  piaIrq,piaIrq
},{
/* I:  A/B,CA1/B1,CA2/B2 */  0,0, 0,0, 0,0,
/* O:  A/B,CA2/B2        */  pia1a_w,pia1b_w,0,pia1cb2_w,
/* IRQ: A/B              */  piaIrq,piaIrq
}};

static int by6803_irq(void) {
  static int last = 0;
  pia_set_input_ca1(1, last = !last);
  return 0;
}

static WRITE_HANDLER(by6803_soundCmd) { }

static core_tData by6803Data = {
  0,
  by6803_updSw, 1, by6803_soundCmd, "by6803",
  core_swSeq2m, core_swSeq2m, core_m2swSeq, core_m2swSeq
};

static void by6803_zeroCross(int data) {
  /*- toggle zero/detection circuit-*/
  int state = locals.phase_a;
  pia_set_input_cb1(0, 0); pia_set_input_cb1(0, 1);
  /*toggle phase_a*/
  locals.phase_a = !locals.phase_a;
  cpu_set_irq_line(0, M6800_TIN_LINE, state ? ASSERT_LINE : CLEAR_LINE);
}
static void by6803_init(void) {
  if (locals.initDone) CORE_DOEXIT(by6803_exit);

  if (core_init(&by6803Data)) return;
  memset(&locals, 0, sizeof(locals));

  /* init PIAs */
  pia_config(0, PIA_STANDARD_ORDERING, &piaIntf[0]);
  pia_config(1, PIA_STANDARD_ORDERING, &piaIntf[1]);
  //if (coreGlobals.soundEn) by6803_soundInit();
  pia_reset();
  locals.vblankCount = 1;
  locals.zctimer = timer_pulse(TIME_IN_HZ(BY6803_ZCFREQ),0,by6803_zeroCross);

  locals.initDone = TRUE;
}

static void by6803_exit(void) {
#ifdef PINMAME_EXIT
  if (locals.zctimer) { timer_remove(locals.zctimer); locals.zctimer = NULL; }
#endif
  //if (coreGlobals.soundEn) by6803_soundExit();
  core_exit();
}

static READ_HANDLER(port1_r) { 
	int data = 0;
	logerror("port 1 read: %x\n",data);
	return data;
}

static READ_HANDLER(port2_r) { 
	int data = locals.phase_a;
	logerror("port 2 read: %x\n",data);
	return data;
}


static WRITE_HANDLER(port1_w) { 
	logerror("port 1 write = %x\n",data);
}

static WRITE_HANDLER(port2_w) {
	locals.diagnosticLed=((data>>2)&1);
	logerror("port 2 write = %x\n",data);
}

/*-----------------------------------
/  Memory map for CPU board
/------------------------------------*/
/*
NMI: = SW1?
Port 1:
(out)P10 = J5-7 -> SJ1-1 = Sound Data 1?
(out)P11 = J5-8 -> SJ1-2 = Sound Data 2?
(out)P12 = J5-9 -> SJ1-3 = Sound Data 3?
(out)P13 = J5-10 -> SJ1-4 = Sound Data 4?
(out)P14 = J5-11 ->NA?
(out)P15 = J5-12 ->NA?
(out)P16 = J5-13 ->NA?
(out)P17 = J5-14 ->NA?
Port 2:
(in) P20 = Measure Phase A (In conjunction w/P21 output?)
(in) P21 = NA?
(in) P22 = NA?
(in) P23 = 1 Unless JW7 Installed?
(out)P20 = NA?
(out)P21 = Controls Reading of Phase A on P20?
(out)P22 = Drives LED?
(out)P23 = NA?
(out)P24 = P24 = J5-15 -> SJ1-8 = Sound Latch?
*/
static MEMORY_READ_START(by6803_readmem)
	{ 0x0000, 0x001f, m6803_internal_registers_r },
	{ 0x0020, 0x0023, pia_0_r },
	{ 0x0040, 0x0043, pia_1_r },
	{ 0x0080, 0x00ff, MRA_RAM },	/*Internal 128K RAM*/
	{ 0x1000, 0x17ff, MRA_RAM },	/*External RAM*/
	{ 0x8000, 0xffff, MRA_ROM },	/*U2 & U3 ROM */
MEMORY_END

static MEMORY_WRITE_START(by6803_writemem)
	{ 0x0000, 0x001f, m6803_internal_registers_w },
	{ 0x0020, 0x0023, pia_0_w },
	{ 0x0040, 0x0043, pia_1_w },
	{ 0x0080, 0x00ff, MWA_RAM },	/*Internal 128K RAM*/
	{ 0x1000, 0x17ff, MWA_RAM },	/*External RAM*/
	{ 0x8000, 0xffff, MWA_ROM },	/*U2 & U3 ROM */
MEMORY_END

static PORT_READ_START( by6803_readport )
	{ M6803_PORT1, M6803_PORT1, port1_r },
	{ M6803_PORT2, M6803_PORT2, port2_r },
PORT_END

static PORT_WRITE_START( by6803_writeport )
	{ M6803_PORT1, M6803_PORT1, port1_w },
	{ M6803_PORT2, M6803_PORT2, port2_w },
PORT_END


struct MachineDriver machine_driver_by6803 = {
  {{  CPU_M6803, 3580000/4, /* 3.58/4 = 900hz */
      by6803_readmem, by6803_writemem, by6803_readport, by6803_writeport,
      by6803_vblank, 1, by6803_irq, BY6803_IRQFREQ
  }},
  BY6803_VBLANKFREQ, DEFAULT_60HZ_VBLANK_DURATION,
  50, by6803_init, CORE_EXITFUNC(by6803_exit)
  CORE_SCREENX, CORE_SCREENY, { 0, CORE_SCREENX-1, 0, CORE_SCREENY-1 },
  0, sizeof(core_palette)/sizeof(core_palette[0][0])/3, 0, core_initpalette,
  VIDEO_TYPE_RASTER | VIDEO_SUPPORTS_DIRTY, 0,
  NULL, NULL, gen_refresh,
  0,0,0,0, {{0}},
  by6803_nvram
};
/*-----------------------------------------------
/ Load/Save static ram
/-------------------------------------------------*/
static void by6803_nvram(void *file, int write) {
  core_nvram(file, write, memory_region(BY6803_MEMREG_CPU)+0x1000, 0x800,0xff);
}


