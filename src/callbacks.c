/*
   Copyright (C) 2003 Lars Lindner <lars.lindner@gmx.net>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
*/

// FIXME: maybe split callbacks.c in several files...
// FIXME: improve method names...

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <string.h>

#include "interface.h"
#include "support.h"
#include "folder.h"
#include "feed.h"
#include "item.h"
#include "conf.h"
#include "common.h"
#include "htmlview.h"
#include "callbacks.h"
#include "ui_selection.h"
#include "update.h"

#include "vfolder.h"	// FIXME

/* possible selected new dialog feed types */
static gint selectableTypes[] = {	FST_AUTODETECT,
					FST_RSS,
					FST_CDF,
					FST_PIE,
					FST_OCS,
					FST_OPML
				};
				
#define MAX_TYPE_SELECT	6
				
GtkWidget		*mainwindow;
GtkWidget		*filedialog = NULL;
GtkWidget		*newdialog = NULL;
static GtkWidget	*propdialog = NULL;
static GtkWidget	*prefdialog = NULL;
static GtkWidget	*newfolderdialog = NULL;
static GtkWidget	*foldernamedialog = NULL;

GtkTreeStore	*itemstore = NULL;
GtkTreeStore	*feedstore = NULL;

/* icons for itemlist */
static GdkPixbuf	*readIcon = NULL;
static GdkPixbuf	*unreadIcon = NULL;
static GdkPixbuf	*flagIcon = NULL;
/* icons for feedlist */
GdkPixbuf	*availableIcon = NULL;
static GdkPixbuf	*unavailableIcon = NULL;
/* icons for OCS */
static GdkPixbuf	*listIcon = NULL;
/* icons for grouping */
static GdkPixbuf	*directoryIcon = NULL;
static GdkPixbuf	*helpIcon = NULL;
GdkPixbuf	*emptyIcon = NULL;
/* VFolder */
static GdkPixbuf	*vfolderIcon = NULL;

extern GAsyncQueue	*results;
extern GThread		*updateThread;

extern GHashTable	*folders; // FIXME!
extern GHashTable	*feedHandler;
extern feedPtr		allItems;

static gint	itemlist_loading = 0;	/* freaky workaround for item list focussing problem */
gboolean	itemlist_mode = TRUE;	/* TRUE means three pane, FALSE means two panes */

/* flag to check if DND should be aborted (e.g. on folders and help feeds) */
static gboolean	drag_successful = FALSE;

/* feedPtr which points to the last selected (and in most cases actually selected) feed */
feedPtr	selected_fp = NULL;

/* like selected_fp, to remember the last selected item */
itemPtr	selected_ip = NULL;

/* contains the type of the actually selected feedlist entry */
gint selected_type = FST_INVALID;

/* path of root folder */
static gchar	*root_prefix = "";

/* pointer to the selected directory key prefix, needed when creating new feeds */
gchar	*selected_keyprefix = NULL;

/* prototypes */
void preFocusItemlist(void);
void setSelectedFeed(feedPtr fp, gchar *keyprefix);
GtkTreeStore * getFeedStore(void);
GtkTreeStore * getItemStore(void);
void loadItemList(feedPtr fp, gchar *searchstring);
void displayItemList(void);
void clearItemList(void);

/*------------------------------------------------------------------------------*/
/* helper functions								*/
/*------------------------------------------------------------------------------*/

void initGUI(void) {
	GtkWidget 	*widget;
	
	if(NULL == selected_keyprefix)
		selected_keyprefix = g_strdup(root_prefix);
		
	if(NULL != (widget = lookup_widget(mainwindow, "toolbar"))) {
		if(getBooleanConfValue(DISABLE_TOOLBAR))
			gtk_widget_hide(widget);
		else
			gtk_widget_show(widget);
	}

	if(NULL != (widget = lookup_widget(mainwindow, "menubar"))) {
		if(getBooleanConfValue(DISABLE_MENUBAR))
			gtk_widget_hide(widget);
		else
			gtk_widget_show(widget);
	}
	
	if(NULL == readIcon)		readIcon	= create_pixbuf("read.xpm");
	if(NULL == unreadIcon)		unreadIcon	= create_pixbuf("unread.xpm");
	if(NULL == flagIcon)		flagIcon	= create_pixbuf("flag.png");
	if(NULL == availableIcon)	availableIcon	= create_pixbuf("available.png");
	if(NULL == unavailableIcon)	unavailableIcon	= create_pixbuf("unavailable.png");
	if(NULL == listIcon)		listIcon	= create_pixbuf("ocs.png");
	if(NULL == directoryIcon)	directoryIcon	= create_pixbuf("directory.png");
	if(NULL == helpIcon)		helpIcon	= create_pixbuf("help.png");
	if(NULL == vfolderIcon)		vfolderIcon	= create_pixbuf("vfolder.png");
	if(NULL == emptyIcon)		emptyIcon	= create_pixbuf("empty.png");
	
	setupTrayIcon();
	setupSelection(mainwindow);
}

/* returns the selected feed list iterator */
gboolean getFeedListIter(GtkTreeIter *iter) {
	GtkWidget		*treeview;
	GtkTreeSelection	*select;
        GtkTreeModel		*model;
	
	if(NULL == mainwindow)
		return FALSE;
	
	if(NULL == (treeview = lookup_widget(mainwindow, "feedlist"))) {
		g_warning(_("entry list widget lookup failed!\n"));
		return FALSE;
	}
		
	if(NULL == (select = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview)))) {
		print_status(_("could not retrieve selection of entry list!"));
		return FALSE;
	}

        gtk_tree_selection_get_selected(select, &model, iter);
	return TRUE;
}

void redrawFeedList(void) {
	GtkWidget	*list;
	
	if(NULL == mainwindow)
		return;
	
	if(NULL != (list = lookup_widget(mainwindow, "feedlist")))
		gtk_widget_queue_draw(list);
}

void redrawItemList(void) {
	GtkWidget	*list;
	
	if(NULL == mainwindow)
		return;
	
	if(NULL != (list = lookup_widget(mainwindow, "Itemlist")))
		gtk_widget_queue_draw(list);
}

/*------------------------------------------------------------------------------*/
/* callbacks 									*/
/*------------------------------------------------------------------------------*/

void on_refreshbtn_clicked(GtkButton *button, gpointer user_data) {

	updateAllFeeds();
}

void on_popup_refresh_selected(void) { 

	if(!FEED_MENU(selected_type)) {
		showErrorBox(_("You have to select a feed entry!"));
		return;
	}

	updateFeed(selected_fp); 
}

/*------------------------------------------------------------------------------*/
/* preferences dialog callbacks 						*/
/*------------------------------------------------------------------------------*/

