/**
 * @file search_dialog.c  Search engine subscription dialog
 *
 * Copyright (C) 2007-2010 Lars Lindner <lars.lindner@gmail.com>
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

#include "ui/search_dialog.h"

#include <string.h>

#include "common.h"
#include "feed.h"
#include "feedlist.h"
#include "htmlview.h"
#include "itemlist.h"
#include "node.h"
#include "node_view.h"
#include "rule.h"
#include "vfolder.h"
#include "ui/itemview.h"
#include "ui/liferea_dialog.h"
#include "ui/rule_editor.h"
#include "ui/feed_list_view.h"

/* shared functions */

/** 
 * Loads a search result into the item list and renders
 * some info text into the HTML view pane.
 *
 * @param searchResult		valid search result node
 * @param searchString		search text (or NULL)
 */
static void
search_load_results (nodePtr searchResult, const gchar *searchString)
{
	GString	*buffer;
	itemSetPtr itemSet;
	nodeViewType viewMode;
	
	/* Clear feed and item display and load search results */
	feed_list_view_select (NULL);
	itemlist_unload (FALSE);
	
	/* Ensure that we are in a useful viewing mode (3 paned) */
	viewMode = itemlist_get_view_mode ();
	if ((NODE_VIEW_MODE_NORMAL != viewMode) &&
	    (NODE_VIEW_MODE_WIDE != viewMode))
		itemview_set_layout (NODE_VIEW_MODE_NORMAL);
		
	itemSet = node_get_itemset (searchResult);
	itemlist_load_search_result (itemSet);
	itemset_free (itemSet);

	buffer = g_string_new (NULL);
	htmlview_start_output (buffer, NULL, TRUE, FALSE);
	g_string_append_printf (buffer, "<div class='content'><h2>");
	
	if (searchString)
		g_string_append_printf (buffer, ngettext("%d Search Result for \"%s\"", 
	                                        	 "%d Search Results for \"%s\"",
	                                        	 searchResult->itemCount),
	                        	searchResult->itemCount, searchString);
	else 
		g_string_append_printf (buffer, ngettext("%d Search Result", 
	                                        	 "%d Search Results",
	                                        	 searchResult->itemCount),
	                        	searchResult->itemCount);
					
	g_string_append_printf (buffer, "</h2><p>");
	g_string_append_printf (buffer, _("The item list now contains all items matching the "
	                                "specified search pattern. If you want to save this search "
	                                "result permanently you can click the \"Search Folder\" button in "
	                                "the search dialog and Liferea will add a search folder to your "
	                                "feed list."));
	g_string_append_printf (buffer, "</p></div>");
	htmlview_finish_output (buffer);
	itemview_display_info (buffer->str);
	g_string_free (buffer, TRUE);
}

/* complex search dialog */

static SearchDialog *search = NULL;

static void search_dialog_class_init	(SearchDialogClass *klass);
static void search_dialog_init		(SearchDialog *sd);

#define SEARCH_DIALOG_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), SEARCH_DIALOG_TYPE, SearchDialogPrivate))

struct SearchDialogPrivate {
	GtkWidget	*dialog;	/**< the dialog widget */
	RuleEditor	*re;		/**< search folder rule editor widget set */

	nodePtr		searchResult;	/**< FIXME: evil no nodePtr should be necessary here! */
	vfolderPtr	vfolder;	/**< temporary search folder representing the search result */
};

static GObjectClass *parent_class = NULL;

GType
search_dialog_get_type (void) 
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) 
	{
		static const GTypeInfo our_info = 
		{
			sizeof (SearchDialogClass),
			NULL, /* base_init */
			NULL, /* base_finalize */
			(GClassInitFunc) search_dialog_class_init,
			NULL,
			NULL, /* class_data */
			sizeof (SearchDialog),
			0, /* n_preallocs */
			(GInstanceInitFunc) search_dialog_init
		};

		type = g_type_register_static (G_TYPE_OBJECT,
					       "SearchDialog",
					       &our_info, 0);
	}

	return type;
}

