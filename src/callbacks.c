/*
   Copyright (C) 2003 Lars Lindner <lars.lindner@gmx.net>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.
   
   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.
   
   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gtk/gtk.h>
#include <gdk/gdk.h>

#include "interface.h"
#include "support.h"
#include "backend.h"
#include "conf.h"
#include "common.h"
#include "callbacks.h"

#include "vfolder.h"	// FIXME

/* list of all namespaces supported by preferences dialog (FIXME) */
gchar	*nslist[] = { "dc", "content", "slash", "fm", "syn", "admin", NULL };

GtkWidget	*mainwindow;
GtkWidget	*newdialog = NULL;
GtkWidget	*feednamedialog = NULL;
GtkWidget	*propdialog = NULL;
GtkWidget	*prefdialog = NULL;
GtkWidget	*newfolderdialog = NULL;
GtkWidget	*foldernamedialog = NULL;

extern GdkPixbuf	*unreadIcon;
extern GdkPixbuf	*readIcon;
extern GdkPixbuf	*directoryIcon;
extern GdkPixbuf	*helpIcon;
extern GdkPixbuf	*listIcon;
extern GdkPixbuf	*availableIcon;
extern GdkPixbuf	*unavailableIcon;
extern GdkPixbuf	*vfolderIcon;

extern GThread	*updateThread;

static gint	itemlist_loading = 0;	/* freaky workaround */
static gchar	*new_key;		/* used by new feed dialog */

/* two globals to keep selected entry info while DND actions */
static gchar	*drag_source_key = NULL;
static gchar	*drag_source_keyprefix = NULL;

/*------------------------------------------------------------------------------*/
/* helper functions								*/
/*------------------------------------------------------------------------------*/

static gchar * getEntryViewSelection(GtkWidget *feedlist) {
	GtkTreeSelection	*select;
        GtkTreeModel		*model;
	GtkTreeIter		iter;	
	gchar			*feedkey = NULL;
		
	if(NULL == feedlist) {
		/* this is possible for the feed list editor window */
		return NULL;
	}
			
	if(NULL == (select = gtk_tree_view_get_selection(GTK_TREE_VIEW(feedlist)))) {
		print_status(_("could not retrieve selection of feed list!"));
		return NULL;
	}

        if(gtk_tree_selection_get_selected (select, &model, &iter))
                gtk_tree_model_get(model, &iter, FS_KEY, &feedkey, -1);
	else {
		return NULL;
	}
	
	return feedkey;
}

gchar * getMainFeedListViewSelection(void) {
	GtkWidget	*feedlistview;
	
	if(NULL == mainwindow)
		return NULL;
	
	if(NULL == (feedlistview = lookup_widget(mainwindow, "feedlist"))) {
		g_warning(_("feed list widget lookup failed!\n"));
		return NULL;
	}
	
	return getEntryViewSelection(feedlistview);
}

// FIXME: use something like getEntryPrefix()
gchar * getEntryViewSelectionPrefix(GtkWidget *window) {
	GtkWidget		*treeview;
	GtkTreeSelection	*select;
        GtkTreeModel		*model;
	GtkTreeIter		iter;
	GtkTreeIter		topiter;	
	gchar			*keyprefix;
	gboolean		valid;
	gint			tmp_type;
	
	if(NULL == window)
		return NULL;

	if(NULL == (treeview = lookup_widget(window, "feedlist"))) {
		g_warning(_("entry list widget lookup failed!\n"));
		return NULL;
	}
		
	if(NULL == (select = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview)))) {
		print_status(_("could not retrieve selection of entry list!"));
		return NULL;
	}

        if(gtk_tree_selection_get_selected (select, &model, &iter)) {
		
		/* the selected iter is usually not the directory iter... */
		if(0 != gtk_tree_store_iter_depth(GTK_TREE_STORE(model), &iter)) {

			/* scan through top level iterators till we find
			   the correct ancestor */
			valid = gtk_tree_model_get_iter_first(model, &topiter);
			while(valid) {	
				if(gtk_tree_store_is_ancestor(GTK_TREE_STORE(model), &topiter, &iter)) {
			                gtk_tree_model_get(model, &topiter, FS_KEY, &keyprefix, -1);
					return keyprefix;
				}
				valid = gtk_tree_model_iter_next(model, &topiter);
			}
		}
                gtk_tree_model_get(model, &iter, FS_TYPE, &tmp_type, 
		                                 FS_KEY, &keyprefix, -1);

		if(IS_FEED(tmp_type)) {
			/* this is necessary for all feeds in the default folder */	
			g_free(keyprefix);
			keyprefix = g_strdup("");
		}		
	} else {
		return NULL;
	}
	
	return keyprefix;
}

gint getEntryViewSelectionType(GtkWidget *window) {
	GtkWidget		*treeview;
	GtkTreeSelection	*select;
        GtkTreeModel		*model;
	GtkTreeIter		iter;	
	gint			type;
	
	if(NULL == window)
		return FST_INVALID;
	
	if(NULL == (treeview = lookup_widget(window, "feedlist"))) {
		g_warning(_("entry list widget lookup failed!\n"));
		return FST_INVALID;
	}
		
	if(NULL == (select = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview)))) {
		print_status(_("could not retrieve selection of entry list!"));
		return FST_INVALID;
	}

        if(gtk_tree_selection_get_selected (select, &model, &iter))
                gtk_tree_model_get(model, &iter, FS_TYPE, &type, -1);
	else {
		return FST_INVALID;
	}
	
	return type;
}

