#include "driver.h"


/***************************************************************************

  Many games use a master-slave CPU setup. Typically, the main CPU writes
  a command to some register, and then writes to another register to trigger
  an interrupt on the slave CPU (the interrupt might also be triggered by
  the first write). The slave CPU, notified by the interrupt, goes and reads
  the command.

***************************************************************************/

static int cleared_value = 0x00;

static int latch,read_debug;


static void soundlatch_callback(int param)
{
	if (read_debug == 0 && latch != param)
		logerror("Warning: sound latch written before being read. Previous: %02x, new: %02x\n",latch,param);
	latch = param;
	read_debug = 0;
}

WRITE_HANDLER( soundlatch_w )
{
	/* make all the CPUs synchronize, and only AFTER that write the new command to the latch */
	timer_set(TIME_NOW,data,soundlatch_callback);
}

WRITE16_HANDLER( soundlatch_word_w )
{
	static data16_t word;
	COMBINE_DATA(&word);

	/* make all the CPUs synchronize, and only AFTER that write the new command to the latch */
	timer_set(TIME_NOW,word,soundlatch_callback);
}

READ_HANDLER( soundlatch_r )
{
	read_debug = 1;
	return latch;
}

READ16_HANDLER( soundlatch_word_r )
{
	read_debug = 1;
	return latch;
}

WRITE_HANDLER( soundlatch_clear_w )
{
	latch = cleared_value;
}


static int latch2,read_debug2;

static void soundlatch2_callback(int param)
{
	if (read_debug2 == 0 && latch2 != param)
		logerror("Warning: sound latch 2 written before being read. Previous: %02x, new: %02x\n",latch2,param);
	latch2 = param;
	read_debug2 = 0;
}

WRITE_HANDLER( soundlatch2_w )
{
	/* make all the CPUs synchronize, and only AFTER that write the new command to the latch */
	timer_set(TIME_NOW,data,soundlatch2_callback);
}

WRITE16_HANDLER( soundlatch2_word_w )
{
	static data16_t word;
	COMBINE_DATA(&word);

	/* make all the CPUs synchronize, and only AFTER that write the new command to the latch */
	timer_set(TIME_NOW,word,soundlatch2_callback);
}

READ_HANDLER( soundlatch2_r )
{
	read_debug2 = 1;
	return latch2;
}

READ16_HANDLER( soundlatch2_word_r )
{
	read_debug2 = 1;
	return latch2;
}

WRITE_HANDLER( soundlatch2_clear_w )
{
	latch2 = cleared_value;
}


static int latch3,read_debug3;

static void soundlatch3_callback(int param)
{
	if (read_debug3 == 0 && latch3 != param)
		logerror("Warning: sound latch 3 written before being read. Previous: %02x, new: %02x\n",latch3,param);
	latch3 = param;
	read_debug3 = 0;
}

WRITE_HANDLER( soundlatch3_w )
{
	/* make all the CPUs synchronize, and only AFTER that write the new command to the latch */
	timer_set(TIME_NOW,data,soundlatch3_callback);
}

WRITE16_HANDLER( soundlatch3_word_w )
{
	static data16_t word;
	COMBINE_DATA(&word);

	/* make all the CPUs synchronize, and only AFTER that write the new command to the latch */
	timer_set(TIME_NOW,word,soundlatch3_callback);
}

READ_HANDLER( soundlatch3_r )
{
	read_debug3 = 1;
	return latch3;
}

READ16_HANDLER( soundlatch3_word_r )
{
	read_debug3 = 1;
	return latch3;
}

WRITE_HANDLER( soundlatch3_clear_w )
{
	latch3 = cleared_value;
}


static int latch4,read_debug4;

static void soundlatch4_callback(int param)
{
	if (read_debug4 == 0 && latch4 != param)
		logerror("Warning: sound latch 4 written before being read. Previous: %02x, new: %02x\n",latch2,param);
	latch4 = param;
	read_debug4 = 0;
}

