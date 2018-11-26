#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>

#define GRID_LEFT_MARGIN 50
#define GRID_RIGHT_MARGIN 20
#define GRID_TOP_MARGIN 20
#define GRID_BOTTOM_MARGIN 50

#define SCALE_LEFT_MARGIN (GRID_LEFT_MARGIN-5)
#define SCALE_RIGHT_MARGIN (GRID_RIGHT_MARGIN-5)
#define SCALE_TOP_MARGIN (GRID_TOP_MARGIN-5)
#define SCALE_BOTTOM_MARGIN  (GRID_BOTTOM_MARGIN-5)

#define SCALE_SHORT_LENGHT 5
#define SCALE_LONG_LENGHT 10

#define TIME_SCALE_UNIT 1//1s
#define CURRENT_SCALE_UNIT 5//10mA

#define DEFAULT_WINDOW_WIDTH 800
#define DEFAULT_WINDOW_HEIGHT 400

#define MIN_SHOW_DATA_COUNT	10

#define PWD_FILE_MAGIC "PWD-FILE"

struct power_data{
	gdouble current; //current unit:mA
	gdouble voltage; //current unit:V
	gdouble power; //watt unit:W
	gdouble timestamp;//s
};

struct power_wave_info{
	char *meter_name;

	gboolean run_state;//

	gdouble max_data;
	gdouble min_data;
	gdouble avg_data;
	gdouble sum_data;
	int data_count;
	gdouble last_timestamp;//s, last input data timestamp

	gdouble start_draw_timestamp;//s, start timestamp in x_axis
	gdouble end_draw_timestamp;//s, end timestamp in x_axis
	int start_draw_timestamp_s;//s, start timestamp in x_axis
	gdouble start_draw_timestamp_ms;//ms, start timestamp in x_axis
	gdouble scrollbar_value;//scrollbar value

	GList *data_list;
	GList *data_first;
	GList *begin_show_data;
	GList *end_show_data;

	GtkWidget *drawing_area;
	GdkFrameClock *frame_clock;
	GtkAdjustment *hadj;

	int max_show_count;//ms
	unsigned int show_time_range;//ms

	gdouble x_grid_origin;//draw data x origin
	gdouble y_grid_origin;//draw data y origin

	gdouble x_grid_width;//draw data area scale width
	gdouble y_grid_height;//draw data area scale height

	gdouble x_real_width;//draw data area real width
	gdouble y_real_height;//draw data area real  height

	gdouble x_scale;// = x_grid_width/x_real_width
	gdouble y_scale;// = y_grid_height/y_real_height

	gdouble x_marker_origin;//draw marker x origin
	gdouble y_marker_origin;//draw marker y origin

	gdouble x_marker_width;//draw marker area width
	gdouble y_marker_height;//draw marker area height

	int x_marker_unit;// time per pixel;(S/pixel)
	int y_marker_unit;// data per pixel;(mA/pixel)

	gdouble cursor_begin_x;//
	gdouble cursor_end_x;//
	gdouble cursor_max_data;
	gdouble cursor_min_data;
	gdouble cursor_avg_data;

};

struct power_wave_file_header{
	char magic[8];
	char datetime[32];
	int length;
	int checksum;
};

#define MAX_FRAME_QUEUE_SIZE	5
static GQueue *frame_queue;
static GMutex frame_mutex;
static GCond frame_cond;

static struct power_wave_info *get_power_wave_info (void)
{
	static struct power_wave_info pwinfo;
	static int power_wave_init = 0;
	if(power_wave_init == 0){
		memset(&pwinfo, 0, sizeof(struct power_wave_info));
		power_wave_init = 1;
	}
	return &pwinfo;
}

#if 0
static void
queue_frame (cairo_surface_t *frame_data)
{
	g_mutex_lock (&frame_mutex);

	if(frame_queue->length == MAX_FRAME_QUEUE_SIZE){
		//g_mutex_unlock (&frame_mutex);
		g_cond_wait (&frame_cond, &frame_mutex);
		//return;
	}

	g_queue_push_tail (frame_queue, frame_data);
	
	g_mutex_unlock (&frame_mutex);
}

static cairo_surface_t *
unqueue_frame (void)
{
	cairo_surface_t *frame_data;

	g_mutex_lock (&frame_mutex);

	if (frame_queue->length > 0)
	{
		frame_data = g_queue_pop_head (frame_queue);
		g_cond_signal (&frame_cond);
	}else{
		frame_data = NULL;
	}

	g_mutex_unlock (&frame_mutex);

	return frame_data;
}
#endif