void on_prefbtn_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget	*widget;
	GtkAdjustment	*itemCount;
	gchar		*widgetname;
	int		tmp;
				
	if(NULL == prefdialog || !G_IS_OBJECT(prefdialog))
		prefdialog = create_prefdialog ();		
	
	g_assert(NULL != prefdialog);

	/* panel 1 "feed handling" */	
	widget = lookup_widget(prefdialog, "browsercmd");
	tmp = getNumericConfValue(GNOME_BROWSER_ENABLED);
	if((tmp > 2) || (tmp < 1)) 
		tmp = 1;	/* correct configuration if necessary */
		
	gtk_entry_set_text(GTK_ENTRY(widget), getStringConfValue(BROWSER_COMMAND));

	widgetname = g_strdup_printf("%s%d", "browserradiobtn", tmp);
	widget = lookup_widget(prefdialog, widgetname);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), TRUE);
	g_free(widgetname);		

	widget = lookup_widget(prefdialog, "timeformatentry");
	gtk_entry_set_text(GTK_ENTRY(widget), getStringConfValue(TIME_FORMAT));

	tmp = getNumericConfValue(TIME_FORMAT_MODE);
	if((tmp > 3) || (tmp < 1)) 
		tmp = 1;	/* correct configuration if necessary */

	widgetname = g_strdup_printf("%s%d", "timeradiobtn", tmp);
	widget = lookup_widget(prefdialog, widgetname);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), TRUE);
	g_free(widgetname);		

	widget = lookup_widget(prefdialog, "updateallbtn");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), getBooleanConfValue(UPDATE_ON_STARTUP));
	
	widget = lookup_widget(prefdialog, "itemCountBtn");
	itemCount = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(widget));
	gtk_adjustment_set_value(itemCount, getNumericConfValue(DEFAULT_MAX_ITEMS));

	/* panel 2 "notification settings" */	
	widget = lookup_widget(prefdialog, "trayiconbtn");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), getBooleanConfValue(SHOW_TRAY_ICON));


	/* menu / tool bar settings */		
	widget = lookup_widget(prefdialog, "menubarbtn");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), getBooleanConfValue(DISABLE_MENUBAR));
	
	widget = lookup_widget(prefdialog, "toolbarbtn");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), getBooleanConfValue(DISABLE_TOOLBAR));
	
	gtk_widget_show(prefdialog);
}

void on_prefsavebtn_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget	*widget;
	GtkAdjustment	*itemCount;
	gchar		*widgetname;
	gint		tmp, i;
	
	g_assert(NULL != prefdialog);

	/* panel 1 "feed handling" */			
	widget = lookup_widget(prefdialog, "browsercmd");
	setStringConfValue(BROWSER_COMMAND, (gchar *)gtk_entry_get_text(GTK_ENTRY(widget)));

	widget = lookup_widget(prefdialog, "timeformatentry");
	setStringConfValue(TIME_FORMAT, (gchar *)gtk_entry_get_text(GTK_ENTRY(widget)));

	widget = lookup_widget(prefdialog, "itemCountBtn");
	itemCount = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(widget));
	setNumericConfValue(DEFAULT_MAX_ITEMS, gtk_adjustment_get_value(itemCount));
	
	widget = lookup_widget(prefdialog, "updateallbtn");
	setBooleanConfValue(UPDATE_ON_STARTUP, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)));
	
	tmp = 0;
	for(i = 1; i <= 2; i++) {
		widgetname = g_strdup_printf("%s%d", "browserradiobtn", i);
		widget = lookup_widget(prefdialog, widgetname);
		if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
			tmp = i;
		g_free(widgetname);	
	}
	setNumericConfValue(GNOME_BROWSER_ENABLED, tmp);
	
	tmp = 0;
	for(i = 1; i <= 3; i++) {
		widgetname = g_strdup_printf("%s%d", "timeradiobtn", i);
		widget = lookup_widget(prefdialog, widgetname);
		if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
			tmp = i;
		g_free(widgetname);	
	}
	setNumericConfValue(TIME_FORMAT_MODE, tmp);
	
	/* panel 2 "notification settings" */	
	widget = lookup_widget(prefdialog, "trayiconbtn");
	setBooleanConfValue(SHOW_TRAY_ICON, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)));

	/* menu / tool bar settings */		
	widget = lookup_widget(prefdialog, "menubarbtn");
	setBooleanConfValue(DISABLE_MENUBAR, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)));
	
	widget = lookup_widget(prefdialog, "toolbarbtn");
	setBooleanConfValue(DISABLE_TOOLBAR, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)));	
	
	if(getBooleanConfValue(DISABLE_MENUBAR) && getBooleanConfValue(DISABLE_TOOLBAR)) {
		showErrorBox(_("You have disabled both the menubar and the toolbar. Without one of these you won't be able to access all program functions. I'll reenable the toolbar!"));
		setBooleanConfValue(DISABLE_TOOLBAR, FALSE);	
	}
	
	/* refresh item list (in case date format was changed) */
	redrawItemList();
	
	/* reinit GUI for the case menubar or toolbar options changed */
	initGUI();
	
	updateUI();
			
	gtk_widget_hide(prefdialog);
}

/*------------------------------------------------------------------------------*/
/* delete entry callbacks 							*/
/*------------------------------------------------------------------------------*/

void on_deletebtn(void) {
	GtkTreeIter	iter;

	/* block deleting of empty entries */
	if(!FEED_MENU(selected_type)) {
		showErrorBox(_("You have to select a feed entry!"));
		return;
	}

	/* block deleting of help feeds */
	if(0 == strncmp(getFeedKey(selected_fp), "help", 4)) {
		showErrorBox(_("You can't delete help feeds!"));
		return;
	}
	
	print_status(g_strdup_printf("%s \"%s\"",_("Deleting entry"), getFeedTitle(selected_fp)));
	if(getFeedListIter(&iter)) {
		gtk_tree_store_remove(feedstore, &iter);
		removeFeed(selected_fp);
		setSelectedFeed(NULL, "");

		clearItemList();
		clearHTMLView();
		checkForEmptyFolders();
	} else {
		showErrorBox(_("It seems like there is no selected feed entry!"));
	}
}

void on_popup_delete_selected(void) { on_deletebtn(); }

/*------------------------------------------------------------------------------*/
/* property dialog callbacks 							*/
/*------------------------------------------------------------------------------*/

void on_propbtn(GtkWidget *widget) {
	feedPtr		fp = selected_fp;
	GtkWidget 	*feednameentry, *feedurlentry, *updateIntervalBtn;
	GtkAdjustment	*updateInterval;
	gint		defaultInterval;
	gchar		*defaultIntervalStr;

	if(!FEED_MENU(selected_type)) {
		showErrorBox(_("You have to select a feed entry!"));
		return;
	}
	
	/* block changing of help feeds */
	if(0 == strncmp(getFeedKey(fp), "help", strlen("help"))) {
		showErrorBox("You can't modify help feeds!");
		return;
	}
	
	if(NULL == propdialog || !G_IS_OBJECT(propdialog))
		propdialog = create_propdialog();
	
	if(NULL == propdialog)
		return;
		
	feednameentry = lookup_widget(propdialog, "feednameentry");
	feedurlentry = lookup_widget(propdialog, "feedurlentry");
	updateIntervalBtn = lookup_widget(propdialog, "feedrefreshcount");
	updateInterval = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(updateIntervalBtn));
	
	gtk_entry_set_text(GTK_ENTRY(feednameentry), getFeedTitle(fp));
	gtk_entry_set_text(GTK_ENTRY(feedurlentry), getFeedSource(fp));

	if(FST_OCS == getFeedType(fp)) {	
		/* disable the update interval selector for OCS feeds */
		gtk_widget_set_sensitive(lookup_widget(propdialog, "feedrefreshcount"), FALSE);
	} else {
		/* enable and adjust values otherwise */
		gtk_widget_set_sensitive(lookup_widget(propdialog, "feedrefreshcount"), TRUE);	
		
		gtk_adjustment_set_value(updateInterval, getFeedUpdateInterval(fp));

		defaultInterval = getFeedDefaultInterval(fp);
		if(-1 != defaultInterval)
			defaultIntervalStr = g_strdup_printf(_("The provider of this feed suggests an update interval of %d minutes"), defaultInterval);
		else
			defaultIntervalStr = g_strdup(_("This feed specifies no default update interval."));
		gtk_label_set_text(GTK_LABEL(lookup_widget(propdialog, "feedupdateinfo")), defaultIntervalStr);
		g_free(defaultIntervalStr);		
	}

	gtk_widget_show(propdialog);
}

