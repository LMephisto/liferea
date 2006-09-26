/**
 * @file export.c OPML feedlist import&export
 *
 * Copyright (C) 2004-2006 Nathan J. Conrad <t98502@users.sourceforge.net>
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

#include <sys/types.h>
#include <sys/stat.h>
#include <libxml/tree.h>
#include <string.h>
#include "folder.h"
#include "callbacks.h"
#include "interface.h"
#include "support.h"
#include "debug.h"

struct exportData {
	gboolean	trusted; /**< Include all the extra Liferea-specific tags */
	xmlNodePtr	cur;
};

/* Used for exporting, this adds a folder or feed's node to the XML tree */
static void export_append_node_tag(nodePtr node, gpointer userdata) {
	xmlNodePtr 		cur = ((struct exportData*)userdata)->cur;
	gboolean		internal = ((struct exportData*)userdata)->trusted;
	xmlNodePtr		childNode;

	// FIXME: use node type capability for this condition
	if(!internal && ((NODE_TYPE_SOURCE == node->type) || (NODE_TYPE_VFOLDER == node->type)))
		return;
	
	debug_enter("append_node_tag");

	childNode = xmlNewChild(cur, NULL, BAD_CAST"outline", NULL);

	/* 1. write generic node attributes */
	xmlNewProp(childNode, BAD_CAST"title", BAD_CAST node_get_title(node));
	xmlNewProp(childNode, BAD_CAST"text", BAD_CAST node_get_title(node)); /* The OPML spec requires "text" */
	xmlNewProp(childNode, BAD_CAST"description", BAD_CAST node_get_title(node));
	
	if(node_type_to_str(node)) 
		xmlNewProp(childNode, BAD_CAST"type", BAD_CAST node_type_to_str(node));

	/* Don't add the following tags if we are exporting to other applications */
	if(internal) {
		xmlNewProp(childNode, BAD_CAST"id", BAD_CAST node_get_id(node));

		switch(node->sortColumn) {
			case IS_LABEL:
				xmlNewProp(childNode, BAD_CAST"sortColumn", BAD_CAST"title");
				break;
			case IS_TIME:
				xmlNewProp(childNode, BAD_CAST"sortColumn", BAD_CAST"time");
				break;
			case IS_PARENT:
				xmlNewProp(childNode, BAD_CAST"sortColumn", BAD_CAST"parent");
				break;
		}

		if(FALSE == node->sortReversed)
			xmlNewProp(childNode, BAD_CAST"sortReversed", BAD_CAST"false");
			
		if(TRUE == node_get_two_pane_mode(node))
			xmlNewProp(childNode, BAD_CAST"twoPane", BAD_CAST"true");
	}

	/* 2. add node type specific stuff */
	node_export(node, childNode, internal);
	
	debug_exit("append_node_tag");
}

void export_node_children(nodePtr node, xmlNodePtr cur, gboolean trusted) {
	struct exportData	params;
	
	params.cur = cur;
	params.trusted = trusted;
	node_foreach_child_data(node, export_append_node_tag, &params);
}

gboolean export_OPML_feedlist(const gchar *filename, nodePtr node, gboolean trusted) {
	xmlDocPtr 	doc;
	xmlNodePtr 	cur, opmlNode;
	gboolean	error = FALSE;
	gchar		*backupFilename;
	int		old_umask = 0;

	debug_enter("export_OPML_feedlist");
	
	backupFilename = g_strdup_printf("%s~", filename);
	
	if(NULL != (doc = xmlNewDoc("1.0"))) {	
		if(NULL != (opmlNode = xmlNewDocNode(doc, NULL, BAD_CAST"opml", NULL))) {
			xmlNewProp(opmlNode, BAD_CAST"version", BAD_CAST"1.0");
			
			/* create head */
			if(NULL != (cur = xmlNewChild(opmlNode, NULL, BAD_CAST"head", NULL))) {
				xmlNewTextChild(cur, NULL, BAD_CAST"title", BAD_CAST"Liferea Feed List Export");
			}
			
			/* create body with feed list */
			if(NULL != (cur = xmlNewChild(opmlNode, NULL, BAD_CAST"body", NULL)))
				export_node_children(node, cur, trusted);
			
			xmlDocSetRootElement(doc, opmlNode);		
		} else {
			g_warning("could not create XML feed node for feed cache document!");
			error = FALSE;
		}
		
		if(trusted)
			old_umask = umask(077);
			
		if(-1 == xmlSaveFormatFileEnc(backupFilename, doc, NULL, 1)) {
			g_warning("Could not export to OPML file!!");
			error = FALSE;
		}
		
		if(trusted)
			umask(old_umask);
			
		xmlFreeDoc(doc);
	} else {
		g_warning("could not create XML document!");
		error = FALSE;
	}
	
	if(rename(backupFilename, filename) < 0)
		g_warning(_("Error renaming %s to %s\n"), backupFilename, filename);

	g_free(backupFilename);
	
	debug_exit("export_OPML_feedlist");
	return error;
}

