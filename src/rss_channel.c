/*
   RSS channel parsing
      
   Note: portions of the original parser code were inspired by
   the feed reader software Rol which is copyrighted by
   
   Copyright (C) 2002 Jonathan Gordon <eru@unknown-days.com>
   
   The major part of this backend/parsing/storing code written by
   
   Copyright (C) 2003 Lars Lindner <lars.lindner@gmx.net>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.
   
   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.
   
   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/

#include <sys/time.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include "conf.h"
#include "support.h"
#include "common.h"
#include "rss_channel.h"

#include "rss_ns.h"
#include "ns_dc.h"
#include "ns_fm.h"
#include "ns_content.h"
#include "ns_slash.h"
#include "ns_syn.h"
#include "ns_admin.h"

#include "htmlview.h"

/* structure for the hashtable callback which itself calls the 
   namespace output handler */
#define OUTPUT_RSS_CHANNEL_NS_HEADER	0
#define	OUTPUT_RSS_CHANNEL_NS_FOOTER	1
#define OUTPUT_ITEM_NS_HEADER		2
#define OUTPUT_ITEM_NS_FOOTER		3
typedef struct {
	gint		type;	
	gpointer	obj;	/* thats either a RSSChannelPtr or a RSSItemPtr 
				   depending on the type value */
} outputRequest;

extern GMutex * entries_lock;	// FIXME
extern GHashTable *entries;	// FIXME

/* to store the RSSNsHandler structs for all supported RDF namespace handlers */
GHashTable	*rss_nslist = NULL;

/* note: the tag order has to correspond with the RSS_CHANNEL_* defines in the header file */
static gchar *channelTagList[] = {	"title",
					"description",
					"link",
					"image",
					"copyright",
					"language",
					"lastBuildDate",
					"pubDate",
					"webMaster",
					"managingEditor",
					"category",
					NULL
				  };

/* prototypes */
void		setRSSFeedProp(gpointer fp, gint proptype, gpointer data);
gpointer 	getRSSFeedProp(gpointer fp, gint proptype);
gpointer	mergeRSSFeed(gpointer old_cp, gpointer new_cp);
gpointer 	loadRSSFeed(gchar *keyprefix, gchar *key);
gpointer 	readRSSFeed(gchar *url);
void		showRSSFeedInfo(gpointer cp);

feedHandlerPtr initRSSFeedHandler(void) {
	feedHandlerPtr	fhp;
	
	if(NULL == (fhp = (feedHandlerPtr)g_malloc(sizeof(struct feedHandler)))) {
		g_error(_("not enough memory!"));
	}
	memset(fhp, 0, sizeof(struct feedHandler));
	
	g_free(rss_nslist);
	rss_nslist = g_hash_table_new(g_str_hash, g_str_equal);
	
	/* register RSS name space handlers */
	if(getNameSpaceStatus(ns_dc_getRSSNsPrefix()))
		g_hash_table_insert(rss_nslist, (gpointer)ns_dc_getRSSNsPrefix(),
					        (gpointer)ns_dc_getRSSNsHandler());
	if(getNameSpaceStatus(ns_fm_getRSSNsPrefix()))
		g_hash_table_insert(rss_nslist, (gpointer)ns_fm_getRSSNsPrefix(),
					        (gpointer)ns_fm_getRSSNsHandler());					    
	if(getNameSpaceStatus(ns_slash_getRSSNsPrefix()))
		g_hash_table_insert(rss_nslist, (gpointer)ns_slash_getRSSNsPrefix(), 
					        (gpointer)ns_slash_getRSSNsHandler());
	if(getNameSpaceStatus(ns_content_getRSSNsPrefix()))
		g_hash_table_insert(rss_nslist, (gpointer)ns_content_getRSSNsPrefix(),
					        (gpointer)ns_content_getRSSNsHandler());
	if(getNameSpaceStatus(ns_syn_getRSSNsPrefix()))
		g_hash_table_insert(rss_nslist, (gpointer)ns_syn_getRSSNsPrefix(),
					        (gpointer)ns_syn_getRSSNsHandler());
	if(getNameSpaceStatus(ns_admin_getRSSNsPrefix()))
		g_hash_table_insert(rss_nslist, (gpointer)ns_admin_getRSSNsPrefix(),
					        (gpointer)ns_admin_getRSSNsHandler());

	/* prepare feed handler structure */
	fhp->loadFeed		= loadRSSFeed;
	fhp->readFeed		= readRSSFeed;
	fhp->mergeFeed		= mergeRSSFeed;
	fhp->removeFeed		= NULL; // FIXME
	fhp->getFeedProp	= getRSSFeedProp;	
	fhp->setFeedProp	= setRSSFeedProp;
	fhp->showFeedInfo	= showRSSFeedInfo;
	
	return fhp;
}