void on_propchangebtn_clicked(GtkButton *button, gpointer user_data) {
	gchar		*feedurl, *feedname;
	GtkWidget 	*feedurlentry;
	GtkWidget 	*feednameentry;
	GtkWidget 	*updateIntervalBtn;
	GtkAdjustment	*updateInterval;
	gint		interval;
	feedPtr		fp = selected_fp;
	
	g_assert(NULL != propdialog);
		
	if(NULL != fp) {
		feednameentry = lookup_widget(propdialog, "feednameentry");
		feedurlentry = lookup_widget(propdialog, "feedurlentry");

		feedurl = (gchar *)gtk_entry_get_text(GTK_ENTRY(feedurlentry));
		feedname = (gchar *)gtk_entry_get_text(GTK_ENTRY(feednameentry));
	
		setFeedTitle(fp, g_strdup(feedname));  
		setFeedSource(fp, g_strdup(feedurl));
		
		if(IS_FEED(getFeedType(fp))) {
			updateIntervalBtn = lookup_widget(propdialog, "feedrefreshcount");
			updateInterval = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(updateIntervalBtn));

			interval = gtk_adjustment_get_value(updateInterval);
			
			if(0 == interval)
				interval = -1;	/* this is due to ignore this feed while updating */
			setFeedUpdateInterval(fp, interval);
			setFeedUpdateCounter(fp, interval);
		}
		
		redrawFeedList();
	} else {
		g_warning(_("Internal error! No feed selected, but property change requested...\n"));
	}
}

void on_popup_prop_selected(void) { on_propbtn(NULL); }

/*------------------------------------------------------------------------------*/
/* new entry dialog callbacks 							*/
/*------------------------------------------------------------------------------*/

void addToFeedList(feedPtr fp, gboolean startup) {
	GtkTreeSelection	*selection;
	GtkWidget		*treeview;
	GtkTreeIter		selected_iter;
	GtkTreeIter		iter;
	GtkTreeIter		*topiter;
	
	g_assert(NULL != fp);
	g_assert(NULL != getFeedKey(fp));
	g_assert(NULL != getFeedKeyPrefix(fp));
	g_assert(NULL != feedstore);
	g_assert(NULL != folders);
	
	// FIXME: maybe this should not happen here?
	topiter = (GtkTreeIter *)g_hash_table_lookup(folders, (gpointer)(getFeedKeyPrefix(fp)));
	
	if(!startup) {
		/* used when creating feed entries manually */
		if(getFeedListIter(&selected_iter) && IS_FEED(selected_type))
			/* if a feed entry is marked after which we shall insert */
			gtk_tree_store_insert_after(feedstore, &iter, topiter, &selected_iter);
		else
			/* if no feed entry is marked (e.g. on empty folders) */		
			gtk_tree_store_prepend(feedstore, &iter, topiter);
	} else {
		/* typically on startup when adding feeds from configuration */
		gtk_tree_store_append(feedstore, &iter, topiter);
	}

	gtk_tree_store_set(feedstore, &iter,
			   FS_TITLE, getFeedTitle(fp),
			   FS_KEY, getFeedKey(fp),
			   FS_TYPE, getFeedType(fp),
			   -1);

	if(!startup) {			   
		/* this selection fake is necessary for the property dialog, which is
		   opened after feed subscription and depends on the correctly
		   selected feed */
		setSelectedFeed(fp, NULL);
	}
}

static void subscribeTo(gint type, gchar *source, gchar * keyprefix, gboolean showPropDialog) {
	GtkWidget	*updateIntervalBtn;
	feedPtr		fp;
	gint		interval;
	gchar		*tmp;

	if(NULL != (fp = newFeed(type, source, keyprefix))) {
		addToFeedList(fp, FALSE);
		checkForEmptyFolders();

		if(FALSE == getFeedAvailable(fp)) {
			tmp = g_strdup_printf(_("Could not download \"%s\"!\n\n Maybe the URL is invalid or the feed is temporarily not available. You can retry downloading or remove the feed subscription via the context menu from the feed list.\n"), source);
			showErrorBox(tmp);
			g_free(tmp);
		} else {
			if(TRUE == showPropDialog) {
				if(-1 != (interval = getFeedDefaultInterval(fp))) {
					updateIntervalBtn = lookup_widget(propdialog, "feedrefreshcount");
					gtk_spin_button_set_value(GTK_SPIN_BUTTON(updateIntervalBtn), (gfloat)interval);
				}

				on_propbtn(NULL);		/* prepare prop dialog */
			}
		}
	}
}

void on_newbtn_clicked(GtkButton *button, gpointer user_data) {	
	GtkWidget 	*sourceentry;	
	
	if(NULL == newdialog || !G_IS_OBJECT(newdialog)) 
		newdialog = create_newdialog();
		
	if(NULL == propdialog || !G_IS_OBJECT(propdialog))
		propdialog = create_propdialog();

	sourceentry = lookup_widget(newdialog, "newfeedentry");
	gtk_entry_set_text(GTK_ENTRY(sourceentry), "");

	g_assert(NULL != newdialog);
	g_assert(NULL != propdialog);
	gtk_widget_show(newdialog);
}

void on_newfeedbtn_clicked(GtkButton *button, gpointer user_data) {
	gchar		*key, *keyprefix, *title, *source, *tmp;
	GtkWidget 	*sourceentry;	
	GtkWidget 	*titleentry, *typeoptionmenu,
			*updateIntervalBtn;
	feedPtr		fp;
	gint		type, interval;
	
	g_assert(newdialog != NULL);
	g_assert(propdialog != NULL);
g_print("get selection\n");
getSelection(mainwindow);
	sourceentry = lookup_widget(newdialog, "newfeedentry");
	titleentry = lookup_widget(propdialog, "feednameentry");
	typeoptionmenu = lookup_widget(newdialog, "typeoptionmenu");
		
	g_assert(NULL != selected_keyprefix);
	source = g_strdup(gtk_entry_get_text(GTK_ENTRY(sourceentry)));
	type = gtk_option_menu_get_history(GTK_OPTION_MENU(typeoptionmenu));
	
	/* the retrieved number is not yet the real feed type! */
	if(type > MAX_TYPE_SELECT) {
			g_error(_("internal error! invalid type selected! This should never happen!\n"));
			return;
	} else
		type = selectableTypes[type];

	subscribeTo(type, source, g_strdup(selected_keyprefix), TRUE);	
	/* don't free source for it is reused by newFeed! */
}

void on_localfileselect_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget	*source;
	
	gtk_widget_hide(filedialog);
	g_assert(NULL != newdialog);
	if(NULL != (source = lookup_widget(newdialog, "newfeedentry")))
		gtk_entry_set_text(GTK_ENTRY(source), gtk_file_selection_get_filename(GTK_FILE_SELECTION(filedialog)));
}

