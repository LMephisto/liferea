/**
 * @file ui_feed.h	UI actions concerning a single feed
 *
 * Copyright (C) 2004-2005 Lars Lindner <lars.lindner@gmx.net>
 * Copyright (C) 2004-2005 Nathan J. Conrad <t98502@users.sourceforge.net>
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

#ifndef _UI_FEED_H
#define _UI_FEED_H

#include "feed.h"

/**
 * Updates the feed list entry of the given feed.
 *
 * @param fp		the feed to process
 */
void ui_feed_update(feedPtr fp); 

/* feed handling dialog implementations */
GtkWidget* ui_feed_authdialog_new (GtkWindow *parent, feedPtr fp, gint flags);
GtkWidget* ui_feed_propdialog_new (GtkWindow *parent, feedPtr fp);
GtkWidget* ui_feed_newdialog_new (GtkWindow *parent);

#endif /* _UI_FEED_H */