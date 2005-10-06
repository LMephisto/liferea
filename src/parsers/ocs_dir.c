/**
 * @file ocs_dir.c OCS 0.4/0.5 support 
 *
 * Note: this module contains only the rdf specific OCS parsing, 
 * the dc and ocs namespaces are processed by the specific namespace 
 * handlers!
 * 
 * Copyright (C) 2003, 2004 Lars Lindner <lars.lindner@gmx.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 * 
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include "support.h"
#include "common.h"
#include "feed.h"
#include "item.h"
#include "ocs_ns.h"
#include "ocs_dir.h"
#include "callbacks.h"
#include "ns_dc.h"
#include "ns_ocs.h"

/* you can find the OCS specification at

   http://internetalchemy.org/ocs/directory.html 
 */
 
/* to store the nsHandler structs for all supported RDF namespace handlers */
GHashTable	*ocs_nslist = NULL;

/* print information of a format entry in the HTML */
static void showFormatEntry(gpointer data, gpointer userdata) {
	gchar		*tmp;
	gchar		**buffer = (gchar **)userdata;
	formatPtr	f = (formatPtr)data;
	
	if(NULL != f->source) {
		addToHTMLBuffer(buffer, FORMAT_START);

		tmp = g_strdup_printf("<a href=\"%s\">%s</a>", f->source, f->source);
		addToHTMLBuffer(buffer, FORMAT_LINK);
		addToHTMLBuffer(buffer, tmp);		
		g_free(tmp);

		if(NULL != (tmp = f->tags[OCS_LANGUAGE])) {
			addToHTMLBuffer(buffer, FORMAT_LANGUAGE);
			addToHTMLBuffer(buffer, tmp);
		}

		if(NULL != (tmp = f->tags[OCS_UPDATEPERIOD])) {
			addToHTMLBuffer(buffer, FORMAT_UPDATEPERIOD);
			addToHTMLBuffer(buffer, tmp);
		}

		if(NULL != (tmp = f->tags[OCS_UPDATEFREQUENCY])) {
			addToHTMLBuffer(buffer, FORMAT_UPDATEFREQUENCY);
			addToHTMLBuffer(buffer, tmp);
		}
		
		if(NULL != (tmp = f->tags[OCS_CONTENTTYPE])) {
			addToHTMLBuffer(buffer, FORMAT_CONTENTTYPE);
			addToHTMLBuffer(buffer, tmp);
		}
		
		addToHTMLBuffer(buffer, FORMAT_END);	
	}
}

/* display a directory entry description and its formats in the HTML widget */
static gchar * showDirEntry(dirEntryPtr dep) {
	directoryPtr	dp = dep->dp;
	gchar		*tmp, *line, *buffer = NULL;
	
	g_assert(dep != NULL);
	g_assert(dp != NULL);

	if(NULL != dep->source) {
		addToHTMLBuffer(&buffer, HEAD_START);
		line = g_strdup_printf(HEAD_LINE, _("Feed"), dp->tags[OCS_TITLE]);
		addToHTMLBuffer(&buffer, line);
		g_free(line);
		
		tmp = g_strdup_printf("<a href=\"%s\">%s</a>", dep->source, dep->tags[OCS_TITLE]);
		line = g_strdup_printf(HEAD_LINE, _("Item:"), tmp);
		g_free(tmp);
		addToHTMLBuffer(&buffer, line);
		g_free(line);
		
		addToHTMLBuffer(&buffer, HEAD_END);	
	}

	if(NULL != dep->tags[OCS_IMAGE]) {
		addToHTMLBuffer(&buffer, IMG_START);
		addToHTMLBuffer(&buffer, dep->tags[OCS_IMAGE]);
		addToHTMLBuffer(&buffer, IMG_END);	
	}

	if(NULL != dep->tags[OCS_DESCRIPTION])
		addToHTMLBuffer(&buffer, dep->tags[OCS_DESCRIPTION]);
		
	/* output infos about the available formats */
	g_slist_foreach(dep->formats, showFormatEntry, &buffer);

	addToHTMLBuffer(&buffer, FEED_FOOT_TABLE_START);
	FEED_FOOT_WRITE(buffer, "creator",	dep->tags[OCS_CREATOR]);
	FEED_FOOT_WRITE(buffer, "subject",	dep->tags[OCS_SUBJECT]);
	FEED_FOOT_WRITE(buffer, "language",	dep->tags[OCS_LANGUAGE]);
	FEED_FOOT_WRITE(buffer, "updatePeriod",	dep->tags[OCS_UPDATEPERIOD]);
	FEED_FOOT_WRITE(buffer, "contentType",	dep->tags[OCS_CONTENTTYPE]);
	addToHTMLBuffer(&buffer, FEED_FOOT_TABLE_END);
	
	return buffer;
}