void on_localfilebtn_pressed(GtkButton *button, gpointer user_data) {
	GtkWidget	*okbutton;
	
	if(NULL == filedialog || !G_IS_OBJECT(filedialog))
		filedialog = create_fileselection();
		
	if(NULL == (okbutton = lookup_widget(filedialog, "fileselectbtn")))
		g_error(_("internal error! could not find file dialog select button!"));

	g_signal_connect((gpointer) okbutton, "clicked", G_CALLBACK (on_localfileselect_clicked), NULL);
	gtk_widget_show(filedialog);
}

/*------------------------------------------------------------------------------*/
/* new/change/remove folder dialog callbacks 					*/
/*------------------------------------------------------------------------------*/

void on_popup_newfolder_selected(void) {
	if(NULL == newfolderdialog || !G_IS_OBJECT(newfolderdialog))
		newfolderdialog = create_newfolderdialog();
		
	gtk_widget_show(newfolderdialog);
}

void on_newfolderbtn_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget	*foldertitleentry;
	gchar		*folderkey, *foldertitle;
	
	g_assert(newfolderdialog != NULL);
	
	foldertitleentry = lookup_widget(newfolderdialog, "foldertitleentry");
	foldertitle = (gchar *)gtk_entry_get_text(GTK_ENTRY(foldertitleentry));
	if(NULL != (folderkey = addFolderToConfig(foldertitle))) {
		/* add the new folder to the model */
		addFolder(folderkey, g_strdup(foldertitle), FST_NODE);
		checkForEmptyFolders();
	} else {
		print_status(_("internal error! could not get a new folder key!"));
	}	
}

void on_popup_foldername_selected(void) {
	GtkWidget	*foldernameentry;
	gchar 		*title;

	if(selected_type != FST_NODE) {
		showErrorBox(_("You have to select a folder entry!"));
		return;
	}
	
	if(NULL == foldernamedialog || !G_IS_OBJECT(foldernamedialog))
		foldernamedialog = create_foldernamedialog();
		
	foldernameentry = lookup_widget(foldernamedialog, "foldernameentry");
	if(NULL != selected_keyprefix) {
		title = getFolderTitle(selected_keyprefix);
		gtk_entry_set_text(GTK_ENTRY(foldernameentry), title);
		g_free(title);
		gtk_widget_show(foldernamedialog);
	} else {
		showErrorBox("internal error: could not determine folder key!");
	}
}

void on_foldername_activate(GtkMenuItem *menuitem, gpointer user_data) { on_popup_foldername_selected(); }

void on_foldernamechangebtn_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget	*foldernameentry;
	
	if(NULL != selected_keyprefix) {
		foldernameentry = lookup_widget(foldernamedialog, "foldernameentry");
		setFolderTitle(selected_keyprefix, (gchar *)gtk_entry_get_text(GTK_ENTRY(foldernameentry)));
	} else {
		showErrorBox("internal error: could not determine folder key!");
	}

	gtk_widget_hide(foldernamedialog);
}

void on_popup_removefolder_selected(void) {
	GtkTreeStore	*feedstore;
	GtkTreeIter	childiter;
	GtkTreeIter	selected_iter;	
	gint		tmp_type, count;
	
	if(selected_type != FST_NODE) {
		showErrorBox(_("You have to select a folder entry!"));
		return;
	}

	getFeedListIter(&selected_iter);
	feedstore = getFeedStore();
	g_assert(feedstore != NULL);
	
	/* make sure thats no grouping iterator */
	if(NULL != selected_keyprefix) {
		/* check if folder is empty */
		count = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(feedstore), &selected_iter);
		if(0 == count) 
			g_error("this should not happen! A folder must have an empty entry!!!\n");

		if(1 == count) {
			/* check if the only entry is type FST_EMPTY */
			gtk_tree_model_iter_children(GTK_TREE_MODEL(feedstore), &childiter, &selected_iter);
			gtk_tree_model_get(GTK_TREE_MODEL(feedstore), &childiter, FS_TYPE, &tmp_type, -1);
			if(FST_EMPTY == tmp_type) {
				gtk_tree_store_remove(feedstore, &childiter);		/* remove "(empty)" iter */
				gtk_tree_store_remove(feedstore, &selected_iter);	/* remove folder iter */
				removeFolder(selected_keyprefix);
				g_free(selected_keyprefix);
				selected_keyprefix = g_strdup(root_prefix);
			} else {
				showErrorBox(_("A folder must be empty to delete it!"));
			}
		} else {
			showErrorBox(_("A folder must be empty to delete it!"));
		}
	} else {
		print_status(_("Error: Cannot determine folder key!"));
	}
}

/*------------------------------------------------------------------------------*/
/* itemlist popup handler							*/
/*------------------------------------------------------------------------------*/

void on_popup_allunread_selected(void) {
	GtkTreeIter	iter;
	itemPtr		ip;
	gboolean 	valid;

	g_assert(NULL != itemstore);
	valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(itemstore), &iter);
	while(valid) {
               	gtk_tree_model_get(GTK_TREE_MODEL(itemstore), &iter, IS_PTR, &ip, -1);
		g_assert(ip != NULL);
		markItemAsRead(ip);

		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(itemstore), &iter);
	}

	/* redraw feed list to update unread items count */
	redrawFeedList();

	/* necessary to rerender the formerly bold text labels */
	redrawItemList();	
}

void on_popup_launchitem_selected(void) {
	GtkWidget		*itemlist;
	GtkTreeSelection	*selection;
	GtkTreeModel 		*model;
	GtkTreeIter		iter;
	gpointer		tmp_ip;
	gint			tmp_type;

	if(NULL == (itemlist = lookup_widget(mainwindow, "Itemlist")))
		return;

	if(NULL == (selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(itemlist)))) {
		print_status(_("could not retrieve selection of item list!"));
		return;
	}

	if(gtk_tree_selection_get_selected(selection, &model, &iter)) {
	       	gtk_tree_model_get(model, &iter, IS_PTR, &tmp_ip,
						 IS_TYPE, &tmp_type, -1);
		g_assert(tmp_ip != NULL);
		launchURL(getItemSource(tmp_ip));
	}
}


void on_Itemlist_row_activated(GtkTreeView *treeview, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data) {

	on_popup_launchitem_selected();
}

/*------------------------------------------------------------------------------*/
/* selection change callbacks							*/
/*------------------------------------------------------------------------------*/
void preFocusItemlist(void) {
	GtkWidget		*itemlist;
	GtkTreeSelection	*itemselection;
	
	/* the following is important to prevent setting the unread
	   flag for the first item in the item list when the user does
	   the first click into the treeview, if we don't do a focus and
	   unselect, GTK would always (exception: clicking on first item)
	   generate two selection-change events (one for the clicked and
	   one for the selected item)!!! */

	if(NULL == (itemlist = lookup_widget(mainwindow, "Itemlist"))) {
		g_warning(_("item list widget lookup failed!\n"));
		return;
	}

	/* prevent marking as unread before focussing, which leads 
	   to a selection */
	itemlist_loading = 1;
	gtk_widget_grab_focus(itemlist);

	if(NULL == (itemselection = gtk_tree_view_get_selection(GTK_TREE_VIEW(itemlist)))) {
		g_warning(_("could not retrieve selection of item list!\n"));
		return;
	}
	gtk_tree_selection_unselect_all(itemselection);

	gtk_widget_grab_focus(lookup_widget(mainwindow, "feedlist"));
	itemlist_loading = 0;
}

