#ifndef POWER_METER_H 
#define POWER_METER_H

extern int power_meter_init (int  pad);
extern int power_meter_deinit (int ud);
//extern void power_config_new (GtkWindow *parent);
extern void power_wave_update_data(void);
extern int power_wave_open_file(char *filename);
extern int power_wave_save_file(char *filename);
extern double get_current(int ud, GTimer *timer);

#endif