/* method to parse standard tags for the channel element */
static void parseChannel(RSSChannelPtr c, xmlDocPtr doc, xmlNodePtr cur) {
	gchar			*tmp = NULL;
	gchar			*encoding;
	parseChannelTagFunc	fp;
	RSSNsHandler		*nsh;
	int			i;
	
	if((NULL == cur) || (NULL == doc)) {
		g_warning(_("internal error: XML document pointer NULL! This should not happen!\n"));
		return;
	}
		
	cur = cur->xmlChildrenNode;
	while (cur != NULL) {
	
		/* check namespace of this tag */
		if(NULL != cur->ns) {
			if (NULL != cur->ns->prefix) {
				if(NULL != (nsh = (RSSNsHandler *)g_hash_table_lookup(rss_nslist, (gpointer)cur->ns->prefix))) {
					fp = nsh->parseChannelTag;
					if(NULL != fp)
						(*fp)(c, doc, cur);
					cur = cur->next;
					continue;
				} else {
					g_print("unsupported namespace \"%s\"\n", cur->ns->prefix);
				}
			}
		}

		/* check for RDF tags */
		for(i = 0; i < RSS_CHANNEL_MAX_TAG; i++) {
			g_assert(NULL != cur->name);
			if (!xmlStrcmp(cur->name, (const xmlChar *)channelTagList[i])) {
				tmp = c->tags[i];
				if(NULL == (c->tags[i] = g_strdup(xmlNodeListGetString(doc, cur->xmlChildrenNode, 1)))) {
					c->tags[i] = tmp;
				} else {
					g_free(tmp);
				}
			}		
		}
		cur = cur->next;
	}

	/* some postprocessing */
	if(NULL != c->tags[RSS_CHANNEL_TITLE])
		c->tags[RSS_CHANNEL_TITLE] = unhtmlize((gchar *)doc->encoding, c->tags[RSS_CHANNEL_TITLE]);
		
	if(NULL != c->tags[RSS_CHANNEL_DESCRIPTION])
		c->tags[RSS_CHANNEL_DESCRIPTION] = convertToHTML((gchar *)doc->encoding, c->tags[RSS_CHANNEL_DESCRIPTION]);		
	
}

static void parseImage(xmlDocPtr doc, xmlNodePtr cur, RSSChannelPtr cp) {

	if((NULL == cur) || (NULL == doc)) {
		g_warning(_("internal error: XML document pointer NULL! This should not happen!\n"));
		return;
	}
	
	if(NULL == cp) {
		g_warning(_("internal error: parseImage without a channel! Skipping image!\n"));
		return;
	}
		
	cur = cur->xmlChildrenNode;
	while (cur != NULL) {
		if (!xmlStrcmp(cur->name, (const xmlChar *)"url"))
			cp->tags[RSS_CHANNEL_IMAGE] = g_strdup(xmlNodeListGetString(doc, cur->xmlChildrenNode, 1));
			
		cur = cur->next;
	}
}

/* loads a saved RSS feed from disk */
gpointer loadRSSFeed(gchar *keyprefix, gchar *key) {
	RSSChannelPtr	new_cp = NULL;

	// workaround as long loading is not implemented
	if(NULL == (new_cp = (RSSChannelPtr) malloc(sizeof(struct RSSChannel)))) {
		g_error("not enough memory!\n");
		return NULL;
	}

	memset(new_cp, 0, sizeof(struct RSSChannel));
	new_cp->updateInterval = -1;
	new_cp->updateCounter = 0;	/* to enforce immediate reload */
	new_cp->type = FST_RSS;
	
	return (gpointer)new_cp;
}

