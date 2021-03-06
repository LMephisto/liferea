/**
 * @file migrate.c migration between different cache versions
 * 
 * Copyright (C) 2007-2009  Lars Lindner <lars.lindner@gmail.com>
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

#include "migrate.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <libxml/uri.h>

#include "common.h"
#include "db.h"
#include "debug.h"
#include "feed.h"
#include "item.h"
#include "itemset.h"
#include "metadata.h"
#include "node.h"
#include "xml.h"
#include "ui/ui_common.h"

#define LIFEREA_CURRENT_DIR ".liferea_1.7"

static void 
migrate_copy_dir (const gchar *from,
                  const gchar *to,
                  const gchar *subdir) 
{
	gchar *fromDirname, *toDirname;
	gchar *srcfile, *destfile;
   	GDir *dir;
		
	fromDirname = g_build_filename (g_get_home_dir (), from, subdir, NULL);
	toDirname = g_build_filename (g_get_home_dir (), to, subdir, NULL);
	
	dir = g_dir_open (fromDirname, 0, NULL);
	while (NULL != (srcfile = (gchar *)g_dir_read_name (dir))) {
		gchar	*content;
		gsize	length;
		destfile = g_build_filename (toDirname, srcfile, NULL);
		srcfile = g_build_filename (fromDirname, srcfile, NULL);
		if (g_file_test (srcfile, G_FILE_TEST_IS_REGULAR)) {
			g_print ("copying %s\n     to %s\n", srcfile, destfile);
			if (g_file_get_contents (srcfile, &content, &length, NULL))
				g_file_set_contents (destfile, content, length, NULL);
			g_free (content);
		} else {
			g_print("skipping %s\n", srcfile);
		}
		g_free (destfile);
		g_free (srcfile);
	}
	g_dir_close(dir);
	
	g_free (fromDirname);
	g_free (toDirname);
}

static itemPtr
migrate_item_parse_cache (xmlNodePtr cur,
                          gboolean migrateCache) 
{
	itemPtr 	item;
	gchar		*tmp;
	
	g_assert(NULL != cur);
	
	item = item_new();
	item->popupStatus = FALSE;
	
	cur = cur->xmlChildrenNode;
	while(cur) {
		if(cur->type != XML_ELEMENT_NODE ||
		   !(tmp = xmlNodeListGetString(cur->doc, cur->xmlChildrenNode, 1))) {
			cur = cur->next;
			continue;
		}
		
		if(!xmlStrcmp(cur->name, BAD_CAST"title"))
			item_set_title(item, tmp);
			
		else if(!xmlStrcmp(cur->name, BAD_CAST"description"))
			item_set_description(item, tmp);
			
		else if(!xmlStrcmp(cur->name, BAD_CAST"source"))
			item_set_source(item, tmp);
			
		else if(!xmlStrcmp(cur->name, BAD_CAST"real_source_url"))
			metadata_list_set(&(item->metadata), "realSourceUrl", tmp);
			
		else if(!xmlStrcmp(cur->name, BAD_CAST"real_source_title"))
			metadata_list_set(&(item->metadata), "realSourceTitle", tmp);
			
		else if(!xmlStrcmp(cur->name, BAD_CAST"id"))
			item_set_id(item, tmp);
			
		else if(!xmlStrcmp(cur->name, BAD_CAST"validGuid"))
			item->validGuid = TRUE;
			
		else if(!xmlStrcmp(cur->name, BAD_CAST"readStatus"))
			item->readStatus = (0 == atoi(tmp))?FALSE:TRUE;
			
		else if(!xmlStrcmp(cur->name, BAD_CAST"updateStatus"))
			item->updateStatus = (0 == atoi(tmp))?FALSE:TRUE;

		else if(!xmlStrcmp(cur->name, BAD_CAST"mark")) 
			item->flagStatus = (1 == atoi(tmp))?TRUE:FALSE;
			
		else if(!xmlStrcmp(cur->name, BAD_CAST"time"))
			item->time = atol(tmp);
			
		else if(!xmlStrcmp(cur->name, BAD_CAST"attributes"))
			item->metadata = metadata_parse_xml_nodes(cur);
		
		g_free(tmp);	
		tmp = NULL;
		cur = cur->next;
	}
	
	item->hasEnclosure = (NULL != metadata_list_get(item->metadata, "enclosure"));
	
	if (migrateCache && item->description) {
		gchar *desc = xhtml_from_text (item->description);
		item_set_description (item, desc);
		g_free(desc);
	}
	return item;
}

#define FEED_CACHE_VERSION       "1.1"

static void
migrate_load_from_cache (const gchar *sourceDir, const gchar *id) 
{
	nodePtr			node;
	feedParserCtxtPtr	ctxt;
	gboolean		migrateFrom10 = TRUE;
	gchar			*filename;
	guint 			itemCount = 0;
	
	debug_enter ("migrate_load_from_cache");

	node = node_from_id (id);
	if (!node) {
		debug1 (DEBUG_CACHE, "ignoring cache file %s because it is not referenced in feed list...", id);
		return;		/* propably a stale cache file */
	}

	ctxt = feed_create_parser_ctxt ();
	ctxt->subscription = node->subscription;
	ctxt->feed = (feedPtr)node->data;
		
	filename = g_build_filename (sourceDir, node->id, NULL);
	debug2 (DEBUG_CACHE, "loading cache file \"%s\" (feed \"%s\")", filename, subscription_get_source(ctxt->subscription));
	
	if ((!g_file_get_contents (filename, &ctxt->data, &ctxt->dataLength, NULL)) || (*ctxt->data == 0)) {
		debug1 (DEBUG_CACHE, "could not load cache file %s", filename);
		g_free (filename);
		return;
	}

	g_print ("migrating feed %s ", id);

	do {
		xmlNodePtr cur;
		
		g_assert (NULL != ctxt->data);

		if (NULL == xml_parse_feed (ctxt))
			break;

		if (NULL == (cur = xmlDocGetRootElement (ctxt->doc)))
			break;

		while (cur && xmlIsBlankNode (cur))
			cur = cur->next;

		if (cur && !xmlStrcmp (cur->name, BAD_CAST"feed")) {
			xmlChar *version;			
			if ((version = xmlGetProp (cur, BAD_CAST"version"))) {
				migrateFrom10 = xmlStrcmp (BAD_CAST FEED_CACHE_VERSION, version);
				xmlFree (version);
			}
		} else {
			break;		
		}

		cur = cur->xmlChildrenNode;
		while (cur) {

			if (!xmlStrcmp (cur->name, BAD_CAST"item")) {
				itemPtr item;
							
				itemCount++;
				item = migrate_item_parse_cache (cur, migrateFrom10);
				item->nodeId = g_strdup (id);
				
				/* migrate item to DB */
				g_assert (0 == item->id);
				db_item_update (item);
				
				if (0 == (itemCount % 10))
					g_print(".");
				item_unload(item);
			}
			
			cur = cur->next;
		}
	} while (FALSE);

	if (ctxt->doc)
		xmlFreeDoc (ctxt->doc);
		
	g_free (ctxt->data);
	g_free (filename);

	feed_free_parser_ctxt (ctxt);
	
	db_node_update (node);
	if (node->subscription)
		db_subscription_update (node->subscription);
	
	g_print ("\n");
	
	debug_exit ("migrate_load_from_cache");
}

