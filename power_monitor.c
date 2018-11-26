#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <string.h>
#include <signal.h>
#include "power_wave.h"
#include "power_tuning.h"
#include "power_meter.h"

#define DEFAULT_WINDOW_WIDTH 800
#define DEFAULT_WINDOW_HEIGHT 500

#define WINDOW_VISIBLE_SET_NO_DRAW		1<<0
#define WINDOW_STATE_SET_MO_DRAW		2<<0

//#define POWER_TEST

struct monitor_data{
	GMutex mutex;
	GTimer *data_timer;
	GThread *data_thread;
	GThread *tuning_thread;

	int run_state;//0: off; 1: on
	int update_state;

	int meter_address;//meter address
	int meter_handle;//meter handle return from power_meter_init()
	int sample_rate;//ms
	int power_tuning;//YES or NO
	gdouble power_value;//W
	unsigned int time_range;//s

	GList *disable_list;//all widgets should be disabled in running status
}global_data;
#define MAX_CPU_NUM			10
#define MAX_CPU_FREQ_NUM	40
struct cpu_data{
	int online;
	int policy;
};

struct cpu_policy_data{
	int policy;
	int curr_freq_index;
	int max_freq_index;
	int cpufreq[MAX_CPU_FREQ_NUM];
};

static gboolean start_monitor(GtkWidget *window);
static void stop_monitor(void);
static gpointer poll_meter(gpointer user_data);
static void close_window (void)
{
  stop_monitor();
  power_wave_free();
  gtk_main_quit ();
  printf("quit\n");
}

static void power_error_show (GtkWidget *parent, int error)
{
  GtkWidget *dialog;

  dialog = gtk_message_dialog_new(GTK_WINDOW (parent), GTK_DIALOG_DESTROY_WITH_PARENT, 
		  GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,"打开测量设备失败,请检查:\n \
		  1. 设备电源是否上电\n \
		  2. 确认GPIB接口连接是否正常\n \
		  3. 设备节点是否有访问权限\n \
		  4. GPIB设备配置是否正确，如果配置正确请尝试运行sudo gpib_config");

  g_signal_connect_swapped (dialog, "response",
                           G_CALLBACK (gtk_widget_destroy), dialog);
  gtk_widget_show_all (dialog);
}

