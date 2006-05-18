/**
 * @file itemlist.c itemlist handling
 *
 * Copyright (C) 2004-2006 Lars Lindner <lars.lindner@gmx.net>
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
 
#include "itemlist.h"
#include "conf.h"
#include "debug.h"
#include "feed.h"
#include "feedlist.h"
#include "node.h"
#include "support.h"
#include "vfolder.h"
#include "itemset.h"
#include "ui/ui_itemlist.h"
#include "ui/ui_htmlview.h"
#include "ui/ui_mainwindow.h"

/* This is a simple controller implementation for itemlist handling. 
   It manages the currently displayed itemset and provides synchronisation
   for backend and GUI access to this itemset.  
   
   Bypass only for read-only item access! */

itemSetPtr	displayed_itemSet = NULL;
static itemPtr	displayed_item = NULL;		/* displayed item = selected item */

/* internal item list states */
static gboolean twoPaneMode = FALSE;	/* TRUE if two pane mode is active */
static gboolean itemlistLoading;	/* TRUE to prevent selection effects when loading the item list */
gint disableSortingSaving;		/* set in ui_itemlist.c to disable sort-changed callback */

static gboolean deferred_item_remove = FALSE;	/* TRUE if selected item needs to be removed on unselecting */

itemPtr itemlist_get_selected(void) {

	return displayed_item;
}

nodePtr itemlist_get_displayed_node(void) {

	if(NULL != displayed_itemSet)
		return displayed_itemSet->node;
	else
		return NULL;
}

static void itemlist_check_for_deferred_removal(void) {
	itemPtr ip;

	if(NULL != displayed_item) {
		ip = displayed_item;
		displayed_item = NULL;
		if(TRUE == deferred_item_remove) {
			deferred_item_remove = FALSE;
			itemlist_remove_item(ip);
		}
	}
}

/**
 * To be called whenever an itemset was updated. If it is the
 * displayed itemset it will be merged against the item list
 * tree view.
 */
void itemlist_merge_itemset(itemSetPtr itemSet) {
	gboolean	loadReadItems = TRUE;
	gchar		*buffer = NULL;
	GList		*iter;

	debug_enter("itemlist_merge_itemset");
	
	if(displayed_itemSet == NULL)
		return; /* Nothing to do if nothing is displayed */
	
	if(itemSet != displayed_itemSet)
		return;

	debug1(DEBUG_GUI, "reloading item list with node \"%s\"", node_get_title(itemSet->node));

	if(ITEMSET_TYPE_FOLDER == displayed_itemSet->type) {
		if(0 == getNumericConfValue(FOLDER_DISPLAY_MODE))
			return;
	
		loadReadItems = !getBooleanConfValue(FOLDER_DISPLAY_HIDE_READ);
	}

	/* update item list tree view */	
	iter = g_list_last(displayed_itemSet->items);
	while(iter) {
		itemPtr item = iter->data;
		g_assert(NULL != item);

		if((FALSE == item->readStatus) || (TRUE == loadReadItems))
			ui_itemlist_add_item(item, TRUE);

		iter = g_list_previous(iter);
	}

	/* update HTML view according to mode */
	if(TRUE == itemlist_get_two_pane_mode()) {
		/* in 2 pane mode all items are shown at once
		   so after merging it needs to be redisplayed */
		buffer = itemset_render_all(displayed_itemSet);
	} else {
		/* in 3 pane mode we don't update the HTML view
		   except when no item is selected (when loading
		   the items for the first time) then we show
		   the nodes description */
		if(!displayed_item) 
			buffer = node_render(displayed_itemSet->node);
	}

	if(buffer) {
		ui_htmlview_write(ui_mainwindow_get_active_htmlview(), buffer, 
				  itemset_get_base_url(displayed_itemSet));
		g_free(buffer);
	}

	debug_exit("itemlist_merge_itemset");
}

/** 
 * To be called whenever a feed was selected and should
 * replace the current itemlist.
 */