GtkTreeIter * getEntryViewSelectionIter(GtkWidget *window) {
	GtkTreeIter		*iter;
	GtkWidget		*treeview;
	GtkTreeSelection	*select;
        GtkTreeModel		*model;
	
	if(NULL == (iter = (GtkTreeIter *)g_malloc(sizeof(GtkTreeIter)))) 
		g_error("could not allocate memory!\n");
	
	if(NULL == window)
		return NULL;
	
	if(NULL == (treeview = lookup_widget(window, "feedlist"))) {
		g_warning(_("entry list widget lookup failed!\n"));
		return NULL;
	}
		
	if(NULL == (select = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview)))) {
		print_status(_("could not retrieve selection of entry list!"));
		return NULL;
	}

        gtk_tree_selection_get_selected(select, &model, iter);
	
	return iter;
}

static void selectFeedViewItem(GtkWidget *window, gchar *viewname, gchar *feedkey) {
	GtkWidget		*view;
	GtkTreeSelection	*select;
        GtkTreeModel		*model;
	GtkTreeIter		iter;
	GtkTreeStore		*feedstore;	
	gboolean		valid;
	gchar			*tmp_key;
	gint			tmp_type;
		
	if(NULL == window) {
		/* this is possible for the feed list editor window */
		return;	
	}
	
	if(NULL == (view = lookup_widget(window, viewname))) {
		g_warning(_("feed list widget lookup failed!\n"));
		return;
	}
		
	if(NULL == (select = gtk_tree_view_get_selection(GTK_TREE_VIEW(view)))) {
		g_warning(_("could not retrieve selection of feed list!\n"));
		return;
	}

	feedstore = getEntryStore();
	valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(feedstore), &iter);
	while(valid) {
		gtk_tree_model_get(GTK_TREE_MODEL(feedstore), &iter, 
		                   FS_KEY, &tmp_key,
				   FS_TYPE, &tmp_type,
				   -1);
		
		// FIXME: this should not be feed specific (OCS support!)
		if(IS_FEED(tmp_type)) {
			g_assert(NULL != tmp_key);
			if(0 == strcmp(tmp_key, feedkey)) {
				gtk_tree_selection_select_iter(select, &iter);
				break;
			}
		}

		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(feedstore), &iter);
	}
}

void redrawFeedList(void) {
	GtkWidget	*list;
	
	if(NULL == mainwindow)
		return;
	
	list = lookup_widget(mainwindow, "feedlist");
	if(NULL != list)  {
		gtk_widget_queue_draw(list);
	}
}

void redrawItemList(void) {
	GtkWidget	*list;
	
	if(NULL == mainwindow)
		return;
	
	list = lookup_widget(mainwindow, "Itemlist");
	if(NULL != list)  {
		gtk_widget_queue_draw(list);
	}
}


/*------------------------------------------------------------------------------*/
/* callbacks 									*/
/*------------------------------------------------------------------------------*/

void on_refreshbtn_clicked(GtkButton *button, gpointer user_data) {

	resetAllUpdateCounters();
	updateNow();	
}


void on_popup_refresh_selected(void) {
	gchar	*feedkey;
	
	if(NULL == mainwindow)
		return;	
	
	if(NULL != (feedkey = getEntryViewSelection(lookup_widget(mainwindow, "feedlist"))))
		updateEntry(feedkey);
}

/*------------------------------------------------------------------------------*/
/* preferences dialog callbacks 						*/
/*------------------------------------------------------------------------------*/

void on_prefbtn_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget	*browsercmdentry, 
			*timeformatentry,
			*proxyhostentry,
			*proxyportentry,
			*useproxycheck,
			*nscheck;
	int		i;
	gchar		*tmp;
				
	if(NULL == prefdialog || !G_IS_OBJECT(prefdialog))
		prefdialog = create_prefdialog ();		
	
	g_assert(NULL != prefdialog);

	browsercmdentry = lookup_widget(prefdialog, "browsercmd");
	timeformatentry = lookup_widget(prefdialog, "timeformat");
	proxyhostentry = lookup_widget(prefdialog, "proxyhost");
	proxyportentry = lookup_widget(prefdialog, "proxyport");
	useproxycheck =  lookup_widget(prefdialog, "useproxy");
	
	gtk_entry_set_text(GTK_ENTRY(browsercmdentry), getStringConfValue(BROWSER_COMMAND));
	gtk_entry_set_text(GTK_ENTRY(timeformatentry), getStringConfValue(TIME_FORMAT));
	gtk_entry_set_text(GTK_ENTRY(proxyhostentry), getStringConfValue(PROXY_HOST));
	gtk_entry_set_text(GTK_ENTRY(proxyportentry), g_strdup_printf("%d", getNumericConfValue(PROXY_PORT)));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(useproxycheck), getBooleanConfValue(USE_PROXY));
	
	/* set all namespace checkboxes */
	for(i = 0; NULL != nslist[i]; i++) {
		tmp = g_strdup_printf("use%s", nslist[i]);
		nscheck = lookup_widget(prefdialog, tmp);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(nscheck), 
				             getNameSpaceStatus(nslist[i]));
		g_free(tmp);
	}
				
	gtk_widget_show(prefdialog);
}

void on_prefsavebtn_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget	*browsercmdentry, 
			*timeformatentry,
			*proxyhostentry,
			*proxyportentry,
			*useproxycheck,
			*nscheck;
	int		i;
	gchar		*tmp;
	
	g_assert(NULL != prefdialog);
		
	browsercmdentry = lookup_widget(prefdialog, "browsercmd");
	timeformatentry = lookup_widget(prefdialog, "timeformat");
	proxyhostentry = lookup_widget(prefdialog, "proxyhost");
	proxyportentry = lookup_widget(prefdialog, "proxyport");
	useproxycheck =  lookup_widget(prefdialog, "useproxy");
	
	setStringConfValue(BROWSER_COMMAND,
			   (gchar *)gtk_entry_get_text(GTK_ENTRY(browsercmdentry)));
	setStringConfValue(TIME_FORMAT,
			   (gchar *)gtk_entry_get_text(GTK_ENTRY(timeformatentry)));
	setStringConfValue(PROXY_HOST,
			   (gchar *)gtk_entry_get_text(GTK_ENTRY(proxyhostentry)));
	setNumericConfValue(PROXY_PORT,
			   atoi(gtk_entry_get_text(GTK_ENTRY(proxyportentry))));
	setBooleanConfValue(USE_PROXY,
			   (gboolean)gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(useproxycheck)));
			   
	/* get and save all namespace checkboxes */
	for(i = 0; NULL != nslist[i]; i++) {
		tmp = g_strdup_printf("use%s", nslist[i]);
		nscheck = lookup_widget(prefdialog, tmp);
		setNameSpaceStatus(nslist[i], gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(nscheck)));
		g_free(tmp);
	}
						     
	/* reinitialize */
	loadConfig();
	initBackend();
			
	gtk_widget_hide(prefdialog);
}