static gpointer power_tuning_thread(gpointer user_data)
{
	int i, j;
	int cpus = 0;
	int power_index;
	gdouble power_avg;
	int power_diff;
	struct cpu_data cpu_info[MAX_CPU_NUM];
	struct cpu_policy_data policy_info[MAX_CPU_NUM];
	int max_policy = 0;
	int policy = 0;
	int index;
	int turning_done;

	while(global_data.run_state){
		if(!global_data.power_tuning){
			printf("powe turning disabled\n");
			goto go_try;
		}
		if(adb_get_device_state()){
			printf("adb device no found\n");
			goto go_try;
		}
		if(adb_root()){
			printf("root fail! need userdebug version\n");
			goto go_try;
		}
		cpus = adb_get_cpus();
		if(cpus<0){
			printf("cpu number = %d\n", cpus);
			goto go_try;
		}
		printf("cpu number = %d\n", cpus);

		if(adb_disable_charging(1)){
			printf("disable charging fail!\n");
			goto go_try;
		}

		/*get cpu policy*/	
		for(i = 0, index=0; i<cpus; i++){
			if(adb_get_cpu_policy(i) == 0){
				policy_info[index].policy = i;
				printf("found policy%d\n", i);
				index ++;
				max_policy = index;
			}
		}

		/*all cpu online*/
		for(i = 0; i<cpus; i++){
			adb_set_cpu_online(i, 1);
			cpu_info[i].online = 1;
		}

		if(adb_get_device_state()){
			printf("adb device no found\n");
			goto go_try;
		}
		/*get all cpu policy infomation*/
		for(i = 0; i<max_policy; i++){
			/*set cpu governor to usespace*/
			if(adb_set_cpu_governor(policy_info[i].policy)){
				printf("set cpu governor fail!\n");
				goto go_try;
			}
			/*get cpu frequence table*/
			if(adb_get_cpu_freq_list(policy_info[i].policy, policy_info[i].cpufreq, MAX_CPU_FREQ_NUM)){
				printf("get little cpu frequence table fail!\n");
				goto go_try;
			}else{
				for(j = 0; j<MAX_CPU_FREQ_NUM; j++){
					if(policy_info[i].cpufreq[j] <= 0)
						break;
					policy_info[i].max_freq_index = j;
				}
			}
			/*set default cpu freq*/
			policy_info[i].curr_freq_index = policy_info[i].max_freq_index/3;
			adb_set_cpu_freq(policy_info[i].policy, policy_info[i].cpufreq[policy_info[i].curr_freq_index]);
		}

		if(adb_get_device_state()){
			printf("adb device no found\n");
			goto go_try;
		}
		adb_disable_ssr();
		adb_disable_msm_performance();

		/*run cpuburn*/
		adb_stop_cpuburn_tool();
		adb_run_cpuburn_tool(cpus);

		/*tuning power*/
		while(!adb_get_device_state()){
			if(!global_data.run_state)
				return NULL;
			g_usleep(5*1000*1000);//sleep 5 Second, wait for power stability
			/*get power*/
			power_avg = power_wave_get_avg(1);//get avarage power at the last second.
			power_diff = (int)((power_avg - global_data.power_value)*1000);
		
			printf("set power =%f, check power = %fW, diff = %dmW\n", global_data.power_value, power_avg, power_diff);

			turning_done = 0;
			if(power_diff > 100){
				if(power_diff < 500){
					for(i = 0; i<max_policy; i++){
						power_index = policy_info[i].curr_freq_index - 1;
						if(power_index >= 0){
							adb_set_cpu_freq(policy_info[i].policy, policy_info[i].cpufreq[power_index]);
							policy_info[i].curr_freq_index = power_index;
							turning_done = 1;
							break;
						}
					}
				}else{
					for(i = max_policy-1; i>=0; i--){
						power_index = policy_info[i].curr_freq_index - 1;
						if(power_index >= 0){
							adb_set_cpu_freq(policy_info[i].policy, policy_info[i].cpufreq[power_index]);
							policy_info[i].curr_freq_index = power_index;
							turning_done = 1;
							break;
						}
					}
				}
				/*maybe need kill cpuburn*/
				if(!turning_done)
					printf("need manual tuning\n");	
			}else if(power_diff < -100){
				if(power_diff > -500){
					for(i = 0; i<max_policy; i++){
						power_index = policy_info[i].curr_freq_index + 1;
						if(power_index <= policy_info[i].max_freq_index){
							adb_set_cpu_freq(policy_info[i].policy, policy_info[i].cpufreq[power_index]);
							policy_info[i].curr_freq_index = power_index;
							turning_done = 1;
							break;
						}
					}
				}else{
					for(i = max_policy-1; i>=0; i--){
						power_index = policy_info[i].curr_freq_index + 1;
						if(power_index <= policy_info[i].max_freq_index){
							adb_set_cpu_freq(policy_info[i].policy, policy_info[i].cpufreq[power_index]);
							policy_info[i].curr_freq_index = power_index;
							turning_done = 1;
							break;
						}
					}
				}
				/*maybe need kill cpuburn*/
				if(!turning_done)
					printf("need manual tuning\n");	
			}
		}
go_try:
		g_usleep(2*1000*1000);//sleep 5 Second
	}
	return NULL;
}

