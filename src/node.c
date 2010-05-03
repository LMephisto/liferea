/**
 * @file node.c  hierarchic feed list node handling
 * 
 * Copyright (C) 2003-2010 Lars Lindner <lars.lindner@gmail.com>
 * Copyright (C) 2004-2006 Nathan J. Conrad <t98502@users.sourceforge.net>
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
 
#include <string.h>

#include "common.h"
#include "db.h"
#include "debug.h"
#include "itemlist.h"
#include "itemset.h"
#include "item_state.h"
#include "node.h"
#include "node_view.h"
#include "render.h"
#include "update.h"
#include "vfolder.h"
#include "fl_sources/node_source.h"
#include "ui/liferea_shell.h"
#include "ui/ui_node.h"
#include "ui/ui_tray.h"

static GHashTable *nodes = NULL;	/**< node id -> node lookup table */

#define NODE_ID_LEN	7

nodePtr
node_is_used_id (const gchar *id)
{
	if (!id || !nodes)
		return NULL;

	return (nodePtr)g_hash_table_lookup (nodes, id);
}

gchar *
node_new_id (void)
{
	gchar *id;
	
	id = g_new0 (gchar, NODE_ID_LEN + 1);
	do {
		int i;
		for (i = 0; i < NODE_ID_LEN; i++)
			id[i] = (gchar)g_random_int_range ('a', 'z');
	} while (NULL != node_is_used_id (id));
	
	return id;
}

nodePtr
node_from_id (const gchar *id)
{
	nodePtr node;
	
	node = node_is_used_id (id);
	if (!node)
		debug1 (DEBUG_GUI, "Fatal: no node with id \"%s\" found!", id);
		
	return node;
}

nodePtr
node_new (nodeTypePtr type)
{
	nodePtr	node;
	gchar	*id;
	
	g_assert (NULL != type);

	node = (nodePtr)g_new0 (struct node, 1);
	node->type = type;
	node->sortColumn = NODE_VIEW_SORT_BY_TIME;
	node->sortReversed = TRUE;	/* default sorting is newest date at top */
	node->available = TRUE;
	node_set_icon (node, NULL);	/* initialize favicon file name */

	id = node_new_id ();
	node_set_id (node, id);
	g_free (id);
	
	return node;
}

void
node_set_data (nodePtr node, gpointer data)
{
	g_assert (NULL == node->data);
	g_assert (NULL != node->type);

	node->data = data;
}

void
node_set_subscription (nodePtr node, subscriptionPtr subscription) 
{

	g_assert (NULL == node->subscription);
	g_assert (NULL != node->type);
		
	node->subscription = subscription;
	subscription->node = node;
	
	db_update_state_load (node->id, subscription->updateState);
}

void
node_update_subscription (nodePtr node, gpointer user_data) 
{
	if (node->source->root == node) {
		node_source_update (node);
		return;
	}
	
	if (node->subscription)
		subscription_update (node->subscription, GPOINTER_TO_UINT (user_data));
		
	node_foreach_child_data (node, node_update_subscription, user_data);
}

void
node_auto_update_subscription (nodePtr node) 
{
	if (node->source->root == node) {
		node_source_auto_update (node);
		return;
	}
	
	if (node->subscription)
		subscription_auto_update (node->subscription);	
		
	node_foreach_child (node, node_auto_update_subscription);
}

void
node_reset_update_counter (nodePtr node, GTimeVal *now) 
{
	subscription_reset_update_counter (node->subscription, now);
	
	node_foreach_child_data (node, node_reset_update_counter, now);
}

gboolean
node_is_ancestor (nodePtr node1, nodePtr node2)
{
	nodePtr	tmp;

	tmp = node2->parent;
	while (tmp) {
		if (node1 == tmp)
			return TRUE;
		tmp = tmp->parent;
	}
	return FALSE;
}