WRITE_HANDLER( soundlatch4_w )
{
	/* make all the CPUs synchronize, and only AFTER that write the new command to the latch */
	timer_set(TIME_NOW,data,soundlatch4_callback);
}

WRITE16_HANDLER( soundlatch4_word_w )
{
	static data16_t word;
	COMBINE_DATA(&word);

	/* make all the CPUs synchronize, and only AFTER that write the new command to the latch */
	timer_set(TIME_NOW,word,soundlatch4_callback);
}

READ_HANDLER( soundlatch4_r )
{
	read_debug4 = 1;
	return latch4;
}

READ16_HANDLER( soundlatch4_word_r )
{
	read_debug4 = 1;
	return latch4;
}

WRITE_HANDLER( soundlatch4_clear_w )
{
	latch4 = cleared_value;
}


void soundlatch_setclearedvalue(int value)
{
	cleared_value = value;
}





/***************************************************************************



***************************************************************************/

static void *sound_update_timer;
static double refresh_period;
static double refresh_period_inv;


struct snd_interface
{
	unsigned sound_num;										/* ID */
	const char *name;										/* description */
	int (*chips_num)(const struct MachineSound *msound);	/* returns number of chips if applicable */
	int (*chips_clock)(const struct MachineSound *msound);	/* returns chips clock if applicable */
	int (*start)(const struct MachineSound *msound);		/* starts sound emulation */
	void (*stop)(void);										/* stops sound emulation */
	void (*update)(void);									/* updates emulation once per frame if necessary */
	void (*reset)(void);									/* resets sound emulation */
};


#if (HAS_CUSTOM)
static const struct CustomSound_interface *cust_intf;

int custom_sh_start(const struct MachineSound *msound)
{
	cust_intf = msound->sound_interface;

	if (cust_intf->sh_start)
		return (*cust_intf->sh_start)(msound);
	else return 0;
}
void custom_sh_stop(void)
{
	if (cust_intf->sh_stop) (*cust_intf->sh_stop)();
}
void custom_sh_update(void)
{
	if (cust_intf->sh_update) (*cust_intf->sh_update)();
}
#endif

#if (HAS_DAC)
int DAC_num(const struct MachineSound *msound) { return ((struct DACinterface*)msound->sound_interface)->num; }
#endif
#if (HAS_YM2151 || HAS_YM2151_ALT)
int YM2151_clock(const struct MachineSound *msound) { return ((struct YM2151interface*)msound->sound_interface)->baseclock; }
int YM2151_num(const struct MachineSound *msound) { return ((struct YM2151interface*)msound->sound_interface)->num; }
#endif
#if (HAS_HC55516)
int HC55516_num(const struct MachineSound *msound) { return ((struct hc55516_interface*)msound->sound_interface)->num; }
#endif



struct snd_interface sndintf[] =
{
    {
		SOUND_DUMMY,
		"",
		0,
		0,
		0,
		0,
		0,
		0
	},
#if (HAS_CUSTOM)
    {
		SOUND_CUSTOM,
		"Custom",
		0,
		0,
		custom_sh_start,
		custom_sh_stop,
		custom_sh_update,
		0
	},
#endif
#if (HAS_SAMPLES)
    {
		SOUND_SAMPLES,
		"Samples",
		0,
		0,
		samples_sh_start,
#ifdef PINMAME
		samples_sh_stop,
#else
		0,
#endif
		0,
		0
	},
#endif
#if (HAS_DAC)
    {
		SOUND_DAC,
		"DAC",
		DAC_num,
		0,
		DAC_sh_start,
		0,
		0,
		0
	},
#endif
#if (HAS_HC55516)
    {
		SOUND_HC55516,
		"HC55516",
		HC55516_num,
		0,
		hc55516_sh_start,
		0,
		0,
		0
	},
#endif
#if (HAS_YM2151 || HAS_YM2151_ALT)
    {
		SOUND_YM2151,
		"YM2151",
		YM2151_num,
		YM2151_clock,
		YM2151_sh_start,
		YM2151_sh_stop,
		0,
		YM2151_sh_reset
	},
#endif

};