/* reads a RSS feed URL and returns a new channel structure (even if
   the feed could not be read) */
gpointer readRSSFeed(gchar *url) {
	xmlDocPtr 	doc;
	xmlNodePtr 	cur;
	RSSItemPtr 	ip;
	RSSChannelPtr 	cp;
	gchar		*encoding;
	short 		rdf = 0;
	int 		error = 0;
	
	/* initialize channel structure */
	if(NULL == (cp = (RSSChannelPtr) malloc(sizeof(struct RSSChannel)))) {
		g_error("not enough memory!\n");
		return NULL;
	}
	memset(cp, 0, sizeof(struct RSSChannel));
	cp->nsinfos = g_hash_table_new(g_str_hash, g_str_equal);		
	
	cp->updateCounter = cp->defaultUpdateInterval = cp->updateInterval = -1;
	cp->key = NULL;	
	cp->items = NULL;
	cp->available = FALSE;
	cp->source = g_strdup(url);
	cp->type = FST_RSS;
	
	while(1) {
		print_status(g_strdup_printf(_("reading from %s"),url));
		
		doc = xmlParseFile(url);

		if(NULL == doc) {
			print_status(g_strdup_printf(_("XML error wile reading feed! Feed \"%s\" could not be loaded!"), url));
			error = 1;
			break;
		}

		cur = xmlDocGetRootElement(doc);

		if(NULL == cur) {
			print_status(_("Empty document! Feed was not added!"));
			xmlFreeDoc(doc);
			error = 1;
			break;			
		}

		if (!xmlStrcmp(cur->name, (const xmlChar *)"rss")) {
			rdf = 0;
		} else if (!xmlStrcmp(cur->name, (const xmlChar *)"rdf") || 
                	   !xmlStrcmp(cur->name, (const xmlChar *)"RDF")) {
			rdf = 1;
		} else {
			print_status(_("Could not find RDF/RSS header! Feed was not added!"));
			xmlFreeDoc(doc);
			error = 1;
			break;			
		}

		cur = cur->xmlChildrenNode;
		while(cur && xmlIsBlankNode(cur)) {
			cur = cur->next;
		}
	
		while (cur != NULL) {
			if ((!xmlStrcmp(cur->name, (const xmlChar *) "channel"))) {
				parseChannel(cp, doc, cur);
				g_assert(NULL != cur);
				if(0 == rdf)
					cur = cur->xmlChildrenNode;
				break;
			}
			g_assert(NULL != cur);
			cur = cur->next;
		}

		cp->time = getActualTime();
		cp->encoding = g_strdup(doc->encoding);
		cp->available = TRUE;

		/* parse channel contents */
		while (cur != NULL) {
			/* save link to channel image */
			if ((!xmlStrcmp(cur->name, (const xmlChar *) "image"))) {
				parseImage(doc, cur, cp);
			}

			/* collect channel items */
			if ((!xmlStrcmp(cur->name, (const xmlChar *) "item"))) {
				if(NULL != (ip = (RSSItemPtr)parseItem(doc, cur))) {
					cp->unreadCounter++;
					ip->cp = cp;
					if(NULL == ip->time)
						ip->time = cp->time;
					ip->next = NULL;
					cp->items = g_slist_append(cp->items, ip);
				}
			}
			cur = cur->next;
		}

		xmlFreeDoc(doc);
		break;
	}

	return cp;
}


/* used to merge two RSSChannelPtr structures after while
   updating a feed, returns a RSSChannelPtr to the merged
   structure and frees (FIXME) all unneeded memory */
