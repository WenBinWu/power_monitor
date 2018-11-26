#include <gtk/gtk.h>
#include <string.h>
#include "power_wave.h"

#define DEFAULT_WINDOW_WIDTH 200
#define DEFAULT_WINDOW_HEIGHT 100

/* Function to open a dialog box displaying the message provided. */
void power_config_new (GtkWindow *parent)
{
  GtkWidget *content_area;
  GtkWidget *grid;
  GtkWidget *scrolled, *check, *entry, *box, *label, *spin, *separator;
	      /* Create the widgets */
  GtkWidget *dialog = gtk_dialog_new_with_buttons ("配置",
	                                          parent,
                                              GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
	                                          "OK",
	                                          GTK_RESPONSE_ACCEPT,
	                                          "Cancel",
	                                          GTK_RESPONSE_REJECT,
	                                          NULL);

  content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

  grid = gtk_grid_new ();
  gtk_container_add (GTK_CONTAINER (content_area), grid);
  gtk_widget_set_size_request (grid, DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT);

  /*device address setting*/
  label = gtk_label_new ("设备地址");
  gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);

  spin = gtk_spin_button_new_with_range(0, 30, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON (spin), 15);
  gtk_grid_attach (GTK_GRID (grid), spin, 1, 0, 1, 1);

//  g_object_set_data ((GObject*)window, "toolbar-address", spin);
  gtk_widget_set_can_focus(spin, 0);

  /*sample rate setting*/
  label = gtk_label_new ("采样速度(毫秒)");
  gtk_grid_attach (GTK_GRID (grid), label, 0, 1, 1, 1);

  spin = gtk_spin_button_new_with_range(2, 100, 2);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON (spin), 20);
  gtk_grid_attach (GTK_GRID (grid), spin, 1, 1, 1, 1);

//  g_object_set_data ((GObject*)window, "toolbar-rate", spin);
  gtk_widget_set_can_focus(spin, 0);


  check = gtk_check_button_new_with_label ("自动调整功率(瓦)");
//  gtk_widget_set_valign (GTK_WIDGET(button), GTK_ALIGN_CENTER);
//  gtk_actionable_set_action_name (GTK_ACTIONABLE (check), "win.tuning");
  gtk_grid_attach (GTK_GRID (grid), check, 0, 2, 1, 1);
 // g_object_set_data ((GObject*)window, "toolbar-tuning", check);

  spin = gtk_spin_button_new_with_range(2, 4, 0.5);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON (spin) ,2.5);
  gtk_grid_attach (GTK_GRID (grid), spin, 1, 2, 1, 1);

//  g_object_set_data ((GObject*)window, "toolbar-tuning-power", spin);
  gtk_widget_set_can_focus(spin, 0);

 
	/* Ensure that the dialog box is destroyed when the user responds */
	g_signal_connect_swapped (dialog, "response", G_CALLBACK (gtk_widget_destroy),  dialog);
	/* Add the label, and show everything we've added to the dialog */
	gtk_widget_show_all (dialog);
}
