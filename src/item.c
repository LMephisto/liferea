/*
   common item handling
   
   Copyright (C) 2003, 2004 Lars Lindner <lars.lindner@gmx.net>
   Copyright (C) 2004 Nathan J. Conrad <t98502@users.sourceforge.net>
		      
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

#include <glib.h>
#include <time.h>
#include <string.h> /* For memset() */

#include "cdf_item.h"
#include "rss_item.h"
#include "pie_entry.h"
#include "ocs_dir.h"
#include "vfolder.h"
#include "item.h"
#include "support.h"
#include "common.h"
#include "htmlview.h"
#include "callbacks.h"
#include "ui_tray.h"
#include "ui_notification.h"
#include "metadata.h"

/* function to create a new feed structure */
itemPtr item_new(void) {
	itemPtr		ip;
	
	ip = g_new0(struct item, 1);
	ip->readStatus = FALSE;
	ip->marked = FALSE;

	return ip;
}

void item_set_title(itemPtr ip, const gchar * title) {
	g_free(ip->title);
	ip->title = g_strdup(title);
}

void item_set_description(itemPtr ip, const gchar * description) {
	g_free(ip->description);
	ip->description = g_strdup(description);
}

void item_set_source(itemPtr ip, const gchar * source) {
	g_free(ip->source);
	ip->source = g_strdup(source);
}

void item_set_time(itemPtr ip, const time_t t) { ip->time = t; }

void item_set_read_status(itemPtr ip, const gboolean readStatus) { ip->readStatus = readStatus; }

void item_set_id(itemPtr ip, const gchar * id) {
	g_free(ip->id);
	ip->id = g_strdup(id);
}

void item_set_hidden(itemPtr ip, const gboolean hidden) { ip->hidden = hidden; }

const gchar *	item_get_id(itemPtr ip) { return (ip != NULL ? ip->id : NULL); }
const gchar *	item_get_title(itemPtr ip) {return (ip != NULL ? ip->title : NULL); }
const gchar *	item_get_description(itemPtr ip) { return (ip != NULL ? ip->description : NULL); }
const gchar *	item_get_source(itemPtr ip) { return (ip != NULL ? ip->source : NULL); }
const time_t	item_get_time(itemPtr ip) { return (ip != NULL ? ip->time : 0); }
const gboolean item_get_read_status(itemPtr ip) { return (ip != NULL ? ip->readStatus : FALSE); }
const gboolean item_get_mark(itemPtr ip) { g_assert(ip != NULL); return ip->marked; }
const gboolean item_get_hidden(itemPtr ip) { g_assert(ip != NULL); return ip->hidden; }
const gboolean item_get_new_status(itemPtr ip) { g_assert(ip != NULL); return ip->newStatus; }

void item_set_mark(itemPtr ip, gboolean flag) {
	ip->marked = flag;

	if (ip->ui_data != NULL)
	ui_update_item(ip);

	if (ip->fp != NULL)
		ip->fp->needsCacheSave = TRUE;
}

void item_set_new_status(itemPtr ip, const gboolean newStatus) { 

	if(newStatus != ip->newStatus) {
		ip->newStatus = newStatus; 
		
		if(TRUE == newStatus)
			feed_increase_new_counter((feedPtr)(ip->fp));
		else
			feed_decrease_new_counter((feedPtr)(ip->fp));
	}
}

void item_set_unread(itemPtr ip) { 
	GSList		*vfolders;
	
	if(TRUE == ip->readStatus) {
		feed_increase_unread_counter((feedPtr)(ip->fp));

		vfolders = ip->vfolders;
		while(NULL != vfolders) {
			feed_increase_unread_counter(vfolders->data);
			vfolders = g_slist_next(vfolders);
		}
		
		ip->readStatus = FALSE;
		if (ip->ui_data != NULL)
			ui_update_item(ip);
		if (ip->fp != NULL)
			ip->fp->needsCacheSave = TRUE;
		ui_notification_update(ip->fp);
	} 
}

void item_set_read(itemPtr ip) { 
	GSList		*vfolders;
	feedPtr		fp;

	if(FALSE == ip->readStatus) {
		feed_decrease_unread_counter((feedPtr)(ip->fp));

		vfolders = ip->vfolders;
		while(NULL != vfolders) {
			fp = vfolders->data;
			feed_decrease_unread_counter(fp);
			vfolders = g_slist_next(vfolders);
		}
		
		ip->readStatus = TRUE; 
		if (ip->ui_data)
			ui_update_item(ip);
		if (ip->fp != NULL)
			ip->fp->needsCacheSave = TRUE;
	}
	ui_tray_zero_new();
}

/* called when a item matches a vfolder... */
void addVFolderToItem(itemPtr ip, gpointer fp) {

	ip->vfolders = g_slist_append(ip->vfolders, fp);
}

/* called when a vfolder is removed, to remove it from the
   items vfolder list */
void removeVFolderFromItem(itemPtr ip, gpointer fp) {

	ip->vfolders = g_slist_remove(ip->vfolders, fp);
}

void item_free(itemPtr ip) {
	
	if(FALSE == ip->readStatus) {
		feed_decrease_unread_counter(ip->fp);
		ui_notification_update(ip->fp);
	}

	g_free(ip->title);
	g_free(ip->description);
	g_free(ip->source);
	g_free(ip->id);
	metadata_list_free(ip->metadata);
	/* FIXME: remove item from all assigned VFolders! */
	if (ip->ui_data != NULL)
		ui_free_item_ui_data(ip);
	g_free(ip);
}