/* writes directory info as HTML */
static gchar * showDirectoryInfo(directoryPtr dp, gchar *url) {
	gchar		*tmp, *line, *buffer = NULL;	

	g_assert(dp != NULL);	

	addToHTMLBuffer(&buffer, HEAD_START);	

	line = g_strdup_printf(HEAD_LINE, _("Feed:"), dp->tags[OCS_TITLE]);
	addToHTMLBuffer(&buffer, line);
	g_free(line);

	if(NULL != url) {
		tmp = g_strdup_printf("<a href=\"%s\">%s</a>", url, url);
		line = g_strdup_printf(HEAD_LINE, _("Source:"), tmp);
		g_free(tmp);
		addToHTMLBuffer(&buffer, line);
		g_free(line);
	}

	addToHTMLBuffer(&buffer, HEAD_END);	

	if(NULL != dp->tags[OCS_DESCRIPTION])
		addToHTMLBuffer(&buffer, dp->tags[OCS_DESCRIPTION]);

	addToHTMLBuffer(&buffer, FEED_FOOT_TABLE_START);
	FEED_FOOT_WRITE(buffer, "creator",	dp->tags[OCS_CREATOR]);
	FEED_FOOT_WRITE(buffer, "subject",	dp->tags[OCS_SUBJECT]);
	FEED_FOOT_WRITE(buffer, "language",	dp->tags[OCS_LANGUAGE]);
	FEED_FOOT_WRITE(buffer, "updatePeriod",	dp->tags[OCS_UPDATEPERIOD]);
	FEED_FOOT_WRITE(buffer, "contentType",	dp->tags[OCS_CONTENTTYPE]);
	addToHTMLBuffer(&buffer, FEED_FOOT_TABLE_END);
	
	return buffer;
}

static void parseFormatEntry(formatPtr fep, xmlNodePtr cur) {
	parseOCSTagFunc		fp;
	OCSNsHandler		*nsh;
	int			i;
	
	g_assert(NULL != cur);
	g_assert(NULL != cur->doc);

	cur = cur->xmlChildrenNode;
	while(cur != NULL) {
		if(NULL == cur->name) {
			g_warning("invalid XML: parser returns NULL value -> tag ignored!");
			cur = cur->next;
			continue;
		}
		
		/* check namespace of this tag */
		if(NULL != cur->ns) {
			if(NULL != cur->ns->prefix) {	
				if(!xmlStrcmp(cur->ns->prefix, BAD_CAST"rdf")) {				
					g_warning("unexpected OCS hierarchy, this should never happen! ignoring third level rdf tags!\n");
					
				} else {
					g_assert(NULL != ocs_nslist);
					if(NULL != (nsh = (OCSNsHandler *)g_hash_table_lookup(ocs_nslist, (gpointer)cur->ns->prefix))) {
						fp = nsh->parseFormatTag;
						if(NULL != fp)
							(*fp)(fep, cur);
						else
							g_print(_("no namespace handler for <%s:%s>!\n"), cur->ns->prefix, cur->name);
					}
				}
			}
		}
		cur = cur->next;
	}

	/* some postprocessing, all format-infos will be displayed in the HTML view */
	for(i = 0; i < OCS_MAX_TAG; i++)
		if(NULL != fep->tags[i])
			fep->tags[i] = convertToHTML(fep->tags[i]);

}

