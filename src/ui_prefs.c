/**
 * @file ui_prefs.c program preferences
 *
 * Copyright (C) 2004 Nathan J. Conrad <t98502@users.sourceforge.net>
 * Copyright (C) 2004 Lars Lindner <lars.lindner@gmx.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <gtk/gtk.h>

#include "conf.h"
#include "interface.h"
#include "support.h"
#include "ui_mainwindow.h"
#include "ui_itemlist.h"
#include "ui_prefs.h"
#include "ui_mainwindow.h"
#include "ui_tray.h"
#include "htmlview.h"
#include "callbacks.h"

extern GSList *availableBrowserModules;

static GtkWidget *prefdialog = NULL;

static void on_browsermodule_changed(GtkObject *object, gchar *libname);
static void on_browser_changed(GtkOptionMenu *optionmenu, gpointer user_data);
static void on_browser_place_changed(GtkOptionMenu *optionmenu, gpointer user_data);

struct browser {
	gchar *id; /**< Unique ID used in storing the prefs */
	gchar *display; /**< Name to display in the prefs */
	gchar *defaultplace; /**< Default command.... Use %s to specify URL */
	gchar *existingwin;
	gchar *newwin;
	gchar *newtab;
};

struct browser browsers[] = {
	{"gnome", "Gnome Default Browser", "gnome-open \"%s\"", NULL, NULL, NULL},
	{"mozilla", "Mozilla", "mozilla \"%s\"", "mozilla -remote \"openURL(%s)\"",
	 "mozillax -remote 'openURL(%s,new-window)'", "mozilla -remote 'openURL(%s,new-tab)'"},
	{"firefox", "Firefox", "firefox \"%s\"", "firefox -a firefox -remote \"openURL(%s)\"",
	 "firefox -a firefox -remote 'openURL(%s,new-window)'", "firefox -a firefox -remote 'openURL(%s,new-tab)'"},
	{"netscape", "Netscape", "netscape \"%s\"", NULL, "netscape -remote \"openURL(%s,new-window)\"", NULL},
	{"opera", "Opera", "opera \"%s\"", "opera -remote \"openurl(%s)\"", "opera -newwindow \"%s\"", "opera -newpage \"%s\""},
	{"konqueror", "Konqueror", "kfmclient openURL \"%s\"", NULL, NULL, NULL},
	{NULL, NULL, NULL, NULL, NULL, NULL}
};

gchar *prefs_get_browser_cmd() {
	gchar *ret = NULL;
	gchar *libname;
	gint place = getNumericConfValue(BROWSER_PLACE);

	libname = getStringConfValue(BROWSER_ID);
	if (!strcmp(libname, "manual")) {
		ret = g_strdup(getStringConfValue(BROWSER_COMMAND));
	} else {
		struct browser *iter;
		for (iter = browsers; iter->id != NULL; iter++) {
			if(!strcmp(libname, iter->id)) {
				printf("found browser %s\n", iter->id);
				switch (place) {
				case 1:
					ret = g_strdup(iter->existingwin);
					break;
				case 2:
					ret = g_strdup(iter->newwin);
					break;
				case 3:
					ret = g_strdup(iter->newtab);
					break;
				}
				if (ret == NULL) /* Default when no special mode defined */
					ret = g_strdup(iter->defaultplace);
			}
		}
	}
	g_free(libname);
	if (ret == NULL)
		ret = g_strdup(browsers[0].defaultplace);
	return ret;
}

/*------------------------------------------------------------------------------*/
/* preferences dialog callbacks 						*/
/*------------------------------------------------------------------------------*/