static gboolean start_monitor(GtkWidget *window)
{
	if(global_data.run_state == 0){
#ifndef	POWER_TEST
		global_data.meter_handle = power_meter_init (global_data.meter_address);
		if(global_data.meter_handle < 0){
			power_error_show(window, 0);
			return FALSE;
		}
#endif
		g_timer_start(global_data.data_timer);
		power_wave_start();
		power_wave_set_range(global_data.time_range);

		global_data.data_thread = g_thread_new("meter-thread", poll_meter, window);
		global_data.run_state = 1;
		if(global_data.power_tuning)
			global_data.tuning_thread = g_thread_new("tuning-thread", power_tuning_thread, window);
	}
	printf("start monitor, meter_handle=%d, rate=%dms, power-tuning=%d, power-setting=%f, range=%d\n",
			global_data.meter_handle, global_data.sample_rate, global_data.power_tuning, global_data.power_value, global_data.time_range);
	return TRUE;
}

static void stop_monitor(void)
{
	if(global_data.run_state == 1){
		printf("stop monitor begin\n");
		global_data.run_state = 0;
		if(global_data.power_tuning){
			g_thread_join(global_data.tuning_thread);
		}
		g_thread_join(global_data.data_thread);
		g_timer_stop(global_data.data_timer);
#ifndef	POWER_TEST
		power_meter_deinit(global_data.meter_handle);
#endif
		global_data.meter_handle = -1;
		printf("power_wave_stop\n");
		power_wave_stop();
		printf("stop monitor, meter_handle=%d\n", global_data.meter_handle);
	}else{
		printf("already stop\n");
	}
}

static void widget_set_sensitive_all(gboolean disable)
{

	GList *l;
	GtkWidget *widget;

	for (l = g_list_first(global_data.disable_list); l != NULL; l = l->next)
	{
		widget = (GtkWidget *)l->data;
		gtk_widget_set_sensitive(widget, disable);
	}
}

static void power_monitor_run(GtkToggleToolButton *toggle_tool_button, gpointer user_data)
{
	GtkWidget *window = (GtkWidget*)user_data;
	GtkToolItem *button = g_object_get_data ((GObject*)window, "toolbar-run");
	GtkWidget *rate = g_object_get_data ((GObject*)window, "toolbar-rate");
	GtkWidget *tuning_power = g_object_get_data ((GObject*)window, "toolbar-tuning-power");
	GtkWidget *time_range = g_object_get_data ((GObject*)window, "toolbar-time-range");
	GtkWidget *meter_address = g_object_get_data ((GObject*)window, "toolbar-address");
	gboolean state = gtk_toggle_tool_button_get_active(toggle_tool_button);

	if (state){
		global_data.sample_rate = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON (rate));
		global_data.power_value = gtk_spin_button_get_value(GTK_SPIN_BUTTON (tuning_power));
		global_data.time_range = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON (time_range));
		global_data.meter_address = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON (meter_address));

		if(!start_monitor(user_data)){
			return;
		}

		gtk_tool_button_set_label(GTK_TOOL_BUTTON (button), "停止");
		/*disable widget fouce*/
		widget_set_sensitive_all(FALSE);
	}else{	
		stop_monitor();
		gtk_tool_button_set_label(GTK_TOOL_BUTTON (button), "开始");
		/*enable widget fouce*/
		widget_set_sensitive_all(TRUE);
	}
}

static void power_monitor_tuning(GtkToggleButton *toggle_button, gpointer user_data)
{
	gboolean state = gtk_toggle_button_get_active(toggle_button);

	printf("%s\n", __func__);
	if (state){
		global_data.power_tuning = 1;
	}else{	
		global_data.power_tuning = 0;
	}
}
static void 
init_global_data(void)
{
	global_data.run_state = 0;
	global_data.update_state = 1;
	global_data.sample_rate = 20;
	global_data.meter_address = 15;
	global_data.meter_handle = -1;
	g_mutex_init (&global_data.mutex);
	global_data.data_timer = g_timer_new();
}

