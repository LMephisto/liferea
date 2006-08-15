/**
 * @file fl_plugin.c generic feedlist provider implementation
 * 
 * Copyright (C) 2005-2006 Lars Lindner <lars.lindner@gmx.net>
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

#include <gmodule.h>
#include <gtk/gtk.h>
#include <string.h>
#include "callbacks.h"
#include "common.h"
#include "debug.h"
#include "node.h"
#include "folder.h"
#include "plugin.h"
#include "render.h"
#include "support.h"
#include "fl_sources/fl_plugin.h"
#include "fl_sources/fl_plugin-ui.h"
#include "notification/notif_plugin.h"
#include "ui/ui_node.h"

static flPluginPtr fl_plugin_find(gchar *typeStr, guint capabilities) {
	flPluginPtr	flPlugin;
	pluginPtr	plugin;

	GSList *iter = plugin_mgmt_get_list();
	while(iter) {
		plugin = (pluginPtr)iter->data;
		if(plugin->type == PLUGIN_TYPE_FEEDLIST_PROVIDER) {
			flPlugin = plugin->symbols;
			if(((NULL == typeStr) || !strcmp(flPlugin->id, typeStr)) &&
			   ((0 == capabilities) || (flPlugin->capabilities & capabilities)))
				return flPlugin;
		}
		iter = g_slist_next(iter);
	}
	
	g_warning("Could not find plugin for source type \"%s\"\n!", typeStr);
	return NULL;
}

void fl_plugins_setup_root(nodePtr node) {
	flPluginPtr	flPlugin;
	
	debug_enter("fl_plugins_setup_root");

	flPlugin = fl_plugin_find(NULL, FL_PLUGIN_CAPABILITY_IS_ROOT);
	if(!flPlugin) 
		g_error("No root capable feed list provider plugin found!");
		
	node->source = g_new0(struct flNodeSource, 1);
	node->source->root = node;
	node->source->plugin = flPlugin;
	flPlugin->source_import(node);
	
	debug_exit("fl_plugins_setup_root");
}

typedef	flPluginPtr (*infoFunc)();

gboolean fl_plugin_load(pluginPtr plugin, GModule *handle) {
	flPluginPtr	flPlugin = NULL;
	infoFunc	fl_plugin_get_info;

	if(g_module_symbol(handle, "fl_plugin_get_info", (void*)&fl_plugin_get_info)) {
		/* load feed list provider plugin info */
		if(NULL == (flPlugin = (*fl_plugin_get_info)()))
			return FALSE;
	}

	/* check feed list provider plugin version */
	if(FL_PLUGIN_API_VERSION != flPlugin->api_version) {
		debug3(DEBUG_PLUGINS, "feed list API version mismatch: \"%s\" has version %d should be %d\n", flPlugin->name, flPlugin->api_version, FL_PLUGIN_API_VERSION);
		return FALSE;
	} 

	/* check if all mandatory symbols are provided */
	if(!(flPlugin->plugin_init &&
	     flPlugin->plugin_deinit)) {
		debug1(DEBUG_PLUGINS, "mandatory symbols missing: \"%s\"\n", flPlugin->name);
		return FALSE;
	}

	/* allow the plugin to initialize */
	(*flPlugin->plugin_init)();

	/* assign the symbols so the caller will accept the plugin */
	plugin->symbols = flPlugin;
	
	return TRUE;
}

void fl_plugin_import(nodePtr node, xmlNodePtr cur) {
	flPluginPtr	flPlugin;
	xmlChar		*typeStr = NULL;

	debug_enter("fl_plugin_import");

	if(NULL != (typeStr = xmlGetProp(cur, BAD_CAST"pluginType"))) {
		debug2(DEBUG_CACHE, "creating feed list plugin instance (type=%s,id=%s)\n", typeStr, node->id);
		
		node->available = FALSE;

		/* scan for matching plugin and create new instance */
		flPlugin = fl_plugin_find(typeStr, 0);
		
		if(NULL == flPlugin) {
			/* Plugin is not available for some reason, but
			   we need a plugin representation to keep the
			   node in the feed list. So we load a dummy 
			   instead and save the real source id in the
			   unused node's data field */
			flPlugin = fl_plugin_find(FL_DUMMY_SOURCE_ID, 0);
			g_assert(NULL != flPlugin);
			node->data = g_strdup(typeStr);
		}

		node->type = FST_PLUGIN;
		node->available = TRUE;
		node->source = g_new0(struct flNodeSource, 1);
		node->source->updateState = g_new0(struct updateState, 1);
		node->source->root = node;
		node->source->plugin = flPlugin;
		node->source->url = xmlGetProp(cur, BAD_CAST"xmlUrl");
		
		update_state_import(cur, node->source->updateState);
				
		flPlugin->source_import(node);
	} else {
		g_warning("No plugin type given for node \"%s\"", node_get_title(node));
	}	

	debug_exit("fl_plugin_import");
}

void fl_plugin_export(nodePtr node, xmlNodePtr cur) {

	debug_enter("fl_plugin_export");

	debug2(DEBUG_CACHE, "plugin export for node %s, id=%s\n", node->title, FL_PLUGIN(node)->id);
	if(!strcmp(FL_PLUGIN(node)->id, FL_DUMMY_SOURCE_ID))
		xmlNewProp(cur, BAD_CAST"pluginType", BAD_CAST(node->data));
	else
		xmlNewProp(cur, BAD_CAST"pluginType", BAD_CAST(FL_PLUGIN(node)->id));
		
	if(node->source->url)
		xmlNewProp(cur, BAD_CAST"xmlUrl", node->source->url);
		
	update_state_export(cur, node->source->updateState);

	debug_exit("fl_plugin_export");
}