/*------------------------------------------------------------------------------*/
/* delete entry callbacks 							*/
/*------------------------------------------------------------------------------*/

void on_deletebtn(GtkWidget *feedlist) {
	GtkTreeIter	*iter;
	gchar		*keyprefix;
	gchar		*key;

	/* user_data has to contain the feed list widget reference */
	key = getEntryViewSelection(feedlist);
	keyprefix = getEntryViewSelectionPrefix(mainwindow);
	iter = getEntryViewSelectionIter(mainwindow);

	/* make sure thats no grouping iterator */
	if(NULL != key) {
		/* block deleting of help feeds */
		if(0 == strncmp(key, "help", strlen("help"))) {
			showErrorBox("You can't delete help feeds!");
			return;
		}

		print_status(g_strdup_printf("%s \"%s\"",_("Deleting entry"), getDefaultEntryTitle(key)));
		removeEntry(keyprefix, key);
		gtk_tree_store_remove(getEntryStore(), iter);
		g_free(key);
		g_free(iter);
				
		clearItemList();
	} else {
		print_status(_("Error: Cannot delete this list entry!"));
	}
}

void on_popup_delete_selected(void) {
	
	if(NULL == mainwindow)
		return;
		
	on_deletebtn(lookup_widget(mainwindow, "feedlist"));
}

/*------------------------------------------------------------------------------*/
/* property dialog callbacks 							*/
/*------------------------------------------------------------------------------*/

void on_propbtn(GtkWidget *feedlist) {
	gint		type;
	gchar		*feedkey;
	GtkWidget 	*feednameentry, *feedurlentry, *updateIntervalBtn;
	GtkAdjustment	*updateInterval;
	gint		defaultInterval;
	gchar		*defaultIntervalStr;
	
	/* user_data has to contain the feed list widget reference */
	if(NULL == (feedkey = getEntryViewSelection(feedlist))) {
		print_status(_("Internal Error! Feed pointer NULL!\n"));
		return;
	}

	/* block changing of help feeds */
	if(0 == strncmp(feedkey, "help", strlen("help"))) {
		showErrorBox("You can't modify help feeds!");
		return;
	}
	
	type = getEntryViewSelectionType(mainwindow);
	
	if(NULL == propdialog || !G_IS_OBJECT(propdialog))
		propdialog = create_propdialog();
	
	if(NULL == propdialog)
		return;
		
	feednameentry = lookup_widget(propdialog, "feednameentry");
	feedurlentry = lookup_widget(propdialog, "feedurlentry");
	updateIntervalBtn = lookup_widget(propdialog, "feedrefreshcount");
	updateInterval = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(updateIntervalBtn));

	gtk_entry_set_text(GTK_ENTRY(feednameentry), getDefaultEntryTitle(feedkey));
	gtk_entry_set_text(GTK_ENTRY(feedurlentry), (gchar *)getFeedProp(feedkey, FEED_PROP_SOURCE));

	if(FST_OCS == type) {	
		/* disable the update interval selector for OCS feeds */
		gtk_widget_set_sensitive(lookup_widget(propdialog, "feedrefreshcount"), FALSE);
	} else {
		/* enable and adjust values otherwise */
		gtk_widget_set_sensitive(lookup_widget(propdialog, "feedrefreshcount"), TRUE);	
		
		gtk_adjustment_set_value(updateInterval, (gint)getFeedProp(feedkey, FEED_PROP_UPDATEINTERVAL));

		defaultInterval = (gint)getFeedProp(feedkey, FEED_PROP_DFLTUPDINTERVAL);
		if(-1 != defaultInterval)
			defaultIntervalStr = g_strdup_printf(_("The feed specifies an update interval of %d minutes"), defaultInterval);
		else
			defaultIntervalStr = g_strdup(_("This feed specifies no default interval."));
		gtk_label_set_text(GTK_LABEL(lookup_widget(propdialog, "feedupdateinfo")), defaultIntervalStr);
		g_free(defaultIntervalStr);		
	}
	
	/* note: the OK buttons signal is connected on the fly
	   to pass the correct dialog widget that feedlist was
	   clicked... */

	g_signal_connect((gpointer)lookup_widget(propdialog, "propchangebtn"), 
			 "clicked", G_CALLBACK (on_propchangebtn_clicked), feedlist);
   
	   
	gtk_widget_show(propdialog);
}

