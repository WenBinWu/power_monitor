#ifndef POWER_WAVE_H 
#define POWER_WAVE_H

extern GtkWidget *power_wave_new (GtkWidget *parant);
extern void power_wave_free(void);
extern void power_wave_reset_data(void);
extern void power_wave_insert_data(gdouble current, gdouble volt, gdouble timestamp);
extern void power_wave_set_range(unsigned int time_range);
extern void power_wave_start(void);
extern void power_wave_stop(void);
extern void power_wave_update_data(void);
extern gdouble power_wave_get_avg(int time_range);
#endif