void power_wave_update_data(void)
{
	struct power_wave_info *pwinfo = get_power_wave_info();

	gtk_widget_queue_draw(pwinfo->drawing_area);
}

static void draw_prepare (int width, int height)
{
	struct power_wave_info *pwinfo = get_power_wave_info();
	gdouble start_draw_timestamp;//s
	gdouble end_draw_timestamp;//s
	GList *l;
	gdouble sum_data = 0;
	struct power_data *data;
	gdouble x;
	gdouble cursor_begin_x;
	gdouble cursor_end_x;
	gdouble cursor_temp;
	gdouble cursor_x1 = 0;
	gdouble cursor_y1 = 0;
	gdouble cursor_x2 = 0;
	gdouble cursor_y2 = 0;
	int data_count = 0;

	pwinfo->x_grid_width = width - GRID_LEFT_MARGIN - GRID_RIGHT_MARGIN;
	pwinfo->y_grid_height = height - GRID_BOTTOM_MARGIN - GRID_TOP_MARGIN;
	pwinfo->x_marker_width = width - SCALE_LEFT_MARGIN - SCALE_RIGHT_MARGIN;
	pwinfo->y_marker_height = height - SCALE_BOTTOM_MARGIN - SCALE_TOP_MARGIN;

	pwinfo->x_grid_origin = GRID_LEFT_MARGIN;
	pwinfo->y_grid_origin = height-GRID_BOTTOM_MARGIN;
	pwinfo->x_marker_origin = SCALE_LEFT_MARGIN;
	pwinfo->y_marker_origin = height-SCALE_BOTTOM_MARGIN;

	pwinfo->start_draw_timestamp = 0;
	pwinfo->start_draw_timestamp_s = 0;
	pwinfo->start_draw_timestamp_ms = 0;

	start_draw_timestamp = pwinfo->last_timestamp - pwinfo->show_time_range;
	start_draw_timestamp = start_draw_timestamp>0?start_draw_timestamp:0;

	end_draw_timestamp = pwinfo->last_timestamp;

	if( pwinfo->run_state == FALSE){
		start_draw_timestamp = pwinfo->scrollbar_value;
		end_draw_timestamp = start_draw_timestamp + pwinfo->show_time_range;
	}

	pwinfo->begin_show_data = NULL;
	for (l = pwinfo->data_first; l != NULL; l = l->prev)
	{
		data = (struct power_data *)l->data;
		if(data->timestamp >= start_draw_timestamp){
			pwinfo->start_draw_timestamp = data->timestamp;
			pwinfo->start_draw_timestamp_s = (int)data->timestamp;
			pwinfo->start_draw_timestamp_ms = data->timestamp - pwinfo->start_draw_timestamp_s;
			pwinfo->begin_show_data = l;
			break;	
		}
	}

	pwinfo->end_show_data = pwinfo->begin_show_data;
	for (l = pwinfo->begin_show_data; l != NULL; l = l->prev)
	{
		data = (struct power_data *)l->data;
		if(data->timestamp >= end_draw_timestamp){
			pwinfo->end_show_data = l;
			break;
		}
	}

	pwinfo->x_real_width = pwinfo->show_time_range;
	pwinfo->y_real_height = ((int)pwinfo->max_data/50 + 1) * 50;

	pwinfo->x_scale = pwinfo->x_grid_width/pwinfo->x_real_width;
	pwinfo->y_scale = pwinfo->y_grid_height/pwinfo->y_real_height;

	pwinfo->y_marker_unit = (int)(CURRENT_SCALE_UNIT/pwinfo->y_scale);

	if(pwinfo->y_marker_unit<CURRENT_SCALE_UNIT)
		pwinfo->y_marker_unit = CURRENT_SCALE_UNIT;

	if( pwinfo->run_state == FALSE && pwinfo->cursor_begin_x >= 0){
		cursor_begin_x = pwinfo->cursor_begin_x / pwinfo->x_scale;
		cursor_end_x = pwinfo->cursor_end_x / pwinfo->x_scale;

		if(cursor_begin_x > cursor_end_x){
			cursor_temp = cursor_begin_x;
			cursor_begin_x = cursor_end_x;
			cursor_end_x = cursor_temp;
		}


		for (l = pwinfo->begin_show_data; l != NULL; l = l->prev)
		{
			data = (struct power_data *)l->data;

			x = data->timestamp - pwinfo->start_draw_timestamp;
			if(cursor_begin_x == cursor_end_x){
				if(x <= cursor_begin_x){//find a data nearer to cursor(left)
					pwinfo->cursor_min_data = data->current;
					pwinfo->cursor_max_data = data->current;
					pwinfo->cursor_avg_data = data->current;
					cursor_x1 = x;
					cursor_y1 = data->current;
				}else{//find a data nearer to cursor(right), cal data value between a and b
					cursor_x2 = x;
					cursor_y2 = data->current;
					pwinfo->cursor_avg_data = cursor_y2	- (cursor_x2-cursor_begin_x)*((cursor_y2-cursor_y1)/(cursor_x2-cursor_x1));
					pwinfo->cursor_min_data = pwinfo->cursor_avg_data;
					pwinfo->cursor_max_data = pwinfo->cursor_avg_data;
					break;
				}
			}else if(x >= cursor_begin_x && x <= cursor_end_x){
				if(data_count == 0){
					pwinfo->cursor_min_data = data->current;
					pwinfo->cursor_max_data = data->current;
				}

				if(data->current < pwinfo->cursor_min_data)
					pwinfo->cursor_min_data = data->current;
				if(pwinfo->cursor_max_data < data->current)
					pwinfo->cursor_max_data = data->current;

				sum_data += data->current;
				data_count ++;
				pwinfo->cursor_avg_data = sum_data/data_count;
			}
			if(l == pwinfo->end_show_data)
				break;
		}
	}else{
		pwinfo->cursor_max_data = pwinfo->max_data;
		pwinfo->cursor_min_data = pwinfo->min_data;
		pwinfo->cursor_avg_data = pwinfo->avg_data;
	}
}

