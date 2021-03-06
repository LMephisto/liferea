/**
 * @file google_source_feed.c  Google reader feed subscription routines
 * 
 * Copyright (C) 2008 Arnold Noronha <arnstein87@gmail.com>
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

#include <glib.h>
#include <string.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "common.h"
#include "debug.h"
#include "xml.h"

#include "feedlist.h"
#include "google_source.h"
#include "subscription.h"
#include "node.h"
#include "google_source_edit.h"
#include "metadata.h"
#include "db.h"
#include "item_state.h"

/**
 * This is identical to xpath_foreach_match, except that it takes the context
 * as parameter.
 */
static void
google_source_xpath_foreach_match (const gchar* expr, xmlXPathContextPtr xpathCtxt, xpathMatchFunc func, gpointer user_data) 
{
	xmlXPathObjectPtr xpathObj = NULL;
	xpathObj = xmlXPathEval ((xmlChar*)expr, xpathCtxt);
	
	if (xpathObj && xpathObj->nodesetval && xpathObj->nodesetval->nodeMax) {
		int	i;
		for (i = 0; i < xpathObj->nodesetval->nodeNr; i++) {
			(*func) (xpathObj->nodesetval->nodeTab[i], user_data);
			xpathObj->nodesetval->nodeTab[i] = NULL ;
		}
	}
	
	if (xpathObj)
		xmlXPathFreeObject (xpathObj);
}

void
google_source_migrate_node(nodePtr node) 
{
	/* scan the node for bad ID's, if so, brutally remove the node */
	itemSetPtr itemset = node_get_itemset (node);
	GList *iter = itemset->ids;
	for (; iter; iter = g_list_next (iter)) {
		itemPtr item = item_load (GPOINTER_TO_UINT (iter->data));
		if (item && item->sourceId) {
			if (!g_str_has_prefix(item->sourceId, "tag:google.com")) {
				debug1(DEBUG_UPDATE, "Item with sourceId [%s] will be deleted.", item->sourceId);
				db_item_remove(GPOINTER_TO_UINT(iter->data));
			} 
		}
		if (item) item_unload (item);
	}

	/* cleanup */
	itemset_free (itemset);
}

static void
google_source_xml_unlink_node (xmlNodePtr node, gpointer data) 
{
	xmlUnlinkNode (node);
	xmlFreeNode (node);
}

void 
google_source_item_set_flag (nodePtr node, itemPtr item, gboolean newStatus)
{
	const gchar* sourceUrl = metadata_list_get (item->metadata, "GoogleBroadcastOrigFeed");
	if (!sourceUrl) sourceUrl = node->subscription->source;
	nodePtr root = google_source_get_root_from_node (node);
	google_source_edit_mark_starred ((GoogleSourcePtr)root->data, item->sourceId, sourceUrl, newStatus);
	item_flag_state_changed(item, newStatus);
}
void
google_source_item_mark_read (nodePtr node, itemPtr item, 
                              gboolean newStatus)
{
	const gchar* sourceUrl = metadata_list_get(item->metadata, "GoogleBroadcastOrigFeed");
	if (!sourceUrl) sourceUrl = node->subscription->source;
	nodePtr root = google_source_get_root_from_node (node);
	google_source_edit_mark_read ((GoogleSourcePtr)root->data, item->sourceId, sourceUrl, newStatus);
	item_read_state_changed(item, newStatus);
}

static void
google_source_set_orig_source(const xmlNodePtr node, gpointer userdata)
{
	itemPtr item = (itemPtr) userdata ;
	xmlChar*   value = xmlNodeGetContent (node);
	const gchar*     prefix1 = "tag:google.com,2005:reader/feed/";
	const gchar*     prefix2 = "tag:google.com,2005:reader/user/";

	debug1(DEBUG_UPDATE, "GoogleSource: Got %s as id while updating", value);

	if (g_str_has_prefix (value, prefix1) || g_str_has_prefix (value, prefix2)) {
		metadata_list_set (&item->metadata, "GoogleBroadcastOrigFeed", value + strlen (prefix1));
	}
	xmlFree (value);
}

static void
google_source_set_shared_by (xmlNodePtr node, gpointer userdata) 
{
	itemPtr     item    = (itemPtr) userdata;
	xmlChar     *value  = xmlNodeGetContent (node);
	xmlChar     *apos   = strrchr (value, '\'');
	gchar       *name;

	if (!apos) return;
	name = g_strndup (value, apos-value);

	metadata_list_set (&item->metadata, "sharedby", name);
	
	g_free (name);
	xmlFree (value);
}