void import_parse_outline(xmlNodePtr cur, nodePtr parentNode, nodeSourcePtr nodeSource, gboolean trusted) {
	gchar		*title, *typeStr, *tmp, *sortStr;
	nodePtr		node;
	gboolean	needsUpdate = FALSE;
	
	debug_enter("import_parse_outline");

	/* 1. do general node parsing */	
	node = node_new();
	node->source = nodeSource;
	node->parent = parentNode;

	/* The id should only be used from feedlist.opml. Otherwise,
	   it could cause corruption if the same id was imported
	   multiple times. */
	if(trusted) {
		gchar *id = NULL;
		id = xmlGetProp(cur, BAD_CAST"id");
		if(id) {
			node_set_id(node, id);
			xmlFree(id);
		} else
			needsUpdate = TRUE;
	} else
		needsUpdate = TRUE;
	
	/* title */
	title = xmlGetProp(cur, BAD_CAST"title");
	if(!title || !xmlStrcmp(title, BAD_CAST"")) {
		if(title)
			xmlFree(title);
		title = xmlGetProp(cur, BAD_CAST"text");
	}
	node_set_title(node, title);

	if(title)
		xmlFree(title);

	/* sorting order */
	sortStr = xmlGetProp(cur, BAD_CAST"sortColumn");
	if(sortStr) {
		if(!xmlStrcmp(sortStr, "title"))
			node->sortColumn = IS_LABEL;
		else if(!xmlStrcmp(sortStr, "time"))
			node->sortColumn = IS_TIME;
		else if(!xmlStrcmp(sortStr, "parent"))
			node->sortColumn = IS_PARENT;
		xmlFree(sortStr);
	}
	sortStr = xmlGetProp(cur, BAD_CAST"sortReversed");
	if(sortStr && !xmlStrcmp(sortStr, BAD_CAST"false"))
		node->sortReversed = FALSE;
	if(sortStr)
		xmlFree(sortStr);
	
	tmp = xmlGetProp(cur, BAD_CAST"twoPane");
	node_set_two_pane_mode(node, (tmp && !xmlStrcmp(tmp, BAD_CAST"true")));
	if(tmp)
		xmlFree(tmp);

	/* expansion state */		
	if(xmlHasProp(cur, BAD_CAST"expanded"))
		node->expanded = TRUE;
	else if(xmlHasProp(cur, BAD_CAST"collapsed"))
		node->expanded = FALSE;
	else 
		node->expanded = TRUE;

	/* 2. determine node type */
	typeStr = xmlGetProp(cur, BAD_CAST"type");
	if(typeStr) {
		debug1(DEBUG_VERBOSE, "-> node type tag found: \"%s\"\n", typeStr);
		node_set_type(node, node_str_to_type(typeStr));
		xmlFree(typeStr);
	} 
	
	/* if we didn't find a type attribute we use heuristics */
	if(!node->type) {
		/* check for a source URL */
		if(NULL == (tmp = xmlGetProp(cur, BAD_CAST"xmlUrl")));
			tmp = xmlGetProp(cur, BAD_CAST"xmlUrl");
		
		if(tmp) {
			debug0(DEBUG_VERBOSE, "-> URL found assuming type feed");
			node_set_type(node, feed_get_node_type());
			xmlFree(tmp);
		} else {
			/* if the outline has no type and URL it just has to be a folder */
			node_set_type(node, folder_get_node_type());
			debug0(DEBUG_VERBOSE, "-> must be a folder");
		}
	}
	
	/* 3. do node type specific parsing */
	node_import(node, parentNode, cur, trusted);

	/* 4. update immediately if necessary */
	if(needsUpdate && (NODE_TYPE(node) != NULL)) {
		debug1(DEBUG_CACHE, "seems to be an import, setting new id: %s and doing first download...", node_get_id(node));
		node_request_update(node, (xmlHasProp(cur, BAD_CAST"updateInterval") ? 0 : FEED_REQ_RESET_UPDATE_INT)
		        		  | FEED_REQ_DOWNLOAD_FAVICON
		        		  | FEED_REQ_AUTH_DIALOG);
	}

	debug_exit("import_parse_outline");
}