static gpointer poll_meter(gpointer user_data)
{
	GtkWidget *window = (GtkWidget *)user_data;
//	GdkWindow *gdk_window = gtk_widget_get_window(window);
#ifndef POWER_TEST
	double current = 0;
	gdouble take_time;
#endif
	gdouble elapsed;
	int wait;

	while(global_data.run_state){
		wait = global_data.sample_rate;

//		printf("%s window visible = %d\n", __func__, gtk_widget_get_state_flags(window));
//		printf("%s window state = %d\n", __func__, gdk_window_get_state (gdk_window));
//		printf("%s window is shadowed = %d\n", __func__, gtk_widget_get_app_paintable(window));
//		printf("%s window has screen = %d\n", __func__, gtk_widget_has_screen(window));
//		printf("%s window is visible = %d\n", __func__, gtk_widget_is_visible(window));
//		printf("%s window is shadowed = %d\n", __func__, gtk_widget_get_app_paintable(window));
#ifdef POWER_TEST
		elapsed = g_timer_elapsed(global_data.data_timer, NULL);
		power_wave_insert_data(g_random_double_range(0.001, 1), 4, elapsed);
#else
		if(global_data.meter_handle >= 0){
			elapsed = g_timer_elapsed(global_data.data_timer, NULL);
		    current = get_current(global_data.meter_handle, global_data.data_timer);
			if(current < 0)
				current = 0;
			power_wave_insert_data(current, 4.0, elapsed);
			take_time = g_timer_elapsed(global_data.data_timer, NULL) - elapsed;
			wait = wait - take_time*1000;
		}
#endif
		if(global_data.update_state)
			power_wave_update_data();
		if(wait>0)
			g_usleep(wait*1000);
	}
	printf("%s exit\n", __func__);
	return NULL;
}
static void
xscale_change_cb (GtkWidget *widget, gpointer user_data)
{
	GtkSpinButton *spin;

	spin = GTK_SPIN_BUTTON (widget);

	global_data.time_range = gtk_spin_button_get_value_as_int(spin);

	power_wave_set_range(global_data.time_range);

}

static void power_monitor_open_callback (GtkButton *button, gpointer user_data)
{
    GtkWidget *parent_window = (GtkWidget*)user_data;
	GtkWidget *dialog;
	GtkFileFilter *filter;
	GtkWidget *time_range = g_object_get_data ((GObject*)parent_window, "toolbar-time-range");

	dialog = gtk_file_chooser_dialog_new ("打开波形文件",
                                      GTK_WINDOW(parent_window),
                                      GTK_FILE_CHOOSER_ACTION_OPEN,
                                      "Cancel", GTK_RESPONSE_CANCEL,
                                      "Open", GTK_RESPONSE_ACCEPT,
                                      NULL);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, "Power monitor data file");
	gtk_file_filter_add_pattern (filter, "*.pmd");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);
	gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (dialog), filter);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
	{
		char *filename;

		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
		global_data.time_range = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON (time_range));
		power_wave_set_range(global_data.time_range);
		power_wave_open_file (filename);
		printf("OPEN: %s\n", filename);
	}

	gtk_widget_destroy (dialog);

}

static void power_monitor_save_callback (GtkButton *button, gpointer user_data)
{
    GtkWidget *parent_window = (GtkWidget*)user_data;
	GtkWidget *dialog;
	GtkFileFilter *filter;

	dialog = gtk_file_chooser_dialog_new ("保存波形文件",
                                      GTK_WINDOW(parent_window),
                                      GTK_FILE_CHOOSER_ACTION_SAVE,
                                      "Cancel", GTK_RESPONSE_CANCEL,
                                      "Save", GTK_RESPONSE_ACCEPT,
                                      NULL);
	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, "Power monitor data file");
	gtk_file_filter_add_pattern (filter, "*.pmd");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);
	gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (dialog), filter);

	gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), "Untitled document.pmd");

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
	{
		char *filename;

		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
		power_wave_save_file (filename);
		printf("SAVE %s\n", filename);
	}

	gtk_widget_destroy (dialog);
}