static void
google_source_fix_broadcast_item (xmlNodePtr entry, itemPtr item) 
{
	xmlXPathContextPtr xpathCtxt = xmlXPathNewContext (entry->doc) ;
	xmlXPathRegisterNs (xpathCtxt, "atom", "http://www.w3.org/2005/Atom");
	xpathCtxt->node = entry;
	
	google_source_xpath_foreach_match ("./atom:source/atom:id", xpathCtxt, google_source_set_orig_source, item);
	
	/* who is sharing this? */
	google_source_xpath_foreach_match ("./atom:link[@rel='via']/@title", xpathCtxt, google_source_set_shared_by, item);

	db_item_update (item);
	/* free up xpath related data */
	if (xpathCtxt) xmlXPathFreeContext (xpathCtxt);
}

static itemPtr
google_source_load_item_from_sourceid (nodePtr node, gchar *sourceId, GHashTable *cache) 
{
	gpointer    ret = g_hash_table_lookup (cache, sourceId);
	itemSetPtr  itemset;
	int         num = g_hash_table_size (cache);
	GList       *iter; 
	itemPtr     item = NULL;

	if (ret) return item_load (GPOINTER_TO_UINT (ret));

	/* skip the top 'num' entries */
	itemset = node_get_itemset (node);
	iter = itemset->ids;
	while (num--) iter = g_list_next (iter);

	for (; iter; iter = g_list_next (iter)) {
		item = item_load (GPOINTER_TO_UINT (iter->data));
		if (item && item->sourceId) {
			/* save to cache */
			g_hash_table_insert (cache, g_strdup(item->sourceId), (gpointer) item->id);
			if (g_str_equal (item->sourceId, sourceId)) {
				itemset_free (itemset);
				return item;
			}
		}
		item_unload (item);
	}

	g_warning ("Could not find item for %s!", sourceId);
	itemset_free (itemset);
	return NULL;
}
static void
google_source_item_retrieve_status (const xmlNodePtr entry, subscriptionPtr subscription, GHashTable *cache)
{
	GoogleSourcePtr gsource = (GoogleSourcePtr) google_source_get_root_from_node (subscription->node)->data ;
	xmlNodePtr      xml;
	nodePtr         node = subscription->node;
	xmlChar         *id;
	gboolean        read = FALSE;
	gboolean        starred = FALSE;

	xml = entry->children;
	g_assert (xml);
	g_assert (g_str_equal (xml->name, "id"));

	id = xmlNodeGetContent (xml);

	for (xml = entry->children; xml; xml = xml->next) {
		if (g_str_equal (xml->name, "category")) {
			xmlChar* label = xmlGetProp (xml, "label");
			if (!label)
				continue;

			if (g_str_equal (label, "read"))
				read = TRUE;
			else if (g_str_equal(label, "starred")) 
				starred = TRUE;

			xmlFree (label);
		}
	}
	
	itemPtr item = google_source_load_item_from_sourceid (node, id, cache);
	if (item && item->sourceId) {
		if (g_str_equal (item->sourceId, id) && !google_source_edit_is_in_queue(gsource, id)) {
			
			if (item->readStatus != read)
				item_read_state_changed (item, read);
			if (item->flagStatus != starred) 
				item_flag_state_changed (item, starred);
			
			if (g_str_equal (subscription->source, GOOGLE_READER_BROADCAST_FRIENDS_URL)) 
				google_source_fix_broadcast_item (entry, item);
		}
	}
	if (item) item_unload (item) ;
	xmlFree (id);
}