/* sets the selected_* variables which indicate the selected feed list
   entry which can either be a feed or a directory 
 
   to set the selection info for a feed: fp must be specified and
                                         keyprefix can be NULL
					 
   to set the selection info for a folder: fp has to be NULL and
                                           keyprefix has to be given				 
 */
void setSelectedFeed(feedPtr fp, gchar *keyprefix) {

	if(NULL != (selected_fp = fp)) {
		/* we select a feed */
		selected_type = getFeedType(fp);;
		g_free(selected_keyprefix);
		if(NULL == keyprefix)
			selected_keyprefix = g_strdup(getFeedKeyPrefix(fp));
		else
			selected_keyprefix = g_strdup(keyprefix);
	} else {
		/* we select a folder */
		if(0 == strcmp(keyprefix, "empty"))
			selected_type = FST_EMPTY;
		else
			selected_type = FST_NODE;

		g_free(selected_keyprefix);
		selected_keyprefix = g_strdup(keyprefix);
	}
	selected_ip = NULL;	
}

void feedlist_selection_changed_cb(GtkTreeSelection *selection, gpointer data) {
	GtkTreeIter		iter;
        GtkTreeModel		*model;
	gchar			*tmp_key;
	gint			tmp_type;
	feedPtr			fp;
	GdkGeometry		geometry;

	undoTrayIcon();
	
        if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
                gtk_tree_model_get(model, &iter, 
				FS_KEY, &tmp_key,
				FS_TYPE, &tmp_type,
				-1);

		selected_ip = NULL;				
		g_assert(NULL != tmp_key);
		/* make sure thats no grouping iterator */
		if(!IS_NODE(tmp_type) && (FST_EMPTY != tmp_type)) {
			fp = getFeed(tmp_key);				
			
			/* FIXME: another workaround to prevent strange window
			   size increasings after feed selection changing */
			geometry.min_height=480;
			geometry.min_width=640;
			g_assert(mainwindow != NULL);
			gtk_window_set_geometry_hints(GTK_WINDOW(mainwindow), mainwindow, &geometry, GDK_HINT_MIN_SIZE);
	
			/* save new selection infos */
			setSelectedFeed(fp, NULL);		
			loadItemList(fp, NULL);
		} else {
			/* save new selection infos */
			setSelectedFeed(NULL, tmp_key);
		}
		g_free(tmp_key);
       	}
}

void itemlist_selection_changed(void) {
	GtkTreeSelection	*selection;
	GtkWidget		*itemlist;
	GtkTreeIter		iter;
        GtkTreeModel		*model;

	gint		type;
	gchar		*tmp_key;

	undoTrayIcon();
	
	/* do nothing upon initial focussing */
	if(!itemlist_loading) {
		g_assert(mainwindow != NULL);
		if(NULL == (itemlist = lookup_widget(mainwindow, "Itemlist"))) {
			print_status(_("could not find item list widget!"));
			return;
		}

		if(NULL == (selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(itemlist)))) {
			print_status(_("could not retrieve selection of item list!"));
			return;
		}

       		if(gtk_tree_selection_get_selected(selection, &model, &iter)) {

               		gtk_tree_model_get(model, &iter, IS_PTR, &selected_ip,
							 IS_TYPE, &type, -1);

			g_assert(selected_ip != NULL);
			if((0 == itemlist_loading)) {
				if(NULL != (itemlist = lookup_widget(mainwindow, "Itemlist"))) {
					displayItem(selected_ip);

					/* redraw feed list to update unread items numbers */
					redrawFeedList();
				}
			}
       		}
	}
}

void on_itemlist_selection_changed (GtkTreeSelection *selection, gpointer data) {

	itemlist_selection_changed();
}

gboolean on_Itemlist_move_cursor(GtkTreeView *treeview, GtkMovementStep  step, gint count, gpointer user_data) {

	itemlist_selection_changed();
	return FALSE;
}

void on_toggle_item_flag(void) {
	
	if(NULL != selected_ip)
		setItemMark(selected_ip, !getItemMark(selected_ip));
		
	updateUI();
}

void on_toggle_condensed_view_selected(void) {
	GtkWidget	*w1;
	gchar		*key;
	
	itemlist_mode = !itemlist_mode;
	setHTMLViewMode(itemlist_mode);

	g_assert(mainwindow);
	w1 = lookup_widget(mainwindow, "itemtabs");
	if(TRUE == itemlist_mode)
		gtk_notebook_set_current_page(GTK_NOTEBOOK(w1), 0);
	else 
		gtk_notebook_set_current_page(GTK_NOTEBOOK(w1), 1);
		
	displayItemList();
}

void on_toggle_condensed_view_activate(GtkMenuItem *menuitem, gpointer user_data) { on_toggle_condensed_view_selected(); }

static feedPtr findUnreadFeed(GtkTreeIter *iter) {
	GtkTreeSelection	*selection;
	GtkTreePath		*path;
	GtkWidget		*treeview;
	GtkTreeIter		childiter;
	gboolean		valid;
	feedPtr			fp;
	gchar			*tmp_key;
	gint			tmp_type;
	
	if(NULL == iter)
		valid = gtk_tree_model_get_iter_root(GTK_TREE_MODEL(feedstore), &childiter);
	else
		valid = gtk_tree_model_iter_children(GTK_TREE_MODEL(feedstore), &childiter, iter);
		
	while(valid) {
               	gtk_tree_model_get(GTK_TREE_MODEL(feedstore), &childiter, FS_KEY, &tmp_key, FS_TYPE, &tmp_type, -1);

		if(IS_FEED(tmp_type)) {
			g_assert(tmp_key != NULL);

			fp = getFeed(tmp_key);
			g_assert(fp != NULL);

			if(getFeedUnreadCount(fp) > 0) {
				/* select the feed entry... */
				if(NULL != (treeview = lookup_widget(mainwindow, "feedlist"))) {
					if(NULL != (selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview)))) {
						gtk_tree_selection_select_iter(selection, &childiter);
						path = gtk_tree_model_get_path(GTK_TREE_MODEL(feedstore), &childiter);
						gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(treeview), path, NULL, FALSE, FALSE, FALSE);
						gtk_tree_path_free(path);
					} else
						g_warning(_("internal error! could not get feed tree view selection!\n"));
				} else {
					g_warning(_("internal error! could not find feed tree view widget!\n"));
				}			

				return fp;
			}		
		} else {
			/* must be a folder, so recursivly go down... */
			if(NULL != (fp = findUnreadFeed(&childiter)))
				return fp;
		}
		
		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(feedstore), &childiter);
	}

	return NULL;	
}

static gboolean findUnreadItem() {
	GtkTreeSelection	*selection;
	GtkTreePath		*path;
	GtkWidget		*treeview;
	GtkTreeIter		iter;
	gboolean		valid;
	itemPtr			ip;
		
	g_assert(NULL != itemstore);
	valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(itemstore), &iter);
	while(valid) {
               	gtk_tree_model_get(GTK_TREE_MODEL(itemstore), &iter, IS_PTR, &ip, -1);
		g_assert(ip != NULL);
		if(FALSE == getItemReadStatus(ip)) {
			/* select found item... */
			if(NULL != (treeview = lookup_widget(mainwindow, "Itemlist"))) {
				if(NULL != (selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview)))) {
					gtk_tree_selection_select_iter(selection, &iter);
					path = gtk_tree_model_get_path(GTK_TREE_MODEL(itemstore), &iter);
					gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(treeview), path, NULL, FALSE, FALSE, FALSE);
					gtk_tree_path_free(path);
				} else
					g_warning(_("internal error! could not get feed tree view selection!\n"));
			} else {
				g_warning(_("internal error! could not find feed tree view widget!\n"));
			}			
			return TRUE;
		}
		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(itemstore), &iter);
	}
	
	return FALSE;
}