void itemlist_load(itemSetPtr itemSet) {
	GtkTreeModel	*model;

	debug_enter("itemlist_load");

	g_assert(NULL != itemSet);

	debug1(DEBUG_GUI, "loading item list with node \"%s\"\n", node_get_title(itemSet->node));

	/* 1. Don't continue if folder is selected and no
	   folder viewing is configured. */
	if((ITEMSET_TYPE_FOLDER == itemSet->type) && 
	   (0 == getNumericConfValue(FOLDER_DISPLAY_MODE)))
			return;

	itemlistLoading = 1;
	twoPaneMode = node_get_two_pane_mode(itemSet->node);
	ui_mainwindow_set_two_pane_toggle(twoPaneMode);
	ui_mainwindow_set_browser_panes(twoPaneMode);

	/* 2. Clear item list and disable sorting for performance reasons */

	/* Free the old itemstore and create a new one; this is the only way to disable sorting */
	ui_itemlist_reset_tree_store();	 /* this also clears the itemlist. */
	model = GTK_TREE_MODEL(ui_itemlist_get_tree_store());

	switch(itemSet->type) {
		case ITEMSET_TYPE_FEED:
			ui_itemlist_enable_favicon_column(FALSE);
			break;
		case ITEMSET_TYPE_VFOLDER:
			ui_itemlist_enable_favicon_column(TRUE);
			break;
		case ITEMSET_TYPE_FOLDER:
			ui_itemlist_enable_favicon_column(TRUE);
			break;
	}

	/* 3. Set sorting again... */
	disableSortingSaving++;
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(model), 
	                                     itemSet->node->sortColumn, 
	                                     itemSet->node->sortReversed?GTK_SORT_DESCENDING:GTK_SORT_ASCENDING);
	disableSortingSaving--;

	/* 4. Load the new one... */
	displayed_itemSet = itemSet;
	itemlist_merge_itemset(itemSet);

	itemlistLoading = 0;

	debug_exit("itemlist_load");
}

void itemlist_unload(gboolean markRead) {

	if(displayed_itemSet) {
		/* 1. Postprocessing for previously selected node, this is necessary
		   to realize reliable read marking when using condensed mode. It's
		   important to do this only when the selection really changed. */
		if(markRead && (TRUE == node_get_two_pane_mode(displayed_itemSet->node))) 
			itemlist_mark_all_read(displayed_itemSet);

		itemlist_check_for_deferred_removal();
		ui_itemlist_clear();
	}

	displayed_item = NULL;
	displayed_itemSet = NULL;
}

void itemlist_update_vfolder(vfolderPtr vp) {

	if(displayed_itemSet == vp->node->itemSet)
		/* maybe itemlist_load(vp) would be faster, but
		   it unloads all feeds and therefore must not be 
		   called from here! */		
		itemlist_merge_itemset(displayed_itemSet);
	else
		ui_node_update(vp->node);
}

void itemlist_reset_date_format(void) {
	
	ui_itemlist_reset_date_format();
	if(!itemlist_get_two_pane_mode())
		ui_itemlist_update();
}

/* next unread selection logic */

static gboolean itemlist_find_unread_item(void) {
	GList	*iter, *selected = NULL;
	
	if(!displayed_itemSet)
		return FALSE;
		
	if(ITEMSET_TYPE_FOLDER == displayed_itemSet->type) {
		feedlist_find_unread_feed(displayed_itemSet->node);
		return FALSE;
	}

	/* Note: to select in sorting order we need to do it in the GUI code
	   otherwise we would have to sort the item list here... */
	
	if(!displayed_item || !ui_itemlist_find_unread_item(displayed_item))
		return ui_itemlist_find_unread_item(NULL);
	else
		return TRUE;
	
	return FALSE;
}

void itemlist_select_next_unread(void) {
	nodePtr		node;
	
	/* before scanning the feed list, we test if there is a unread 
	   item in the currently selected feed! */
	if(itemlist_find_unread_item())
		return;
	
	/* scan feed list and find first feed with unread items */
	if(node = feedlist_find_unread_feed(feedlist_get_root())) {
		
		/* load found feed */
		ui_feedlist_select(node);

		/* find first unread item */
		itemlist_find_unread_item();
	} else {
		/* if we don't find a feed with unread items do nothing */
		ui_mainwindow_set_status_bar(_("There are no unread items "));
	}
}

/* menu commands */
void itemlist_set_flag(itemPtr item, gboolean newStatus) {
	
	if(newStatus != item->flagStatus) {
		item->itemSet->node->needsCacheSave = TRUE;

		/* 1. propagate to model for recursion */
		itemset_set_item_flag(item->itemSet, item, newStatus);

		/* 2. update item list GUI state */
		ui_itemlist_update_item(item);

		/* 3. no update of feed list necessary... */

		/* 4. update notification statistics */
		feedlist_reset_new_item_count();		
	}
}
	
void itemlist_toggle_flag(itemPtr item) {

	itemlist_set_flag(item, !(item->flagStatus));
}

