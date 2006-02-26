/**
 * @file ui_search.c everything about searching
 *
 * Copyright (C) 2003-2006 Lars Lindner <lars.lindner@gmx.net>
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
#include "callbacks.h"
#include "interface.h"
#include "node.h"
#include "vfolder.h"
#include "rule.h"
#include "support.h"
#include "common.h"
#include "ui/ui_search.h"
#include "ui/ui_mainwindow.h"
#include "ui/ui_vfolder.h"
#include "fl_providers/fl_default.h"

extern GtkWidget	*mainwindow;
static GtkWidget	*searchdialog = NULL;
static GtkWidget 	*feedsterdialog = NULL;
static nodePtr		searchResult = NULL;

/*------------------------------------------------------------------------------*/
/* search dialog callbacks							*/
/*------------------------------------------------------------------------------*/

static void ui_search_destroyed_cb(GtkWidget *widget, void *data) {

	searchdialog = NULL;
}

void on_searchbtn_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget	*searchentry;
	gboolean	visible;

	if(NULL == searchdialog) {
		searchdialog = create_searchdialog();
		gtk_window_set_transient_for(GTK_WINDOW(searchdialog), GTK_WINDOW(mainwindow));
		g_signal_connect(G_OBJECT(searchdialog), "destroy", G_CALLBACK(ui_search_destroyed_cb), NULL);
	}
	
	searchentry = lookup_widget(searchdialog, "searchentry");
	gtk_window_set_focus(GTK_WINDOW(searchdialog), searchentry);
	g_object_get(searchdialog, "visible", &visible, NULL);
	g_object_set(searchdialog, "visible", !visible, NULL);
}

void on_hidesearch_clicked(GtkButton *button, gpointer user_data) {

	gtk_widget_hide(searchdialog);
}

void on_searchentry_activate(GtkEntry *entry, gpointer user_data) {
	/* do not use passed entry because callback is used from a button too */
	GtkWidget		*searchentry;
	G_CONST_RETURN gchar	*searchstring;
	gchar			*buffer = NULL, *tmp;
	vfolderPtr		vp;
	
	searchentry = lookup_widget(searchdialog, "searchentry");
	searchstring = gtk_entry_get_text(GTK_ENTRY(searchentry));
	ui_mainwindow_set_status_bar(_("Searching for \"%s\""), searchstring);

	/* remove last search */
	ui_itemlist_clear();
	if(NULL != searchResult) 
		node_remove(searchResult);

	/* create new search */
	vp = vfolder_new();
	vfolder_set_title(vp, searchstring);
	vfolder_add_rule(vp, "exact", searchstring, TRUE);

	searchResult = node_new();
	node_set_title(searchResult, searchstring);
	node_add_data(searchResult, FST_VFOLDER, (gpointer)vp);

	/* calculate vfolder item set */
	vfolder_refresh(vp);

	/* switch to item list view and inform user in HTML view */
	ui_feedlist_select(NULL);
	itemlist_set_two_pane_mode(FALSE);
	itemlist_load(searchResult->itemSet);

	ui_htmlview_start_output(&buffer, NULL, TRUE);
	tmp = g_strdup_printf(_("%s<h2>%d Search Results for \"%s\"</h2>"
	                         "<p>The item list now contains all items matching the "
	                         "specified search pattern. If you want to save this search "
	                         "result permanently you can click the VFolder button in "
	                         "the search dialog and Liferea will add a VFolder to your "
	                         "feed list.</h2>"), buffer, g_list_length(searchResult->itemSet->items), searchstring);
	addToHTMLBufferFast(&buffer, tmp);
	g_free(tmp);
	ui_htmlview_finish_output(&buffer);
	ui_htmlview_write(ui_mainwindow_get_active_htmlview(), buffer, NULL);
	g_free(buffer);

	/* enable vfolder add button */	
	gtk_widget_set_sensitive(lookup_widget(searchdialog, "vfolderaddbtn"), TRUE);
}