void on_next_unread_item_activate(GtkMenuItem *menuitem, gpointer user_data) {
	feedPtr			fp;
	
	/* before scanning the feed list, we test if there is a unread 
	   item in the currently selected feed! */
	if(TRUE == findUnreadItem())
		return;
	
	/* find first feed with unread items */
	g_assert(NULL != feedstore);
	fp = findUnreadFeed(NULL);
	
// FIXME: workaround to prevent segfaults...
// something with the selection is buggy!
selected_ip = NULL;

	if(NULL == fp) {
		print_status(_("There are no unread items!"));
		return;	/* if we don't find a feed with unread items do nothing */
	}

	/* load found feed */
	loadItemList(fp, NULL);
	
	/* find first unread item */
	findUnreadItem();
}

void on_popup_next_unread_item_selected(void) { on_next_unread_item_activate(NULL, NULL); }

void on_popup_zoomin_selected(void) { changeZoomLevel(0.2); }
void on_popup_zoomout_selected(void) { changeZoomLevel(-0.2); }

void on_popup_copy_url_selected(void) {	supplySelection(); }

void on_popup_subscribe_url_selected(void) {

	subscribeTo(FST_AUTODETECT, getSelectedURL(), g_strdup(selected_keyprefix), TRUE);
}

/*------------------------------------------------------------------------------*/
/* treeview creation and rendering						*/
/*------------------------------------------------------------------------------*/

static void renderFeedTitle(GtkTreeViewColumn *tree_column,
	             GtkCellRenderer   *cell,
	             GtkTreeModel      *model,
        	     GtkTreeIter       *iter,
	             gpointer           data)
{
	gint		type;
	gchar		*key, *title;
	feedPtr		fp;
	int		count;
	gboolean	unspecific = TRUE;

	gtk_tree_model_get(model, iter, FS_TYPE, &type,
					FS_TITLE, &title,
	                                FS_KEY, &key, -1);
	
	if(!IS_NODE(type) && (type != FST_EMPTY)) {
		g_assert(NULL != key);
		fp = getFeed(key);
		count = getFeedUnreadCount(fp);
		   
		if(count > 0) {
			unspecific = FALSE;
			g_object_set(GTK_CELL_RENDERER(cell), "font", "bold", NULL);
			g_object_set(GTK_CELL_RENDERER(cell), "text", g_strdup_printf("%s (%d)", getFeedTitle(fp), count), NULL);
		} else {
			g_object_set(GTK_CELL_RENDERER(cell), "text", getFeedTitle(fp), NULL);
			g_object_set(GTK_CELL_RENDERER(cell), "font", "normal", NULL);	
		}
	} else {
		g_object_set(GTK_CELL_RENDERER(cell), "text", title, NULL);
		g_object_set(GTK_CELL_RENDERER(cell), "font", "normal", NULL);	
	}
	g_free(title);
	g_free(key);
}


static void renderFeedStatus(GtkTreeViewColumn *tree_column,
	             GtkCellRenderer   *cell,
	             GtkTreeModel      *model,
        	     GtkTreeIter       *iter,
	             gpointer           data)
{
	gchar		*tmp_key;
	gint		type;
	feedPtr		fp;
	gpointer	icon;
	
	gtk_tree_model_get(model, iter,	FS_KEY, &tmp_key, 
					FS_TYPE, &type, -1);

	if(IS_FEED(type)) {
		g_assert(NULL != tmp_key);
		fp = getFeed(tmp_key);
	}
	
	switch(type) {
		case FST_HELPNODE:
			g_object_set(GTK_CELL_RENDERER(cell), "pixbuf", helpIcon, NULL);
			break;
		case FST_NODE:
			g_object_set(GTK_CELL_RENDERER(cell), "pixbuf", directoryIcon, NULL);
			break;
		case FST_EMPTY:
			g_object_set(GTK_CELL_RENDERER(cell), "pixbuf", emptyIcon, NULL);
			break;
		case FST_VFOLDER:
			g_object_set(GTK_CELL_RENDERER(cell), "pixbuf", vfolderIcon, NULL);
			break;
		case FST_OPML:
		case FST_OCS:
			if(getFeedAvailable(fp))
				g_object_set(GTK_CELL_RENDERER(cell), "pixbuf", listIcon, NULL);
			else
				g_object_set(GTK_CELL_RENDERER(cell), "pixbuf", unavailableIcon, NULL);
			break;
		case FST_HELPFEED:
		case FST_PIE:
		case FST_RSS:			
		case FST_CDF:
			if(getFeedAvailable(fp)) {
				if(NULL != (icon = getFeedIcon(fp)))
					g_object_set(GTK_CELL_RENDERER(cell), "pixbuf", icon, NULL);					
				else
					g_object_set(GTK_CELL_RENDERER(cell), "pixbuf", availableIcon, NULL);
			} else
				g_object_set(GTK_CELL_RENDERER(cell), "pixbuf", unavailableIcon, NULL);
			break;
		default:
			g_print(_("internal error! unknown entry type! cannot display appropriate icon!\n"));
			break;
			
	}
	
	g_free(tmp_key);
}

/* set up the entry list store and connects it to the entry list
   view in the main window */
void setupFeedList(GtkWidget *mainview) {
	GtkCellRenderer		*textRenderer;
	GtkCellRenderer		*iconRenderer;	
	GtkTreeViewColumn 	*column;
	GtkTreeSelection	*select;	
	GtkTreeStore		*feedstore;
	
	g_assert(mainwindow != NULL);
		
	feedstore = getFeedStore();

	gtk_tree_view_set_model(GTK_TREE_VIEW(mainview), GTK_TREE_MODEL(feedstore));
	
	/* we only render the state and title */
	iconRenderer = gtk_cell_renderer_pixbuf_new();
	textRenderer = gtk_cell_renderer_text_new();

	column = gtk_tree_view_column_new();
	
	gtk_tree_view_column_pack_start(column, iconRenderer, FALSE);
	gtk_tree_view_column_pack_start(column, textRenderer, TRUE);
	
	gtk_tree_view_column_set_attributes(column, iconRenderer, "pixbuf", FS_STATE, NULL);
	gtk_tree_view_column_set_attributes(column, textRenderer, "text", FS_TITLE, NULL);
	
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(mainview), column);

	gtk_tree_view_column_set_cell_data_func (column, iconRenderer, 
					   renderFeedStatus, NULL, NULL);
					   
	gtk_tree_view_column_set_cell_data_func (column, textRenderer,
                                           renderFeedTitle, NULL, NULL);			   
		
	/* Setup the selection handler for the main view */
	select = gtk_tree_view_get_selection(GTK_TREE_VIEW(mainview));
	gtk_tree_selection_set_mode(select, GTK_SELECTION_SINGLE);
	g_signal_connect(G_OBJECT(select), "changed",
                 	 G_CALLBACK(feedlist_selection_changed_cb),
                	 lookup_widget(mainwindow, "feedlist"));
			
}