void
node_free (nodePtr node)
{
	if (node->data && NODE_TYPE (node)->free)
		NODE_TYPE (node)->free (node);

	g_assert (NULL == node->children);
	
	g_hash_table_remove (nodes, node->id);
	
	update_job_cancel_by_owner (node);

	if (node->subscription)
		subscription_free (node->subscription);

	if (node->icon)
		g_object_unref (node->icon);
	g_free (node->iconFile);
	g_free (node->title);
	g_free (node->id);
	g_free (node);
}

static void
node_calc_counters (nodePtr node)
{
	/* Order is important! First update all children
	   so that hierarchical nodes (folders and feed
	   list sources) can determine their own unread
	   count as the sum of all childs afterwards */
	node_foreach_child (node, node_calc_counters);
	
	NODE_TYPE (node)->update_counters (node);
}

static void
node_update_parent_counters (nodePtr node)
{
	guint old;

	if (!node)
		return;
		
	old = node->unreadCount;

	NODE_TYPE (node)->update_counters (node);
	
	if (old != node->unreadCount) {
		ui_node_update (node->id);
		ui_tray_update ();
		liferea_shell_update_unread_stats ();
	}
	
	if (node->parent)
		node_update_parent_counters (node->parent);
}

void
node_update_counters (nodePtr node)
{
	guint oldUnreadCount = node->unreadCount;
	guint oldItemCount = node->itemCount;

	/* Update the node itself and its children */
	node_calc_counters (node);
	
	if ((oldUnreadCount != node->unreadCount) ||
	    (oldItemCount != node->itemCount))
		ui_node_update (node->id);
		
	/* Update the unread count of the parent nodes,
	   usually they just add all child unread counters */
	if (!IS_VFOLDER (node))
		node_update_parent_counters (node->parent);
}

void
node_update_favicon (nodePtr node)
{
	if (NODE_TYPE (node)->capabilities & NODE_CAPABILITY_UPDATE_FAVICON) {
		debug1 (DEBUG_UPDATE, "favicon of node %s needs to be updated...", node->title);
		subscription_update_favicon (node->subscription);
	}
	
	/* Recursion */
	if (node->children)
		node_foreach_child (node, node_update_favicon);
}

itemSetPtr
node_get_itemset (nodePtr node)
{
	return NODE_TYPE (node)->load (node);
}

void
node_mark_all_read (nodePtr node)
{
	if (!node)
		return;

	if (0 != node->unreadCount) {
		itemset_mark_read (node);
		node->unreadCount = 0;
		node->needsUpdate = TRUE;
	}
		
	if (node->children)
		node_foreach_child (node, node_mark_all_read);
}

gchar *
node_render(nodePtr node)
{
	return NODE_TYPE (node)->render (node);
}

/* import callbacks and helper functions */

void
node_set_parent (nodePtr node, nodePtr parent, gint position)
{
	g_assert (NULL != parent);

	parent->children = g_slist_insert (parent->children, node, position);
	node->parent = parent;
	
	/* new nodes may be provided by another node source, if 
	   not they are handled by the parents node source */
	if (!node->source)
		node->source = parent->source;
}

void
node_remove (nodePtr node)
{
	/* using itemlist_remove_all_items() ensures correct unread
	   and item counters for all parent folders and matching 
	   search folders */
	itemlist_remove_all_items (node);
	
	NODE_TYPE (node)->remove (node);
}

static xmlDocPtr
node_to_xml (nodePtr node)
{
	xmlDocPtr	doc;
	xmlNodePtr	rootNode;
	gchar		*tmp;
	
	doc = xmlNewDoc("1.0");
	rootNode = xmlNewDocNode(doc, NULL, "node", NULL);
	xmlDocSetRootElement(doc, rootNode);

	xmlNewTextChild(rootNode, NULL, "title", node_get_title(node));
	
	tmp = g_strdup_printf("%u", node->unreadCount);
	xmlNewTextChild(rootNode, NULL, "unreadCount", tmp);
	g_free(tmp);
	
	tmp = g_strdup_printf("%u", g_slist_length(node->children));
	xmlNewTextChild(rootNode, NULL, "children", tmp);
	g_free(tmp);
	
	return doc;
}