static void
search_dialog_finalize (GObject *object)
{
	SearchDialog *sd = SEARCH_DIALOG (object);
	
	gtk_widget_destroy (sd->priv->dialog);
	if (sd->priv->searchResult)
		node_free (sd->priv->searchResult);
	search = NULL;
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
search_dialog_class_init (SearchDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = search_dialog_finalize;

	g_type_class_add_private (object_class, sizeof(SearchDialogPrivate));
}

static void
search_dialog_init (SearchDialog *sd)
{
	sd->priv = SEARCH_DIALOG_GET_PRIVATE (sd);
	sd->priv->searchResult = node_new (vfolder_get_node_type ());
	sd->priv->vfolder = vfolder_new (sd->priv->searchResult);
	node_set_title (sd->priv->searchResult, "Saved Search");
}

static void
on_search_dialog_response (GtkDialog *dialog, gint responseId, gpointer user_data)
{
	SearchDialog	*sd = (SearchDialog *)user_data;
	
	if (1 == responseId) { /* Search */
		rule_editor_save (sd->priv->re, sd->priv->vfolder);
		sd->priv->vfolder->itemset->anyMatch = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (liferea_dialog_lookup (sd->priv->dialog, "anyRuleRadioBtn2")));
		
		vfolder_reset (sd->priv->vfolder);
		search_load_results (sd->priv->searchResult, NULL);
	}
	
	if (2 == responseId) { /* + Search Folder */
		rule_editor_save (sd->priv->re, sd->priv->vfolder);
		sd->priv->vfolder->itemset->anyMatch = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (liferea_dialog_lookup (sd->priv->dialog, "anyRuleRadioBtn2")));
		
		nodePtr node = sd->priv->searchResult;
		sd->priv->searchResult = NULL;
		sd->priv->vfolder = NULL;
		feedlist_node_added (node);
	}
	
	if (1 != responseId)
		g_object_unref (sd);
}

/* callback copied from search_folder_dialog.c */
static void
on_addrulebtn_clicked (GtkButton *button, gpointer user_data)
{
	SearchDialog *sd = SEARCH_DIALOG (user_data);
		
	rule_editor_add_rule (sd->priv->re, NULL);
}

SearchDialog *
search_dialog_open (const gchar *query)
{
	SearchDialog	*sd;
	
	if (search)
		return search;
		
	sd = SEARCH_DIALOG (g_object_new (SEARCH_DIALOG_TYPE, NULL));
	sd->priv->dialog = liferea_dialog_new (NULL, "searchdialog");
	
	if (query)
		itemset_add_rule (sd->priv->vfolder->itemset, "exact", query, TRUE);

	sd->priv->re = rule_editor_new (sd->priv->vfolder);

	/* Note: the following code is somewhat duplicated from search_folder_dialog.c */
	
	/* Setting default rule match type */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (liferea_dialog_lookup (sd->priv->dialog, "anyRuleRadioBtn2")), TRUE);
	
	/* Set up rule list vbox */
	gtk_container_add (GTK_CONTAINER (liferea_dialog_lookup (sd->priv->dialog, "ruleview_search_dialog")), rule_editor_get_widget (sd->priv->re));

	/* bind buttons */
	g_signal_connect (liferea_dialog_lookup (sd->priv->dialog, "addrulebtn2"), "clicked", G_CALLBACK (on_addrulebtn_clicked), sd);
	g_signal_connect (G_OBJECT (sd->priv->dialog), "response", G_CALLBACK (on_search_dialog_response), sd);
	
	gtk_widget_show_all (sd->priv->dialog);
	
	search = sd;
	return sd;
}

/* simple search dialog */

static SimpleSearchDialog *simpleSearch = NULL;

static void simple_search_dialog_class_init	(SimpleSearchDialogClass *klass);
static void simple_search_dialog_init		(SimpleSearchDialog *ssd);

#define SIMPLE_SEARCH_DIALOG_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), SIMPLE_SEARCH_DIALOG_TYPE, SimpleSearchDialogPrivate))

struct SimpleSearchDialogPrivate {
	GtkWidget	*dialog;	/**< the dialog widget */
	GtkWidget	*query;		/**< entry widget for the search query */

	nodePtr		searchResult;	/**< FIXME: evil no nodePtr should be necessary here! */
	vfolderPtr	vfolder;	/**< temporary search folder representing the search result */
};

static GObjectClass *parent_class_simple = NULL;

GType
simple_search_dialog_get_type (void) 
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) 
	{
		static const GTypeInfo our_info = 
		{
			sizeof (SimpleSearchDialogClass),
			NULL, /* base_init */
			NULL, /* base_finalize */
			(GClassInitFunc) simple_search_dialog_class_init,
			NULL,
			NULL, /* class_data */
			sizeof (SimpleSearchDialog),
			0, /* n_preallocs */
			(GInstanceInitFunc) simple_search_dialog_init
		};

		type = g_type_register_static (G_TYPE_OBJECT,
					       "SimpleSearchDialog",
					       &our_info, 0);
	}

	return type;
}