static itemPtr parse05DirectoryEntry(dirEntryPtr dep, xmlNodePtr cur) {
	xmlNodePtr		tmpNode, formatNode;
	formatPtr		new_fp;	
	parseOCSTagFunc		fp;
	OCSNsHandler		*nsh;
	itemPtr			ip;
	gboolean		found;
	
	g_assert(NULL != cur);
	g_assert(NULL != cur->doc);
	ip = item_new();

	cur = cur->xmlChildrenNode;
	while(cur != NULL) {
		if(NULL == cur->name) {
			g_warning("invalid XML: parser returns NULL value -> tag ignored!");
			cur = cur->next;
			continue;
		}
				
		/* check namespace of this tag */
		if(NULL != cur->ns) {
			if(NULL != cur->ns->prefix) {	
				g_assert(NULL != ocs_nslist);
				if(NULL != (nsh = (OCSNsHandler *)g_hash_table_lookup(ocs_nslist, (gpointer)cur->ns->prefix))) {

					fp = nsh->parseDirEntryTag;
					if(NULL != fp) {
						(*fp)(dep, cur);
					} else
						g_print(_("no namespace handler for <%s:%s>!\n"), cur->ns->prefix, cur->name);

				}
			}
		}

		if(!xmlStrcmp(cur->name, BAD_CAST"formats")) {
			found = FALSE;
			tmpNode = cur->xmlChildrenNode;
			while(NULL != tmpNode) {
				if(!xmlStrcmp(tmpNode->name, BAD_CAST"Alt")) {
					found = TRUE;
					break;
				}
				tmpNode = tmpNode->next;
			}
					
			if(found) {
				found = FALSE;
				tmpNode = tmpNode->xmlChildrenNode;
				while(NULL != tmpNode) {
					if(!xmlStrcmp(tmpNode->name, BAD_CAST"li")) {
						found = TRUE;
						break;
					}
					tmpNode = tmpNode->next;
				}
			}

			/* FIXME: something remembers me to use XPath or something... :-) */
			if(found) {
				found = FALSE;
				tmpNode = tmpNode->xmlChildrenNode;
				while(NULL != tmpNode) {
					if(!xmlStrcmp(tmpNode->name, BAD_CAST"Description")) {
						/* now search for <format> nodes... */
						formatNode = tmpNode->xmlChildrenNode;
						while(NULL != formatNode) {
							if(!xmlStrcmp(formatNode->name, BAD_CAST"format")) {
								new_fp = g_new0(struct format, 1);
								new_fp->source = utf8_fix(xmlGetProp(tmpNode, BAD_CAST"about"));
								new_fp->tags[OCS_CONTENTTYPE] = utf8_fix(xmlGetProp(formatNode, BAD_CAST"resource"));
								dep->formats = g_slist_append(dep->formats, (gpointer)new_fp);
							}
							formatNode = formatNode->next;
						}
					}
					tmpNode = tmpNode->next;
				}
			}
		}
		cur = cur->next;
	}

	/* after parsing we fill the infos into the itemPtr structure */
	item_set_source(ip, dep->source);
	ip->readStatus = TRUE;
	item_set_id(ip, NULL);

	/* some postprocessing */
	if(NULL != dep->tags[OCS_TITLE])
		dep->tags[OCS_TITLE] = unhtmlize(dep->tags[OCS_TITLE]);
		
	if(NULL != dep->tags[OCS_DESCRIPTION])
		dep->tags[OCS_DESCRIPTION] = convertToHTML(dep->tags[OCS_DESCRIPTION]);

	item_set_title(ip, dep->tags[OCS_TITLE]);		
	item_set_description(ip, showDirEntry(dep));
	/* FIXME: free formats! */
	g_slist_free(dep->formats);
	g_free(dep);
		
	return ip;
}