static void renderItemTitle(GtkTreeViewColumn *tree_column,
	             GtkCellRenderer   *cell,
	             GtkTreeModel      *model,
        	     GtkTreeIter       *iter,
	             gpointer           data)
{
	gpointer	ip;

	gtk_tree_model_get(model, iter, IS_PTR, &ip, -1);

	if(FALSE == getItemReadStatus(ip)) {
		g_object_set(GTK_CELL_RENDERER(cell), "font", "bold", NULL);
	} else {
		g_object_set(GTK_CELL_RENDERER(cell), "font", "normal", NULL);
	}
}

static void renderItemStatus(GtkTreeViewColumn *tree_column,
	             GtkCellRenderer   *cell,
	             GtkTreeModel      *model,
        	     GtkTreeIter       *iter,
	             gpointer           data)
{
	gpointer	ip;

	gtk_tree_model_get(model, iter, IS_PTR, &ip, -1);

	if(FALSE == getItemMark(ip)) {
		if(FALSE == getItemReadStatus(ip)) {
			g_object_set(GTK_CELL_RENDERER(cell), "pixbuf", unreadIcon, NULL);
		} else {
			g_object_set(GTK_CELL_RENDERER(cell), "pixbuf", readIcon, NULL);
		}
	} else {
		g_object_set(GTK_CELL_RENDERER(cell), "pixbuf", flagIcon, NULL);
	}
}

static void renderItemDate(GtkTreeViewColumn *tree_column,
	             GtkCellRenderer   *cell,
	             GtkTreeModel      *model,
        	     GtkTreeIter       *iter,
	             gpointer           data)
{
	gint		time;
	gchar		*tmp;

	gtk_tree_model_get(model, iter, IS_TIME, &time, -1);
	if(0 != time) {
		tmp = formatDate((time_t)time);	// FIXME: sloooowwwwww...
		g_object_set(GTK_CELL_RENDERER(cell), "text", tmp, NULL);
		g_free(tmp);
	} else {
		g_object_set(GTK_CELL_RENDERER(cell), "text", "", NULL);
	}
}

/* sort function for the item list date column */
gint timeCompFunc(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data) {
	time_t	timea, timeb;
	
	g_assert(model != NULL);
	g_assert(a != NULL);
	g_assert(b != NULL);
	gtk_tree_model_get(model, a, IS_TIME, &timea, -1);
	gtk_tree_model_get(model, b, IS_TIME, &timeb, -1);
	
	return timea-timeb;
}

void setupItemList(GtkWidget *itemlist) {
	GtkCellRenderer		*renderer;
	GtkTreeViewColumn 	*column;
	GtkTreeSelection	*select;
	GtkTreeStore		*itemstore;	
	
	g_assert(mainwindow != NULL);
	
	itemstore = getItemStore();

	gtk_tree_view_set_model(GTK_TREE_VIEW(itemlist), GTK_TREE_MODEL(itemstore));

	/* we only render the state, title and time */
	renderer = gtk_cell_renderer_pixbuf_new();
	column = gtk_tree_view_column_new_with_attributes("", renderer, "pixbuf", IS_STATE, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(itemlist), column);
	/*gtk_tree_view_column_set_sort_column_id(column, IS_STATE); ...leads to segfaults on tab-bing through */
	gtk_tree_view_column_set_cell_data_func(column, renderer, renderItemStatus, NULL, NULL);

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(_("Date"), renderer, "text", IS_TIME, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(itemlist), column);
	gtk_tree_view_column_set_sort_column_id(column, IS_TIME);
	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(itemstore), IS_TIME, timeCompFunc, NULL, NULL);
	gtk_tree_view_column_set_cell_data_func(column, renderer, renderItemDate, NULL, NULL);
	g_object_set(column, "resizable", TRUE, NULL);

	renderer = gtk_cell_renderer_text_new();						   	
	column = gtk_tree_view_column_new_with_attributes(_("Headline"), renderer, "text", IS_TITLE, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(itemlist), column);
	gtk_tree_view_column_set_sort_column_id(column, IS_TITLE);
	gtk_tree_view_column_set_cell_data_func(column, renderer, renderItemTitle, NULL, NULL);
	g_object_set(column, "resizable", TRUE, NULL);
	
	/* Setup the selection handler */
	select = gtk_tree_view_get_selection(GTK_TREE_VIEW(itemlist));
	gtk_tree_selection_set_mode(select, GTK_SELECTION_SINGLE);
	g_signal_connect(G_OBJECT(select), "changed",
                  G_CALLBACK(on_itemlist_selection_changed),
                  NULL);
}

/*------------------------------------------------------------------------------*/
/* status bar callback, error box function, GUI update function			*/
/*------------------------------------------------------------------------------*/

void updateUI(void) {
	
	while (gtk_events_pending())
		gtk_main_iteration();
}

void print_status(gchar *statustext) {
	GtkWidget *statusbar;
	
	g_assert(mainwindow != NULL);
	statusbar = lookup_widget(mainwindow, "statusbar");

	g_print("%s\n", statustext);

	/* lock handling, because this method may be called from main
	   and update thread */
	if(updateThread == g_thread_self())
		gdk_threads_enter();

	gtk_label_set_text(GTK_LABEL(GTK_STATUSBAR(statusbar)->label), statustext);	

	if(updateThread == g_thread_self())
		gdk_threads_leave();
}

