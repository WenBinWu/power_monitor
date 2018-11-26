#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <fcntl.h>
#include <string.h>
#include<stdlib.h>
#include<stdio.h>
#include "power_tuning.h"

static int adb_cmd_send(char* cmd)
{
	printf("run %s\n", cmd);
	return system(cmd);
}

static int adb_cmd_read(char* cmd, char* buffer, int size)
{
	int len = 0;
	FILE *fp;

	memset(buffer, 0, size);

	fp = popen(cmd, "r");
	if(fp!=NULL){
		len = fread(buffer, 1, size, fp);
		printf("read %s = %s\n", cmd, buffer);
		pclose(fp);
	}
	return len;
}

static int adb_shell_send(char* cmd)
{
	char cmd_str[128];

	memset(cmd_str, 0, 128);
	sprintf(cmd_str, "adb shell \"%s\"", cmd);
	
	return adb_cmd_send(cmd_str);
}

static int adb_shell_read(char* cmd, char* buffer, int size)
{
	char cmd_str[128];

	memset(cmd_str, 0, 128);
	sprintf(cmd_str, "adb shell \"%s\"", cmd);

	return adb_cmd_read(cmd_str, buffer, size);
}


int adb_get_device_state(void)
{
	char buffer[32];

	adb_cmd_read("adb get-state", buffer, sizeof(buffer));

	if(strstr(buffer, "device") == NULL)
		return -1;
	return 0;
}

int adb_root(void)
{
	char buffer[32];

	adb_cmd_read("adb root", buffer, sizeof(buffer));

	if(strstr(buffer, "as root") == NULL)
		return -1;
	return 0;
}

int adb_disable_charging(int disable)
{
	char cmd[128];
	char buffer[32];
	char *stopstr;

	memset(cmd, 0, 128);
	sprintf(cmd, "echo %d > /sys/class/meizu/charger/cmd_discharging", disable);
	adb_shell_send(cmd);

	adb_shell_read("cat /sys/class/meizu/charger/cmd_discharging", buffer, sizeof(buffer));
	if(strtol(buffer, &stopstr, 10) != disable)
		return -1;
	return 0;
}

int adb_get_cpus(void)
{
	int cpus;
	char buffer[32];
	char *stopstr;

	adb_shell_read("cat /sys/devices/system/cpu/kernel_max", buffer, sizeof(buffer));

	cpus = strtol(buffer, &stopstr, 10);
	if(cpus >= 0 && cpus < 10)
		return cpus + 1;
	return 0;
}

int adb_get_cpu_policy(int num)
{
	int cpus;
	char buffer[128];
	char policy[32];
	char *stopstr;

	adb_shell_read("ls /sys/devices/system/cpu/cpufreq", buffer, sizeof(buffer));

	memset(policy, 0, 32);

	sprintf(policy, "policy%d", num);

	if(strstr(buffer, policy) == NULL)
		return -1;
	return 0;
}

int adb_set_cpu_governor(int policy)
{
	char cmd[128];
	char buffer[32];

	memset(buffer, 0, 32);
	memset(cmd, 0, 128);

	/*set userspace governor*/
	sprintf(cmd, "echo userspace > /sys/devices/system/cpu/cpufreq/policy%d/scaling_governor", policy);
	adb_shell_send(cmd);

	memset(cmd, 0, 128);
	sprintf(cmd, "cat /sys/devices/system/cpu/cpufreq/policy%d/scaling_governor", policy);
	adb_shell_read(cmd, buffer, sizeof(buffer));

	if(strstr(buffer, "userspace") == NULL)
		return -1;
	return 0;
}

int adb_set_cpu_freq(int cpu, int freq)
{
	char cmd[128];
	char buffer[32];
	char *stopstr;

	memset(cmd, 0, 128);

	/*set cpu frequence*/
	sprintf(cmd, "echo %d > /sys/devices/system/cpu/cpu%d/cpufreq/scaling_setspeed", freq, cpu);
	adb_shell_send(cmd);

	memset(cmd, 0, 128);
	sprintf(cmd, "cat /sys/devices/system/cpu/cpu%d/cpufreq/scaling_setspeed",  cpu);
	adb_shell_read(cmd, buffer, sizeof(buffer));

	if(strtol(buffer, &stopstr, 10) != freq)
		return -1;
	return 0;	
}

int adb_get_cpu_freq_list(int cpu, int *freq_list, int num)
{
	char cmd[128];
	char buffer[1024];
	char *data_str;
	int count;
	int data;
	int i;
	char *stopstr;

	memset(cmd, 0, 128);

	sprintf(cmd, "cat /sys/devices/system/cpu/cpu%d/cpufreq/scaling_available_frequencies", cpu);

	adb_shell_read(cmd, buffer, sizeof(buffer));

	printf("CPU %d Frequence Table:", cpu);
	count = 0;
	data_str = buffer;
	for(i=0; i<num; i++){
		data = strtol(data_str, &stopstr, 10);
		if(data < 0)
			break;	
		*freq_list = data;
		freq_list++;
		count ++;
		printf(" %d", data);
		if(stopstr == NULL)
			break;
		data_str = stopstr;
	}
	printf("\n");
	if(count)
		return 0;
	return -1;
}

int adb_set_cpu_online(int cpu, int on)
{
	char cmd[128];
	char buffer[32];
	char *stopstr;

	memset(cmd, 0, 128);

	sprintf(cmd, "echo %d > /sys/devices/system/cpu/cpu%d/online", on, cpu);
	adb_shell_send(cmd);

	memset(cmd, 0, 128);
	sprintf(cmd, "cat /sys/devices/system/cpu/cpu%d/online", cpu);
	adb_shell_read(cmd, buffer, sizeof(buffer));

	if(strtol(buffer, &stopstr, 10) != on)
		return -1;
	return 0;	
}

int adb_run_cpuburn_tool(int thread_num)
{
	int i=0;
	char cmd[128];
	char buffer[1024];

	memset(cmd, 0, 128);

	for(i=0; i<thread_num; i++)
		adb_cmd_send("adb shell setsid mcpuburn&");

	sprintf(cmd, "ps -A | grep mcpuburn");
	adb_shell_read(cmd, buffer, sizeof(buffer));

	if(strstr(buffer, "mcpuburn") == NULL)
		return -1;
	return 0;	
}

int adb_stop_cpuburn_tool(void)
{
	char cmd[128];
	char buffer[1024];

	memset(cmd, 0, 128);

	adb_cmd_send("adb shell killall mcpuburn");

	sprintf(cmd, "ps -A | grep mcpuburn");
	adb_shell_read(cmd, buffer, sizeof(buffer));

	if(strstr(buffer, "mcpuburn") != NULL)
		return -1;
	return 0;	
}


int adb_disable_ssr(void)
{

	return 0;
}

int adb_disable_msm_performance(void)
{

	return 0;
}
