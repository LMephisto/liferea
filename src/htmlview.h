/**
 * @file htmlview.h implementation of the item view interface for HTML rendering
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
 
#ifndef _HTMLVIEW_H
#define _HTMLVIEW_H

#include <gtk/gtk.h>

#include "item.h"
#include "itemset.h"
#include "itemview.h"

/* interface for item and item set HTML rendering */

/**
 * To be called to clear the HTML view 
 */
void	htmlview_clear(void);

/**
 * Prepares the HTML view for displaying items of the given item set.
 *
 * @param itemSet	the item set that will be rendered
 */
void	htmlview_set_itemset(itemSetPtr itemSet);

/**
 * Adds an item to the HTML view for rendering. The item must belong
 * to the item set that was announced with ui_htmlview_load_itemset().
 *
 * This method _DOES NOT_ update the rendering output.
 *
 * @param item		the item to add to the rendering output
 */
void	htmlview_add_item(itemPtr item);

/**
 * Removes a given item from the HTML view rendering.
 *
 * This method _DOES NOT_ update the rendering output.
 *
 * @param item		the item to remove from the rendering output
 */
void	htmlview_remove_item(itemPtr item);

/**
 * Updates the output of a given item from the HTML view rendering.
 *
 * This method _DOES NOT_ update the rendering output.
 *
 * @param item		the item to mark for update
 */
void	htmlview_update_item(itemPtr item);

/**
 * Renders all added items to the given HTML view. To be called
 * after one or more calls of htmlview_(add|remove|update)_item.
 *
 * @param widget	HTML widget to render to
 * @param mode		item view mode
 */
void	htmlview_update(GtkWidget *widget, itemViewMode mode);

/** helper methods for HTML output */

/**
 * Function to add HTML source header to create a valid HTML source.
 *
 * @param buffer	buffer to add the HTML to
 * @param base		base URL of HTML content
 * @param css		TRUE if CSS definitions are to be added
 * @param script	TRUE if item menu scripts are to be added
 */
void	htmlview_start_output(GString *buffer, const gchar *base, gboolean css, gboolean script);

/**
 * Function to add HTML source footer to create a valid HTML source.
 *
 * @param buffer	buffer to add the HTML to
 */
void	htmlview_finish_output(GString *buffer);

#endif