static itemPtr parse04DirectoryEntry(dirEntryPtr dep, xmlNodePtr cur) {
	formatPtr		new_fp;	
	parseOCSTagFunc		fp;
	OCSNsHandler		*nsh;
	itemPtr			ip;
	
	g_assert(NULL != cur);
	g_assert(NULL != cur->doc);
	ip = item_new();

	cur = cur->xmlChildrenNode;
	while(cur != NULL) {
		if(NULL == cur->name) {
			g_warning("invalid XML: parser returns NULL value -> tag ignored!");
			cur = cur->next;
			continue;
		}
				
		/* check namespace of this tag */
		if(NULL != cur->ns) {
			if(NULL != cur->ns->prefix) {	
				if(!xmlStrcmp(cur->ns->prefix, BAD_CAST"rdf")) {

					/* check for <rdf:description> tags, if we find one, this means
					   a new format for the actual channel */
					if(!xmlStrcmp(cur->name, BAD_CAST"description")) {
						new_fp = g_new0(struct format, 1);
						new_fp->source = utf8_fix(xmlGetProp(cur, BAD_CAST"about"));
						parseFormatEntry(new_fp, cur);
						dep->formats = g_slist_append(dep->formats, (gpointer)new_fp);
					}		
					
		
				} else {
					g_assert(NULL != ocs_nslist);
					if(NULL != (nsh = (OCSNsHandler *)g_hash_table_lookup(ocs_nslist, (gpointer)cur->ns->prefix))) {
						fp = nsh->parseDirEntryTag;
						if(NULL != fp)
							(*fp)(dep, cur);
						else
							g_print(_("no namespace handler for <%s:%s>!\n"), cur->ns->prefix, cur->name);

					}
				}
			}
		}
		cur = cur->next;
	}

	/* after parsing we fill the infos into the itemPtr structure */
	item_set_source(ip, dep->source);
	ip->readStatus = TRUE;
	item_set_id(ip, NULL);

	/* some postprocessing */
	if(NULL != dep->tags[OCS_TITLE])
		dep->tags[OCS_TITLE] = unhtmlize(dep->tags[OCS_TITLE]);
		
	if(NULL != dep->tags[OCS_DESCRIPTION])
		dep->tags[OCS_DESCRIPTION] = convertToHTML(dep->tags[OCS_DESCRIPTION]);

	item_set_title(ip, dep->tags[OCS_TITLE]);		
	item_set_description(ip, showDirEntry(dep));
	/* FIXME: free formats! */
	g_slist_free(dep->formats);
	g_free(dep);
		
	return ip;
}
 
/* ocsVersion is 4 for 0.4, 5 for 0.5 ... */
static void parseDirectory(GList **items, directoryPtr dp, xmlNodePtr cur, gint ocsVersion) {
	parseOCSTagFunc		parseFunc;
	dirEntryPtr		new_dep;
	OCSNsHandler		*nsh;
	itemPtr			ip;
	
	g_assert(NULL != cur);
	g_assert(NULL != cur->doc);

	cur = cur->xmlChildrenNode;
	while(cur != NULL) {
		if(NULL == cur->name) {
			g_warning("invalid XML: parser returns NULL value -> tag ignored!");
			cur = cur->next;
			continue;
		}

		/* check namespace of this tag */
		if(NULL != cur->ns) {
			if(NULL != cur->ns->prefix) {	
				if((0 == xmlStrcmp(cur->ns->prefix, BAD_CAST"rdf")) && (4 >= ocsVersion)) {

					/* check for <rdf:description tags, if we find one this
					   means a new channel description */
					if(!xmlStrcmp(cur->name, BAD_CAST"description")) {
						new_dep = g_new0(struct dirEntry, 1);
						new_dep->source = utf8_fix(xmlGetProp(cur, BAD_CAST"about"));
						new_dep->dp = dp;
						ip = parse04DirectoryEntry(new_dep, cur);
						*items = g_list_append(*items, ip);
					}
		
				} else {
					g_assert(NULL != ocs_nslist);
					if(NULL != (nsh = (OCSNsHandler *)g_hash_table_lookup(ocs_nslist, (gpointer)cur->ns->prefix))) {
						parseFunc = nsh->parseDirectoryTag;
						if(NULL != parseFunc)
							(*parseFunc)(dp, cur);
						else
							g_print(_("no namespace handler for <%s:%s>!\n"), cur->ns->prefix, cur->name);

					}
				}
			}
		}
		cur = cur->next;
	}

	/* some postprocessing */
	if(NULL != dp->tags[OCS_TITLE])
		dp->tags[OCS_TITLE] = unhtmlize(dp->tags[OCS_TITLE]);
		
	if(NULL != dp->tags[OCS_DESCRIPTION])
		dp->tags[OCS_DESCRIPTION] = convertToHTML(dp->tags[OCS_DESCRIPTION]);
}