static void
migrate_items (const gchar *sourceDir)
{
   	GDir 	*dir;
	gchar	*id;

	dir = g_dir_open (sourceDir, 0, NULL);
	while (NULL != (id = (gchar *)g_dir_read_name (dir))) 
	{
		debug_start_measurement (DEBUG_CACHE);
		migrate_load_from_cache (sourceDir, id);
		debug_end_measurement (DEBUG_CACHE, "parse feed");
	}
	g_dir_close (dir);
}

static void 
migrate_from_10 (void)
{
	gchar *sourceDir;
	
	g_print ("Performing 1.0 -> %s cache migration...\n", LIFEREA_CURRENT_DIR);
	migrate_copy_dir (".liferea", LIFEREA_CURRENT_DIR, "");
	migrate_copy_dir (".liferea", LIFEREA_CURRENT_DIR, "cache" G_DIR_SEPARATOR_S "favicons");

	sourceDir = g_build_filename (g_get_home_dir(), ".liferea", "cache", "feeds", NULL);
	migrate_items (sourceDir);
	g_free (sourceDir);
}

static void
migrate_from_12 (void)
{
	gchar *sourceDir;
	
	g_print("Performing .liferea_1.2 -> %s cache migration...\n", LIFEREA_CURRENT_DIR);
	
	/* copy everything besides the feed cache */
	migrate_copy_dir (".liferea_1.2", LIFEREA_CURRENT_DIR, "");
	migrate_copy_dir (".liferea_1.2", LIFEREA_CURRENT_DIR, "cache" G_DIR_SEPARATOR_S "favicons");
	migrate_copy_dir (".liferea_1.2", LIFEREA_CURRENT_DIR, "cache" G_DIR_SEPARATOR_S "scripts");
	migrate_copy_dir (".liferea_1.2", LIFEREA_CURRENT_DIR, "cache" G_DIR_SEPARATOR_S "plugins");
	
	/* migrate feed cache to new DB format */
	sourceDir = g_build_filename (g_get_home_dir(), ".liferea_1.2", "cache", "feeds", NULL);
	migrate_items (sourceDir);
	g_free (sourceDir);
}