gpointer mergeRSSFeed(gpointer old_fp, gpointer new_fp) {
	RSSChannelPtr	new = (RSSChannelPtr) new_fp;
	RSSChannelPtr	old = (RSSChannelPtr) old_fp;
		
	// FIXME: compare items, merge appropriate
	// actually this function does almost nothing
	
	new->updateInterval = old->updateInterval;
	new->updateCounter = old->updateInterval;	/* resetting the counter */
	new->usertitle = old->usertitle;
	new->key = old->key;
	new->source = old->source;
	new->type = old->type;
	new->keyprefix = old->keyprefix;
	
	// FIXME: free old_cp memory
		
	return new_fp;
}

/* ---------------------------------------------------------------------------- */
/* HTML output stuff	 							*/
/* ---------------------------------------------------------------------------- */

/* method called by g_hash_table_foreach from inside the HTML
   generator functions to output namespace specific infos 
   
   not static because its reused by rss_item.c */
void showRSSFeedNSInfo(gpointer key, gpointer value, gpointer userdata) {
	outputRequest	*request = (outputRequest *)userdata;
	RSSNsHandler	*nsh = (RSSNsHandler *)value;
	outputFunc	fp;

	switch(request->type) {
		case OUTPUT_RSS_CHANNEL_NS_HEADER:
			fp = nsh->doChannelHeaderOutput;
			if(NULL != fp)
				(*fp)(request->obj);
			break;
		case OUTPUT_RSS_CHANNEL_NS_FOOTER:
			fp = nsh->doChannelFooterOutput;
			if(NULL != fp)
				(*fp)(request->obj);
			break;
		case OUTPUT_ITEM_NS_HEADER:
			fp = nsh->doItemHeaderOutput;
			if(NULL != fp)
				(*fp)(request->obj);
			break;		
		case OUTPUT_ITEM_NS_FOOTER:
			fp = nsh->doItemFooterOutput;
			if(NULL != fp)
				(*fp)(request->obj);
			break;			
		default:	
			g_warning(_("Internal error! Invalid output request mode for namespace information!"));
			break;		
	}
}

/* writes RSS channel description as HTML into the gtkhtml widget */
void showRSSFeedInfo(gpointer fp) {
	RSSChannelPtr	cp = (RSSChannelPtr)fp;
	gchar		*feedimage;
	gchar		*feeddescription;
	gchar		*tmp;	
	outputRequest	request;

	g_assert(cp != NULL);	
	
	startHTMLOutput();
	writeHTML(HTML_HEAD_START);

	writeHTML(META_ENCODING1);
	writeHTML("UTF-8");
	writeHTML(META_ENCODING2);

	writeHTML(HTML_HEAD_END);

	writeHTML(FEED_HEAD_START);
	
	writeHTML(FEED_HEAD_CHANNEL);
	tmp = g_strdup_printf("<a href=\"%s\">%s</a>",
		cp->tags[RSS_CHANNEL_LINK],
		getDefaultEntryTitle(cp->key));
	writeHTML(tmp);
	g_free(tmp);
	
	writeHTML(HTML_NEWLINE);	

	writeHTML(FEED_HEAD_SOURCE);
	tmp = g_strdup_printf("<a href=\"%s\">%s</a>", cp->source, cp->source);
	writeHTML(tmp);
	g_free(tmp);

	writeHTML(FEED_HEAD_END);	
		
	/* process namespace infos */
	request.obj = (gpointer)cp;
	request.type = OUTPUT_RSS_CHANNEL_NS_HEADER;	
	if(NULL != rss_nslist)
		g_hash_table_foreach(rss_nslist, showRSSFeedNSInfo, (gpointer)&request);

	if(NULL != (feedimage = cp->tags[RSS_CHANNEL_IMAGE])) {
		writeHTML(IMG_START);
		writeHTML(feedimage);
		writeHTML(IMG_END);	
	}

	if(NULL != (feeddescription = cp->tags[RSS_CHANNEL_DESCRIPTION]))
		writeHTML(feeddescription);

	writeHTML(FEED_FOOT_TABLE_START);
	FEED_FOOT_WRITE(doc, "language",		cp->tags[RSS_CHANNEL_LANGUAGE]);
	FEED_FOOT_WRITE(doc, "copyright",		cp->tags[RSS_CHANNEL_COPYRIGHT]);
	FEED_FOOT_WRITE(doc, "last build date",		cp->tags[RSS_CHANNEL_LASTBUILDDATE]);
	FEED_FOOT_WRITE(doc, "publication date",	cp->tags[RSS_CHANNEL_PUBDATE]);
	FEED_FOOT_WRITE(doc, "webmaster",		cp->tags[RSS_CHANNEL_WEBMASTER]);
	FEED_FOOT_WRITE(doc, "managing editor",		cp->tags[RSS_CHANNEL_MANAGINGEDITOR]);
	FEED_FOOT_WRITE(doc, "category",		cp->tags[RSS_CHANNEL_CATEGORY]);
	writeHTML(FEED_FOOT_TABLE_END);
	
	/* process namespace infos */
	request.type = OUTPUT_RSS_CHANNEL_NS_FOOTER;
	if(NULL != rss_nslist)
		g_hash_table_foreach(rss_nslist, showRSSFeedNSInfo, (gpointer)&request);

	finishHTMLOutput();
}