gchar *item_render(itemPtr ip) {
	struct displayset displayset;
	gchar *buffer = NULL;
	gchar *tmp, *tmp2;
	
	displayset.headtable = NULL;
	displayset.head = NULL;
	displayset.body = g_strdup(ip->description);
	displayset.foot = NULL;
	displayset.foottable = NULL;
	
	metadata_list_render(ip->metadata, &displayset);
	
	/* Head table */
	addToHTMLBufferFast(&buffer, HEAD_START);
	/*  -- Feed line */
	if (feed_get_html_uri(ip->fp) != NULL)
		tmp = g_strdup_printf("<a href=\"%s\">%s</a>",
						  feed_get_html_uri(ip->fp),
						  feed_get_title(ip->fp));
	else
		tmp = g_strdup(feed_get_title(ip->fp));
	
	tmp2 = g_strdup_printf(HEAD_LINE, _("Feed:"), tmp);
	g_free(tmp);
	addToHTMLBufferFast(&buffer, tmp2);
	g_free(tmp2);

	/*  -- Item line */
	if (item_get_source(ip) != NULL)
		tmp = g_strdup_printf("<a href=\"%s\">%s</a>",
						  item_get_source(ip),
						  (item_get_title(ip) != NULL)? item_get_title(ip) : _("[No title]"));
	else
		tmp = g_strdup((item_get_title(ip) != NULL) ? item_get_title(ip) : _("[No title]"));
	tmp2 = g_strdup_printf(HEAD_LINE, _("Item:"), tmp);
	g_free(tmp);
	addToHTMLBufferFast(&buffer, tmp2);
	g_free(tmp2);
	
	addToHTMLBufferFast(&buffer, displayset.headtable);
	g_free(displayset.headtable);
	addToHTMLBufferFast(&buffer, HEAD_END);
	
	if (displayset.head != NULL) {
		addToHTMLBufferFast(&buffer, displayset.head);
		g_free(displayset.head);
	}

	if (displayset.body != NULL) {
		addToHTMLBufferFast(&buffer, displayset.body);
		g_free(displayset.body);
	}

	if (displayset.foot != NULL) {
		addToHTMLBufferFast(&buffer, displayset.foot);
		g_free(displayset.foot);
	}

	if (displayset.foottable != NULL) {
		addToHTMLBufferFast(&buffer, FEED_FOOT_TABLE_START);
		addToHTMLBufferFast(&buffer, displayset.foottable);
		addToHTMLBufferFast(&buffer, FEED_FOOT_TABLE_START);
		g_free(displayset.foottable);
	}

	return buffer;
}

void item_display(itemPtr ip) {
	gchar	*buffer = NULL, *tmp;
	
	ui_htmlview_start_output(&buffer, TRUE);
	tmp = item_render(ip);
	addToHTMLBufferFast(&buffer, tmp);
	g_free(tmp);
	ui_htmlview_finish_output(&buffer);
	if (feed_get_source(ip->fp) != NULL &&
	    ip->fp->source[0] != '|' &&
	    strstr(feed_get_source(ip->fp), "://") != NULL)
		ui_htmlview_write(ui_mainwindow_get_active_htmlview(), buffer, feed_get_source(ip->fp));
	else
		ui_htmlview_write(ui_mainwindow_get_active_htmlview(), buffer, NULL);
	g_free(buffer);
}

itemPtr item_parse_cache(xmlDocPtr doc, xmlNodePtr cur) {
	itemPtr 	ip;
	gchar		*tmp;
	
	g_assert(NULL != doc);
	g_assert(NULL != cur);
	
	ip = item_new();
	
	cur = cur->xmlChildrenNode;
	while(cur != NULL) {
		
		if(cur->type != XML_ELEMENT_NODE ||
		   NULL == (tmp = utf8_fix(xmlNodeListGetString(doc, cur->xmlChildrenNode, 1)))) {
			cur = cur->next;
			continue;
		}
		
		if(!xmlStrcmp(cur->name, BAD_CAST"title"))
			item_set_title(ip, tmp);
			
		else if(!xmlStrcmp(cur->name, BAD_CAST"description"))
			item_set_description(ip, tmp);
			
		else if(!xmlStrcmp(cur->name, BAD_CAST"source"))
			item_set_source(ip, tmp);
			
		else if(!xmlStrcmp(cur->name, BAD_CAST"id"))
			item_set_id(ip, tmp);
			
		else if(!xmlStrcmp(cur->name, BAD_CAST"readStatus"))
			item_set_read_status(ip, (0 == atoi(tmp))?FALSE:TRUE);		

		else if(!xmlStrcmp(cur->name, BAD_CAST"mark")) 
			/* we don't call item_set_mark here because it would
			 * update the UI */
			ip->marked = (1 == atoi(tmp))?TRUE:FALSE;
			
		else if(!xmlStrcmp(cur->name, BAD_CAST"time"))
			item_set_time(ip, atol(tmp));
		else if (!xmlStrcmp(cur->name, BAD_CAST"attributes"))
			ip->metadata = metadata_parse_xml_nodes(doc, cur);
		
		g_free(tmp);	
		cur = cur->next;
	}
		
	return ip;
}