void on_propchangebtn_clicked(GtkButton *button, gpointer user_data) {
	gchar		*feedkey;
	GtkWidget 	*feedurlentry;
	GtkWidget 	*feednameentry;
	GtkWidget 	*updateIntervalBtn;
	GtkAdjustment	*updateInterval;
	gint		interval;
	gint		type;

	
	g_assert(NULL != propdialog);
		
	type = getEntryViewSelectionType(GTK_WIDGET(user_data));
	if(NULL != (feedkey = getEntryViewSelection(GTK_WIDGET(user_data)))) {
		feednameentry = lookup_widget(propdialog, "feednameentry");
		feedurlentry = lookup_widget(propdialog, "feedurlentry");

		gchar *feedurl = (gchar *)gtk_entry_get_text(GTK_ENTRY(feedurlentry));
		gchar *feedname = (gchar *)gtk_entry_get_text(GTK_ENTRY(feednameentry));
	
		setFeedProp(feedkey, FEED_PROP_USERTITLE, (gpointer)g_strdup(feedname));  
		setFeedProp(feedkey, FEED_PROP_SOURCE, (gpointer)g_strdup(feedurl));
		
		if(IS_FEED(type)) {
			updateIntervalBtn = lookup_widget(propdialog, "feedrefreshcount");
			updateInterval = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(updateIntervalBtn));

			interval = gtk_adjustment_get_value(updateInterval);
			
			if(0 == interval)
				interval = -1;	/* this is due to ignore this feed while updating */
			setFeedProp(feedkey, FEED_PROP_UPDATEINTERVAL, (gpointer)interval);
		}

		selectFeedViewItem(mainwindow, "feedlist", feedkey);
	}
}

void on_popup_prop_selected(void) {

	g_assert(NULL != mainwindow);
	
	on_propbtn(GTK_WIDGET(lookup_widget(mainwindow, "feedlist")));
}

/*------------------------------------------------------------------------------*/
/* new entry dialog callbacks 							*/
/*------------------------------------------------------------------------------*/

void on_newbtn_clicked(GtkButton *button, gpointer user_data) {	
	GtkWidget	*feedurlentry;
	GtkWidget	*feednameentry;
	gchar		*keyprefix;
	
	keyprefix = getEntryViewSelectionPrefix(mainwindow);
	/* block changing of help feeds */
	if(0 == strncmp(keyprefix, "help", strlen("help"))) {
		showErrorBox("You can't add feeds to the help folder!");
		return;
	}

	if(NULL == newdialog || !G_IS_OBJECT(newdialog)) 
		newdialog = create_newdialog();

	if(NULL == feednamedialog || !G_IS_OBJECT(feednamedialog)) 
		feednamedialog = create_feednamedialog();

	g_assert(NULL != newdialog);
	
	/* always clear the edit field */
	feedurlentry = lookup_widget(newdialog, "newfeedentry");
	gtk_entry_set_text(GTK_ENTRY(feedurlentry), "");	

	gtk_widget_show(newdialog);
}

void on_newfeedbtn_clicked(GtkButton *button, gpointer user_data) {
	gchar		*key;
	gchar		*keyprefix;
	gchar		*source;
	GtkWidget 	*sourceentry;	
	GtkWidget 	*titleentry, *feedradiobtn, *ocsradiobtn, 
			*pieradiobtn, *updateIntervalBtn;
	GtkWidget	*cdfradiobtn;		
	gint		type = FST_RSS;
	gint		interval;
	
	g_assert(newdialog != NULL);
	g_assert(feednamedialog != NULL);
	
	sourceentry = lookup_widget(newdialog, "newfeedentry");
	titleentry = lookup_widget(feednamedialog, "feednameentry");
	feedradiobtn = lookup_widget(newdialog, "typeradiobtn");
	cdfradiobtn = lookup_widget(newdialog, "typeradiobtn1");	
	ocsradiobtn = lookup_widget(newdialog, "typeradiobtn2");
	pieradiobtn = lookup_widget(newdialog, "typeradiobtn3");
	
	source = (gchar *)gtk_entry_get_text(GTK_ENTRY(sourceentry));
	keyprefix = getEntryViewSelectionPrefix(mainwindow);
		
	/* FIXME: make this more generic! */
	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(feedradiobtn))) {
		type = FST_RSS;
	}
	else if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pieradiobtn))) {
		type = FST_PIE;
	}
	else if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cdfradiobtn))) {
		type = FST_CDF;
	}
	else if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ocsradiobtn))) {
		type = FST_OCS;
		/* disable the update interval selector */
		gtk_widget_set_sensitive(lookup_widget(feednamedialog, "feedrefreshcount"), FALSE);
	}

	if(NULL != (keyprefix = getEntryViewSelectionPrefix(mainwindow))) {

		if(NULL != (key = newEntry(type, source, keyprefix))) {

			gtk_entry_set_text(GTK_ENTRY(titleentry), getDefaultEntryTitle(key));
			new_key = key;
		
			updateIntervalBtn = lookup_widget(feednamedialog, "feedrefreshcount");
			if(-1 != (interval = (gint)getFeedProp(key, FEED_PROP_DFLTUPDINTERVAL)))
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(updateIntervalBtn), (gfloat)interval);

			gtk_widget_show(feednamedialog);
		} // FIXME: else
	} else {
		print_status("could not get entry key prefix! maybe you did not select a group");
	}
	
	/* don't free source/keyprefix for they are reused by newEntry! */
}

void on_feednamebutton_clicked(GtkButton *button, gpointer user_data) {	
	GtkWidget	*feednameentry;
	GtkWidget 	*updateIntervalBtn;
	GtkAdjustment	*updateInterval;
	gint		interval;
	
	gchar		*feedurl;
	gchar		*feedname;

	g_assert(feednamedialog != NULL);

	feednameentry = lookup_widget(feednamedialog, "feednameentry");
	updateIntervalBtn = lookup_widget(feednamedialog, "feedrefreshcount");
	updateInterval = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(updateIntervalBtn));

	/* this button may be disabled, because we added an OCS directory
	   enable it for the next new dialog usage... */
	gtk_widget_set_sensitive(GTK_WIDGET(updateIntervalBtn), TRUE);
	
	feedname = (gchar *)gtk_entry_get_text(GTK_ENTRY(feednameentry));
	interval = gtk_adjustment_get_value(updateInterval);
	
	setFeedProp(new_key, FEED_PROP_USERTITLE, (gpointer)g_strdup(feedname));
	
	if(IS_FEED(getEntryType(new_key)))
		setFeedProp(new_key, FEED_PROP_UPDATEINTERVAL, (gpointer)interval);
		
	new_key = NULL;
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
		addFolder(folderkey, foldertitle, FST_NODE);
	} else {
		print_status(_("internal error! could not get a new folder key!"));
	}	
}