/*Draw power couve*/
static void
draw_key_data (cairo_t *cr)
{
	struct power_wave_info *pwinfo = get_power_wave_info();
	gdouble x;
	gdouble y;
	gdouble max_data;
	gdouble min_data;
	gdouble avg_data;
	cairo_text_extents_t te;
	char scale_text[16];

	max_data = pwinfo->cursor_max_data;
	min_data = pwinfo->cursor_min_data;
	avg_data = pwinfo->cursor_avg_data;

	/*draw wave*/
	cairo_set_source_rgb(cr, 0, 1, 0);//green
	cairo_set_line_width (cr, 0.5);
	cairo_rectangle(cr, SCALE_LEFT_MARGIN+2, SCALE_TOP_MARGIN+2, 100, 45);
    cairo_fill(cr);

	cairo_set_source_rgb(cr, 0, 0, 0);//blank

	memset(scale_text, 0, 16);
	g_sprintf(scale_text, "max = %0.4f", max_data);

	cairo_text_extents (cr, scale_text, &te);
	cairo_set_font_size (cr, 10);
	x = SCALE_LEFT_MARGIN+5;
	y = SCALE_TOP_MARGIN+5+te.height;
	cairo_move_to(cr, x, y);
	cairo_show_text(cr, scale_text);

	y += (te.height+5);
	memset(scale_text, 0, 16);
	g_sprintf(scale_text, "min = %0.4f", min_data);

	cairo_text_extents (cr, scale_text, &te);
	cairo_set_font_size (cr, 10);
	cairo_move_to(cr, x, y);
	cairo_show_text(cr, scale_text);

	y += (te.height+5);
	memset(scale_text, 0, 16);
	g_sprintf(scale_text, "avg = %0.4f", avg_data);

	cairo_text_extents (cr, scale_text, &te);
	cairo_set_font_size (cr, 10);
	cairo_move_to(cr, x, y);
	cairo_show_text(cr, scale_text);

//	printf("x axis: %d, %f, %f, x_scale=%f\n", i, x, y, pwinfo->x_scale);
}