gchar *
node_default_render (nodePtr node)
{
	gchar		*result;
	xmlDocPtr	doc;

	doc = node_to_xml (node);
	result = render_xml (doc, NODE_TYPE(node)->id, NULL);	
	xmlFreeDoc (doc);
		
	return result;
}

/* helper functions to be used with node_foreach* */

void
node_save(nodePtr node)
{
	NODE_TYPE(node)->save(node);
}

/* node attributes encapsulation */

void
node_set_title (nodePtr node, const gchar *title)
{
	g_free (node->title);
	node->title = g_strstrip (g_strdelimit (g_strdup (title), "\r\n", ' '));
}

const gchar *
node_get_title (nodePtr node)
{
	return node->title;
}

void
node_set_icon (nodePtr node, gpointer icon)
{
	if (node->icon) 
		g_object_unref (node->icon);
	node->icon = icon;
	
	g_free (node->iconFile);
	
	if (node->icon)
		node->iconFile = common_create_cache_filename ("cache" G_DIR_SEPARATOR_S "favicons", node->id, "png");
	else
		node->iconFile = g_build_filename (PACKAGE_DATA_DIR, PACKAGE, "pixmaps", "default.png", NULL);
}

/** determines the nodes favicon or default icon */
gpointer
node_get_icon (nodePtr node)
{
	if (!node->icon)
		return (gpointer) NODE_TYPE(node)->icon;

	return node->icon;
}

const gchar *
node_get_favicon_file (nodePtr node)
{
	return node->iconFile;
}

void
node_set_id (nodePtr node, const gchar *id)
{
	if (!nodes)
		nodes = g_hash_table_new(g_str_hash, g_str_equal);

	if (node->id) {
		g_hash_table_remove (nodes, node->id);
		g_free (node->id);
	}
	node->id = g_strdup (id);
	
	g_hash_table_insert (nodes, node->id, node);
}

const gchar *
node_get_id (nodePtr node)
{
	return node->id;
}

gboolean
node_set_sort_column (nodePtr node, nodeViewSortType sortColumn, gboolean reversed)
{
	if (node->sortColumn == sortColumn &&
	    node->sortReversed == reversed)
	    	return FALSE;

	node->sortColumn = sortColumn;
	node->sortReversed = reversed;

	return TRUE;
}

void
node_set_view_mode (nodePtr node, nodeViewType viewMode)
{
	node->viewMode = viewMode;
}

nodeViewType
node_get_view_mode (nodePtr node)
{
	return node->viewMode;
}

const gchar *
node_get_base_url(nodePtr node)
{
	const gchar 	*baseUrl = NULL;

	if (node->subscription)
		baseUrl = subscription_get_homepage (node->subscription);

	/* prevent feed scraping commands to end up as base URI */
	if (!((baseUrl != NULL) &&
	      (baseUrl[0] != '|') &&
	      (strstr(baseUrl, "://") != NULL)))
	   	baseUrl = NULL;

	return baseUrl;
}

/* node children iterating interface */

void
node_foreach_child_full (nodePtr node, gpointer func, gint params, gpointer user_data)
{
	GSList		*children, *iter;
	
	g_assert (NULL != node);

	/* We need to copy because func might modify the list */
	iter = children = g_slist_copy (node->children);
	while (iter) {
		nodePtr childNode = (nodePtr)iter->data;
		
		/* Apply the method to the child */
		if (0 == params)
			((nodeActionFunc)func) (childNode);
		else 
			((nodeActionDataFunc)func) (childNode, user_data);
			
		/* Never descend! */

		iter = g_slist_next (iter);
	}
	
	g_slist_free (children);
}

