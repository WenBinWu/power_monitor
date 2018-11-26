#include <gtk/gtk.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include "gpib/ib.h"

static int perform_read(int ud, char *buffer, int max_num_bytes)
{
	int buffer_size = max_num_bytes;

	memset(buffer, 0, buffer_size);
//	printf("trying to read %i bytes from device...\n", max_num_bytes);

	ibrd(ud, buffer, buffer_size - 1);
	
//	printf("Number of bytes read: %li\n", ThreadIbcntl());
	if(ThreadIbsta() & ERR)
		return 0;
	return ThreadIbcntl();
}

static int perform_write(int ud, char *data)
{
	char buffer[1024];

	if(data == NULL)
		return -1;

	memset(buffer, 0, 1024);
	strncpy(buffer, data, 1023);

	if( ibwrt(ud, buffer, strlen(buffer)) & ERR )
	{
		return -1;
	}
	//printf("write size=%d, str= %s\n", (int)strlen(buffer), buffer);
	return 0;
}

double get_current(int ud, GTimer* timer)
{   
	gdouble elapsed, read_time, write_time;
	char buffer[ 1024 ];
	int max_num_bytes = 1024;
			    
	memset(buffer, 0, 1024);

	elapsed = g_timer_elapsed(timer, NULL);

//	if( perform_write(ud, "MEAS:ARR:CURR?") )
	if( perform_write(ud, "MEAS:CURR?") )
	{
		return -1;
	}
	write_time = g_timer_elapsed(timer, NULL) - elapsed;
	if( !perform_read(ud, buffer, max_num_bytes))
	{
		return 0;
	}

	read_time = g_timer_elapsed(timer, NULL) - elapsed;
	//printf("take time write=%fms, read=%fms, read string: %s\n", write_time*1000,  read_time*1000, buffer);

	return strtod(buffer, NULL);
}

static int get_status(int ud)
{   
	char buffer[ 1024 ];
	int max_num_bytes = 1024;
			    
	memset(buffer, 0, 1024);

	if( perform_write(ud, "*IDN?") )
	{
		return -1;
	}

	if( !perform_read(ud, buffer, max_num_bytes))
	{
		return -1;
	}
	printf("power meter: %s\n", buffer);

	return 0;
}

int device_clear(int ud)
{
	if(ibclr(ud) & ERR)
	{
		return -1;
	}
	printf("Device clear sent.\n" );
	return 0;
}

static int config_meter(int ud)
{   
	if( perform_write(ud, "SENS:SWE:POIN 256") )
	{
		printf("Set sens points fail\n");
		return -1;
	}
	if( perform_write(ud, "SENS:SWE:TINT 15.6E-6") )
	{
		printf("Set sens time fail\n");
		return -1;
	}
#if 0

	if( perform_write(ud, "INIT:NAME ACQ") )
	{
		printf("Set init name acq fail\n");
		return -1;
	}
	if( perform_write(ud, "SENS:FUN \"CURR\"") )
	{
		printf("Set sens func fail\n");
		return -1;
	}
	if( perform_write(ud, "TRIG:SOUR BUS") )
	{
		printf("Set trigger source fail\n");
		return -1;
	}
	if( perform_write(ud, "TRIG:ACQ") )
	{
		printf("Set trigger bus fail\n");
		return -1;
	}
#endif
	return 0;
}

/* returns a device descriptor after prompting user for primary address */
static int open_meter_device(int minor, int pad)
{
	int ud;
	const int sad = 0;
	const int send_eoi = 1;
	const int eos_mode = 0;
	const int timeout = T1s;

	printf("meter: trying to open pad = %i on /dev/gpib%i ...\n", pad, minor);
	ud = ibdev(minor, pad, sad, timeout, send_eoi, eos_mode);
	if(ud < 0)
	{
		printf("meter: ibdev error\n");
		return -1;
	}

	if(device_clear(ud) < 0)
	{
		ibonl(ud, 0);
		printf("meter: clear meter error, close device pad = %d\n", pad);
		return -1;
	}

	if(config_meter(ud) < 0)
	{
		ibonl(ud, 0);
		printf("meter: config meter error, close device pad = %d\n", pad);
		return -1;
	}
#if 1
	if(get_status(ud) < 0)
	{
		ibonl(ud, 0);
		printf("meter: get meter status error, close device pad = %d\n", pad);
		return -1;
	}
#endif
	return ud;
}

int power_meter_init (int pad)
{
	int ud;

	ud =  open_meter_device(0, pad);
	return ud;
}

int power_meter_deinit (int ud)
{

	if(ud >= 0){
		printf("meter: close meter\n");
		ibonl(ud, 0);
	}
	return 0;
}
