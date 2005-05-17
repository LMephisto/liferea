/**
 * @file ui_itemlist.h item list/view handling
 *
 * Copyright (C) 2004 Lars Lindner <lars.lindner@gmx.net>
 * Copyright (C) 2004 Nathan J. Conrad <t98502@users.sourceforge.net>
 *	      
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 * 
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef _UI_ITEMLIST_H
#define _UI_ITEMLIST_H

#include <gtk/gtk.h>
#include <time.h>
#include "feed.h"

/** Enumeration of the columns in the itemstore. */
enum is_columns {
	IS_TIME,		/**< Time of item creation */ /* This is set to the first item so that default sorting is by time */
	IS_TIME_STR,		/**< Time of item creation as a string*/
	IS_LABEL,		/**< Displayed name */
	IS_FEED,		/**< Parent feed */
	IS_ICON,		/**< Pixbuf reference to the item's icon */
	IS_NR,			/**< Item id, to lookup item ptr from parent feed */
	IS_ICON2,		/**< Pixbuf reference to the item's feed's icon */
	IS_LEN			/**< Number of columns in the itemstore */
};

/**
 * Initializes the itemlist. For example, it creates the various
 * columns and renderers needed to show the list.
 */
void ui_itemlist_init(GtkWidget *itemlist);

/**
 * This returns the GtkTreeStore that is internal to the
 * ui_itemlist. This is currently used for setting and getting the
 * sort column.
 */
GtkTreeStore * ui_itemlist_get_tree_store(void);

/**
 * Method to reset the format string of the date column.
 * Should be called upon initializaton and each time the
 * date format changes.
 */
void ui_itemlist_reset_date_format(void);

/* methods needs to but should not be exposed... */
gchar * ui_itemlist_format_date(time_t t);

/**
 * Unselect all items in the list and scroll to top. This is typically
 * called when changing feed.
 */
void ui_itemlist_prefocus(void);

/**
 * Adds content to the htmlview after a new feed has been selected and
 * sets an item as read.
 */
void ui_itemlist_display(void);

/**
 * Add an item to the itemlist
 * @param merge set to true when the itemlist should be searched for
 * the particular item and the item be updated if necessary.
 */
void ui_itemlist_add_item(itemPtr ip, gboolean merge);

/**
 * Remove an item from the itemlist
 */
void ui_itemlist_remove_item(itemPtr ip);

/**
 * Enable the favicon column of the currently displayed itemlist
 */
void ui_itemlist_enable_favicon_column(gboolean enabled);

/**
 * Remove the items from the itemlist.
 */
void ui_itemlist_clear(void);

/**
 * @name Callbacks used from interface.c
 * @{
 */

/**
 * Callback activated when an item is double-clicked. It opens the URL
 * of the item in a web browser.
 */

void
on_Itemlist_row_activated              (GtkTreeView     *treeview,
                                        GtkTreePath     *path,
                                        GtkTreeViewColumn *column,
                                        gpointer         user_data);


/* menu callbacks */

/**
 * Toggles the unread status of the selected item. This is called from
 * a menu.
 */
void on_toggle_unread_status(GtkMenuItem *menuitem, gpointer user_data);

/**
 * Toggles the flag of the selected item. This is called from a menu.
 */
void on_toggle_item_flag(GtkMenuItem *menuitem, gpointer user_data);

/**
 * Opens the selected item in a browser.
 */
void on_popup_launchitem_selected(void);

/**
 * Opens the selected item in a browser.
 */
void on_popup_launchitem_in_tab_selected(void);

/**
 * Toggles the read status of right-clicked item.
 *
 * @param callback_data An itemPtr that points to the clicked item.
 * @param callback_action Unused.
 * @param widget The GtkTreeView that contains the clicked item.
 */
void on_popup_toggle_read(gpointer callback_data,
					 guint callback_action,
					 GtkWidget *widget);
/**
 * Toggles the flag of right-clicked item.
 *
 * @param callback_data An itemPtr that points to the clicked item.
 * @param callback_action Unused.
 * @param widget The GtkTreeView that contains the clicked item.
 */
void on_popup_toggle_flag(gpointer callback_data,
					 guint callback_action,
					 GtkWidget *widget);

/**
 * Toggles the two pane mode
 *
 * @param meunitem	the clicked menu item
 * @param user_data	unused
 */
void on_toggle_condensed_view_activate(GtkMenuItem *menuitem, 
					gpointer user_data);

/**
 * Returns the two pane mode flag (TRUE if active) 
 */
gboolean ui_itemlist_get_two_pane_mode(void);

/**
 * Sets the two pane mode.
 */
void ui_itemlist_set_two_pane_mode(gboolean new_mode);

/**
 * Removes all items from the selected feed.
 *
 * @param menuitem The menuitem that was selected.
 * @param user_data Unused.
 */
void on_remove_items_activate(GtkMenuItem *menuitem, gpointer user_data);

/**
 * Removes the selected item from the selected feed.
 *
 * @param menuitem The menuitem that was selected.
 * @param user_data Unused.
 */  
void on_remove_item_activate(GtkMenuItem *menuitem, gpointer user_data);

void on_popup_remove_selected(gpointer callback_data, guint callback_action, GtkWidget *widget);

/**
 * Searches the displayed feed and then all feeds for an unread
 * item. If one it found, it is displayed.
 *
 * @param menuitem The menuitem that was selected.
 * @param user_data Unused.
 */
void on_next_unread_item_activate(GtkMenuItem *menuitem, gpointer user_data);

void on_popup_next_unread_item_selected(gpointer callback_data, guint callback_action, GtkWidget *widget);

void on_nextbtn_clicked(GtkButton *button, gpointer user_data);

/**
 * Update a single item of the currently displayed item list .
 *
 * @param ip	item pointer
 */
void ui_itemlist_update_item(itemPtr ip);

/**
 * Update all item list entries of the currently displayed item list.
 */
void ui_itemlist_update(void);

/**
 * Resolves a tree iter into the item it represents.
 */
itemPtr ui_itemlist_get_item_from_iter(GtkTreeIter *iter);

/*@}*/

#endif
