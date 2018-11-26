#ifndef POWER_TUNING_H 
#define POWER_TUNING_H

extern int adb_get_device_state(void);
extern int adb_root(void);
extern int adb_disable_charging(int disable);
extern int adb_get_cpus(void);
extern int adb_set_cpu_governor(int policy);
extern int adb_set_cpu_freq(int cpu, int freq);
extern int adb_get_cpu_freq_list(int cpu, int *freq_list, int len);
extern int adb_set_cpu_online(int cpu, int on);
extern int adb_run_cpuburn_tool(int thread_num);
extern int adb_stop_cpuburn_tool(void);
extern int adb_disable_ssr(void);
extern int adb_disable_msm_performance(void);
extern int adb_get_cpu_policy(int num);

#endif