void on_prefbtn_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget	*widget, *entry;
	GtkAdjustment	*itemCount;
	GSList		*list;
	gchar		*widgetname, *proxyport, *libname;
	gboolean	enabled;
	int		tmp, i;
	static int manual;
	struct browser *iter;
	
	if(NULL == prefdialog || !G_IS_OBJECT(prefdialog)) {
		GtkWidget *menu;
		prefdialog = create_prefdialog ();		
		
		/* Set up browser selection popup */
		menu = gtk_menu_new();
		for(i=0, iter = browsers; iter->id != NULL; iter++, i++) {
			entry = gtk_menu_item_new_with_label(iter->display);
			gtk_widget_show(entry);
			gtk_container_add(GTK_CONTAINER(menu), entry);
			gtk_signal_connect(GTK_OBJECT(entry), "activate", GTK_SIGNAL_FUNC(on_browser_changed), GINT_TO_POINTER(i));
		}
		manual = i;
		/* This allows the user to choose their own browser by typing in the command. */
		entry = gtk_menu_item_new_with_label(_("Manual"));
		gtk_widget_show(entry);
		gtk_container_add(GTK_CONTAINER(menu), entry);
		gtk_signal_connect(GTK_OBJECT(entry), "activate", GTK_SIGNAL_FUNC(on_browser_changed), GINT_TO_POINTER(i));
		
		gtk_option_menu_set_menu(GTK_OPTION_MENU(lookup_widget(prefdialog, "browserpopup")), menu);

		/* Create location menu */
		menu = gtk_menu_new();

		entry = gtk_menu_item_new_with_label(_("Browser default"));
		gtk_widget_show(entry);
		gtk_container_add(GTK_CONTAINER(menu), entry);
		gtk_signal_connect(GTK_OBJECT(entry), "activate", GTK_SIGNAL_FUNC(on_browser_place_changed), GINT_TO_POINTER(0));

		entry = gtk_menu_item_new_with_label(_("Existing window"));
		gtk_widget_show(entry);
		gtk_container_add(GTK_CONTAINER(menu), entry);
		gtk_signal_connect(GTK_OBJECT(entry), "activate", GTK_SIGNAL_FUNC(on_browser_place_changed), GINT_TO_POINTER(1));

		entry = gtk_menu_item_new_with_label(_("New window"));
		gtk_widget_show(entry);
		gtk_container_add(GTK_CONTAINER(menu), entry);
		gtk_signal_connect(GTK_OBJECT(entry), "activate", GTK_SIGNAL_FUNC(on_browser_place_changed), GINT_TO_POINTER(2));

		entry = gtk_menu_item_new_with_label(_("New tab"));
		gtk_widget_show(entry);
		gtk_container_add(GTK_CONTAINER(menu), entry);
		gtk_signal_connect(GTK_OBJECT(entry), "activate", GTK_SIGNAL_FUNC(on_browser_place_changed), GINT_TO_POINTER(3));

		gtk_option_menu_set_menu(GTK_OPTION_MENU(lookup_widget(prefdialog, "browserlocpopup")), menu);
	}
	g_assert(NULL != prefdialog);
	
	/* ================= panel 1 "feed handling" ==================== */

	/* set the inside browsing flag */
	widget = lookup_widget(prefdialog, "browseinwindow");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), getBooleanConfValue(BROWSE_INSIDE_APPLICATION));
	
	
	tmp = 0;
	libname = getStringConfValue(BROWSER_ID);
	if (libname[0] == '\0') { /* value unset */
		tmp = getNumericConfValue(GNOME_BROWSER_ENABLED);
		if(tmp == 2)
			tmp = manual; /* This is the manual override */
	}
	
	if (!strcmp(libname, "manual"))
		tmp = manual;
	else
		for(i=0, iter = browsers; iter->id != NULL; iter++, i++)
			if (!strcmp(libname, iter->id))
				tmp = i;
	
	gtk_option_menu_set_history(GTK_OPTION_MENU(lookup_widget(prefdialog, "browserpopup")), tmp);
	g_free(libname);

	entry = lookup_widget(prefdialog, "browsercmd");
	gtk_entry_set_text(GTK_ENTRY(entry), getStringConfValue(BROWSER_COMMAND));
	gtk_widget_set_sensitive(GTK_WIDGET(entry), tmp==manual);
	gtk_widget_set_sensitive(lookup_widget(prefdialog, "manuallabel"), tmp==manual);	
	/* Time format */
	tmp = getNumericConfValue(TIME_FORMAT_MODE);
	if((tmp > 3) || (tmp < 1)) 
		tmp = 1;	/* correct configuration if necessary */

	entry = lookup_widget(prefdialog, "timeformatentry");
	gtk_entry_set_text(GTK_ENTRY(entry), getStringConfValue(TIME_FORMAT));
	gtk_widget_set_sensitive(GTK_WIDGET(entry), tmp==3);


	/* Set fields in the radio widgets so that they know their option # and the pref dialog */
	for(i = 1; i <= 3; i++) {
		widgetname = g_strdup_printf("%s%d", "timeradiobtn", i);
		widget = lookup_widget(prefdialog, widgetname);
		gtk_object_set_data(GTK_OBJECT(widget), "option_number", GINT_TO_POINTER(i));
		gtk_object_set_data(GTK_OBJECT(widget), "entry", entry);
		g_free(widgetname);
	}

	widgetname = g_strdup_printf("%s%d", "timeradiobtn", tmp);
	widget = lookup_widget(prefdialog, widgetname);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), TRUE);
	g_free(widgetname);

	widget = lookup_widget(prefdialog, "updatealloptionbtn");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), getBooleanConfValue(UPDATE_ON_STARTUP));
	
	widget = lookup_widget(prefdialog, "helpoptionbtn");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), !getBooleanConfValue(DISABLE_HELPFEEDS));
	
	widget = lookup_widget(prefdialog, "itemCountBtn");
	itemCount = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(widget));
	gtk_adjustment_set_value(itemCount, getNumericConfValue(DEFAULT_MAX_ITEMS));

	/* ================== panel 2 "notification settings" ================ */
	
	widget = lookup_widget(prefdialog, "trayiconoptionbtn");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), getBooleanConfValue(SHOW_TRAY_ICON));

	/* menu / tool bar settings */	
	for(i = 1; i <= 3; i++) {
		/* Set fields in the radio widgets so that they know their option # and the pref dialog */
		widgetname = g_strdup_printf("%s%d", "menuradiobtn", i);
		widget = lookup_widget(prefdialog, widgetname);
		gtk_object_set_data(GTK_OBJECT(widget), "option_number", GINT_TO_POINTER(i));
		gtk_object_set_data(GTK_OBJECT(widget), "entry", entry);
		g_free(widgetname);
	}

	/* select currently active menu option */
	tmp = 1;
	if(getBooleanConfValue(DISABLE_TOOLBAR)) tmp = 2;
	if(getBooleanConfValue(DISABLE_MENUBAR)) tmp = 3;

	widgetname = g_strdup_printf("%s%d", "menuradiobtn", tmp);
	widget = lookup_widget(prefdialog, widgetname);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), TRUE);
	g_free(widgetname);
	
	/* set up the browser module option menu */
	tmp = i = 0;
	widget = gtk_menu_new();
	list = availableBrowserModules;
	libname = getStringConfValue(BROWSER_MODULE);
	while(NULL != list) {
		g_assert(NULL != list->data);
		entry = gtk_menu_item_new_with_label(((struct browserModule *)list->data)->description);
		gtk_widget_show(entry);
		gtk_container_add(GTK_CONTAINER(widget), entry);
		gtk_signal_connect(GTK_OBJECT(entry), "activate", GTK_SIGNAL_FUNC(on_browsermodule_changed), ((struct browserModule *)list->data)->libname);
		if(0 == strcmp(libname, ((struct browserModule *)list->data)->libname))
			tmp = i;
		i++;
		list = g_slist_next(list);
	}
	gtk_menu_set_active(GTK_MENU(widget), tmp);
	gtk_option_menu_set_menu(GTK_OPTION_MENU(lookup_widget(prefdialog, "htmlviewoptionmenu")), widget);
	
	
	/* ================= panel 3 "proxy settings" ======================== */
	
	enabled = getBooleanConfValue(USE_PROXY);
	widget = lookup_widget(prefdialog, "enableproxybtn");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), enabled);
	
	entry = lookup_widget(prefdialog, "proxyhostentry");
	gtk_entry_set_text(GTK_ENTRY(entry), getStringConfValue(PROXY_HOST));
	
	entry = lookup_widget(prefdialog, "proxyportentry");
	proxyport = g_strdup_printf("%d", getNumericConfValue(PROXY_PORT));
	gtk_entry_set_text(GTK_ENTRY(entry), proxyport);
	g_free(proxyport);

	widget = lookup_widget(prefdialog, "proxytable");
	gtk_widget_set_sensitive(GTK_WIDGET(widget), enabled);
	
	gtk_widget_show(prefdialog);
}