void on_popup_foldername_selected(void) {
	GtkWidget	*foldernameentry;
	gchar 		*keyprefix, *title;

	if(NULL == foldernamedialog || !G_IS_OBJECT(foldernamedialog))
		foldernamedialog = create_foldernamedialog();
		
	foldernameentry = lookup_widget(foldernamedialog, "foldernameentry");
	if(NULL != (keyprefix = getMainFeedListViewSelection())) {
		title = getFolderTitle(keyprefix);
		gtk_entry_set_text(GTK_ENTRY(foldernameentry), title);
		g_free(title);
		gtk_widget_show(foldernamedialog);
	} else {
		showErrorBox("internal error: could not determine folder key!");
	}
}

void on_foldernamechangebtn_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget	*foldernameentry;
	gchar 		*keyprefix;
	
	if(NULL != (keyprefix = getMainFeedListViewSelection())) {
		foldernameentry = lookup_widget(foldernamedialog, "foldernameentry");
		setFolderTitle(keyprefix, (gchar *)gtk_entry_get_text(GTK_ENTRY(foldernameentry)));
	} else {
		showErrorBox("internal error: could not determine folder key!");
	}

	gtk_widget_show(foldernamedialog);
}

void on_popup_removefolder_selected(void) {
	GtkTreeStore	*entrystore;
	GtkTreeIter	*iter;
	gchar		*keyprefix;
	
	keyprefix = getEntryViewSelectionPrefix(mainwindow);
	iter = getEntryViewSelectionIter(mainwindow);
	entrystore = getEntryStore();
	
	g_assert(entrystore != NULL);
	
	/* make sure thats no grouping iterator */
	if(NULL != keyprefix) {
		/* check if folder is empty */
		if(FALSE == gtk_tree_model_iter_has_child(GTK_TREE_MODEL(entrystore), iter)) {
			gtk_tree_store_remove(entrystore, iter);
			removeFolder(keyprefix);
			g_free(keyprefix);
		} else {
			showErrorBox(_("A folder must be empty to delete it!"));
		}
	} else {
		print_status(_("Error: Cannot determine folder key!"));
	}
	g_free(iter);	
}

void on_popup_allunread_selected(void) {
	GtkTreeModel 	*model;
	GtkTreeIter	iter;
	gpointer	tmp_ip;
	gint		type;
	gboolean 	valid;

	model = GTK_TREE_MODEL(getItemStore());
	valid = gtk_tree_model_get_iter_first(model, &iter);

	while(valid) {
               	gtk_tree_model_get (model, &iter, IS_PTR, &tmp_ip,
						  IS_TYPE, &type, -1);
		g_assert(tmp_ip != NULL);
		if(IS_FEED(type))
			markItemAsRead(type, tmp_ip);

		valid = gtk_tree_model_iter_next(model, &iter);
	}

	/* redraw feed list to update unread items count */
	redrawFeedList();

	/* necessary to rerender the formerly bold text labels */
	redrawItemList();	
}

/*------------------------------------------------------------------------------*/
/* search callbacks								*/
/*------------------------------------------------------------------------------*/

/* called when toolbar search button is clicked */
void on_searchbtn_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget	*searchbox;
	gboolean	visible;

	g_assert(mainwindow != NULL);

	if(NULL != (searchbox = lookup_widget(mainwindow, "searchbox"))) {
		g_object_get(searchbox, "visible", &visible, NULL);
		g_object_set(searchbox, "visible", !visible, NULL);
	}
}

/* called when close button in search dialog is clicked */
void on_hidesearch_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget	*searchbox;

	g_assert(mainwindow != NULL);
	
	if(NULL != (searchbox = lookup_widget(mainwindow, "searchbox"))) {
		g_object_set(searchbox, "visible", FALSE, NULL);
	}
}


void on_searchentry_activate(GtkEntry *entry, gpointer user_data) {
	GtkWidget		*searchentry;
	G_CONST_RETURN gchar	*searchstring;

	g_assert(mainwindow != NULL);
	
	if(NULL != (searchentry = lookup_widget(mainwindow, "searchentry"))) {
		searchstring = gtk_entry_get_text(GTK_ENTRY(searchentry));
		print_status(g_strdup_printf(_("searching for \"%s\""), searchstring));
		// FIXME: use a VFolder instead of searchItems()
		searchItems((gchar *)searchstring);
	}
}

void on_newVFolder_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget		*searchentry;
	G_CONST_RETURN gchar	*searchstring;
	rulePtr			rp;	// FIXME: this really does not belong here!!! -> vfolder.c
	gchar			*key, *keyprefix;
	
	g_assert(mainwindow != NULL);
	keyprefix = getEntryViewSelectionPrefix(mainwindow);
		

	if(NULL != (searchentry = lookup_widget(mainwindow, "searchentry"))) {
		searchstring = gtk_entry_get_text(GTK_ENTRY(searchentry));
		print_status(g_strdup_printf(_("creating VFolder for search term \"%s\""), searchstring));

		if(NULL != (keyprefix = getEntryViewSelectionPrefix(mainwindow))) {

			if(NULL != (key = newEntry(FST_VFOLDER, "", keyprefix))) {
				// FIXME: this really does not belong here!!! -> vfolder.c
				/* setup a rule */
				if(NULL == (rp = (rulePtr)g_malloc(sizeof(struct rule)))) 
					g_error(_("could not allocate memory!"));

				rp->value = (gchar *)searchstring;

				/* we set the searchstring as a default title */
				setFeedProp(key, FEED_PROP_USERTITLE, (gpointer)g_strdup_printf(_("VFolder %s"),searchstring));

				loadNewVFolder(key, rp);
			} // FIXME: else
		} else {
			print_status("could not get entry key prefix! maybe you did not select a group");
		}
		
	}