int sound_start(void)
{
	int totalsound = 0;
	int i;

	/* Verify the order of entries in the sndintf[] array */
	for (i = 0;i < SOUND_COUNT;i++)
	{
		if (sndintf[i].sound_num != i)
		{
            int j;
      logerror("Sound #%d wrong ID %d: check enum SOUND_... in src/sndintrf.h!\n",i,sndintf[i].sound_num);
			for (j = 0; j < i; j++)
				logerror("ID %2d: %s\n", j, sndintf[j].name);
            return 1;
		}
	}


	/* samples will be read later if needed */
	Machine->samples = 0;

	refresh_period = TIME_IN_HZ(Machine->drv->frames_per_second);
	refresh_period_inv = 1.0 / refresh_period;
	sound_update_timer = timer_alloc(NULL);

	if (mixer_sh_start() != 0)
		return 1;

	if (streams_sh_start() != 0)
		return 1;

	while (totalsound < MAX_SOUND && Machine->drv->sound[totalsound].sound_type != 0)
	{
		if ((*sndintf[Machine->drv->sound[totalsound].sound_type].start)(&Machine->drv->sound[totalsound]) != 0)
			goto getout;

		totalsound++;
	}

	return 0;


getout:
	/* TODO: should also free the resources allocated before */
	return 1;
}



void sound_stop(void)
{
	int totalsound = 0;


	while (totalsound < MAX_SOUND && Machine->drv->sound[totalsound].sound_type != 0)
	{
		if (sndintf[Machine->drv->sound[totalsound].sound_type].stop)
			(*sndintf[Machine->drv->sound[totalsound].sound_type].stop)();

		totalsound++;
	}

	streams_sh_stop();
	mixer_sh_stop();

	/* free audio samples */
	Machine->samples = 0;
}



void sound_update(void)
{
	int totalsound = 0;


	profiler_mark(PROFILER_SOUND);

	while (totalsound < MAX_SOUND && Machine->drv->sound[totalsound].sound_type != 0)
	{
		if (sndintf[Machine->drv->sound[totalsound].sound_type].update)
			(*sndintf[Machine->drv->sound[totalsound].sound_type].update)();

		totalsound++;
	}

	streams_sh_update();
	mixer_sh_update();

	timer_adjust(sound_update_timer, TIME_NEVER, 0, 0);

	profiler_mark(PROFILER_END);
}


void sound_reset(void)
{
	int totalsound = 0;


	while (totalsound < MAX_SOUND && Machine->drv->sound[totalsound].sound_type != 0)
	{
		if (sndintf[Machine->drv->sound[totalsound].sound_type].reset)
			(*sndintf[Machine->drv->sound[totalsound].sound_type].reset)();

		totalsound++;
	}
}



const char *soundtype_name(int soundtype)
{
	if (soundtype < SOUND_COUNT)
		return sndintf[soundtype].name;
	else
		return "";
}

const char *sound_name(const struct MachineSound *msound)
{
	return soundtype_name(msound->sound_type);
}

int sound_num(const struct MachineSound *msound)
{
	if (msound->sound_type < SOUND_COUNT && sndintf[msound->sound_type].chips_num)
		return (*sndintf[msound->sound_type].chips_num)(msound);
	else
		return 0;
}

int sound_clock(const struct MachineSound *msound)
{
	if (msound->sound_type < SOUND_COUNT && sndintf[msound->sound_type].chips_clock)
		return (*sndintf[msound->sound_type].chips_clock)(msound);
	else
		return 0;
}


int sound_scalebufferpos(int value)
{
    int result;
	double elapsed = timer_timeelapsed(sound_update_timer);
	if(elapsed < 0.)
		elapsed = 0.;
	result = (int)((double)value * elapsed * refresh_period_inv);
	if (value >= 0) return (result < value) ? result : value;
	else return (result > value) ? result : value;
}
