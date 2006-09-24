/**
 * @file newsbin.c  news bin node type implementation
 * 
 * Copyright (C) 2006 Lars Lindner <lars.lindner@gmx.net>
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

#include <gtk/gtk.h>
#include "feed.h"
#include "feedlist.h"
#include "interface.h"
#include "itemlist.h"
#include "newsbin.h"
#include "render.h"
#include "support.h"
#include "ui/ui_feedlist.h"
#include "ui/ui_node.h"
#include "ui/ui_popup.h"

static GtkWidget *newnewsbindialog = NULL;
static GtkWidget *newsbinnamedialog = NULL;
static GSList * newsbin_list = NULL;

GSList * newsbin_get_list(void) { return newsbin_list; }

static void newsbin_new(nodePtr node) {

	itemSetPtr itemSet = (itemSetPtr)g_new0(struct itemSet, 1); /* create empty itemset */
	itemSet->type = ITEMSET_TYPE_FEED;
	node_set_itemset(node, itemSet);
	node->needsCacheSave = TRUE;
	feedlist_schedule_save();
}

static void newsbin_import(nodePtr node, nodePtr parent, xmlNodePtr cur, gboolean trusted) {

	feed_get_node_type()->import(node, parent, cur, trusted);
	((feedPtr)node->data)->cacheLimit = CACHE_UNLIMITED;
}

static void newsbin_initial_load(nodePtr node) {

	newsbin_list = g_slist_append(newsbin_list, node);
	feed_get_node_type()->initial_load(node);
}

static void newsbin_remove(nodePtr node) {

	newsbin_list = g_slist_remove(newsbin_list, node);
	feed_get_node_type()->remove(node);
	ui_popup_update_menues();
}

static gchar * newsbin_render(nodePtr node) {
	gchar		**params = NULL, *output = NULL;
	xmlDocPtr	doc;

	doc = feed_to_xml(node, NULL, TRUE);
	params = render_add_parameter(params, "pixmapsDir='file://" PACKAGE_DATA_DIR G_DIR_SEPARATOR_S PACKAGE G_DIR_SEPARATOR_S "pixmaps" G_DIR_SEPARATOR_S "'");
	output = render_xml(doc, "newsbin", params);
	xmlFree(doc);

	return output;
}

static void ui_newsbin_add(nodePtr parent) {
	GtkWidget	*nameentry;
	
	if(!newnewsbindialog || !G_IS_OBJECT(newnewsbindialog))
		newnewsbindialog = create_newnewsbindialog();

	nameentry = lookup_widget(newnewsbindialog, "nameentry");
	gtk_entry_set_text(GTK_ENTRY(nameentry), "");
		
	gtk_widget_show(newnewsbindialog);
}

void on_newnewsbinbtn_clicked(GtkButton *button, gpointer user_data) {
	nodePtr		newsbin;
	int		pos;
	
	newsbin = node_new();
	node_set_title(newsbin, (gchar *)gtk_entry_get_text(GTK_ENTRY(lookup_widget(newnewsbindialog, "nameentry"))));
	node_set_type(newsbin, newsbin_get_node_type());
	node_set_data(newsbin, (gpointer)feed_new("newsbin", NULL, 0));
	newsbin_new(newsbin);

	ui_feedlist_get_target_folder(&pos);
	node_add_child(feedlist_get_insertion_point(), newsbin, pos);
	ui_feedlist_select(newsbin);
	
	newsbin_list = g_slist_append(newsbin_list, newsbin);
	ui_popup_update_menues();
}

static void ui_newsbin_properties(nodePtr node) {
	GtkWidget	*nameentry;
	
	if(!newsbinnamedialog || !G_IS_OBJECT(newsbinnamedialog))
		newsbinnamedialog = create_newsbinnamedialog();

	nameentry = lookup_widget(newsbinnamedialog, "nameentry");
	gtk_entry_set_text(GTK_ENTRY(nameentry), node_get_title(node));
	gtk_object_set_data(GTK_OBJECT(newsbinnamedialog), "node", node);
		
	gtk_widget_show(newsbinnamedialog);
}

void on_newsbinnamechange_clicked(GtkButton *button, gpointer user_data) {
	nodePtr	node;

	node = (nodePtr)gtk_object_get_data(GTK_OBJECT(newsbinnamedialog), "node");
	node->needsCacheSave = TRUE;
	node_set_title(node, (gchar *)gtk_entry_get_text(GTK_ENTRY(lookup_widget(newsbinnamedialog, "nameentry"))));

	ui_node_update(node);
	feedlist_schedule_save();
	ui_popup_update_menues();
}

void on_popup_copy_to_newsbin(gpointer user_data, guint callback_action, GtkWidget *widget) {
	nodePtr		newsbin;
	itemPtr		item, copy;

	newsbin = g_slist_nth_data(newsbin_list, callback_action);
	item = itemlist_get_selected();
	if(item) {
		node_load(newsbin);
		copy = item_copy(item);
		if(!copy->real_source_url)
			copy->real_source_url = g_strdup(itemset_get_base_url(item->itemSet));
		if(!copy->real_source_title)
			copy->real_source_title = g_strdup(node_get_title(item->itemSet->node));
		itemset_prepend_item(newsbin->itemSet, copy);
		newsbin->needsCacheSave = TRUE;
		ui_node_update(newsbin);
		node_unload(newsbin);
	}
}

void newsbin_request_auto_update_dummy(nodePtr node) { }
void newsbin_request_update_dummy(nodePtr node, guint flags) { }

nodeTypePtr newsbin_get_node_type(void) {
	static nodeTypePtr	nodeType;

	if(!nodeType) {
		/* derive the plugin node type from the folder node type */
		nodeType = (nodeTypePtr)g_new0(struct nodeType, 1);
		nodeType->capabilities		= NODE_CAPABILITY_RECEIVE_ITEMS |
		                                  NODE_CAPABILITY_SHOW_UNREAD_COUNT;
		nodeType->id			= "newsbin";
		nodeType->icon			= icons[ICON_NEWSBIN];
		nodeType->type			= NODE_TYPE_NEWSBIN;
		nodeType->import		= newsbin_import;
		nodeType->export		= feed_get_node_type()->export;
		nodeType->initial_load		= newsbin_initial_load;
		nodeType->load			= feed_get_node_type()->load;
		nodeType->save			= feed_get_node_type()->save;
		nodeType->unload		= feed_get_node_type()->unload;
		nodeType->reset_update_counter	= feed_get_node_type()->reset_update_counter;
		nodeType->request_update	= newsbin_request_update_dummy;
		nodeType->request_auto_update	= newsbin_request_auto_update_dummy;
		nodeType->remove		= newsbin_remove;
		nodeType->mark_all_read		= feed_get_node_type()->mark_all_read;
		nodeType->render		= newsbin_render;
		nodeType->request_add		= ui_newsbin_add;
		nodeType->request_properties	= ui_newsbin_properties;
	}

	return nodeType; 
}