static void draw_power_cursor_line (cairo_t *cr)
{
	struct power_wave_info *pwinfo = get_power_wave_info();

	if( pwinfo->run_state == TRUE )
		return;
	if(pwinfo->cursor_begin_x < 0)
		return;
	cairo_set_source_rgba(cr, 1, 0, 0, 1);
	cairo_set_line_width (cr, 1);
	cairo_save (cr);
 	/*draw wave*/
	cairo_translate (cr, pwinfo->x_grid_origin, pwinfo->y_grid_origin);

	if(pwinfo->cursor_begin_x == pwinfo->cursor_end_x){
		cairo_move_to(cr, pwinfo->cursor_begin_x, 0);
		cairo_line_to(cr, pwinfo->cursor_begin_x, -pwinfo->y_grid_height);
	}else{
		cairo_rectangle(cr, pwinfo->cursor_begin_x, 0, pwinfo->cursor_end_x-pwinfo->cursor_begin_x, -pwinfo->y_grid_height);
	}
	cairo_restore (cr);
    cairo_stroke(cr);

}
/*Draw power couve*/
static void draw_power_data (cairo_t *cr)
{
	struct power_wave_info *pwinfo = get_power_wave_info();
	gdouble x;
	gdouble y;
	GList *l;
	struct power_data *data;

	cairo_set_source_rgba(cr, 0, 0, 1, 1);
	cairo_set_line_width (cr, 1);
	cairo_save (cr);
 	/*draw wave*/
	cairo_translate (cr, pwinfo->x_grid_origin, pwinfo->y_grid_origin);
	cairo_scale(cr,pwinfo->x_scale, pwinfo->y_scale);

	for (l = pwinfo->begin_show_data; l != NULL; l = l->prev)
	{
		data = (struct power_data *)l->data;
		y = data->current * -1;
		x = data->timestamp - pwinfo->start_draw_timestamp;
		if(pwinfo->begin_show_data == l)
			cairo_move_to(cr, x, y);
		cairo_line_to(cr, x, y);

		if(l == pwinfo->end_show_data)
			break;
	}
	cairo_restore (cr);
	cairo_stroke(cr);
}

/*Draw grid*/
static void draw_grid_marker (cairo_t *cr)
{
	struct power_wave_info *pwinfo = get_power_wave_info();
	gdouble x;
	gdouble y;
	int i;
    gdouble bigdashes[] = {3,5,3,5};
    int bigdash = sizeof (bigdashes) / sizeof (bigdashes [0]);
	
	/*draw y_axis grid*/
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_set_line_width (cr, 0.25);
	cairo_set_dash (cr, bigdashes, bigdash, 0);

	cairo_save (cr);
	cairo_translate (cr, pwinfo->x_grid_origin, pwinfo->y_grid_origin);
	cairo_scale(cr,pwinfo->x_scale, pwinfo->y_scale);

	for(i=0,y=0; y>=-pwinfo->y_real_height; y-=pwinfo->y_marker_unit, i++){
		if(i%5 == 0){
			cairo_move_to(cr, 0, y);
			cairo_line_to(cr, pwinfo->x_real_width, y);
		}
	}

	/*draw x_axis grid*/
	//always draw the leftmost line of grid 
    cairo_move_to(cr, 0, 0);
	cairo_line_to(cr, 0, -pwinfo->y_real_height);

	for(i=0, x=-pwinfo->start_draw_timestamp_ms; x<=pwinfo->x_real_width; x+=TIME_SCALE_UNIT, i++){
		if(x>=0){
		    cairo_move_to(cr, x, 0);
			cairo_line_to(cr, x, -pwinfo->y_real_height);
	    }
	}
	cairo_restore (cr);
	cairo_stroke(cr);
	cairo_set_dash (cr, bigdashes, 0, 0);//dash off
}