/*------------------------------------------------------------------------------*/
/* preference callbacks 							*/
/*------------------------------------------------------------------------------*/
void on_updatealloptionbtn_clicked(GtkButton *button, gpointer user_data) {
	setBooleanConfValue(UPDATE_ON_STARTUP, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)));
}


void on_trayiconoptionbtn_clicked(GtkButton *button, gpointer user_data) {
	gboolean enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
	setBooleanConfValue(SHOW_TRAY_ICON, enabled);
	ui_tray_enable(enabled);
}

void on_popupwindowsoptionbtn_clicked(GtkButton *button, gpointer user_data) {
	gboolean enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
	setBooleanConfValue(SHOW_POPUP_WINDOWS, enabled);
}


void on_browserselection_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget *editbox = gtk_object_get_data(GTK_OBJECT(button), "entry");
	int active_button = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(button),"option_number"));

	setNumericConfValue(GNOME_BROWSER_ENABLED, active_button);
	gtk_widget_set_sensitive(GTK_WIDGET(editbox), active_button == 2);
}


void on_browsercmd_changed(GtkEditable *editable, gpointer user_data) {
	setStringConfValue(BROWSER_COMMAND, gtk_editable_get_chars(editable,0,-1));
}

static void on_browser_changed(GtkOptionMenu *optionmenu, gpointer user_data) {
	int num = GPOINTER_TO_INT(user_data);

	gtk_widget_set_sensitive(lookup_widget(prefdialog, "browsercmd"), browsers[num].id == NULL);	
	gtk_widget_set_sensitive(lookup_widget(prefdialog, "manuallabel"), browsers[num].id == NULL);	

	if (browsers[num].id == NULL)
		setStringConfValue(BROWSER_ID, "manual");
	else
		setStringConfValue(BROWSER_ID, browsers[num].id);
}

