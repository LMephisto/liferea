/**
 * @file fl_plugin.h generic feed list provider interface
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

#ifndef _FL_PLUGIN_H
#define _FL_PLUGIN_H

#include <glib.h>
#include <gmodule.h>
#include "node.h"
#include "plugin.h"

/* Liferea allows to have different sources for the feed list.
   Source can but need not to be single instance only. Sources
   provide a subtree of the feed list that can be read-only
   or not. A source might allow or not allow to add sub folders
   and reorder folder contents.

   The default node source type must be capable of serving as the root
   node for all other source types. This mean it has to ensure to load
   all other node source instances at their insertion nodes in
   the feed list.

   Each source type has to be able to serve user requests and is 
   responsible for keeping its feed list node's states up-to-date.
   A source type can omit all callbacks marked as optional. */


#define NODE_SOURCE_API_VERSION 4

enum {
	NODE_SOURCE_CAPABILITY_IS_ROOT			= (1<<0),	/**< flag only for default feed list source */
	NODE_SOURCE_CAPABILITY_MULTI_INSTANCES		= (1<<1),	/**< allows multiple source instances */
	NODE_SOURCE_CAPABILITY_DYNAMIC_CREATION		= (1<<2),	/**< feed list source is user created */
	NODE_SOURCE_CAPABILITY_WRITABLE_FEEDLIST	= (1<<3)	/**< the feed list tree of the source can be changed */
};

/** feed list node source type */
typedef struct nodeSourceType {
	unsigned int	api_version;
	
	gchar		*id;		/**< a unique feed list source plugin identifier */
	gchar		*name;		/**< a descriptive plugin name (for preferences and menus) */
	gulong		capabilities;	/**< bitmask of feed list source capabilities */

	/* source type loading and unloading methods */
	void		(*source_type_init)(void);
	void 		(*source_type_deinit)(void);

	/**
	 * This OPTIONAL callback is used to create an instance
	 * of the implemented plugin type. It is to be called by 
	 * the parent plugin's node_request_add_*() implementation. 
	 * Mandatory for all plugin's except the root provider plugin.
	 */
	void 		(*source_new)(nodePtr parent);

	/**
	 * This OPTIONAL callback is used to delete an instance
	 * of the implemented source type. It is to be called
	 * by the parent source node_remove() implementation.
	 * Mandatory for all sources except the root provider source.
	 */
	void 		(*source_delete)(nodePtr node);

	/**
	 * This mandatory method is called when the source is to
	 * create the feed list subtree attached to the source root
	 * node.
	 */
	void 		(*source_import)(nodePtr node);

	/**
	 * This mandatory method is called when the source is to
	 * save it's feed list subtree (if necessary at all). This
	 * is not a request to save the data of the attached nodes!
	 */
	void 		(*source_export)(nodePtr node);
	
	/**
	 * This method is called to get an OPML representation of
	 * the feedlist of the given node source. Returns a newly
	 * allocated filename string that is to be freed by the
	 * caller.
	 */
	gchar *		(*source_get_feedlist)(nodePtr node);
	
	/**
	 * This OPTIONAL callback is called to start a user requested
	 * update of the source the node belongs to.
	 */
	void		(*source_update)(nodePtr node);
	
	/**
	 * This MANDATORY callback is called regularily to allow 
	 * the the source the node belongs to to auto-update.
	 */
	void		(*source_auto_update)(nodePtr node);
} *nodeSourceTypePtr;

/** feed list source instance */
typedef struct nodeSource {
	nodeSourceTypePtr	type;		/**< node source type of this source instance */
	updateStatePtr 		updateState;	/**< the update state (etags, last modified, cookies...) of this source */
	nodePtr			root;		/**< insertion node of this node source instance */
	gchar			*url;		/**< URL or filename of the node source instance */
} *nodeSourcePtr;

/** Use this to cast the node source type from a node structure. */
#define NODE_SOURCE_TYPE(node) ((nodeSourceTypePtr)(node->source))->type

/** Feed list plugins are to be declared with this macro. */
#define DECLARE_NODE_SOURCE_PLUGIN(nodeSourceType) \
        G_MODULE_EXPORT nodeSourceTypePtr node_source_type_get_info() { \
                return &nodeSourceType; \
        }

/** 
 * Scans the source type list for the root source provider.
 * If found creates a new root source and starts it's import.
 *
 * @param node		the root node
 */
void node_source_setup_root(nodePtr node);

/**
 * Registers a node source type.
 *
 * @param nodeSourceType	node source type
 *
 * @returns TRUE on success
 */
gboolean node_source_type_register(nodeSourceTypePtr type);

/**
 * Source type specific export a given source root node.
 *
 * @param node	the root node of the source to import
 * @param cur	DOM node to parse
 */
void node_source_import(nodePtr node, xmlNodePtr cur); 

/**
 * Source type specific import a given source root node.
 *
 * @param node	the root node of the source to export
 * @param cur	DOM node to write to
 */
void node_source_export(nodePtr node, xmlNodePtr cur); 

/**
 * Creates a new source and assigns it to the given new node. 
 * To be used to prepare a source node before adding it to the 
 * feed list.
 *
 * @param node		a newly created node
 * @param flPlugin	plugin implementing the source
 * @param sourceUrl	URI of the source
 */
void node_source_new(nodePtr node, nodeSourceTypePtr nodeSourceType, const gchar *sourceUrl);

/**
 * Launches a plugin creation dialog. The new plugin
 * instance will be added to the given node.
 *
 * @param node	the parent node
 */
void ui_node_source_type_dialog(nodePtr node);

/* implementation of the node type interface */
nodeTypePtr node_source_get_node_type(void);

#endif