static void
google_feed_subscription_process_update_result (subscriptionPtr subscription, const struct updateResult* const result, updateFlags flags)
{
	
	debug_start_measurement (DEBUG_UPDATE);

	if (result->data) { 
		updateResultPtr resultCopy;

		/* FIXME: The following is a very dirty hack to edit the feed's
		   XML before processing it */
		resultCopy = update_result_new () ;
		resultCopy->source = g_strdup (result->source); 
		resultCopy->httpstatus = result->httpstatus;
		resultCopy->contentType = g_strdup (result->contentType);
		g_free (resultCopy->updateState);
		resultCopy->updateState = update_state_copy (result->updateState);
		
		/* update the XML by removing 'read', 'reading-list' etc. as labels. */
		xmlDocPtr doc = xml_parse (result->data, result->size, NULL);
		xmlXPathContextPtr xpathCtxt = xmlXPathNewContext (doc) ;
		xmlXPathRegisterNs (xpathCtxt, "atom", "http://www.w3.org/2005/Atom");
		google_source_xpath_foreach_match ("/atom:feed/atom:entry/atom:category[@scheme='http://www.google.com/reader/']", xpathCtxt, google_source_xml_unlink_node, NULL);


		/* delete the via link for broadcast subscription */
		if (g_str_equal (subscription->source, GOOGLE_READER_BROADCAST_FRIENDS_URL)) 
			google_source_xpath_foreach_match ("/atom:feed/atom:entry/atom:link[@rel='via']/@href", xpathCtxt, google_source_xml_unlink_node, NULL);
		
		xmlXPathFreeContext (xpathCtxt);
		
		/* good now we have removed the read and unread labels. */
		
		xmlChar    *newXml; 
		int        newXmlSize ;
		
		xmlDocDumpMemory (doc, &newXml, &newXmlSize);
		
		resultCopy->data = g_strndup ((gchar*) newXml, newXmlSize);
		resultCopy->size = newXmlSize;
		
		xmlFree (newXml);
		xmlFreeDoc (doc);
		
		feed_get_subscription_type ()->process_update_result (subscription, resultCopy, flags);
		update_result_free (resultCopy);
	} else { 
		feed_get_subscription_type ()->process_update_result (subscription, result, flags);
		return ; 
	}
	
	/* FIXME: The following workaround ensure that the code below,
	   that uses UI callbacks item_*_state_changed(), does not 
	   reset the newCount of the feed list (see SF #2666478)
	   by getting the newCount first and setting it again later. */
	guint newCount = feedlist_get_new_item_count ();

	xmlDocPtr doc = xml_parse (result->data, result->size, NULL);
	if (doc) {		
		xmlNodePtr root = xmlDocGetRootElement (doc);
		xmlNodePtr entry = root->children ; 
		GHashTable *cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

		while (entry) { 
			if (!g_str_equal (entry->name, "entry")) {
				entry = entry->next;
				continue; /* not an entry */
			}
			
			google_source_item_retrieve_status (entry, subscription, cache);
			entry = entry->next;
		}
		
		g_hash_table_unref (cache);
		xmlFreeDoc (doc);
	} else { 
		debug0 (DEBUG_UPDATE, "google_feed_subscription_process_update_result(): Couldn't parse XML!");
		g_warning ("google_feed_subscription_process_update_result(): Couldn't parse XML!");
	}

	// FIXME: part 2 of the newCount workaround
	feedlist_update_new_item_count (newCount);
	
	debug_end_measurement (DEBUG_UPDATE, "time taken to update statuses");
}



static gboolean
google_feed_subscription_prepare_update_request (subscriptionPtr subscription, 
                                                 struct updateRequest *request)
{
	debug0 (DEBUG_UPDATE, "preparing google reader feed subscription for update\n");
	GoogleSourcePtr gsource = (GoogleSourcePtr) google_source_get_root_from_node (subscription->node)->data; 
	
	g_assert(gsource); 
	if (gsource->loginState == GOOGLE_SOURCE_STATE_NONE) { 
		subscription_update (google_source_get_root_from_node (subscription->node)->subscription, 0) ;
		return FALSE;
	}
	debug0 (DEBUG_UPDATE, "Setting cookies for a Google Reader subscription");

	if (!g_str_equal (request->source, GOOGLE_READER_BROADCAST_FRIENDS_URL)) { 
		gchar* source_escaped = g_uri_escape_string(request->source, NULL, TRUE);
		gchar* newUrl = g_strdup_printf ("http://www.google.com/reader/atom/feed/%s", source_escaped);
		update_request_set_source (request, newUrl);
		g_free (newUrl);
		g_free (source_escaped);
	}
	update_request_set_auth_value (request, gsource->authHeaderValue);
	return TRUE;
}

struct subscriptionType googleSourceFeedSubscriptionType = {
	google_feed_subscription_prepare_update_request,
	google_feed_subscription_process_update_result
};