void itemlist_set_read_status(itemPtr item, gboolean newStatus) {

	if(newStatus != item->readStatus) {		
		item->itemSet->node->needsCacheSave = TRUE;

		/* 1. propagate to model for recursion */
		itemset_set_item_read_status(item->itemSet, item, newStatus);

		/* 2. update item list GUI state */
		ui_itemlist_update_item(item);

		/* 3. updated feed list unread counters */
		node_update_counters(item->itemSet->node);
		ui_node_update(item->itemSet->node);
		if(item->sourceNode)
			ui_node_update(item->sourceNode);

		/* 4. update notification statistics */
		feedlist_reset_new_item_count();
	}
}

void itemlist_toggle_read_status(itemPtr item) {

	itemlist_set_read_status(item, !(item->readStatus));
}

void itemlist_set_update_status(itemPtr item, const gboolean newStatus) { 
	
	if(newStatus != item->updateStatus) {	
		item->itemSet->node->needsCacheSave = TRUE;

		/* 1. propagate to model for recursion */
		itemset_set_item_update_status(item->itemSet, item, newStatus);

		/* 2. update item list GUI state */
		ui_itemlist_update_item(item);	

		/* 3. no update of feed list necessary... */
		node_update_counters(item->itemSet->node);

		/* 4. update notification statistics */
		feedlist_reset_new_item_count();
	}
}

void itemlist_update_item(itemPtr item) {
	
	ui_itemlist_update_item(item);
}

void itemlist_remove_item(itemPtr item) {
	
	if(itemset_lookup_item(item->itemSet, item->itemSet->node, item->nr)) {
		/* if the currently selected item should be removed we
		   don't do it and set a flag to do it when unselecting */
		if(displayed_item != item) {
			ui_itemlist_remove_item(item);
			itemset_remove_item(item->itemSet, item);
			ui_node_update(item->itemSet->node);
		} else {
			deferred_item_remove = TRUE;
			/* update the item to show new state that forces
			   later removal */
			ui_itemlist_update_item(item);
		}
	}
}

void itemlist_remove_items(itemSetPtr itemSet) {

	ui_itemlist_clear();
	ui_htmlview_clear(ui_mainwindow_get_active_htmlview());
	itemset_remove_items(itemSet);
	ui_node_update(itemSet->node);
}

void itemlist_mark_all_read(itemSetPtr itemSet) {

	itemset_mark_all_read(itemSet);
	itemSet->node->needsCacheSave = TRUE;
	ui_itemlist_update();
	ui_node_update(itemSet->node);
}

/* mouse/keyboard interaction callbacks */
void itemlist_selection_changed(itemPtr item) {
	gchar 	*buffer;

	debug_enter("itemlist_selection_changed");
	
	if(!itemlistLoading && (FALSE == itemlist_get_two_pane_mode())) {
		/* vfolder postprocessing to remove unselected items not
		   more matching the rules because they have changed state */
		itemlist_check_for_deferred_removal();
	
		debug1(DEBUG_GUI, "item list selection changed to \"%s\"", item_get_title(item));
		displayed_item = item;

		/* set read and unset update status when selecting */
		if(item) {
			itemlist_set_read_status(item, TRUE);
			itemlist_set_update_status(item, FALSE);

			buffer = itemset_render_item(item->itemSet, item);
			ui_htmlview_write(ui_mainwindow_get_active_htmlview(), buffer, itemset_get_base_url(item->itemSet));
			g_free(buffer);
		}

		ui_node_update(displayed_itemSet->node);

		feedlist_reset_new_item_count();
	}

	debug_exit("itemlist_selection_changed");
}

/* two/three pane mode callbacks */

void itemlist_set_two_pane_mode(gboolean newMode) {

	ui_mainwindow_set_browser_panes(newMode);
	twoPaneMode = newMode;
}

gboolean itemlist_get_two_pane_mode(void) { return twoPaneMode; }

void on_toggle_condensed_view_activate(GtkToggleAction *menuitem, gpointer user_data) { 
	nodePtr		node;

	twoPaneMode = gtk_toggle_action_get_active(menuitem);

	if(node = itemlist_get_displayed_node()) {
		itemlist_unload(FALSE);
		
		node_set_two_pane_mode(node, twoPaneMode);
		ui_mainwindow_set_browser_panes(twoPaneMode);

		/* grab necessary to force HTML widget update (display must
		   change from feed description to list of items and vica 
		   versa */
		gtk_widget_grab_focus(lookup_widget(mainwindow, "feedlist"));
		
		itemlist_load(node->itemSet);
	}
}
