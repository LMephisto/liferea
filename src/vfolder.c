/**
 * @file vfolder.c  search folder node type
 *
 * Copyright (C) 2003-2010 Lars Lindner <lars.lindner@gmail.com>
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

#include "vfolder.h"

#include "common.h"
#include "db.h"
#include "debug.h"
#include "feedlist.h"
#include "itemset.h"
#include "itemlist.h"
#include "node.h"
#include "rule.h"
#include "ui/icons.h"
#include "ui/ui_node.h"
#include "ui/search_folder_dialog.h"

/** The list of all existing vfolders. Used for updating vfolder information upon item changes */
static GSList		*vfolders = NULL;

vfolderPtr
vfolder_new (nodePtr node) 
{
	vfolderPtr	vfolder;

	debug_enter ("vfolder_new");

	vfolder = g_new0 (struct vfolder, 1);
	vfolder->itemset = g_new0 (struct itemSet, 1);
	vfolder->itemset->nodeId = node->id;
	vfolder->node = node;
	vfolder->anyMatch = TRUE;
	vfolders = g_slist_append (vfolders, vfolder);

	if (!node->title)
		node_set_title (node, _("New Search Folder"));	/* set default title */
	node_set_data (node, (gpointer) vfolder);

	debug_exit ("vfolder_new");
	
	return vfolder;
}

static void
vfolder_import_rules (xmlNodePtr cur,
                      vfolderPtr vfolder)
{
	xmlChar		*matchType, *type, *ruleId, *value, *additive;
	
	matchType = xmlGetProp (cur, BAD_CAST"matchType");
	if (matchType) {
		/* currently we now only OR or AND'ing the rules,
		   "any" is the value for OR'ing, "all" for AND'ing */
		vfolder->anyMatch = (0 != xmlStrcmp (matchType, BAD_CAST"all"));
	} else {
		vfolder->anyMatch = TRUE;
	}
	xmlFree (matchType);

	/* process any children */
	cur = cur->xmlChildrenNode;
	while (cur) {
		if (!xmlStrcmp (cur->name, BAD_CAST"outline")) {
			type = xmlGetProp (cur, BAD_CAST"type");
			if (type && !xmlStrcmp (type, BAD_CAST"rule")) {

				ruleId = xmlGetProp (cur, BAD_CAST"rule");
				value = xmlGetProp (cur, BAD_CAST"value");
				additive = xmlGetProp (cur, BAD_CAST"additive");

				if (ruleId && value) {			
					debug2 (DEBUG_CACHE, "loading rule \"%s\" \"%s\"", ruleId, value);

					if (additive && !xmlStrcmp (additive, BAD_CAST"true"))
						vfolder_add_rule (vfolder, ruleId, value, TRUE);
					else
						vfolder_add_rule (vfolder, ruleId, value, FALSE);
				} else {
					g_warning ("ignoring invalid rule entry for vfolder \"%s\"...\n", node_get_title (vfolder->node));
				}
				
				xmlFree (ruleId);
				xmlFree (value);
				xmlFree (additive);
			}
			xmlFree (type);
		}
		cur = cur->next;
	}
}

static itemSetPtr
vfolder_load (nodePtr node) 
{
	vfolderPtr vfolder = (vfolderPtr)node->data;

	return vfolder->itemset;
}

void
vfolder_update_counters (vfolderPtr vfolder) 
{
	/* There is no unread handling for search folders
	   for performance reasons. So set everything to 0 
	   here and don't bother with GUI updates... */
	vfolder->node->unreadCount = 0;
	vfolder->node->itemCount = 0;
}

void
vfolder_refresh (vfolderPtr vfolder)
{
	g_return_if_fail (NULL != vfolder->node);

	// FIXME:
	
	vfolder_update_counters (vfolder);
}

void
vfolder_foreach_data (vfolderActionDataFunc func, itemPtr item)
{
	GSList	*iter = vfolders;
	
	g_assert (NULL != func);
	while (iter) {
		vfolderPtr vfolder = (vfolderPtr)iter->data;
		(*func) (vfolder, item);
		iter = g_slist_next (iter);
	}
}

void
vfolder_remove_item (vfolderPtr vfolder, itemPtr item)
{
	vfolder->itemset->ids = g_list_remove (vfolder->itemset->ids, GUINT_TO_POINTER (item->id));
}

void
vfolder_check_item (vfolderPtr vfolder, itemPtr item)
{
	if (rules_check_item (vfolder->rules, vfolder->anyMatch, item))
		vfolder->itemset->ids = g_list_append (vfolder->itemset->ids, GUINT_TO_POINTER (item->id));
	else
		vfolder_remove_item (vfolder, item);
}

