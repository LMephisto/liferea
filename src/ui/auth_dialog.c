/**
 * @file auth_dialog.c  authentication dialog
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

#include "ui/auth_dialog.h"

#include <libxml/uri.h>
#include <string.h> 

#include "common.h"
#include "debug.h"
#include "ui/liferea_dialog.h"

static void auth_dialog_class_init	(AuthDialogClass *klass);
static void auth_dialog_init		(AuthDialog *ad);

#define AUTH_DIALOG_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), AUTH_DIALOG_TYPE, AuthDialogPrivate))

struct AuthDialogPrivate {

	subscriptionPtr subscription;
	
	GtkWidget *dialog;
	GtkWidget *username;
	GtkWidget *password;	
	
	gint flags;
};

static GObjectClass *parent_class = NULL;

GType
auth_dialog_get_type (void) 
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) 
	{
		static const GTypeInfo our_info = 
		{
			sizeof (AuthDialogClass),
			NULL, /* base_init */
			NULL, /* base_finalize */
			(GClassInitFunc) auth_dialog_class_init,
			NULL,
			NULL, /* class_data */
			sizeof (AuthDialog),
			0, /* n_preallocs */
			(GInstanceInitFunc) auth_dialog_init,
			NULL /* value_table */
		};

		type = g_type_register_static (G_TYPE_OBJECT,
					       "AuthDialog",
					       &our_info, 0);
	}

	return type;
}

static void
auth_dialog_finalize (GObject *object)
{
	AuthDialog *ad = AUTH_DIALOG (object);
	
	if (ad->priv->subscription != NULL)
		ad->priv->subscription->activeAuth = FALSE;
	
	gtk_widget_destroy (ad->priv->dialog);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
auth_dialog_class_init (AuthDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = auth_dialog_finalize;

	g_type_class_add_private (object_class, sizeof(AuthDialogPrivate));
}

static void
on_authdialog_response (GtkDialog *dialog,
                        gint response_id,
			gpointer user_data) 
{
	AuthDialog *ad = (AuthDialog *)user_data;
	
	if (response_id == GTK_RESPONSE_OK) {
		g_assert (NULL != ad->priv->subscription->updateOptions);
		
		g_free (ad->priv->subscription->updateOptions->username);
		g_free (ad->priv->subscription->updateOptions->password);
		
		ad->priv->subscription->updateOptions->username = g_strdup (BAD_CAST gtk_entry_get_text (GTK_ENTRY (ad->priv->username)));
		ad->priv->subscription->updateOptions->password = g_strdup (BAD_CAST gtk_entry_get_text (GTK_ENTRY (ad->priv->password)));
		subscription_update (ad->priv->subscription, ad->priv->flags);
	}

	g_object_unref (ad);
}

static void
auth_dialog_load (AuthDialog *ad,
                     subscriptionPtr subscription,
                     gint flags)
{
	AuthDialogPrivate	*ui_data = ad->priv;
	gchar			*promptStr;
	gchar			*source = NULL;
	xmlURIPtr		uri;
	
	subscription->activeAuth = TRUE;
	
	ui_data->subscription = subscription;
	ui_data->flags = flags;
	
	ui_data->username = liferea_dialog_lookup (ui_data->dialog, "usernameEntry");
	ui_data->password = liferea_dialog_lookup (ui_data->dialog, "passwordEntry");
	
	uri = xmlParseURI (BAD_CAST subscription_get_source (ui_data->subscription));
	
	if (uri) {
		if (uri->user) {
			gchar *user = uri->user;
			gchar *pass = strstr (user, ":");
			if(pass) {
				pass[0] = '\0';
				pass++;
				gtk_entry_set_text (GTK_ENTRY (ui_data->password), pass);
			}
			gtk_entry_set_text (GTK_ENTRY (ui_data->username), user);
			xmlFree (uri->user);
			uri->user = NULL;
		}
		xmlFree (uri->user);
		uri->user = NULL;
		source = xmlSaveUri (uri);
		xmlFreeURI (uri);
	}
	
	promptStr = g_strdup_printf ( _("Enter the username and password for \"%s\" (%s):"),
	                             node_get_title (ui_data->subscription->node), source?source:_("Unknown source"));
	gtk_label_set_text (GTK_LABEL (liferea_dialog_lookup (ui_data->dialog, "prompt")), promptStr);
	g_free (promptStr);
	if (source)
		xmlFree (source);
}

static void
auth_dialog_init (AuthDialog *ad)
{
	GtkWidget	*authdialog;
	
	ad->priv = AUTH_DIALOG_GET_PRIVATE (ad);
	
	ad->priv->dialog = authdialog = liferea_dialog_new ("auth.ui", "authdialog");
	ad->priv->username = liferea_dialog_lookup (authdialog, "usernameEntry");
	ad->priv->password = liferea_dialog_lookup (authdialog, "passwordEntry");
	
	g_signal_connect (G_OBJECT (authdialog), "response", G_CALLBACK (on_authdialog_response), ad);

	gtk_widget_show_all (authdialog);
}


AuthDialog *
auth_dialog_new (subscriptionPtr subscription, gint flags) 
{
	AuthDialog *ad;
	
	if (subscription->activeAuth) {
		debug0 (DEBUG_UPDATE, "Missing/wrong authentication. Skipping, as a dialog is already active.");
		return NULL;
	}
	
	ad = AUTH_DIALOG (g_object_new (AUTH_DIALOG_TYPE, NULL));
	auth_dialog_load(ad, subscription, flags);
	return ad;
}