/*Draw time scale*/
static void draw_time_marker (cairo_t *cr)
{
	struct power_wave_info *pwinfo = get_power_wave_info();
	gdouble x;
	gdouble y;
	gdouble y_scale;
	int i;
	cairo_text_extents_t te;
	char scale_text[16];

	//draw scale
	cairo_set_source_rgb(cr, 0, 0, 0);//blank
	cairo_set_line_width (cr, 0.5);
	cairo_rectangle(cr, SCALE_LEFT_MARGIN, SCALE_TOP_MARGIN, pwinfo->x_marker_width, pwinfo->y_marker_height);
    cairo_stroke(cr);

	cairo_set_source_rgb(cr, 1, 0, 0);//red
	cairo_set_line_width (cr, 0.5);
	cairo_set_font_size (cr, 10);

	cairo_save (cr);
	cairo_translate (cr, pwinfo->x_grid_origin, pwinfo->y_marker_origin);
	/*x axis scale*/
//	cairo_scale(cr,pwinfo->x_scale, 1);
	for(i=pwinfo->start_draw_timestamp_s, x=-pwinfo->start_draw_timestamp_ms*pwinfo->x_scale; x<=pwinfo->x_grid_width; x+=TIME_SCALE_UNIT*pwinfo->x_scale, i++){
//	for(i=pwinfo->start_draw_timestamp_s, x=-pwinfo->start_draw_timestamp_ms; x<=pwinfo->x_real_width; x+=TIME_SCALE_UNIT, i++){
		if(x>=0){
			cairo_move_to(cr, x, 0);
			cairo_line_to(cr, x, SCALE_LONG_LENGHT);

			memset(scale_text, 0, 16);
			g_sprintf(scale_text, "%d", i*TIME_SCALE_UNIT);
			//printf("x axis: %d, %f, %f,%f x_scale=%f\n", i, x, y, pwinfo->show_start_draw_timestamp, pwinfo->x_scale);
			cairo_text_extents (cr, scale_text, &te);
			cairo_device_to_user_distance(cr, &te.width, &te.height);
			cairo_move_to(cr, x-te.width/2, SCALE_LONG_LENGHT+te.height);
			cairo_show_text(cr, scale_text);
//			cairo_text_path(cr, scale_text);
//			cairo_fill_preserve(cr);
		}
	}
	cairo_restore (cr);
	cairo_stroke(cr);

	cairo_save (cr);
	cairo_translate (cr, pwinfo->x_marker_origin, pwinfo->y_grid_origin);
	/*y axis scale*/
	for(i=0, y=0; y<=pwinfo->y_grid_height; y+=pwinfo->y_marker_unit*pwinfo->y_scale, i++){
		cairo_move_to(cr, 0, -y);
		if(i%5){
			cairo_line_to(cr, -SCALE_SHORT_LENGHT, -y);
		}else{
			cairo_line_to(cr, -SCALE_LONG_LENGHT, -y);

			memset(scale_text, 0, 16);
			y_scale = i*pwinfo->y_marker_unit;
			//printf("y axis: %d, %f, %f, y_scale=%f\n", i, x, y, pwinfo->y_scale);
			g_sprintf(scale_text, "%d", (int)y_scale);
			cairo_text_extents (cr, scale_text, &te);
			cairo_move_to(cr, -SCALE_LONG_LENGHT-te.width, -y+te.height/2);
			cairo_show_text(cr, scale_text);
		}
	}
	cairo_restore (cr);
	cairo_stroke(cr);

	/*draw xy axis unit*/
	cairo_set_source_rgb(cr, 0, 0, 0);//blank
	cairo_set_font_size (cr, 12);
	cairo_text_extents (cr, "time(s)", &te);
	cairo_move_to(cr, GRID_LEFT_MARGIN + pwinfo->x_grid_width/2 - te.width/2, pwinfo->y_marker_origin+SCALE_LONG_LENGHT+te.height*2);
	cairo_show_text(cr, "time(s)");

	cairo_save (cr);
	cairo_set_font_size (cr, 12);
	cairo_text_extents (cr, "current(mA)", &te);
	cairo_translate(cr, te.height, GRID_TOP_MARGIN+(pwinfo->y_grid_height)/2+te.width/2);
	cairo_rotate(cr, -G_PI/2);
	cairo_move_to(cr, 0 ,0);
	cairo_show_text(cr, "current(mA)");
	cairo_restore (cr);
}

static void
clear_surface (cairo_t *cr)
{
  cairo_set_source_rgb (cr, 1, 1, 1);
  cairo_paint (cr);
}

static void
draw_all (GtkWidget *widget, cairo_t *cr)
{
  cairo_surface_t *overlay;
  cairo_t *overlay_cr;
  cairo_rectangle_t rect;
  int width = gtk_widget_get_allocated_width (widget);
  int height = gtk_widget_get_allocated_height (widget);


//  printf("%s: width = %d, height=%d\n", __func__, width, height);
  rect.x = 0;
  rect.y = 0;
  rect.width = width;
  rect.height = height;
  overlay = cairo_recording_surface_create(CAIRO_CONTENT_COLOR, &rect);

  overlay_cr = cairo_create (overlay);

  //clear surface
  clear_surface (overlay_cr);

  cairo_set_source_rgb (overlay_cr, 0., 0., 0.);

  draw_prepare(width, height);
  draw_time_marker(overlay_cr);
  draw_grid_marker(overlay_cr);
  draw_power_data(overlay_cr);
  draw_power_cursor_line(overlay_cr);
  draw_key_data(overlay_cr);

  cairo_set_source_surface (cr, overlay, 0, 0);

  cairo_paint (cr);
  cairo_destroy (overlay_cr);
  cairo_surface_destroy (overlay);

}


