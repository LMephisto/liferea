/*
 * DO NOT EDIT THIS FILE - it is generated by Glade.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include "bloglines_source-cb.h"
#include "bloglines_source-ui.h"
#include "support.h"

#define GLADE_HOOKUP_OBJECT(component,widget,name) \
  g_object_set_data_full (G_OBJECT (component), name, \
    gtk_widget_ref (widget), (GDestroyNotify) gtk_widget_unref)

#define GLADE_HOOKUP_OBJECT_NO_REF(component,widget,name) \
  g_object_set_data (G_OBJECT (component), name, widget)

GtkWidget*
create_bloglines_source_dialog (void)
{
  GtkWidget *bloglines_source_dialog;
  GtkWidget *dialog_vbox1;
  GtkWidget *vbox1;
  GtkWidget *label1;
  GtkWidget *table1;
  GtkWidget *label2;
  GtkWidget *label3;
  GtkWidget *userEntry;
  GtkWidget *passwordEntry;
  GtkWidget *dialog_action_area1;
  GtkWidget *cancelbutton1;
  GtkWidget *okbutton1;

  bloglines_source_dialog = gtk_dialog_new ();
  gtk_window_set_title (GTK_WINDOW (bloglines_source_dialog), _("Add Bloglines Account"));
  gtk_window_set_type_hint (GTK_WINDOW (bloglines_source_dialog), GDK_WINDOW_TYPE_HINT_DIALOG);

  dialog_vbox1 = GTK_DIALOG (bloglines_source_dialog)->vbox;
  gtk_widget_show (dialog_vbox1);

  vbox1 = gtk_vbox_new (FALSE, 12);
  gtk_widget_show (vbox1);
  gtk_box_pack_start (GTK_BOX (dialog_vbox1), vbox1, TRUE, TRUE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox1), 12);

  label1 = gtk_label_new (_("Please enter your Bloglines account settings."));
  gtk_widget_show (label1);
  gtk_box_pack_start (GTK_BOX (vbox1), label1, FALSE, FALSE, 0);
  gtk_label_set_line_wrap (GTK_LABEL (label1), TRUE);
  gtk_misc_set_alignment (GTK_MISC (label1), 0, 0.5);

  table1 = gtk_table_new (2, 2, FALSE);
  gtk_widget_show (table1);
  gtk_box_pack_start (GTK_BOX (vbox1), table1, FALSE, TRUE, 0);
  gtk_table_set_row_spacings (GTK_TABLE (table1), 6);
  gtk_table_set_col_spacings (GTK_TABLE (table1), 6);

  label2 = gtk_label_new_with_mnemonic (_("_Username"));
  gtk_widget_show (label2);
  gtk_table_attach (GTK_TABLE (table1), label2, 0, 1, 0, 1,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  gtk_misc_set_alignment (GTK_MISC (label2), 0, 0.5);

  label3 = gtk_label_new_with_mnemonic (_("_Password"));
  gtk_widget_show (label3);
  gtk_table_attach (GTK_TABLE (table1), label3, 0, 1, 1, 2,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  gtk_misc_set_alignment (GTK_MISC (label3), 0, 0.5);

  userEntry = gtk_entry_new ();
  gtk_widget_show (userEntry);
  gtk_table_attach (GTK_TABLE (table1), userEntry, 1, 2, 0, 1,
                    (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);

  passwordEntry = gtk_entry_new ();
  gtk_widget_show (passwordEntry);
  gtk_table_attach (GTK_TABLE (table1), passwordEntry, 1, 2, 1, 2,
                    (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  gtk_entry_set_visibility (GTK_ENTRY (passwordEntry), FALSE);

  dialog_action_area1 = GTK_DIALOG (bloglines_source_dialog)->action_area;
  gtk_widget_show (dialog_action_area1);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (dialog_action_area1), GTK_BUTTONBOX_END);

  cancelbutton1 = gtk_button_new_from_stock ("gtk-cancel");
  gtk_widget_show (cancelbutton1);
  gtk_dialog_add_action_widget (GTK_DIALOG (bloglines_source_dialog), cancelbutton1, GTK_RESPONSE_CANCEL);
  GTK_WIDGET_SET_FLAGS (cancelbutton1, GTK_CAN_DEFAULT);

  okbutton1 = gtk_button_new_from_stock ("gtk-ok");
  gtk_widget_show (okbutton1);
  gtk_dialog_add_action_widget (GTK_DIALOG (bloglines_source_dialog), okbutton1, GTK_RESPONSE_OK);
  GTK_WIDGET_SET_FLAGS (okbutton1, GTK_CAN_DEFAULT);

  gtk_label_set_mnemonic_widget (GTK_LABEL (label2), userEntry);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label3), passwordEntry);

  /* Store pointers to all widgets, for use by lookup_widget(). */
  GLADE_HOOKUP_OBJECT_NO_REF (bloglines_source_dialog, bloglines_source_dialog, "bloglines_source_dialog");
  GLADE_HOOKUP_OBJECT_NO_REF (bloglines_source_dialog, dialog_vbox1, "dialog_vbox1");
  GLADE_HOOKUP_OBJECT (bloglines_source_dialog, vbox1, "vbox1");
  GLADE_HOOKUP_OBJECT (bloglines_source_dialog, label1, "label1");
  GLADE_HOOKUP_OBJECT (bloglines_source_dialog, table1, "table1");
  GLADE_HOOKUP_OBJECT (bloglines_source_dialog, label2, "label2");
  GLADE_HOOKUP_OBJECT (bloglines_source_dialog, label3, "label3");
  GLADE_HOOKUP_OBJECT (bloglines_source_dialog, userEntry, "userEntry");
  GLADE_HOOKUP_OBJECT (bloglines_source_dialog, passwordEntry, "passwordEntry");
  GLADE_HOOKUP_OBJECT_NO_REF (bloglines_source_dialog, dialog_action_area1, "dialog_action_area1");
  GLADE_HOOKUP_OBJECT (bloglines_source_dialog, cancelbutton1, "cancelbutton1");
  GLADE_HOOKUP_OBJECT (bloglines_source_dialog, okbutton1, "okbutton1");

  return bloglines_source_dialog;
}