#if 1
static gboolean 
window_state_event_cb (GtkWidget      *widget,
						GdkEventWindowState *event,
						gpointer        data)
{
	printf("window change mask %x, state: %d\n",event->changed_mask, event->new_window_state);
	if(event->new_window_state == 0 ||
			event->new_window_state & (GDK_WINDOW_STATE_WITHDRAWN | GDK_WINDOW_STATE_ICONIFIED))
		global_data.update_state = 0;
	else
		global_data.update_state = 1;

	return TRUE;
}
	
static gboolean 
window_visibility_event_cb (GtkWidget      *widget,
						GdkEventVisibility *event,
						gpointer        data)
{
	printf("window visibility state: %d\n", event->state);
	if(event->state == 2)
		global_data.update_state = 0;
	else
		global_data.update_state = 1;

	return TRUE;
}
#endif

static void power_monitor_init (GtkWidget *window)
{
  GtkWidget *frame;
  GtkWidget *grid;
  GtkWidget *toolbar;
  GtkToolItem *button;
  GtkWidget *check, *box, *label, *spin, *separator;
  GtkWidget *drawing_area;

  init_global_data();

  gtk_window_set_title (GTK_WINDOW (window), "Meizu Bsp Power Monitor");
  gtk_container_set_border_width (GTK_CONTAINER (window), 8);
  gtk_widget_set_app_paintable(window, FALSE);
  gtk_window_set_resizable(GTK_WINDOW (window), FALSE);
  /* set a minimum size */
  //gtk_widget_set_size_request (window, DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT);

  g_signal_connect (window, "destroy", G_CALLBACK (close_window), NULL);

  grid = gtk_grid_new ();
  gtk_container_add (GTK_CONTAINER (window), grid);

  /*control toolbar*/
  toolbar = gtk_toolbar_new();
  gtk_toolbar_set_style(GTK_TOOLBAR (toolbar), GTK_TOOLBAR_BOTH);
  //gtk_toolbar_set_style(GTK_TOOLBAR (toolbar), GTK_TOOLBAR_BOTH_HORIZ);

  button = gtk_tool_button_new (NULL, "打开");
  gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (button), "document-open");
  g_signal_connect (button, "clicked", G_CALLBACK(power_monitor_open_callback), window);
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), button, -1);
  global_data.disable_list = g_list_append(global_data.disable_list, button);

  button = gtk_tool_button_new (NULL, "保存");
  gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (button), "document-save");
  g_signal_connect (button, "clicked", G_CALLBACK(power_monitor_save_callback), window);
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), button, -1);
  global_data.disable_list = g_list_append(global_data.disable_list, button);

  button = gtk_separator_tool_item_new ();
  gtk_separator_tool_item_set_draw (GTK_SEPARATOR_TOOL_ITEM (button), FALSE);
  gtk_tool_item_set_expand (GTK_TOOL_ITEM (button), TRUE);
  gtk_container_add (GTK_CONTAINER (toolbar), GTK_WIDGET (button));

  /*control button*/
  button = gtk_toggle_tool_button_new ();
  gtk_tool_button_set_label(GTK_TOOL_BUTTON (button), "开始");
  gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (button), "media-record");
  //gtk_actionable_set_detailed_action_name (GTK_ACTIONABLE (button), "win.run");
  g_signal_connect (button, "toggled", G_CALLBACK(power_monitor_run), window);
  gtk_container_add (GTK_CONTAINER (toolbar), GTK_WIDGET (button));