/* ---------------------------------------------------------------------------- */
/* just some encapsulation 							*/
/* ---------------------------------------------------------------------------- */

void setRSSFeedProp(gpointer fp, gint proptype, gpointer data) {
	RSSChannelPtr	c = (RSSChannelPtr)fp;
	
	if(NULL != c) {
		g_assert(FST_RSS == c->type);
		switch(proptype) {
			case FEED_PROP_TITLE:
				g_free(c->tags[RSS_CHANNEL_TITLE]);
				c->tags[RSS_CHANNEL_TITLE] = (gchar *)data;
				break;
			case FEED_PROP_USERTITLE:
				g_free(c->usertitle);
				c->usertitle = (gchar *)data;
				break;
			case FEED_PROP_SOURCE:
				g_free(c->source);
				c->source = (gchar *)data;
				break;
			case FEED_PROP_DFLTUPDINTERVAL:
				c->defaultUpdateInterval = (gint)data;
				break;
			case FEED_PROP_UPDATEINTERVAL:
				c->updateInterval = (gint)data;
				break;
			case FEED_PROP_UPDATECOUNTER:
				c->updateCounter = (gint)data;
				break;
			case FEED_PROP_UNREADCOUNT:
				c->unreadCounter = (gint)data;
				break;
			case FEED_PROP_AVAILABLE:
				c->available = (gboolean)data;
				break;
			case FEED_PROP_ITEMLIST:
				g_error("please don't do this!");
				break;
			default:
				g_error(g_strdup_printf(_("intenal error! unknow feed property type %d!\n"), proptype));
				break;
		}
	}
}

gpointer getRSSFeedProp(gpointer fp, gint proptype) {
	RSSChannelPtr	c = (RSSChannelPtr)fp;

	if(NULL != c) {
		g_assert(FST_RSS == c->type);
		switch(proptype) {
			case FEED_PROP_TITLE:
				return (gpointer)c->tags[RSS_CHANNEL_TITLE];
				break;
			case FEED_PROP_USERTITLE:
				return (gpointer)c->usertitle;
				break;
			case FEED_PROP_SOURCE:
				return (gpointer)c->source;
				break;
			case FEED_PROP_DFLTUPDINTERVAL:
				return (gpointer)c->defaultUpdateInterval;
				break;
			case FEED_PROP_UPDATEINTERVAL:
				return (gpointer)c->updateInterval;
				break;
			case FEED_PROP_UPDATECOUNTER:
				return (gpointer)c->updateCounter;
				break;
			case FEED_PROP_UNREADCOUNT:
				return (gpointer)c->unreadCounter;
				break;
			case FEED_PROP_AVAILABLE:
				return (gpointer)c->available;
				break;
			case FEED_PROP_ITEMLIST:
				return (gpointer)c->items;
				break;
			default:
				g_error(g_strdup_printf(_("intenal error! unknow feed property type %d!\n"), proptype));
				break;
		}
	} else {
		return NULL;
	}
}