void fl_plugin_new_source(nodePtr node, flPluginPtr flPlugin, const gchar *sourceUrl) {

	g_assert(NULL == node->source);
	node->source = g_new0(struct flNodeSource, 1);
	node->source->root = node;
	node->source->plugin = flPlugin;
	node->source->url = g_strdup(sourceUrl);
	node->source->updateState = g_new0(struct updateState, 1);
}

/* plugin instance creation dialog */

static void on_fl_plugin_type_selected(GtkDialog *dialog, gint response_id, gpointer user_data) {
	GtkTreeSelection	*selection;
	GtkTreeModel		*model;
	GtkTreeIter		iter;
	nodePtr 		parent = (nodePtr)user_data;
	flPluginPtr		flPlugin;

	if(response_id == GTK_RESPONSE_OK) {
		selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(lookup_widget(GTK_WIDGET(dialog), "plugin_type_list")));
		g_assert(NULL != selection);
		gtk_tree_selection_get_selected(selection, &model, &iter);
		gtk_tree_model_get(model, &iter, 1, &flPlugin, -1);
		flPlugin->source_new(parent);
	}
	
	gtk_widget_destroy(GTK_WIDGET(dialog));
}

void ui_fl_plugin_type_dialog(nodePtr parent) {
	GSList 			*iter;
	GtkWidget 		*dialog, *treeview;
	GtkTreeStore		*treestore;
	GtkCellRenderer		*renderer;
	GtkTreeViewColumn 	*column;
	GtkTreeIter		treeiter;
	flPluginPtr	flPlugin;
	pluginPtr	plugin;

	if(NULL == (iter = plugin_mgmt_get_list())) {
		ui_show_error_box(_("No feed list source plugins found!"));
		return;
	}		

	/* set up the dialog */
	dialog = create_fl_plugin_type_dialog();

	treestore = gtk_tree_store_new(2, G_TYPE_STRING, G_TYPE_POINTER);
	/* add available feed list plugins to treestore */

	while(iter) {
		plugin = (pluginPtr)iter->data;
		flPlugin = plugin->symbols;
		if((plugin->type == PLUGIN_TYPE_FEEDLIST_PROVIDER) &&
		   (flPlugin->capabilities & FL_PLUGIN_CAPABILITY_DYNAMIC_CREATION)) {

			gtk_tree_store_append(treestore, &treeiter, NULL);
			gtk_tree_store_set(treestore, &treeiter, 
			                              0, flPlugin->name, 
			                              1, flPlugin,
						      -1);
		}
		iter = g_slist_next(iter);
	}

	treeview = lookup_widget(dialog, "plugin_type_list");
	g_assert(NULL != treeview);

	column = gtk_tree_view_column_new();
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeview), -1, _("Source Type"), renderer, "text", 0, NULL);
	gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(treestore));
	g_object_unref(treestore);

	gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview)),
	                            GTK_SELECTION_SINGLE);

	g_signal_connect(G_OBJECT(dialog), "response",
			 G_CALLBACK(on_fl_plugin_type_selected), 
			 (gpointer)parent);

	gtk_widget_show_all(dialog);
}

/* implementation of the node type interface */

static void fl_plugin_request_update(nodePtr node, guint flags) {

	if(NULL != FL_PLUGIN(node)->source_update)
		FL_PLUGIN(node)->source_update(node);
}

static void fl_plugin_request_auto_update(nodePtr node) {

	if(NULL != FL_PLUGIN(node)->source_auto_update)
		FL_PLUGIN(node)->source_auto_update(node);
}

static void fl_plugin_remove(nodePtr node) {

	/* remove all children */
	node_foreach_child(node, node_remove);

	g_slist_free(node->children);
	node->children = NULL;

	/* remove the plugin node */
	node->parent->children = g_slist_remove(node->parent->children, node);
	notification_node_removed(node);
	ui_node_remove_node(node);
}

static gchar * fl_plugin_render(nodePtr node) {
	gchar	*result, *filename, **params = NULL;

	params = render_add_parameter(params, "headlineCount='%d'", g_list_length(node->itemSet->items));
	filename = common_create_cache_filename("cache" G_DIR_SEPARATOR_S "plugins", node->id, "opml");
	result = render_file(filename, "fl_plugin", params);
	g_free(filename);
	g_strfreev(params);
	
	return result;
}

static void ui_fl_plugin_dummy_properties(nodePtr node) {

	g_warning("Not supported!");
}

static void fl_plugin_save(nodePtr node) {

	node_foreach_child(node, node_save);
}

nodeTypePtr fl_plugin_get_node_type(void) {
	nodeTypePtr	nodeType;

	/* derive the plugin node type from the folder node type */
	nodeType = (nodeTypePtr)g_new0(struct nodeType, 1);
	nodeType->initial_load		= folder_get_node_type()->initial_load;
	nodeType->load			= folder_get_node_type()->load;
	nodeType->save			= fl_plugin_save;
	nodeType->unload		= folder_get_node_type()->unload;
	nodeType->reset_update_counter	= folder_get_node_type()->reset_update_counter;
	nodeType->request_update	= fl_plugin_request_update;
	nodeType->request_auto_update	= fl_plugin_request_auto_update;
	nodeType->remove		= fl_plugin_remove;
	nodeType->mark_all_read		= folder_get_node_type()->mark_all_read;
	nodeType->render		= fl_plugin_render;
	nodeType->request_add		= ui_fl_plugin_type_dialog;
	nodeType->request_properties	= ui_fl_plugin_dummy_properties;

	return nodeType; 
}