static void ocs_parse(feedPtr fp, xmlDocPtr doc, xmlNodePtr cur ) {
	directoryPtr	dp = NULL;
	dirEntryPtr	new_dep;
	GList		*items = NULL;
	int 		error = 0;

	do {
		if(!xmlStrcmp(cur->name, BAD_CAST"rdf") || 
                   !xmlStrcmp(cur->name, BAD_CAST"RDF")) {
		    	/* nothing */
		} else {
			addToHTMLBuffer(&(fp->parseErrors), _("<p>Could not find RDF header!</p>"));
			error = 1;
			break;			
		}

		cur = cur->xmlChildrenNode;
		while(cur && xmlIsBlankNode(cur)) {
			cur = cur->next;
		}
		
		while(cur != NULL) {
			if(NULL == cur->name) {
				g_warning("invalid XML: parser returns NULL value -> tag ignored!");
				cur = cur->next;
				continue;
			}

			/* handling OCS 0.5 directory tag... */
			if(!xmlStrcmp(cur->name, BAD_CAST"directory")) {
				dp = g_new0(struct directory, 1);
				parseDirectory(&items, dp, cur, 5);
			}
			/* handling OCS 0.5 channel tag... */
			else if(!xmlStrcmp(cur->name, BAD_CAST"channel")) {
				new_dep = g_new0(struct dirEntry, 1);
				new_dep->source = utf8_fix(xmlGetProp(cur, "about"));
				new_dep->dp = dp;					
				items = g_list_append(items, parse05DirectoryEntry(new_dep, cur));
			}
			/* handling OCS 0.4 top level description tag... */
			else if(!xmlStrcmp(cur->name, BAD_CAST"description")) {
				dp = g_new0(struct directory, 1);
				parseDirectory(&items, dp, cur, 4);
				break;
			}
			cur = cur->next;
		}

		/* after parsing we fill in the infos into the feedPtr structure */		
		feed_add_items(fp, items);
		feed_set_update_interval(fp, -1);
		fp->title = dp->tags[OCS_TITLE];
		
		if(0 == error) {
			fp->description = showDirectoryInfo(dp, fp->source);
			feed_set_available(fp, TRUE);
		} else {
			ui_mainwindow_set_status_bar(_("There were errors while parsing this feed!"));
			fp->title = g_strdup(fp->source);
		}
			
		g_free(dp);
	} while (FALSE);
}

gboolean ocs_format_check(xmlDocPtr doc, xmlNodePtr cur) {
	gboolean ocs = FALSE;
	
	if(!xmlStrcmp(cur->name, BAD_CAST"rdf") || 
	   !xmlStrcmp(cur->name, BAD_CAST"RDF")) {
		xmlNs * ns = cur->nsDef;

		while (ns != NULL) {
			if (!xmlStrcmp(ns->prefix, "ocs") ||
			    !xmlStrcmp(ns->href, "http://InternetAlchemy.org/ocs/directory#") ||
			    !xmlStrcmp(ns->href, "http://purl.org/ocs/directory/0.5/#")) {
				ocs = TRUE;
			}
			ns = ns->next;
		}
		if (ocs == FALSE) {
			xmlNodePtr child = cur->xmlChildrenNode;
			while (child != NULL) {
				if (child->type == XML_ELEMENT_NODE &&
				    child->name != NULL &&
				    !xmlStrcmp(child->name, "directory"))
					ocs = TRUE;
				child = child->next;
			}
		}
	}

	return ocs;
}

feedHandlerPtr ocs_init_feed_handler(void) {
	feedHandlerPtr	fhp;
	OCSNsHandler	*handler;
	
	fhp = g_new0(struct feedHandler, 1);

	g_free(ocs_nslist);
	ocs_nslist = g_hash_table_new(g_str_hash, g_str_equal);
	
	/* register OCS name space handlers */
	handler = ns_dc_getOCSNsHandler();
	g_hash_table_insert(ocs_nslist, handler->prefix, (gpointer)handler);
	handler = ns_ocs_getOCSNsHandler();
	g_hash_table_insert(ocs_nslist, handler->prefix, (gpointer)handler);

	/* prepare feed handler structure */
	fhp->typeStr = "ocs";
	fhp->icon = ICON_OCS;
	fhp->directory = TRUE;
	fhp->feedParser	= ocs_parse;
	fhp->checkFormat = ocs_format_check;
	fhp->merge = FALSE;
	
	return fhp;
}