/* Create a new surface of the appropriate size to store our scribbles */
static gboolean
configure_event_cb (GtkWidget         *widget,
                    GdkEventConfigure *event,
                    gpointer           data)
{
	/* Initialize the surface to white */
//	draw_all(widget);
	printf("%s\n", __func__);
	/* We've handled the configure event, no need for further processing. */
	return TRUE;
}

/* Redraw the screen from the surface. Note that the ::draw
 * signal receives a ready-to-be-used cairo_t that is already
 * clipped to only draw the exposed areas of the widget
 */
static gboolean
draw_cb (GtkWidget *widget,
         cairo_t   *cr,
         gpointer   data)
{
#if 0
	cairo_surface_t *src_surface = NULL;

	src_surface = unqueue_frame();
	if(src_surface){
		printf("%s: begin\n", __func__);
		cairo_set_source_surface (cr, src_surface, 0, 0);

		cairo_paint (cr);
		cairo_surface_destroy (src_surface);
		printf("%s: end\n", __func__);
	}
#else
	draw_all(widget, cr);
#endif
	return TRUE;
}

/* Handle button press events by either drawing a rectangle
 * or clearing the surface, depending on which button was pressed.
 * The ::button-press signal handler receives a GdkEventButton
 * struct which contains this information.
 */
static gboolean
button_press_event_cb (GtkWidget      *widget,
                       GdkEventButton *event,
                       gpointer        data)
{
  struct power_wave_info *pwinfo = get_power_wave_info();
  int width = gtk_widget_get_allocated_width (widget);
  int height = gtk_widget_get_allocated_height (widget);

  if (event->button == GDK_BUTTON_PRIMARY && pwinfo->run_state == FALSE)
    {
	  if(event->x >= GRID_LEFT_MARGIN && event->x <= (width - GRID_RIGHT_MARGIN)
			 && event->y >= GRID_TOP_MARGIN && event->y <= (height-GRID_BOTTOM_MARGIN)){
	    pwinfo->cursor_begin_x = event->x - GRID_LEFT_MARGIN;
	    pwinfo->cursor_end_x = event->x - GRID_LEFT_MARGIN;
		power_wave_update_data();
		//printf("x=%f, width = %d\n", event->x, width);
	  }
    }else if(event->button == GDK_BUTTON_SECONDARY && pwinfo->run_state == FALSE){
	    pwinfo->cursor_begin_x = -1;
	    pwinfo->cursor_end_x = -1;
		power_wave_update_data();
	}
  /* We've handled the event, stop processing */
  return TRUE;
}

/* Handle motion events by continuing to draw if button 1 is
 * still held down. The ::motion-notify signal handler receives
 * a GdkEventMotion struct which contains this information.
 */
static gboolean
motion_notify_event_cb (GtkWidget      *widget,
                        GdkEventMotion *event,
                        gpointer        data)
{
	struct power_wave_info *pwinfo = get_power_wave_info();
	int width = gtk_widget_get_allocated_width (widget);
	int height = gtk_widget_get_allocated_height (widget);

	if (event->state & GDK_BUTTON1_MASK && pwinfo->run_state == FALSE){
		if(event->x >= GRID_LEFT_MARGIN && event->x <= (width - GRID_RIGHT_MARGIN)
			 && event->y >= GRID_TOP_MARGIN && event->y <= (height-GRID_BOTTOM_MARGIN)){
			printf("x=%f, width = %d\n", event->x, width);
			pwinfo->cursor_end_x = event->x - GRID_LEFT_MARGIN;
			power_wave_update_data();
		}
	}

	/* We've handled it, stop processing */
	return TRUE;
}

gdouble power_wave_get_avg(int time_range)
{
	struct power_wave_info *pwinfo = get_power_wave_info();
	GList *l;
	GList *begin_avg_data;
	int count = 0;
	gdouble sum, start_avg_timestamp;
	struct power_data *data;

	start_avg_timestamp = pwinfo->last_timestamp - time_range;

	begin_avg_data = NULL;
	for (l = pwinfo->data_first; l != NULL; l = l->prev)
	{
		data = (struct power_data *)l->data;
		if(data->timestamp >= start_avg_timestamp){
			begin_avg_data = l;
			break;	
		}
	}

	for (l = begin_avg_data; l != NULL; l = l->prev)
	{
		data = (struct power_data *)l->data;

		count ++;
		sum += data->power;
	}

	return count ? sum/count : 0;
}