static void import_parse_body(xmlNodePtr n, nodePtr parentNode, nodeSourcePtr nodeSource, gboolean trusted) {
	xmlNodePtr cur;
	
	cur = n->xmlChildrenNode;
	while(cur) {
		if((!xmlStrcmp(cur->name, BAD_CAST"outline")))
			import_parse_outline(cur, parentNode, nodeSource, trusted);
		cur = cur->next;
	}
}

static void import_parse_OPML(xmlNodePtr n, nodePtr parentNode, nodeSourcePtr nodeSource, gboolean trusted) {
	xmlNodePtr cur;
	
	cur = n->xmlChildrenNode;
	while(cur) {
		/* we ignore the head */
		if((!xmlStrcmp(cur->name, BAD_CAST"body"))) {
			import_parse_body(cur, parentNode, nodeSource, trusted);
		}
		cur = cur->next;
	}	
}

gboolean import_OPML_feedlist(const gchar *filename, nodePtr parentNode, nodeSourcePtr nodeSource, gboolean showErrors, gboolean trusted) {
	xmlDocPtr 	doc;
	xmlNodePtr 	cur;
	gboolean	error = FALSE;
	
	debug1(DEBUG_CACHE, "Importing OPML file: %s", filename);
	
	/* read the feed list */
	if(NULL == (doc = xmlParseFile(filename))) {
		if(showErrors)
			ui_show_error_box(_("XML error while reading cache file! Could not import \"%s\"!"), filename);
		else
			g_warning(_("XML error while reading cache file! Could not import \"%s\"!"), filename);
		error = TRUE;
	} else {
		if(NULL == (cur = xmlDocGetRootElement(doc))) {
			if(showErrors)
				ui_show_error_box(_("Empty document! OPML document \"%s\" should not be empty when importing."), filename);
			else
				g_warning(_("Empty document! OPML document \"%s\" should not be empty when importing."), filename);
			error = TRUE;
		} else {
			while(cur) {
				if(!xmlIsBlankNode(cur)) {
					if(!xmlStrcmp(cur->name, BAD_CAST"opml")) {
						import_parse_OPML(cur, parentNode, nodeSource, trusted);
					} else {
						if(showErrors)
							ui_show_error_box(_("\"%s\" is not a valid OPML document! Liferea cannot import this file!"), filename);
						else
							g_warning(_("\"%s\" is not a valid OPML document! Liferea cannot import this file!"), filename);
					}
				}
				cur = cur->next;
			}
		}
		xmlFreeDoc(doc);
	}
	
	return !error;
}


/* UI stuff */

void on_import_activate_cb(const gchar *filename, gpointer user_data) {
	
	if(filename) {
		nodePtr node = node_new();
		node_set_title(node, _("Imported feed list"));
		node_set_type(node, folder_get_node_type());
		
		/* add the new folder to the model */
		node_add_child(NULL, node, 0);
		
		import_OPML_feedlist(filename, node, node->source, TRUE /* show errors */, FALSE /* not trusted */);
	}
}

void on_import_activate(GtkMenuItem *menuitem, gpointer user_data) {

	ui_choose_file(_("Import Feed List"), GTK_WINDOW(mainwindow), _("Import"), FALSE, on_import_activate_cb, NULL, NULL, NULL);
}

static void on_export_activate_cb(const gchar *filename, gpointer user_data) {

	if(filename) {
		if(FALSE == export_OPML_feedlist(filename, feedlist_get_root(), FALSE))
			ui_show_error_box(_("Error while exporting feed list!"));
		else 
			ui_show_info_box(_("Feed List exported!"));
	}
}

void on_export_activate(GtkMenuItem *menuitem, gpointer user_data) {
	
	ui_choose_file(_("Export Feed List"), GTK_WINDOW(mainwindow), _("Export"), TRUE, on_export_activate_cb,  NULL, "feedlist.opml", NULL);
}