g_print("VFolder created!\n");
	/* don't free keyprefix and searchstring for its reused by newEntry! */
}

/*------------------------------------------------------------------------------*/
/* selection change callbacks							*/
/*------------------------------------------------------------------------------*/

void feedlist_selection_changed_cb(GtkTreeSelection *selection, gpointer data) {
	GtkWidget		*itemlist;
	GtkTreeSelection	*itemselection;
	GtkTreeIter		iter;
        GtkTreeModel		*model;
	gchar			*tmp_key;
	gint			tmp_type;
	GdkGeometry		geometry;

	g_assert(mainwindow != NULL);

        if (gtk_tree_selection_get_selected (selection, &model, &iter))
        {
                gtk_tree_model_get (model, &iter, 
				FS_KEY, &tmp_key,
				FS_TYPE, &tmp_type,
				-1);
				
		/* make sure thats no grouping iterator */
		if(!IS_NODE(tmp_type)) {
			g_assert(NULL != tmp_key);

			clearItemList();
			loadItemList(tmp_key, NULL);
			g_free(tmp_key);
			
			/* FIXME: another workaround to prevent strange window
			   size increasings after feed selection changing */
			geometry.min_height=480;
			geometry.min_width=640;
			gtk_window_set_geometry_hints(GTK_WINDOW(mainwindow), mainwindow, &geometry, GDK_HINT_MIN_SIZE);

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
			itemlist_loading = 0;
		}		
       	}
}