GSList *
vfolder_get_all_with_item_id (gulong id)
{
	GSList	*result = NULL;
	GSList	*iter = vfolders;
	
	while (iter) {
		vfolderPtr vfolder = (vfolderPtr)iter->data;
		if (g_list_find (vfolder->itemset->ids, GUINT_TO_POINTER (id)))
			result = g_slist_append (result, vfolder);
		iter = g_slist_next (iter);
	}

	return result;
}

static void
vfolder_import (nodePtr node,
                nodePtr parent,
                xmlNodePtr cur,
                gboolean trusted) 
{
	vfolderPtr vfolder;

	debug1 (DEBUG_CACHE, "import vfolder: title=%s", node_get_title (node));

	vfolder = vfolder_new (node);
	vfolder->itemset = db_search_folder_load (node->id);
	
	vfolder_import_rules (cur, vfolder);
}

static void
vfolder_export (nodePtr node,
                xmlNodePtr cur,
                gboolean trusted)
{
	vfolderPtr	vfolder = (vfolderPtr) node->data;
	xmlNodePtr	ruleNode;
	rulePtr		rule;
	GSList		*iter;

	debug_enter ("vfolder_export");
	
	g_assert (TRUE == trusted);
	
	xmlNewProp (cur, BAD_CAST"matchType", BAD_CAST (vfolder->anyMatch?"any":"all"));

	iter = vfolder->rules;
	while (iter) {
		rule = iter->data;
		ruleNode = xmlNewChild (cur, NULL, BAD_CAST"outline", NULL);
		xmlNewProp (ruleNode, BAD_CAST"type", BAD_CAST "rule");
		xmlNewProp (ruleNode, BAD_CAST"text", BAD_CAST rule->ruleInfo->title);
		xmlNewProp (ruleNode, BAD_CAST"rule", BAD_CAST rule->ruleInfo->ruleId);
		xmlNewProp (ruleNode, BAD_CAST"value", BAD_CAST rule->value);
		if (rule->additive)
			xmlNewProp (ruleNode, BAD_CAST"additive", BAD_CAST "true");
		else
			xmlNewProp (ruleNode, BAD_CAST"additive", BAD_CAST "false");

		iter = g_slist_next (iter);
	}
	
	debug1 (DEBUG_CACHE, "adding vfolder: title=%s", node_get_title (node));

	debug_exit ("vfolder_export");
}

void
vfolder_add_rule (vfolderPtr vfolder,
                  const gchar *ruleId,
                  const gchar *value,
                  gboolean additive)
{
	rulePtr		rule;
	
	rule = rule_new (vfolder, ruleId, value, additive);
	if (rule)
		vfolder->rules = g_slist_append (vfolder->rules, rule);
	else
		g_warning ("unknown search folder rule id: \"%s\"", ruleId);
}

static void
vfolder_free (nodePtr node) 
{
	vfolderPtr	vfolder = (vfolderPtr) node->data;
	GSList		*rule;

	debug_enter ("vfolder_free");
	
	vfolders = g_slist_remove (vfolders, vfolder);
	
	/* free vfolder rules */
	rule = vfolder->rules;
	while (rule) {
		rule_free (rule->data);
		rule = g_slist_next (rule);
	}
	g_slist_free (vfolder->rules);
	vfolder->rules = NULL;
	
	debug_exit ("vfolder_free");
}

/* implementation of the node type interface */

static void vfolder_save (nodePtr node) { }

static void
vfolder_update_unread_count (nodePtr node) 
{
	g_warning("Should never be called!");
}

static void
vfolder_remove (nodePtr node) 
{
	db_search_folder_remove (node->id);
}

static void
vfolder_properties (nodePtr node)
{
	search_folder_dialog_new (node);
}

static gboolean
vfolder_add (void)
{
	nodePtr	node;

	node = node_new (vfolder_get_node_type ());
	vfolder_new (node);
	vfolder_properties (node);
	
	return TRUE;
}

nodeTypePtr
vfolder_get_node_type (void)
{ 
	static struct nodeType nti = {
		NODE_CAPABILITY_SHOW_ITEM_FAVICONS |
		NODE_CAPABILITY_SHOW_UNREAD_COUNT |
		NODE_CAPABILITY_SHOW_ITEM_COUNT,
		"vfolder",
		NULL,
		vfolder_import,
		vfolder_export,
		vfolder_load,
		vfolder_save,
		vfolder_update_unread_count,
		vfolder_remove,
		node_default_render,
		vfolder_add,
		vfolder_properties,
		vfolder_free
	};
	nti.icon = icon_get (ICON_VFOLDER);

	return &nti; 
}