static void on_browser_place_changed(GtkOptionMenu *optionmenu, gpointer user_data) {
	int num = GPOINTER_TO_INT(user_data);
	
	setNumericConfValue(BROWSER_PLACE, num);
}

static void on_browsermodule_changed(GtkObject *object, gchar *libname) {
	setStringConfValue(BROWSER_MODULE, libname);
}


void on_openlinksinsidebtn_clicked(GtkToggleButton *button, gpointer user_data) {
	setBooleanConfValue(BROWSE_INSIDE_APPLICATION, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)));
}



void on_timeformatselection_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget *editbox = gtk_object_get_data(GTK_OBJECT(button), "entry");
	int active_button = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(button),"option_number"));

	setNumericConfValue(TIME_FORMAT_MODE, active_button);
	gtk_widget_set_sensitive(GTK_WIDGET(editbox), active_button == 3);
	ui_itemlist_reset_date_format();
	ui_update_itemlist();
}

void on_timeformatentry_changed(GtkEditable *editable, gpointer user_data) {
	setStringConfValue(TIME_FORMAT, gtk_editable_get_chars(editable,0,-1));
	ui_itemlist_reset_date_format();
	ui_update_itemlist();
}

void on_itemCountBtn_value_changed(GtkSpinButton *spinbutton, gpointer user_data) {
	GtkAdjustment	*itemCount;
	itemCount = gtk_spin_button_get_adjustment(spinbutton);
	setNumericConfValue(DEFAULT_MAX_ITEMS, gtk_adjustment_get_value(itemCount));
}

void on_menuselection_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget	*editbox;
	gint		active_button;
	
	editbox = gtk_object_get_data(GTK_OBJECT(button), "entry");
	active_button = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(button), "option_number"));

	switch(active_button) {
		case 1:
			setBooleanConfValue(DISABLE_MENUBAR, FALSE);
			setBooleanConfValue(DISABLE_TOOLBAR, FALSE);
			break;
		case 2:
			setBooleanConfValue(DISABLE_MENUBAR, FALSE);
			setBooleanConfValue(DISABLE_TOOLBAR, TRUE);
			break;
		case 3:
			setBooleanConfValue(DISABLE_MENUBAR, TRUE);
			setBooleanConfValue(DISABLE_TOOLBAR, FALSE);
			break;
		default:
			break;
	}
	
	ui_mainwindow_update_menubar();
	ui_mainwindow_update_toolbar();
}


void on_helpoptionbtn_clicked(GtkButton *button, gpointer user_data) {
	setBooleanConfValue(DISABLE_HELPFEEDS, !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)));
}


void on_enableproxybtn_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget	*widget;
	gboolean	enabled;

	enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
	setBooleanConfValue(USE_PROXY, enabled);
	
	if(NULL != (widget = lookup_widget(prefdialog, "proxytable")))
		gtk_widget_set_sensitive(GTK_WIDGET(widget), enabled);

	loadConfig();
}


void on_proxyhostentry_changed(GtkEditable *editable, gpointer user_data) {

	setStringConfValue(PROXY_HOST, gtk_editable_get_chars(editable,0,-1));
	loadConfig();
}


void on_proxyportentry_changed(GtkEditable *editable, gpointer user_data) {

	setNumericConfValue(PROXY_PORT, atoi(gtk_editable_get_chars(editable,0,-1)));
	loadConfig();
}