void power_wave_insert_data(gdouble current, gdouble volt, gdouble timestamp)
{
	struct power_wave_info *pwinfo = get_power_wave_info();
	struct power_data *data;
	struct power_data *data_first;

	data = g_malloc(sizeof(struct power_data));
	if(data != NULL){
		data->current = current*1000;//A --> mA
		data->voltage = volt;
		data->power = data->current*data->voltage/1000;
		data->timestamp = timestamp;

		if(pwinfo->data_count == 0)
		{
			pwinfo->max_data = data->current;
			pwinfo->min_data = data->current;
			//add first data in timestamp = 0
			data_first = g_malloc(sizeof(struct power_data));
			if(data != NULL){
				data_first->current = data->current;
				data_first->voltage = data->voltage;
				data_first->power = data->power;
				data_first->timestamp = 0;
				pwinfo->data_list = g_list_append(pwinfo->data_list, data_first); 
				pwinfo->data_first = pwinfo->data_list;
				pwinfo->data_count ++;
				pwinfo->sum_data += data_first->current;
			}
		}

		if(data->current < pwinfo->min_data)
			pwinfo->min_data = data->current;
		if(pwinfo->max_data < data->current)
			pwinfo->max_data = data->current;

		pwinfo->sum_data += data->current;
		pwinfo->data_count ++;
		pwinfo->avg_data = pwinfo->sum_data/pwinfo->data_count;
		pwinfo->last_timestamp = data->timestamp;

		pwinfo->data_list = g_list_prepend(pwinfo->data_list, data); 
	}
}

static void power_wave_free_data(gpointer data)
{
	g_free((struct power_data*)data);

}

static void power_wave_reset_data(void)
{
	struct power_wave_info *pwinfo = get_power_wave_info();

	pwinfo->cursor_begin_x = -1;
	pwinfo->cursor_end_x = -1;
	pwinfo->scrollbar_value = 0;

	pwinfo->data_count = 0;
	pwinfo->sum_data = 0;
	pwinfo->max_data = 0;
	pwinfo->min_data = 0;
	pwinfo->avg_data = 0;
	pwinfo->last_timestamp = 0;
	pwinfo->max_show_count = 10;
	pwinfo->show_time_range = 10;
	g_list_free_full(pwinfo->data_list, power_wave_free_data);
	pwinfo->data_list = NULL;
	pwinfo->data_first = NULL;
}

static void power_wave_adjustment_config(struct power_wave_info *pwinfo)
{
	gdouble start_scroll;


	if ( pwinfo->run_state == FALSE ){
		start_scroll = pwinfo->last_timestamp - pwinfo->show_time_range;
		start_scroll = start_scroll>0?start_scroll:0;
		gtk_adjustment_configure(pwinfo->hadj, start_scroll, 0, pwinfo->last_timestamp, 1, 1, pwinfo->show_time_range);
	}
}

void power_wave_start(void)
{
	struct power_wave_info *pwinfo = get_power_wave_info();

	power_wave_reset_data();
	pwinfo->run_state = TRUE;
	gtk_adjustment_configure(pwinfo->hadj, 0, 0, 0, 0, 0, 0);
}

void power_wave_stop(void)
{
	struct power_wave_info *pwinfo = get_power_wave_info();

	pwinfo->run_state = FALSE;
	power_wave_adjustment_config(pwinfo);
}

void power_wave_set_range(unsigned int time_range)
{
	struct power_wave_info *pwinfo = get_power_wave_info();

	if(time_range > 0){
		pwinfo->show_time_range = time_range;

		power_wave_adjustment_config(pwinfo);
	}
}

