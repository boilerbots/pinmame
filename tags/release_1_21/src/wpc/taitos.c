#include "driver.h"
#include "machine/6821pia.h"
#include "cpu/m6800/m6800.h"
#include "core.h"
#include "taito.h"
#include "snd_cmd.h"
#include "sndbrd.h"
#include "taitos.h"

/*----------------------------------------
/ Taito Sound System 
/ Taito uses three different sound board:
/ - sintevox
/ - sintetizator (sitevox with an additonal SC-01)
/ - sintevox with a daugher board installed
/
/ cpu: 6802
/ 
/ 0x0000 - 0x0000: PIA 
/ 0x1000 - 0x17FF: ROM 2
/ 0x1800 - 0x1fff: ROM 1
/ 
/ sintetizator:
/ SC-01 connected to output of PIA Port B
/ 
/-----------------------------------------*/

#define SINTEVOX		0
#define SINTETIZADOR	1
#define SITEVOX_PP		2

static struct {
  struct sndbrdData brdData;
} taitos_locals;

#define SP_PIA0  0

MEMORY_READ_START(taitos_readmem)
  { 0x0000, 0x007f, MRA_RAM },
  { 0x0400, 0x0403, pia_r(SP_PIA0) },
  { 0x1000, 0x1fff, MRA_ROM },
//  { 0x2000, 0x2fff, MRA_ROM },
  { 0xf000, 0xffff, MRA_ROM }, /* reset vector */
MEMORY_END

MEMORY_WRITE_START(taitos_writemem)
  { 0x0000, 0x007f, MWA_RAM },
  { 0x0400, 0x0403, pia_w(SP_PIA0) },
  { 0x1000, 0x1fff, MWA_ROM },
  { 0xf000, 0xffff, MWA_ROM }, /* reset vector */
MEMORY_END

static void taitos_irq(int state) {
	logerror("sound irq\n");
	cpu_set_irq_line(taitos_locals.brdData.cpuNo, M6802_IRQ_LINE, state ? ASSERT_LINE : CLEAR_LINE);
}

static WRITE_HANDLER(pia0a_w)
{
//	logerror("pia0a_w: %02x\n", data);
	DAC_data_w(0, data);
}

static WRITE_HANDLER(pia0b_w)
{
//	logerror("pia0b_w: %02x\n", data);

	if ( taitos_locals.brdData.subType!=SINTEVOX )
		votraxsc01_w(0, data);
}

static WRITE_HANDLER(pia0ca2_w)
{
	logerror("pia0ca2_w: %02x\n", data);
}

/* enable diagnostic led */
static WRITE_HANDLER(pia0cb2_w)
{
	logerror("pia0cb2_w: %02x\n", data);

	coreGlobals.diagnosticLed = data?0x01:0x00;
}

static READ_HANDLER(pia0ca1_r)
{
	logerror("pia0ca1_r\n");

	return votraxsc01_status_r(0);
}

static void votrax_busy(int state)
{
//	logerror("votrax busy: %i\n", state);

	if ( taitos_locals.brdData.subType!=SINTEVOX )
		pia_set_input_ca1(SP_PIA0, state);
}

static const struct pia6821_interface sp_pia = {
  /*i: A/B,CA/B1,CA/B2 */ 0, 0, 0, 0, 0, 0,
  /*o: A/B,CA/B2       */ pia0a_w, pia0b_w, pia0ca2_w, pia0cb2_w,
  /*irq: A/B           */ taitos_irq,taitos_irq
};

/* sound strobe */
static WRITE_HANDLER(taitos_ctrl_w)
{
	// logerror("taitos_ctrl_w: %i\n", data);

	pia_set_input_cb1(SP_PIA0, data?0x01:0x00);
}

/* sound input */
static WRITE_HANDLER(taitos_data_w)
{
    // logerror("taitos_data_w: %i\n", data);

    pia_set_input_b(SP_PIA0, data^0xff);
	sndbrd_ctrl_w(0, data);
}

static void taitos_init(struct sndbrdData *brdData)
{
	memset(&taitos_locals, 0x00, sizeof(taitos_locals));
	taitos_locals.brdData = *brdData;

	if ( taitos_locals.brdData.subType!=SITEVOX_PP )
		memcpy(memory_region(TAITO_MEMREG_SCPU)+0xf000, memory_region(TAITO_MEMREG_SCPU)+0x1000, 0x1000);
	else
		memcpy(memory_region(TAITO_MEMREG_SCPU)+0xf000, memory_region(TAITO_MEMREG_SCPU)+0x7000, 0x1000);

	pia_config(SP_PIA0, PIA_STANDARD_ORDERING, &sp_pia);
}