void itemlist_selection_changed(void) {
	GtkTreeSelection	*selection;
	GtkWidget		*itemlist;
	GtkTreeIter		iter;
        GtkTreeModel		*model;

	gpointer	tmp_ip;
	gint		type;
	gchar		*tmp_key;

	/* do nothing upon initial focussing */
	if(!itemlist_loading) {
		g_assert(mainwindow != NULL);

		if(NULL == (itemlist = lookup_widget(mainwindow, "Itemlist"))) {
			return;
		}

		if(NULL == (selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(itemlist)))) {
			print_status(_("could not retrieve selection of feed list!"));
			return;
		}

       		if(gtk_tree_selection_get_selected(selection, &model, &iter)) {

               		gtk_tree_model_get (model, &iter, IS_PTR, &tmp_ip,
							  IS_TYPE, &type, -1);

			g_assert(tmp_ip != NULL);
			if((0 == itemlist_loading)) {
				if(NULL != (itemlist = lookup_widget(mainwindow, "Itemlist"))) {
					loadItem(type, tmp_ip);

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

/*------------------------------------------------------------------------------*/
/* treeview creation and rendering						*/
/*------------------------------------------------------------------------------*/

static void renderEntryTitle(GtkTreeViewColumn *tree_column,
	             GtkCellRenderer   *cell,
	             GtkTreeModel      *model,
        	     GtkTreeIter       *iter,
	             gpointer           data)
{
	gint		tmp_type;
	gchar		*tmp_key;
	int		count;
	gboolean	unspecific = TRUE;

	gtk_tree_model_get(model, iter, FS_TYPE, &tmp_type, 
	                                FS_KEY, &tmp_key, -1);
	
	if(IS_FEED(tmp_type)) {
		g_assert(NULL != tmp_key);	
		count = (gint)getFeedProp(tmp_key, FEED_PROP_UNREADCOUNT);
		   
		if(count > 0) {
			unspecific = FALSE;
			g_object_set(GTK_CELL_RENDERER(cell), "font", "bold", NULL);
			g_object_set(GTK_CELL_RENDERER(cell), "text", g_strdup_printf("%s (%d)", getDefaultEntryTitle(tmp_key), count), NULL);
		}
	}
	g_free(tmp_key);
	
	if(unspecific)
		g_object_set(GTK_CELL_RENDERER(cell), "font", "normal", NULL);
}


static void renderEntryStatus(GtkTreeViewColumn *tree_column,
	             GtkCellRenderer   *cell,
	             GtkTreeModel      *model,
        	     GtkTreeIter       *iter,
	             gpointer           data)
{
	GdkPixbuf	*tmp_state;
	gchar		*tmp_key;
	gint		tmp_type;
	
	gtk_tree_model_get(model, iter, FS_TYPE, &tmp_type, 
					FS_KEY, &tmp_key,
	                                FS_STATE, &tmp_state, -1);

	switch(tmp_type) {
		case FST_HELPNODE:
			g_object_set(GTK_CELL_RENDERER(cell), "pixbuf", helpIcon, NULL);
			break;
		case FST_NODE:
			g_object_set(GTK_CELL_RENDERER(cell), "pixbuf", directoryIcon, NULL);
			break;
		case FST_VFOLDER:
			g_object_set(GTK_CELL_RENDERER(cell), "pixbuf", vfolderIcon, NULL);
			break;
		case FST_OCS:
			if((gboolean)getFeedProp(tmp_key, FEED_PROP_AVAILABLE))
				g_object_set(GTK_CELL_RENDERER(cell), "pixbuf", listIcon, NULL);
			else
				g_object_set(GTK_CELL_RENDERER(cell), "pixbuf", unavailableIcon, NULL);
			break;
		case FST_PIE:
		case FST_RSS:			
		case FST_CDF:
			if((gboolean)getFeedProp(tmp_key, FEED_PROP_AVAILABLE))
				g_object_set(GTK_CELL_RENDERER(cell), "pixbuf", availableIcon, NULL);
			else
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
void setupEntryList(GtkWidget *mainview) {
	GtkCellRenderer		*textRenderer;
	GtkCellRenderer		*iconRenderer;	
	GtkTreeViewColumn 	*column;
	GtkTreeSelection	*select;	
	GtkTreeStore		*entrystore;
	
	g_assert(mainwindow != NULL);
		
	entrystore = getEntryStore();

	gtk_tree_view_set_model(GTK_TREE_VIEW(mainview), GTK_TREE_MODEL(entrystore));
	
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
					   renderEntryStatus, NULL, NULL);
					   
	gtk_tree_view_column_set_cell_data_func (column, textRenderer,
                                           renderEntryTitle, NULL, NULL);			   
		
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
	gpointer	*tmp_ip;
	gint		type;
	gboolean 	result;

	gtk_tree_model_get(model, iter, IS_PTR, &tmp_ip,
					IS_TYPE, &type, -1);

	if(IS_FEED(type))
		result = getItemReadStatus(type, tmp_ip);
	
	if(FALSE == result) {
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
	gpointer	tmp_ip;
	gint		type;
	gboolean	result = TRUE;

	gtk_tree_model_get(model, iter, IS_PTR, &tmp_ip,
					IS_TYPE, &type, -1);

	if(IS_FEED(type))
		result = getItemReadStatus(type, tmp_ip);
			
	if(FALSE == result) {
		g_object_set(GTK_CELL_RENDERER(cell), "pixbuf", unreadIcon, NULL);
	} else {
		g_object_set(GTK_CELL_RENDERER(cell), "pixbuf", readIcon, NULL);
	}
}

static void renderItemDate(GtkTreeViewColumn *tree_column,
	             GtkCellRenderer   *cell,
	             GtkTreeModel      *model,
        	     GtkTreeIter       *iter,
	             gpointer           data)
{
	gint		tmp_time;
	gchar		*tmp;

	gtk_tree_model_get(model, iter, IS_TIME, &tmp_time, -1);
	tmp = formatDate((time_t)tmp_time);	// FIXME: sloooowwwwww...
	g_object_set(GTK_CELL_RENDERER(cell), "text", tmp, NULL);
	g_free(tmp);
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
						
	renderer = gtk_cell_renderer_text_new();						   	
	column = gtk_tree_view_column_new_with_attributes(_("Headline"), renderer, "text", IS_TITLE, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(itemlist), column);
	gtk_tree_view_column_set_sort_column_id(column, IS_TITLE);
	gtk_tree_view_column_set_cell_data_func(column, renderer, renderItemTitle, NULL, NULL);

	/* Setup the selection handler */
	select = gtk_tree_view_get_selection(GTK_TREE_VIEW(itemlist));
	gtk_tree_selection_set_mode(select, GTK_SELECTION_SINGLE);
	g_signal_connect(G_OBJECT(select), "changed",
                  G_CALLBACK(on_itemlist_selection_changed),
                  NULL);
}

/*------------------------------------------------------------------------------*/
/* popup menu callbacks 							*/
/*------------------------------------------------------------------------------*/

static GtkItemFactoryEntry feedentry_menu_items[] = {
      {"/_Update Feed", 	NULL, on_popup_refresh_selected, 0, NULL},
      {"/_New",			NULL, 0, 0, "<Branch>" },
      {"/_New/New _Feed", 	NULL, on_newbtn_clicked, 0, NULL},
      {"/_New/New F_older", 	NULL, on_popup_newfolder_selected, 0, NULL},
      {"/_Delete Feed",		NULL, on_popup_delete_selected, 0, NULL},
      {"/_Properties",		NULL, on_popup_prop_selected, 0, NULL}
};

static GtkItemFactoryEntry ocsentry_menu_items[] = {
      {"/_Update Directory",	NULL, on_popup_refresh_selected, 0, NULL},
      {"/_New",			NULL, 0, 0, "<Branch>" },
      {"/_New/New _Feed", 	NULL, on_newbtn_clicked, 0, NULL},
      {"/_New/New F_older", 	NULL, on_popup_newfolder_selected, 0, NULL},      
      {"/_Delete Directory",	NULL, on_popup_delete_selected, 0, NULL},
      {"/_Properties",		NULL, on_popup_prop_selected, 0, NULL}
};

static GtkItemFactoryEntry node_menu_items[] = {
      {"/_New",			NULL, 0, 0, "<Branch>" },
      {"/_New/New _Feed", 	NULL, on_newbtn_clicked, 0, NULL},
      {"/_New/New F_older", 	NULL, on_popup_newfolder_selected, 0, NULL},
      {"/_Rename Folder",	NULL, on_popup_foldername_selected, 0 , NULL},
      {"/_Delete Folder", 	NULL, on_popup_removefolder_selected, 0, NULL}
};

static GtkItemFactoryEntry vfolder_menu_items[] = {
      {"/_New",			NULL, 0, 0, "<Branch>" },
      {"/_New/New _Feed", 	NULL, on_newbtn_clicked, 0, NULL},
      {"/_New/New F_older", 	NULL, on_popup_newfolder_selected, 0, NULL},      
      {"/_Delete VFolder",	NULL, on_popup_delete_selected, 0, NULL},
};

static GtkItemFactoryEntry default_menu_items[] = {
      {"/_New Folder", 	NULL, on_popup_newfolder_selected, 0, NULL}
};

static GtkMenu *make_entry_menu(gint type) {
	GtkWidget 		*menubar;
	GtkItemFactory 		*item_factory;
	gint 			nmenu_items;
	GtkItemFactoryEntry	*menu_items;
	
	switch(type) {
		case FST_NODE:
			menu_items = node_menu_items;
			nmenu_items = sizeof(node_menu_items)/(sizeof(node_menu_items[0]));
			break;
		case FST_VFOLDER:
			menu_items = vfolder_menu_items;
			nmenu_items = sizeof(vfolder_menu_items)/(sizeof(vfolder_menu_items[0]));
			break;
		case FST_PIE:
		case FST_RSS:
		case FST_CDF:
			menu_items = feedentry_menu_items;
			nmenu_items = sizeof(feedentry_menu_items)/(sizeof(feedentry_menu_items[0]));
			break;
		case FST_OCS:
			menu_items = ocsentry_menu_items;
			nmenu_items = sizeof(ocsentry_menu_items)/(sizeof(ocsentry_menu_items[0]));
			break;
		default:
			menu_items = default_menu_items;
			nmenu_items = sizeof(default_menu_items)/(sizeof(default_menu_items[0]));
			break;
	}

	item_factory = gtk_item_factory_new(GTK_TYPE_MENU, "<feedentrypopup>", NULL);
	gtk_item_factory_create_items(item_factory, nmenu_items, menu_items, NULL);
	menubar = gtk_item_factory_get_widget(item_factory, "<feedentrypopup>");

	return GTK_MENU(menubar);
}


static GtkItemFactoryEntry item_menu_items[] = {
      {"/_Mark all as read", 	NULL, on_popup_allunread_selected, 0, NULL}
};

static GtkMenu *make_item_menu(void) {
	GtkWidget 		*menubar;
	GtkItemFactory 		*item_factory;
	gint 			nmenu_items;
	GtkItemFactoryEntry	*menu_items;
	
	menu_items = item_menu_items;
	nmenu_items = sizeof(item_menu_items)/(sizeof(item_menu_items[0]));

	item_factory = gtk_item_factory_new(GTK_TYPE_MENU, "<itempopup>", NULL);
	gtk_item_factory_create_items(item_factory, nmenu_items, menu_items, NULL);
	menubar = gtk_item_factory_get_widget(item_factory, "<itempopup>");

	return GTK_MENU(menubar);
}


gboolean on_mainfeedlist_button_press_event(GtkWidget *widget,
					    GdkEventButton *event,
                                            gpointer user_data)
{
	GdkEventButton 	*eb;
	gboolean 	retval;
	gint		type;
  
	if (event->type != GDK_BUTTON_PRESS) return FALSE;
	eb = (GdkEventButton*) event;

	if (eb->button != 3)
		return FALSE;

	// FIXME: don't use existing selection, but determine
	// which selection would result from the right mouse click
	type = getEntryViewSelectionType(mainwindow);
	gtk_menu_popup(make_entry_menu(type), NULL, NULL, NULL, NULL, eb->button, eb->time);
		
	return TRUE;
}

gboolean on_itemlist_button_press_event(GtkWidget *widget,
					    GdkEventButton *event,
                                            gpointer user_data)
{
	GdkEventButton 	*eb;
	gboolean 	retval;
	gint		type;
  
	if (event->type != GDK_BUTTON_PRESS) return FALSE;
	eb = (GdkEventButton*) event;

	if (eb->button != 3) 
		return FALSE;

	/* right click -> popup */
	gtk_menu_popup(make_item_menu(), NULL, NULL, NULL, NULL, eb->button, eb->time);
		
	return TRUE;
}

/*------------------------------------------------------------------------------*/
/* status bar callback 								*/
/*------------------------------------------------------------------------------*/

void print_status(gchar *statustext) {
	GtkWidget *statusbar;
	
	g_assert(mainwindow != NULL);
	statusbar = lookup_widget(mainwindow, "statusbar");

	g_print("%s\n", statustext);
	
	/* lock handling, because this method is called from main
	   and update thread */
	if(updateThread == g_thread_self())
		gdk_threads_enter();
	
	gtk_label_set_text(GTK_LABEL(GTK_STATUSBAR(statusbar)->label), statustext);
	
	if(updateThread == g_thread_self())
		gdk_threads_leave();
}

void showErrorBox(gchar *msg) {
	GtkWidget	*dialog;

	if(updateThread == g_thread_self())	// FIXME: deadlock when using this function from update thread
		return;
			
	dialog = gtk_message_dialog_new(GTK_WINDOW(mainwindow),
                  GTK_DIALOG_DESTROY_WITH_PARENT,
                  GTK_MESSAGE_ERROR,
                  GTK_BUTTONS_CLOSE,
                  msg);
	 gtk_dialog_run (GTK_DIALOG (dialog));
	 gtk_widget_destroy (dialog);
}

/*------------------------------------------------------------------------------*/
/* feed list DND handling (currently under construction :-)			*/
/*------------------------------------------------------------------------------*/

void on_feedlist_drag_end(GtkWidget *widget, GdkDragContext  *drag_context, gpointer user_data) {
	GtkTreeStore *feedstore;
	
	//feedstore = getEntryStore();

	//g_assert(NULL != drag_source_key);
	//g_assert(NULL != drag_source_keyprefix);
	
	//moveInEntryList(drag_source_keyprefix, drag_source_key);

	//g_free(drag_source_key);
	//g_free(drag_source_keyprefix);
}

void on_feedlist_drag_begin(GtkWidget *widget, GdkDragContext  *drag_context, gpointer user_data) {

	//drag_source_key = getEntryViewSelection(lookup_widget(mainwindow, "feedlist"));
	//drag_source_keyprefix = getEntryViewSelectionPrefix(mainwindow);
	//g_print("key:%s\n", drag_source_key);
	//g_print("keyprefix:%s\n", drag_source_keyprefix);
}

void
on_feedlist_drag_data_received         (GtkWidget       *widget,
                                        GdkDragContext  *drag_context,
                                        gint             x,
                                        gint             y,
                                        GtkSelectionData *data,
                                        guint            info,
                                        guint            time,
                                        gpointer         user_data)
{
	//g_print("DND received %s %d %d\n", data->data, x, y);
}