void showErrorBox(gchar *msg) {
	GtkWidget	*dialog;
	
	dialog = gtk_message_dialog_new(GTK_WINDOW(mainwindow),
                  GTK_DIALOG_DESTROY_WITH_PARENT,
                  GTK_MESSAGE_ERROR,
                  GTK_BUTTONS_CLOSE,
                  msg);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

void showInfoBox(gchar *msg) {
	GtkWidget	*dialog;
			
	dialog = gtk_message_dialog_new(GTK_WINDOW(mainwindow),
                  GTK_DIALOG_DESTROY_WITH_PARENT,
                  GTK_MESSAGE_INFO,
                  GTK_BUTTONS_CLOSE,
                  msg);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

/*------------------------------------------------------------------------------*/
/* timeout callback to check for update results					*/
/*------------------------------------------------------------------------------*/

gint checkForUpdateResults(gpointer data) {
	struct feed_request	*request = NULL;
	feedHandlerPtr		fhp;
	gint			type;

	if(NULL == (request = g_async_queue_try_pop(results)))
		return TRUE;

	if(NULL != request->new_fp) {
		type = getFeedType(request->fp);
		g_assert(NULL != feedHandler);
		if(NULL == (fhp = g_hash_table_lookup(feedHandler, (gpointer)&type))) {
			/* can happen during a long update e.g. of an OCS directory, then the type is not set, FIXME ! */
			//g_warning(g_strdup_printf(_("internal error! unknown feed type %d while updating feeds!"), type));
			return TRUE;
		}

		if(TRUE == fhp->merge)
			/* If the feed type supports merging... */
			mergeFeed(request->fp, request->new_fp);
		else {
			/* Otherwise we simply use the new feed info... */
			copyFeed(request->fp, request->new_fp);
//			print_status(g_strdup_printf(_("\"%s\" updated..."), getFeedTitle(request->fp)));
		}
		
		/* note this is to update the feed URL on permanent redirects */
		if(0 != strcmp(request->feedurl, getFeedSource(request->fp))) {
			setFeedSource(request->fp, g_strdup(request->feedurl));	
//			print_status(g_strdup_printf(_("The URL of \"%s\" has changed permanently and was updated."), getFeedTitle(request->fp)));
		}

		/* now fp contains the actual feed infos */
		saveFeed(request->fp);

		if((NULL != request->fp) && (selected_fp == request->fp)) {
			clearItemList();
			loadItemList(request->fp, NULL);
			preFocusItemlist();
		}
		redrawFeedList();	// FIXME: maybe this is overkill ;=)		
	} else {
//		print_status(g_strdup_printf(_("\"%s\" is not available!"), getFeedTitle(request->fp)));
		request->fp->available = FALSE;
	}
	
	return TRUE;
}

/*------------------------------------------------------------------------------*/
/* feed list DND handling							*/
/*------------------------------------------------------------------------------*/

void on_feedlist_drag_end(GtkWidget *widget, GdkDragContext  *drag_context, gpointer user_data) {

	g_assert(NULL != selected_keyprefix);
	
	if(drag_successful) {	
		moveInFeedList(selected_keyprefix, getFeedKey(selected_fp));
		checkForEmptyFolders();	/* to add an "(empty)" entry */
	}
	
	preFocusItemlist();
}

void on_feedlist_drag_begin(GtkWidget *widget, GdkDragContext  *drag_context, gpointer user_data) {

	drag_successful = FALSE;
}

gboolean on_feedlist_drag_drop(GtkWidget *widget, GdkDragContext *drag_context, gint x, gint y, guint time, gpointer user_data) {
	gboolean	stop = FALSE;

	g_assert(NULL != selected_keyprefix);
	
	/* don't allow folder DND */
	if(IS_NODE(selected_type)) {
		showErrorBox(_("Sorry Liferea does not yet support drag&drop of folders!"));
		stop = TRUE;
	} 
	/* also don't allow "(empty)" entry moving */
	else if(FST_EMPTY == selected_type) {
		stop = TRUE;
	} 
	/* also don't allow help feed dragging */
	else if(0 == strncmp(getFeedKey(selected_fp), "help", 4)) {
		showErrorBox(_("you cannot modify the special help folder contents!"));
		stop = TRUE;
	}
	
	drag_successful = !stop;
	
	return stop;
}

/*------------------------------------------------------------------------------*/
/* tree store setup								*/
/*------------------------------------------------------------------------------*/

GtkTreeStore * getItemStore(void) {

	if(NULL == itemstore) {
		/* set up a store of these attributes: 
			- item title
			- item state (read/unread)		
			- pointer to item data
			- date time_t value
			- the type of the feed the item belongs to

		 */
		itemstore = gtk_tree_store_new(5, G_TYPE_STRING, 
						  GDK_TYPE_PIXBUF, 
						  G_TYPE_POINTER, 
						  G_TYPE_INT,
						  G_TYPE_INT);
	}
	
	return itemstore;
}

GtkTreeStore * getFeedStore(void) {

	if(NULL == feedstore) {
		/* set up a store of these attributes: 
			- feed title
			- feed state icon (not available/available)
			- feed key in gconf
			- feed list view type (node/rss/cdf/pie/ocs...)
		 */
		feedstore = gtk_tree_store_new(4, G_TYPE_STRING, 
						  GDK_TYPE_PIXBUF, 
						  G_TYPE_STRING,
						  G_TYPE_INT);
	}
	
	return feedstore;
}

void clearItemList(void) {
	gtk_tree_store_clear(GTK_TREE_STORE(itemstore));
}

void displayItemList(void) {
	GtkTreeIter	iter;
	gchar		*buffer = NULL;
	gboolean	valid;
	itemPtr		ip;

	/* HTML widget can be used only from GTK thread */	
	if(gnome_vfs_is_primary_thread()) {
		startHTML(&buffer, itemlist_mode);
		if(!itemlist_mode) {
			/* two pane mode */
			valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(itemstore), &iter);
			while(valid) {	
				gtk_tree_model_get(GTK_TREE_MODEL(itemstore), &iter, IS_PTR, &ip, -1);
				
				if(getItemReadStatus(ip)) 
					addToHTMLBuffer(&buffer, UNSHADED_START);
				else
					addToHTMLBuffer(&buffer, SHADED_START);
				
				addToHTMLBuffer(&buffer, getItemDescription(ip));
				
				if(getItemReadStatus(ip))
					addToHTMLBuffer(&buffer, UNSHADED_END);
				else {
					addToHTMLBuffer(&buffer, SHADED_END);
					markItemAsRead(ip);
				}
					
				valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(itemstore), &iter);
			}
		} else {
			/* three pane mode */
			if(NULL == selected_ip) {
				/* display feed info */
				if(NULL != selected_fp)
					addToHTMLBuffer(&buffer, getFeedDescription(selected_fp));
			} else {
				/* display item content */
				markItemAsRead(ip);
				addToHTMLBuffer(&buffer, getItemDescription(selected_ip));
			}
		}
		finishHTML(&buffer);
		writeHTML(buffer);
	}
}

void loadItemList(feedPtr fp, gchar *searchstring) {
	GtkTreeIter	iter;
	GSList		*itemlist;
	itemPtr		ip;
	gint		count = 0;
	gchar		*title, *description;
	gboolean	add;

	if(NULL == fp) {
		g_warning(_("internal error! item list display for NULL pointer requested!\n"));
		return;
	}

	clearItemList();	
	itemlist = getFeedItemList(fp);		
	while(NULL != itemlist) {
		ip = itemlist->data;
		title = getItemTitle(ip);
		description = getItemDescription(ip);
		
		add = TRUE;
		if(NULL != searchstring) {
			add = FALSE;
				
			if((NULL != title) && (NULL != strstr(title, searchstring)))
				add = TRUE;

			if((NULL != description) && (NULL != strstr(description, searchstring)))
				add = TRUE;
		}

		if(add) {
			gtk_tree_store_append(itemstore, &iter, NULL);
			gtk_tree_store_set(itemstore, &iter,
	     		   		IS_TITLE, title,
					IS_PTR, ip,
					IS_TIME, getItemTime(ip),
					IS_TYPE, getFeedType(fp),	/* not the item type, this would fail for VFolders! */
					-1);
		}

		itemlist = g_slist_next(itemlist);
	}
	displayItemList();
	preFocusItemlist();
}

gboolean on_quit(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
	GtkWidget	*pane;
	gint		x,y;
	
	saveAllFeeds();
	saveAllFolderCollapseStates();
	
	/* save pane proportions */
	if(NULL != (pane = lookup_widget(mainwindow, "leftpane"))) {
		x = gtk_paned_get_position(GTK_PANED(pane));
		setNumericConfValue(LAST_VPANE_POS, x);
	}
	
	if(NULL != (pane = lookup_widget(mainwindow, "rightpane"))) {
		y = gtk_paned_get_position(GTK_PANED(pane));
		setNumericConfValue(LAST_HPANE_POS, y);
	}
	
	/* save window size */
	gtk_window_get_size(GTK_WINDOW(mainwindow), &x, &y);
	setNumericConfValue(LAST_WINDOW_WIDTH, x);
	setNumericConfValue(LAST_WINDOW_HEIGHT, y);	
	
	gtk_main_quit();
	return FALSE;
}