struct DACinterface TAITO_dacInt =
{ 
  1,			/* 1 Chip */
  {100}		    /* Volume */
};

struct VOTRAXSC01interface TAITO_votrax_sc01_interface = {															
	1,						/* 1 chip */
	{ 100 },				/* master volume */
	{ 8000 },				/* dynamically changing this is currently not supported */
	{ &votrax_busy }		/* set NMI when busy signal get's low */
};

/*-------------------
/ exported interface
/--------------------*/
const struct sndbrdIntf taitoIntf = {
  taitos_init, NULL, NULL, taitos_data_w, NULL, taitos_ctrl_w, NULL, SNDBRD_NODATASYNC|SNDBRD_NOCTRLSYNC
};

MACHINE_DRIVER_START(taitos_sintevox)
  MDRV_CPU_ADD_TAG("scpu", M6802, 500000) // 0.5 MHz ??? */
  MDRV_CPU_FLAGS(CPU_AUDIO_CPU)
  MDRV_CPU_MEMORY(taitos_readmem, taitos_writemem)
  MDRV_INTERLEAVE(50)
  MDRV_SOUND_ADD(DAC, TAITO_dacInt)
  MDRV_SOUND_ADD(SAMPLES, samples_interface)
MACHINE_DRIVER_END

MACHINE_DRIVER_START(taitos_sintetizador)
  MDRV_IMPORT_FROM(taitos_sintevox)

  MDRV_SOUND_ADD(VOTRAXSC01, TAITO_votrax_sc01_interface)
MACHINE_DRIVER_END

// piggy pack board

static WRITE_HANDLER(ay0_0)
{
}

static WRITE_HANDLER(ay0_1)
{
}

static WRITE_HANDLER(ay0_2)
{
}

static WRITE_HANDLER(ay1_0)
{
}

static WRITE_HANDLER(ay1_1)
{
}

static WRITE_HANDLER(ay1_2)
{
}

static WRITE_HANDLER(unknown2000)
{
}

struct AY8910interface TAITO_ay8910Int = {
	2,			/* 2 chips */
	2000000,	/* 2 MHz */
	{ 50, 50 }, /* Volume */
	{ 0 },
	{ 0 },
	{ 0 },
	{ 0 }
};

MEMORY_READ_START(taitospp_readmem)
  { 0x0000, 0x007f, MRA_RAM },
  { 0x0400, 0x0403, pia_r(SP_PIA0) },
  { 0x5000, 0x7fff, MRA_ROM },
  { 0xf000, 0xffff, MRA_ROM }, /* reset vector */
MEMORY_END

MEMORY_WRITE_START(taitospp_writemem)
  { 0x0000, 0x007f, MWA_RAM },
  { 0x0400, 0x0403, pia_w(SP_PIA0) },
  { 0x1000, 0x1000, ay0_0 },
  { 0x1003, 0x1003, ay0_1 },
  { 0x100a, 0x100a, ay0_2 },
  { 0x100b, 0x100b, ay1_0 },
  { 0x100c, 0x100c, ay1_1 },
  { 0x100e, 0x100e, ay1_2 },
  { 0x2000, 0x2000, unknown2000 },
  { 0x5000, 0x7fff, MWA_ROM },
  { 0xf000, 0xffff, MWA_ROM }, /* reset vector */
MEMORY_END

MACHINE_DRIVER_START(taitos_sintevoxpp)
  MDRV_CPU_ADD_TAG("scpu", M6802, 500000) // 0.5 MHz ??? */
  MDRV_CPU_FLAGS(CPU_AUDIO_CPU)
  MDRV_CPU_MEMORY(taitospp_readmem, taitospp_writemem)
  MDRV_INTERLEAVE(50)
  MDRV_SOUND_ADD(DAC, TAITO_dacInt)
  MDRV_SOUND_ADD(SAMPLES, samples_interface)
  MDRV_SOUND_ADD(VOTRAXSC01, TAITO_votrax_sc01_interface)
  MDRV_SOUND_ADD(AY8910, TAITO_ay8910Int)
MACHINE_DRIVER_END