//  gtk_tool_item_set_expand (GTK_TOOL_ITEM (button), TRUE);
  g_object_set_data ((GObject*)window, "toolbar-run", button);

  gtk_grid_attach (GTK_GRID (grid), toolbar, 0, 0, 1, 1);

  /*setting bar*/
  frame = gtk_frame_new(NULL);
  gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_OUT);
  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
  
  gtk_container_add (GTK_CONTAINER (frame), box);

  /*device address setting*/
  label = gtk_label_new ("设备地址");
  gtk_container_add (GTK_CONTAINER (box), label);
  spin = gtk_spin_button_new_with_range(0, 30, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON (spin), 15);
  gtk_container_add (GTK_CONTAINER (box), spin);
  g_object_set_data ((GObject*)window, "toolbar-address", spin);
  gtk_widget_set_can_focus(spin, 0);
  global_data.disable_list = g_list_append(global_data.disable_list, spin);

  separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
  gtk_container_add (GTK_CONTAINER (box), separator);

  /*sample rate setting*/
  label = gtk_label_new ("采样速度(毫秒)");
  gtk_container_add (GTK_CONTAINER (box), label);
  spin = gtk_spin_button_new_with_range(15, 100, 2);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON (spin), 20);
  gtk_container_add (GTK_CONTAINER (box), spin);
  g_object_set_data ((GObject*)window, "toolbar-rate", spin);
  gtk_widget_set_can_focus(spin, 0);
  global_data.disable_list = g_list_append(global_data.disable_list, spin);

  separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
  gtk_container_add (GTK_CONTAINER (box), separator);

  label = gtk_label_new ("X轴刻度(秒)");
  gtk_container_add (GTK_CONTAINER (box), label);
  spin = gtk_spin_button_new_with_range(1, 100, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON (spin), 10);
  gtk_container_add (GTK_CONTAINER (box), spin);
  g_object_set_data ((GObject*)window, "toolbar-time-range", spin);
  gtk_widget_set_can_focus(spin, 0);
  g_signal_connect (spin, "value-changed", G_CALLBACK (xscale_change_cb), window);

  separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
  gtk_container_add (GTK_CONTAINER (box), separator);

  check = gtk_check_button_new_with_label ("自动调整功率(瓦)");
//  gtk_widget_set_valign (GTK_WIDGET(button), GTK_ALIGN_CENTER);
  //gtk_actionable_set_action_name (GTK_ACTIONABLE (check), "win.tuning");
  g_signal_connect (check, "toggled", G_CALLBACK(power_monitor_tuning), window);
  gtk_container_add (GTK_CONTAINER (box), check);
  g_object_set_data ((GObject*)window, "toolbar-tuning", check);
  global_data.disable_list = g_list_append(global_data.disable_list, check);

  spin = gtk_spin_button_new_with_range(2, 4, 0.5);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON (spin) ,2.5);
  gtk_container_add (GTK_CONTAINER (box), spin);
  g_object_set_data ((GObject*)window, "toolbar-tuning-power", spin);
  gtk_widget_set_can_focus(spin, 0);
  global_data.disable_list = g_list_append(global_data.disable_list, spin);

  gtk_grid_attach (GTK_GRID (grid), frame, 0, 1, 1, 1);

  /*power draw erea*/
  drawing_area = power_wave_new (NULL);

  gtk_grid_attach (GTK_GRID (grid), drawing_area, 0, 2, 1, 1);



	g_signal_connect (window, "visibility-notify-event",
				G_CALLBACK (window_visibility_event_cb), NULL);

	g_signal_connect (window, "window-state-event",
				G_CALLBACK ( window_state_event_cb), NULL);

	gtk_widget_set_events (window, gtk_widget_get_events (window)|GDK_VISIBILITY_NOTIFY_MASK|GDK_STRUCTURE_MASK);
	gtk_widget_show_all (window);
}

int
main (int    argc,
      char **argv)
{
	GtkWidget *window;
	int freq[30];

	adb_get_device_state();
//	disable_charging();
//  adb_get_cpus();
//	adb_get_cpu_freq_list(0, freq, 30);
//	adb_run_cpuburn_tool(2);
//	adb_get_cpu_policy(0);
//	adb_get_cpu_policy(4);
//	adb_get_cpu_policy(7);

	gtk_init (&argc, &argv);
	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

	power_monitor_init(window);

	gtk_main ();
	return 0;
}
