/*
   popup menus

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

#ifndef _UI_POPUP_H
#define _UI_POPUP_H

#include <gtk/gtk.h>
#include <gdk/gdk.h>

/* prepares the popup menues */
void setupPopupMenues(void);

/* function to generate popup menus for the item list depending
   on the list mode given in itemlist_mode */
GtkMenu *make_item_menu(void);

/* popup menu generating functions for the HTML view */
GtkMenu *make_html_menu(void);
GtkMenu *make_url_menu(void);

#endif