static void
simple_search_dialog_finalize (GObject *object)
{
	SimpleSearchDialog *ssd = SIMPLE_SEARCH_DIALOG (object);
	
	gtk_widget_destroy (ssd->priv->dialog);
	
	if (ssd->priv->searchResult)
		node_free (ssd->priv->searchResult);
		
	simpleSearch = NULL;
	
	G_OBJECT_CLASS (parent_class_simple)->finalize (object);
}

static void
simple_search_dialog_class_init (SimpleSearchDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class_simple = g_type_class_peek_parent (klass);

	object_class->finalize = simple_search_dialog_finalize;

	g_type_class_add_private (object_class, sizeof(SimpleSearchDialogPrivate));
}

static void
simple_search_dialog_init (SimpleSearchDialog *ssd)
{
	ssd->priv = SIMPLE_SEARCH_DIALOG_GET_PRIVATE (ssd);
}

static void
on_simple_search_dialog_response (GtkDialog *dialog, gint responseId, gpointer user_data)
{
	SimpleSearchDialog	*ssd = (SimpleSearchDialog *)user_data;
	const gchar		*searchString;

	searchString = 	gtk_entry_get_text (GTK_ENTRY (ssd->priv->query));
	
	if (1 == responseId) {	/* Search */
		/* Clean up old search result data and display... */
		if (ssd->priv->searchResult) {
			if (ssd->priv->searchResult == itemlist_get_displayed_node ())
				itemlist_unload (FALSE);
			
			node_free (ssd->priv->searchResult);
		}
		
		/* Create new search... */
		ssd->priv->searchResult = node_new (vfolder_get_node_type ());
		ssd->priv->vfolder = vfolder_new (ssd->priv->searchResult);
	
		node_set_title (ssd->priv->searchResult, searchString);
		itemset_add_rule (ssd->priv->vfolder->itemset, "exact", searchString, TRUE);
		vfolder_reset (ssd->priv->vfolder);

		search_load_results (ssd->priv->searchResult, searchString);
	}
	
	if (2 == responseId)	/* Advanced... */			
		search_dialog_open (searchString);

	/* Do not close the dialog when "just" searching. The user
	   should click "Close" to close the dialog to be able to
	   do subsequent searches... */	
	if (1 != responseId)
		g_object_unref (ssd);
}

static void
on_searchentry_activated (GtkEntry *entry, gpointer user_data)
{
	SimpleSearchDialog	*ssd = SIMPLE_SEARCH_DIALOG (user_data);
	
	/* simulate search response */
	on_simple_search_dialog_response (GTK_DIALOG (ssd->priv->dialog), 1, ssd);
}

static void
on_searchentry_changed (GtkEditable *editable, gpointer user_data)
{
	SimpleSearchDialog	*ssd = SIMPLE_SEARCH_DIALOG (user_data);
	gchar 			*searchString;
	
	/* just to disable the start search button when search string is empty... */
	searchString = gtk_editable_get_chars (editable, 0, -1);
	gtk_widget_set_sensitive (liferea_dialog_lookup (ssd->priv->dialog, "searchstartbtn"), searchString && (0 < strlen (searchString)));
}

SimpleSearchDialog *
simple_search_dialog_open (void)
{
	SimpleSearchDialog	*ssd;

	if (simpleSearch)
		return simpleSearch;

	ssd = SIMPLE_SEARCH_DIALOG (g_object_new (SIMPLE_SEARCH_DIALOG_TYPE, NULL));
	ssd->priv->dialog = liferea_dialog_new (NULL, "simplesearchdialog");
	ssd->priv->query = liferea_dialog_lookup (ssd->priv->dialog, "searchentry");
	
	gtk_window_set_focus (GTK_WINDOW (ssd->priv->dialog), ssd->priv->query);
	
	g_signal_connect (G_OBJECT (ssd->priv->dialog), "response", G_CALLBACK (on_simple_search_dialog_response), ssd);
	g_signal_connect (G_OBJECT (ssd->priv->query), "changed", G_CALLBACK (on_searchentry_changed), ssd);
	g_signal_connect (G_OBJECT (ssd->priv->query), "activate", G_CALLBACK (on_searchentry_activated), ssd);
	
	gtk_widget_show_all (ssd->priv->dialog);
	
	simpleSearch = ssd;
	return ssd;
}