int power_wave_save_file(char *filename)
{
	struct power_wave_info *pwinfo = get_power_wave_info();
	int fd;
	struct power_wave_file_header fheader;
	GList *l;
	struct power_data *data;

	fd = open(filename, O_RDWR | O_CREAT | O_TRUNC);
	if(fd<0){
		printf("creat file %s fail!\n", filename);
		return -1;
	}
	//write file header
	strncpy(fheader.magic, PWD_FILE_MAGIC, 8);
	strcpy(fheader.datetime, "2018/10/9 11:00:00");
	fheader.length = 100;
	fheader.checksum = 0;
	write(fd, &fheader, sizeof(struct power_wave_file_header));

	//write data
	for (l = pwinfo->data_first; l != NULL; l = l->prev)
	{
		data = (struct power_data *)l->data;
		write(fd, data, sizeof(struct power_data));
	}

	close(fd);
	return 0;
}

int power_wave_open_file(char *filename)
{
	struct power_wave_info *pwinfo = get_power_wave_info();
	int fd;
	int len;
	struct power_wave_file_header fheader;
	struct power_data data;

	fd = open(filename, O_RDONLY);
	if(fd<0){
		printf("open file %s fail!\n", filename);
		return -1;
	}
	//read file header
	len = read(fd, &fheader, sizeof(struct power_wave_file_header));
	if(len != sizeof(struct power_wave_file_header))
	{
		goto read_err;
	}

	if(strncmp(fheader.magic, PWD_FILE_MAGIC, 8) != 0)
		goto read_err;

	//clean old data
	power_wave_reset_data();
	//read data
	len = read(fd, &data, sizeof(struct power_data));
	while(len == sizeof(struct power_data)){
		power_wave_insert_data(data.current/1000, data.voltage, data.timestamp);
		len = read(fd, &data, sizeof(struct power_data));
	}
	close(fd);

	power_wave_adjustment_config(pwinfo);
	return 0;
read_err:
	printf("read file %s fail!\n", filename);
	close(fd);
	return -1;
	
}

static void
value_changed_cb (GtkAdjustment *a)
{
	struct power_wave_info *pwinfo = get_power_wave_info();

	if ( pwinfo->run_state == FALSE ){
		pwinfo->scrollbar_value = gtk_adjustment_get_value(a);

		power_wave_update_data();
	}
}

GtkWidget *power_wave_new (GtkWidget *parant)
{
	struct power_wave_info *pwinfo = get_power_wave_info();
	GtkWidget *drawing_area;
	GtkWidget *hscrollbar;
	GtkWidget *box;
	GtkAdjustment *hadjustment;
	gdouble a;

	power_wave_reset_data();
	pwinfo->run_state = FALSE;

	a = cos(1);
	printf("%f\n", a);
	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

	drawing_area = gtk_drawing_area_new ();

	gtk_widget_set_hexpand (drawing_area, TRUE);
	gtk_widget_set_vexpand (drawing_area, TRUE);
	gtk_widget_set_size_request (drawing_area, DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT);

	gtk_container_add (GTK_CONTAINER (box), drawing_area);

	hadjustment = gtk_adjustment_new(0, 0, 0, 0, 0, 0);

	hscrollbar = gtk_scrollbar_new(GTK_ORIENTATION_HORIZONTAL, hadjustment);
	gtk_container_add (GTK_CONTAINER (box), hscrollbar);

	pwinfo->hadj = hadjustment;

	g_signal_connect (hadjustment, "value-changed", G_CALLBACK (value_changed_cb), NULL);

	/* Signals used to handle the backing surface */
	g_signal_connect (drawing_area, "draw",
                    G_CALLBACK (draw_cb), NULL);
	g_signal_connect (drawing_area,"configure-event",
                    G_CALLBACK (configure_event_cb), NULL);
//	g_signal_connect (drawing_area,"expose-event",
//                  G_CALLBACK (expose_event_cb), NULL);


	/* Ask to receive events the drawing area doesn't normally
	* subscribe to. In particular, we need to ask for the
	* button press and motion notify events that want to handle.
	*/
	/* Event signals */
	g_signal_connect (drawing_area, "motion-notify-event",
					G_CALLBACK (motion_notify_event_cb), NULL);
	g_signal_connect (drawing_area, "button-press-event",
					G_CALLBACK (button_press_event_cb), NULL);

	gtk_widget_set_events (drawing_area, gtk_widget_get_events (drawing_area)|GDK_BUTTON_PRESS_MASK|GDK_POINTER_MOTION_MASK);

	pwinfo->drawing_area = drawing_area;

	frame_queue = g_queue_new ();
	g_mutex_init (&frame_mutex);
	g_cond_init (&frame_cond);

	return box;
}

void power_wave_free(void)
{
	power_wave_reset_data();
}