static void
migrate_from_14 (const gchar *olddir)
{
	g_print("Performing %s -> %s cache migration...\n", olddir, LIFEREA_CURRENT_DIR);	
	
	/* close already loaded DB */
	db_deinit ();

	/* just copying all files */
	migrate_copy_dir (olddir, LIFEREA_CURRENT_DIR, "");
	migrate_copy_dir (olddir, LIFEREA_CURRENT_DIR, "cache" G_DIR_SEPARATOR_S "favicons");
	migrate_copy_dir (olddir, LIFEREA_CURRENT_DIR, "cache" G_DIR_SEPARATOR_S "scripts");
	migrate_copy_dir (olddir, LIFEREA_CURRENT_DIR, "cache" G_DIR_SEPARATOR_S "plugins");	
	
	/* and reopen the copied one */
	db_init ();
}

static void
migrate_from_16 (const gchar *olddir)
{
	g_print("Performing %s -> %s cache migration...\n", olddir, LIFEREA_CURRENT_DIR);	
	
	/* close already loaded DB */
	db_deinit ();

	/* just copying all files */
	migrate_copy_dir (olddir, LIFEREA_CURRENT_DIR, "");
	migrate_copy_dir (olddir, LIFEREA_CURRENT_DIR, "cache" G_DIR_SEPARATOR_S "favicons");
	migrate_copy_dir (olddir, LIFEREA_CURRENT_DIR, "cache" G_DIR_SEPARATOR_S "scripts");
	migrate_copy_dir (olddir, LIFEREA_CURRENT_DIR, "cache" G_DIR_SEPARATOR_S "plugins");	
	
	/* and reopen the copied one */
	db_init ();
}

void
migration_execute (migrationMode mode)
{
	const gchar *olddir;

	switch (mode) {
		case MIGRATION_FROM_10:
			olddir = ".liferea";
			migrate_from_10 ();
			break;
		case MIGRATION_FROM_12:
			olddir = ".liferea_1.2";
			migrate_from_12 ();
			break;
		case MIGRATION_FROM_14:
			olddir = ".liferea_1.4";
			migrate_from_14 (olddir);
			break;
		case MIGRATION_FROM_16:
			olddir = ".liferea_1.6";
			migrate_from_16 (olddir);
			break;		
		case MIGRATION_MODE_INVALID:
		default:
			g_error ("Invalid migration mode!");
			return;
			break;
	}
	
	ui_show_info_box (_("This version of Liferea uses a new cache format and has migrated your "
	                    "feed cache. The cache content in %s was not deleted automatically. "
			    "Please remove this directory manually once you are sure migration was successful!"), olddir);
}