void on_searchentry_changed(GtkEditable *editable, gpointer user_data) {
	gchar *searchtext;
	
	/* just to disable the start search button when search string is empty... */
	searchtext = gtk_editable_get_chars(editable,0,-1);
	gtk_widget_set_sensitive(lookup_widget(searchdialog, "searchstartbtn"), (NULL != searchtext) && (0 < strlen(searchtext)));
		
}

void on_newVFolder_clicked(GtkButton *button, gpointer user_data) {
	gint			pos;
	nodePtr			node, folder;
	
	if(NULL != searchResult) {
		node = searchResult;
		searchResult = NULL;
		folder = ui_feedlist_get_target_folder(&pos);
		feedlist_add_node(folder, node, pos);
		ui_feedlist_select(node);
	} else {
		ui_show_info_box(_("Please do a search first!"));
	}
}

void on_new_vfolder_activate(GtkMenuItem *menuitem, gpointer user_data) {
	gint			pos;
	vfolderPtr		vp;
	nodePtr			np, folder = NULL;
	
	vp = vfolder_new();
	vfolder_set_title(vp, _("New VFolder"));

	np = node_new();
	node_set_title(np, vfolder_get_title(vp));
	node_add_data(np, FST_VFOLDER, (gpointer)vp);

	folder = ui_feedlist_get_target_folder(&pos);
	feedlist_add_node(folder, np, pos);
	ui_feedlist_select(np);
	ui_vfolder_propdialog_new(GTK_WINDOW(mainwindow), np);
}


/*------------------------------------------------------------------------------*/
/* feedster support								*/
/*------------------------------------------------------------------------------*/

void on_feedsterbtn_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget	*keywords, *resultCountButton;
	GtkAdjustment	*resultCount;
	gchar		*searchtext;

	keywords = lookup_widget(feedsterdialog, "feedsterkeywords");
	resultCountButton = lookup_widget(feedsterdialog, "feedsterresultcount");
	if((NULL != keywords) && (NULL != resultCountButton)) {
		resultCount = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(resultCountButton));
		searchtext = (gchar *)g_strdup(gtk_entry_get_text(GTK_ENTRY(keywords)));
		searchtext = encode_uri_string(searchtext);
		searchtext = g_strdup_printf("http://www.feedster.com/search.php?q=%s&sort=date&type=rss&ie=UTF-8&limit=%d", 
					    searchtext, (int)gtk_adjustment_get_value(resultCount));

		node_request_automatic_add(NULL, 
		                           searchtext, 
					   NULL, 
					   NULL, 
		                           /*FEED_REQ_SHOW_PROPDIALOG | <- not needed*/
		                           FEED_REQ_RESET_TITLE |
		                           FEED_REQ_RESET_UPDATE_INT | 
		                           FEED_REQ_AUTO_DISCOVER | 
					   FEED_REQ_PRIORITY_HIGH |
					   FEED_REQ_DOWNLOAD_FAVICON |
					   FEED_REQ_AUTH_DIALOG);


		g_free(searchtext);
	}
}

static void ui_feedster_destroyed_cb(GtkWidget *widget, void *data) {

	feedsterdialog = NULL;
}

void on_search_with_feedster_activate(GtkMenuItem *menuitem, gpointer user_data) {
	GtkWidget	*keywords;
	
	if(NULL == feedsterdialog) {
		feedsterdialog = create_feedsterdialog();
		gtk_window_set_transient_for(GTK_WINDOW(feedsterdialog), GTK_WINDOW(mainwindow));
		g_signal_connect(G_OBJECT(feedsterdialog), "destroy", G_CALLBACK(ui_feedster_destroyed_cb), NULL);
	}
		
	keywords = lookup_widget(feedsterdialog, "feedsterkeywords");
	gtk_window_set_focus(GTK_WINDOW(feedsterdialog), keywords);
	gtk_entry_set_text(GTK_ENTRY(keywords), "");
	gtk_widget_show(feedsterdialog);
}
