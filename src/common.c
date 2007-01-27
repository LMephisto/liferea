/**
 * @file common.c common routines for Liferea
 * 
 * Copyright (C) 2003-2006  Lars Lindner <lars.lindner@gmx.net>
 * Copyright (C) 2004-2006  Nathan J. Conrad <t98502@users.sourceforge.net>
 * Copyright (C) 2004       Karl Soderstrom <ks@xanadunet.net>
 *
 * parts of the RFC822 timezone decoding were reused from the gmime source
 *
 *    Authors: Michael Zucchi <notzed@helixcode.com>
 *             Jeffrey Stedfast <fejj@helixcode.com>
 *
 *    Copyright 2000 Helix Code, Inc. (www.helixcode.com)
 * 
 * The date formatting was reused from the Evolution code base
 *
 *    Author: Chris Lahey <clahey@ximian.com
 *
 *    Copyright 2001, Ximian, Inc.
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#define _XOPEN_SOURCE /* glibc2 needs this (man strptime) */

#include <libxml/xmlerror.h>
#include <libxml/uri.h>
#include <libxml/parser.h>
#include <libxml/entities.h>
#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <pango/pango-types.h>
#include <gtk/gtk.h>

#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <locale.h>
#include <time.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>	/* fsync */

#include "support.h"
#include "common.h"
#include "conf.h"
#include "feed.h"
#include "support.h"
#include "debug.h"

static gchar *lifereaUserPath = NULL;

static void common_buffer_parse_error(void *ctxt, const gchar * msg, ...);

/* Conversion function which should be applied to all read XML strings, 
   to ensure proper UTF8. This is because we use libxml2 in recovery
   mode which can produce invalid UTF-8. 
   
   The valid or a corrected string is returned. The original XML 
   string is modified (FIXME: not sure if this is good). */
gchar * common_utf8_fix(xmlChar *string) {
	const gchar	*invalid_offset;

	if(NULL == string)
		return NULL;
		
	if(!g_utf8_validate(string, -1, &invalid_offset)) {
		/* if we have an invalid string we try to shorten
		   it until it is valid UTF-8 */
		debug0(DEBUG_PARSING, "parser delivered invalid UTF-8!");
		debug1(DEBUG_PARSING, "	>>>%s<<<\n", string);
		debug1(DEBUG_PARSING, "first invalid char is: >>>%s<<<\n" , invalid_offset);
		debug0(DEBUG_PARSING, "removing invalid bytes");
		
		do {
			memmove((void *)invalid_offset, invalid_offset + 1, strlen(invalid_offset + 1) + 1);			
		} while(!g_utf8_validate(string, -1, &invalid_offset));
		
		debug0(DEBUG_PARSING, "result is:\n");
		debug1(DEBUG_PARSING, "	>>>%s<<<\n", string);		
	}
	
	return string;
}

long common_parse_long(gchar *str, long def) {
	long num;

	if(str == NULL)
		return def;
	if(0 == (sscanf(str,"%ld",&num)))
		num = def;
	
	return num;
}

static xmlDocPtr common_parse_html(const gchar *html, gint len) {
	xmlDocPtr out = NULL;
	
	g_assert(html != NULL);
	g_assert(len >= 0);
	

	/* Note: NONET is not implemented so it will return an error
	   because it doesn't know how to handle NONET. But, it might
	   learn in the future. */
	out = htmlReadMemory(html, len, NULL, "utf-8", HTML_PARSE_RECOVER | HTML_PARSE_NONET |
	                     ((debug_level & DEBUG_HTML)?0:(HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING)));
	return out;
}

static xmlNodePtr common_html_doc_find_body(xmlDocPtr doc) {
	xmlXPathContextPtr xpathCtxt = NULL;
	xmlXPathObjectPtr xpathObj = NULL;
	xmlNodePtr node = NULL;
	xpathCtxt = xmlXPathNewContext(doc);
	if(!xpathCtxt) goto error;

	xpathObj = xmlXPathEvalExpression("/html/body", xpathCtxt);
	if(!xpathObj) goto error;
	if(!xpathObj->nodesetval->nodeMax) goto error;
	
	node = xpathObj->nodesetval->nodeTab[0];
 error:
	if (xpathObj) xmlXPathFreeObject(xpathObj);
	if (xpathCtxt) xmlXPathFreeContext(xpathCtxt);
	return node;
}

/* Extract XHTML from the children of the passed node. */
gchar * extractHTMLNode(xmlNodePtr cur, gint xhtmlMode, const gchar *defaultBase) {
	xmlBufferPtr	buf;
	gchar		*result = NULL;

	/* Create the new document and add the div tag*/
	xmlDocPtr newDoc = xmlNewDoc( BAD_CAST "1.0" );;
	xmlNodePtr divNode = xmlNewNode(NULL, BAD_CAST "div");;
	xmlDocSetRootElement( newDoc, divNode );
	xmlNewNs(divNode, BAD_CAST "http://www.w3.org/1999/xhtml", NULL);

	/* Set the xml:base  of the div tag */
	if(xmlNodeGetBase(cur->doc, cur))
		xmlNodeSetBase( divNode, xmlNodeGetBase(cur->doc, cur) );
	else if(defaultBase)
		xmlNodeSetBase( divNode, defaultBase);
	
	if(xhtmlMode == 0) { /* Read escaped HTML and convert to XHTML, placing in a div tag */
		xmlDocPtr oldDoc;
		xmlNodePtr copiedNodes = NULL;
		xmlChar *escapedhtml;
		
		/* Parse the HTML into oldDoc*/
		escapedhtml = xmlNodeListGetString(cur->doc, cur->xmlChildrenNode, 1);//xmlNodeDump(tmpBuf, cur->doc, cur, 0, 0);
		if(escapedhtml) {
			escapedhtml = g_strstrip(escapedhtml);	/* stripping whitespaces to make empty string detection easier */
			if(*escapedhtml) {			/* never process empty content, xmlDocCopy() doesn't like it... */
				xmlNodePtr body;
				oldDoc = common_parse_html(escapedhtml, strlen(escapedhtml));
				body = common_html_doc_find_body(oldDoc);

				if(body) {
					/* Copy in the html tags */
					copiedNodes = xmlDocCopyNodeList(newDoc, body->xmlChildrenNode);
					// FIXME: is the above correct? Why only operate on the first child node?
					// It might be unproblematic because all content is wrapped in a <div>...
					xmlAddChildList(divNode, copiedNodes);
				}
				xmlFreeDoc(oldDoc);
				xmlFree(escapedhtml);
			}
		}
	} else if(xhtmlMode == 1 || xhtmlMode == 2){ /* Read multiple XHTML tags and embed in div tag */
		xmlNodePtr copiedNodes = xmlDocCopyNodeList( newDoc, cur->xmlChildrenNode);
		xmlAddChildList(divNode, copiedNodes);
	}
	
	buf = xmlBufferCreate();
	xmlNodeDump(buf, newDoc, xmlDocGetRootElement(newDoc), 0, 0 );
	
	if(xmlBufferLength(buf) > 0)
		result = xmlCharStrdup(xmlBufferContent(buf));

	xmlBufferFree(buf);
	xmlFreeDoc(newDoc);
	return result;
}

gchar * common_strip_dhtml(const gchar *html) {
	gchar *tmp;

	// FIXME: move to XSLT stylesheet that post processes
	// generated XHTML. The solution below might break harmless
	// escaped HTML.
	
	/* remove some nasty DHTML stuff from the given HTML content */
	tmp = g_strdup(html);
	tmp = common_strreplace(tmp, " onload=", " no_onload=");
	tmp = common_strreplace(tmp, " onLoad=", " no_onLoad=");
	tmp = common_strreplace(tmp, "script>", "no_script>");		
	tmp = common_strreplace(tmp, "<script ", "<no_script ");
	tmp = common_strreplace(tmp, "<meta ", "<no_meta ");
	tmp = common_strreplace(tmp, "/meta>", "/no_meta>");
	tmp = common_strreplace(tmp, "<iframe ", "<no_iframe ");
	tmp = common_strreplace(tmp, "/iframe>", "/no_iframe>");
	
	return tmp;
}

/* Convert the given string to proper XHTML content.*/
gchar * common_text_to_xhtml(const gchar *sourceText) {
	gchar		*text, *result = NULL;
	xmlDocPtr	doc = NULL;
	xmlBufferPtr	buf;
	xmlNodePtr 	body, cur;
	
	if(!sourceText)
		return g_strdup("");
	
	text = g_strstrip(g_strdup(sourceText));	/* stripping whitespaces to make empty string detection easier */
	if(*text) {	
		doc = common_parse_html(text, strlen(text));
		
		buf = xmlBufferCreate();
		body = common_html_doc_find_body(doc);
		if(body) {
			cur = body->xmlChildrenNode;
			while(cur) {
				xmlNodeDump(buf, doc, cur, 0, 0 );
				cur = cur->next;
			}
		}

		if(xmlBufferLength(buf) > 0)
			result = xmlCharStrdup(xmlBufferContent(buf));
		else
			result = g_strdup("");

		xmlBufferFree(buf);
		xmlFreeDoc(doc);
		g_free(text);
	} else {
		result = text;
	}
	
	return result;
}

gboolean common_is_well_formed_xhtml(const gchar *data) {
	gchar		*xml;
	gboolean	result;
	errorCtxtPtr	errors;
	xmlDocPtr	doc;

	if(!data)
		return FALSE;
	
	errors = g_new0(struct errorCtxt, 1);
	errors->msg = g_string_new(NULL);

	xml = g_strdup_printf("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\n<test>%s</test>", data);
	
	doc = common_parse_xml(xml, strlen(xml), FALSE, errors);
	if(doc)
		xmlFreeDoc(doc);
		
	g_free(xml);
	g_string_free(errors->msg, TRUE);
	result = (0 == errors->errorCount);
	g_free(errors);
	
	return result;
}

typedef struct {
	gchar	*data;
	gint	length;
} result_buffer;

static void unhtmlizeHandleCharacters (void *user_data, const xmlChar *string, int length) {
	result_buffer	*buffer = (result_buffer *)user_data;
	gint 		old_length;

	old_length = buffer->length;	
	buffer->length += length;
	buffer->data = g_renew(gchar, buffer->data, buffer->length + 1);
        strncpy(buffer->data + old_length, (gchar *)string, length);
	buffer->data[buffer->length] = 0;

}

static void _unhtmlize(gchar *string, result_buffer *buffer) {
	htmlParserCtxtPtr	ctxt;
	htmlSAXHandlerPtr	sax_p = g_new0(htmlSAXHandler, 1);
 	sax_p->characters = unhtmlizeHandleCharacters;
	ctxt = htmlCreatePushParserCtxt(sax_p, buffer, string, strlen(string), "", XML_CHAR_ENCODING_UTF8);
	htmlParseChunk(ctxt, string, 0, 1);
	htmlFreeParserCtxt(ctxt);
 	g_free(sax_p);
}

static void _unxmlize(gchar *string, result_buffer *buffer) {
	xmlParserCtxtPtr	ctxt;
	xmlSAXHandler	*sax_p = g_new0(xmlSAXHandler, 1);
 	sax_p->characters = unhtmlizeHandleCharacters;
	ctxt = xmlCreatePushParserCtxt(sax_p, buffer, string, strlen(string), "");
	xmlParseChunk(ctxt, string, 0, 1);
	xmlFreeParserCtxt(ctxt);
 	g_free(sax_p);
}

/* Converts a UTF-8 strings containing any XML stuff to 
   a string without any entities or tags containing all
   text nodes of the given HTML string. The original 
   string will be freed. */
static gchar * unmarkupize(gchar *string, void(*parse)(gchar *string, result_buffer *buffer)) {
	gchar			*result;
	result_buffer		*buffer;
	
	if(NULL == string)
		return NULL;
		
	string = common_utf8_fix(string);

	/* only do something if there are any entities or tags */
	if(NULL == (strpbrk(string, "&<>")))
		return string;

	buffer = g_new0(result_buffer, 1);
	parse(string, buffer);
	result = buffer->data;
	g_free(buffer);
 
 	if(result == NULL || !g_utf8_strlen(result, -1)) {
 		/* Something went wrong in the parsing.
 		 * Use original string instead */
		g_free(result);
 		return string;
 	} else {
 		g_free(string);
 		return result;
 	}
}

gchar * unhtmlize(gchar * string) { return unmarkupize(string, _unhtmlize); }
gchar * unxmlize(gchar * string) { return unmarkupize(string, _unxmlize); }

#define MAX_PARSE_ERROR_LINES	10

/**
 * Error buffering function to be registered by 
 * xmlSetGenericErrorFunc(). This function is called on
 * each libxml2 error output and collects all output as
 * HTML in the buffer ctxt points to. 
 *
 * @param	ctxt	error context
 * @param	msg	printf like format string 
 */
static void common_buffer_parse_error(void *ctxt, const gchar * msg, ...) {
	va_list		params;
	errorCtxtPtr	errors = (errorCtxtPtr)ctxt;
	gchar		*newmsg;
	gchar		*tmp;
	
	if(MAX_PARSE_ERROR_LINES > errors->errorCount++) {
		
		va_start(params, msg);
		newmsg = g_strdup_vprintf(msg, params);
		va_end(params);

		g_assert(NULL != newmsg);
		newmsg = common_utf8_fix(newmsg);
		tmp = g_markup_escape_text(newmsg, -1);
		g_free(newmsg);
		newmsg = tmp;
	
		g_string_append_printf(errors->msg, "<pre>%s</pre", newmsg);
		g_free(newmsg);
	}
	
	if(MAX_PARSE_ERROR_LINES == errors->errorCount)
		g_string_append_printf(errors->msg, "<br />%s", _("[There were more errors. Output was truncated!]"));
}

static xmlDocPtr entities = NULL;

/* and I thought writing such functions is evil... */
xmlEntityPtr common_process_entities(void *ctxt, const xmlChar *name) {
	xmlEntityPtr	entity, found;
	xmlChar		*tmp;
	
	entity = xmlGetPredefinedEntity(name);
	if(NULL == entity) {
		if(NULL == entities) {
			/* loading HTML entities */
			entities = xmlNewDoc(BAD_CAST "1.0");
			xmlCreateIntSubset(entities, BAD_CAST "HTML entities", NULL, PACKAGE_DATA_DIR "/" PACKAGE "/dtd/html.ent");
			entities->extSubset = xmlParseDTD(entities->intSubset->ExternalID, entities->intSubset->SystemID);
		}
		
		if(NULL != (found = xmlGetDocEntity(entities, name))) {
			/* returning as faked predefined entity... */
			tmp = xmlStrdup(found->content);
			tmp = unhtmlize(tmp);	/* arghh ... slow... */
			entity = (xmlEntityPtr)g_new0(xmlEntity, 1);
			entity->type = XML_ENTITY_DECL;
			entity->name = name;
			entity->orig = NULL;
			entity->content = tmp;
			entity->length = g_utf8_strlen(tmp, -1);
			entity->etype = XML_INTERNAL_PREDEFINED_ENTITY;
		}
	}
	if(NULL == entity) {		
		g_print("unsupported entity: %s\n", name);
	}
	return entity;
}

xmlNodePtr common_xpath_find(xmlNodePtr node, gchar *expr) {
	xmlNodePtr	result = NULL;
	
	if(node && node->doc) {
		xmlXPathContextPtr xpathCtxt = NULL;
		xmlXPathObjectPtr xpathObj = NULL;
		
		if(NULL != (xpathCtxt = xmlXPathNewContext(node->doc))) {
			xpathCtxt->node = node;
			xpathObj = xmlXPathEval(expr, xpathCtxt);
		}
		
		if(xpathObj && !xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
			result = xpathObj->nodesetval->nodeTab[0];
		
		if(xpathObj) xmlXPathFreeObject(xpathObj);
		if(xpathCtxt) xmlXPathFreeContext(xpathCtxt);
	}
	return result;
}

gboolean common_xpath_foreach_match(xmlNodePtr node, gchar *expr, xpathMatchFunc func, gpointer user_data) {
	
	if(node && node->doc) {
		xmlXPathContextPtr xpathCtxt = NULL;
		xmlXPathObjectPtr xpathObj = NULL;
		
		if(NULL != (xpathCtxt = xmlXPathNewContext(node->doc))) {
			xpathCtxt->node = node;
			xpathObj = xmlXPathEval(expr, xpathCtxt);
		}
			
		if(xpathObj && xpathObj->nodesetval->nodeMax) {
			int	i;
			for(i = 0; i < xpathObj->nodesetval->nodeNr; i++)
				(*func)(xpathObj->nodesetval->nodeTab[i], user_data);
		}
		
		if(xpathObj) xmlXPathFreeObject(xpathObj);
		if(xpathCtxt) xmlXPathFreeContext(xpathCtxt);
		return TRUE;
	}
	return FALSE;
}

xmlDocPtr common_parse_xml(gchar *data, size_t length, gboolean recovery, errorCtxtPtr errCtx) {
	xmlParserCtxtPtr	ctxt;
	xmlDocPtr		doc;
	
	g_assert(NULL != data);

	ctxt = xmlNewParserCtxt();
	ctxt->sax->getEntity = common_process_entities;
	
	if(errCtx)
		xmlSetGenericErrorFunc(errCtx, (xmlGenericErrorFunc)common_buffer_parse_error);
	
	doc = xmlSAXParseMemory(ctxt->sax, data, length, /* recovery = */ recovery);
	
	/* This seems to reset the errorfunc to its default, so that the
	   GtkHTML2 module is not unhappy because it also tries to call the
	   errorfunc on occasion. */
	xmlSetGenericErrorFunc(NULL, NULL);
	
	xmlFreeParserCtxt(ctxt);
	
	return doc;
}

xmlDocPtr common_parse_xml_feed(feedParserCtxtPtr fpc) {
	errorCtxtPtr		errors;
		
	g_assert(NULL != fpc->data);
	g_assert(NULL != fpc->feed);
	g_assert(NULL != fpc->itemSet);
	
	fpc->itemSet->valid = FALSE;
	
	/* we don't like no data */
	if(0 == fpc->dataLength) {
		debug1(DEBUG_PARSING, "common_parse_xml_feed(): empty input while parsing \"%s\"!", fpc->node->title);
		g_string_append(fpc->feed->parseErrors, "Empty input!\n");
		return NULL;
	}

	errors = g_new0(struct errorCtxt, 1);
	errors->msg = fpc->feed->parseErrors;
	
	fpc->doc = common_parse_xml(fpc->data, (size_t)fpc->dataLength, fpc->recovery, errors);
	if(!fpc->doc) {
		debug1(DEBUG_PARSING, "xmlReadMemory: could not parse feed \"%s\"!\n", fpc->node->title);
		g_string_prepend(fpc->feed->parseErrors, _("XML Parser: Could not parse document:\n"));
		g_string_append(fpc->feed->parseErrors, "\n");
	}

	fpc->itemSet->valid = (errors->errorCount > 0);
	g_free(errors);
	
	return fpc->doc;
}

/* The following slightly modified function was originally taken 
 * from GConf 2.16.0 backends/xml-dir.c: Copyright (C) 1999, 2000 Red Hat Inc. */

/* for info on why this is used rather than xmlDocDump or xmlSaveFile
 * and friends, see http://bugzilla.gnome.org/show_bug.cgi?id=108329 */
gint common_save_xml(xmlDocPtr doc, gchar *filename) {
	FILE	*fp;
	char	*xmlbuf;
	int	fd, n;

	fp = g_fopen(filename, "w");
	if(NULL == fp)
		return -1;
  
	xmlDocDumpFormatMemory(doc, (xmlChar **)&xmlbuf, &n, TRUE);
	if(n <= 0) {
		errno = ENOMEM;
		return -1;
	}

	if(fwrite(xmlbuf, sizeof (xmlChar), n, fp) < n)	{
		xmlFree (xmlbuf);
		return -1;
	}

	xmlFree (xmlbuf);

	/* From the fflush(3) man page:
	*
	* Note that fflush() only flushes the user space buffers provided by the
	* C library. To ensure that the data is physically stored on disk the
	* kernel buffers must be flushed too, e.g. with sync(2) or fsync(2).
	*/

	/* flush user-space buffers */
	if (fflush (fp) != 0)
		return -1;

	if ((fd = fileno (fp)) == -1)
		return -1;

#ifdef HAVE_FSYNC
	/* sync kernel-space buffers to disk */
	if (fsync (fd) == -1)
		return -1;
#endif

	fclose(fp);

	return 0;
}

#define	TIMESTRLEN	256

gchar * common_format_date(time_t date, const gchar *date_format) {
	gchar		*result;
	struct tm	date_tm;
	
	if (date == 0) {
		return g_strdup ("");
	}
	
	localtime_r (&date, &date_tm);
	
	result = g_new0(gchar, TIMESTRLEN);
	strftime(result, TIMESTRLEN, date_format, &date_tm);
	return result;
}

/* This function is originally from the Evolution 2.6.2 code (e-cell-date.c) */
gchar * common_format_nice_date(time_t date) {
	time_t nowdate = time(NULL);
	time_t yesdate;
	struct tm then, now, yesterday;
	gchar *temp, *buf;
	gboolean done = FALSE;
	
	if (date == 0) {
		return g_strdup ("");
	}
	
	buf = g_new0(gchar, TIMESTRLEN + 1);

	localtime_r (&date, &then);
	localtime_r (&nowdate, &now);

/*	if (nowdate - date < 60 * 60 * 8 && nowdate > date) {
		strftime (buf, TIMESTRLEN, _("%l:%M %p"), &then);
		done = TRUE;
	}*/

	if (!done) {
		if (then.tm_mday == now.tm_mday &&
		    then.tm_mon == now.tm_mon &&
		    then.tm_year == now.tm_year) {
		    	/* translation hint: date format for today, reorder format codes as necessary */
			strftime (buf, TIMESTRLEN, _("Today %l:%M %p"), &then);
			done = TRUE;
		}
	}
	if (!done) {
		yesdate = nowdate - 60 * 60 * 24;
		localtime_r (&yesdate, &yesterday);
		if (then.tm_mday == yesterday.tm_mday &&
		    then.tm_mon == yesterday.tm_mon &&
		    then.tm_year == yesterday.tm_year) {
		    	/* translation hint: date format for yesterday, reorder format codes as necessary */
			strftime(buf, TIMESTRLEN, _("Yesterday %l:%M %p"), &then);
			done = TRUE;
		}
	}
	if (!done) {
		int i;
		for (i = 2; i < 7; i++) {
			yesdate = nowdate - 60 * 60 * 24 * i;
			localtime_r (&yesdate, &yesterday);
			if (then.tm_mday == yesterday.tm_mday &&
			    then.tm_mon == yesterday.tm_mon &&
			    then.tm_year == yesterday.tm_year) {
			    	/* translation hint: date format for dates older than 2 days but not older than a week, reorder format codes as necessary */
				strftime (buf, TIMESTRLEN, _("%a %l:%M %p"), &then);
				done = TRUE;
				break;
			}
		}
	}
	if (!done) {
		if (then.tm_year == now.tm_year) {
			/* translation hint: date format for dates older than a week but from this year, reorder format codes as necessary */
			strftime (buf, TIMESTRLEN, _("%b %d %l:%M %p"), &then);
		} else {
			/* translation hint: date format for dates from the last years, reorder format codes as necessary */
			strftime (buf, TIMESTRLEN, _("%b %d %Y"), &then);
		}
	}

	temp = buf;
	while ((temp = strstr (temp, "  "))) {
		memmove (temp, temp + 1, strlen (temp));
	}
	temp = g_strstrip (buf);
	return temp;
}

/* converts a ISO 8601 time string to a time_t value */
time_t parseISO8601Date(gchar *date) {
	struct tm	tm;
	time_t		t, t2, offset = 0;
	gboolean	success = FALSE;
	gchar *pos;
	
	g_assert(date != NULL);
	
	memset(&tm, 0, sizeof(struct tm));
	
	/* we expect at least something like "2003-08-07T15:28:19" and
	   don't require the second fractions and the timezone info

	   the most specific format:   YYYY-MM-DDThh:mm:ss.sTZD
	 */
	 
	/* full specified variant */
	if(NULL != (pos = strptime((const char *)date, "%t%Y-%m-%dT%H:%M%t", &tm))) {
		/* Parse seconds */
		if (*pos == ':')
			pos++;
		if (isdigit(pos[0]) && !isdigit(pos[1])) {
			tm.tm_sec = pos[0] - '0';
			pos++;
		} else if (isdigit(pos[0]) && isdigit(pos[1])) {
			tm.tm_sec = 10*(pos[0]-'0') + pos[1] - '0';
			pos +=2;
		}
		/* Parse timezone */
		if (*pos == 'Z')
			offset = 0;
		else if ((*pos == '+' || *pos == '-') && isdigit(pos[1]) && isdigit(pos[2]) && strlen(pos) >= 3) {
			offset = (10*(pos[1] - '0') + (pos[2] - '0')) * 60 * 60;
			
			if (pos[3] == ':' && isdigit(pos[4]) && isdigit(pos[5]))
				offset +=  (10*(pos[4] - '0') + (pos[5] - '0')) * 60;
			else if (isdigit(pos[3]) && isdigit(pos[4]))
				offset +=  (10*(pos[3] - '0') + (pos[4] - '0')) * 60;
			
			offset *= (pos[0] == '+') ? 1 : -1;

		}
		success = TRUE;
	/* only date */
	} else if(NULL != strptime((const char *)date, "%t%Y-%m-%d", &tm))
		success = TRUE;
	/* there were others combinations too... */

	if(TRUE == success) {
		if((time_t)(-1) != (t = mktime(&tm))) {
			/* Correct for the local timezone*/
			t = t - offset;
			t2 = mktime(gmtime(&t));
			t = t - (t2 - t);
			
			return t;
		} else {
			debug0(DEBUG_PARSING, "internal error! time conversion error! mktime failed!\n");
		}
	} else {
		debug0(DEBUG_PARSING, "Invalid ISO8601 date format! Ignoring <dc:date> information!\n");
	}
	
	return 0;
}

gchar *dayofweek[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
gchar *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

gchar *createRFC822Date(const time_t *time) {
	struct tm *tm;

	tm = gmtime(time); /* No need to free because it is statically allocated */
	return g_strdup_printf("%s, %2d %s %4d %02d:%02d:%02d GMT", dayofweek[tm->tm_wday], tm->tm_mday,
					   months[tm->tm_mon], 1900 + tm->tm_year, tm->tm_hour, tm->tm_min, tm->tm_sec);
}

/* this table of RFC822 timezones is from gmime-utils.c of the gmime API */
static struct {
	char *name;
	int offset;
} tz_offsets [] = {
	{ "UT", 0 },
	{ "GMT", 0 },
	{ "EST", -500 },        /* these are all US timezones.  bloody yanks */
	{ "EDT", -400 },
	{ "CST", -600 },
	{ "CDT", -500 },
	{ "MST", -700 },
	{ "MDT", -600 },
	{ "PST", -800 },
	{ "PDT", -700 },
	{ "Z", 0 },
	{ "A", -100 },
	{ "M", -1200 },
	{ "N", 100 },
	{ "Y", 1200 }
};
/** @returns timezone offset in seconds */
static time_t common_parse_rfc822_tz(char *token) {
	int offset = 0;
	const char *inptr = token;
	
	if (*inptr == '+' || *inptr == '-') {
		offset = atoi (inptr);
	} else {
		int t;

		if (*inptr == '(')
			inptr++;

		for (t = 0; t < 15; t++)
			if (!strncmp (inptr, tz_offsets[t].name, strlen (tz_offsets[t].name)))
				offset = tz_offsets[t].offset;
	}
	
	return 60 * ((offset / 100) * 60 + (offset % 100));
}


/* converts a RFC822 time string to a time_t value */
time_t parseRFC822Date(gchar *date) {
	struct tm	tm;
	time_t		t, t2;
	char 		*oldlocale;
	char		*pos;
	gboolean	success = FALSE;

	memset(&tm, 0, sizeof(struct tm));

	/* we expect at least something like "03 Dec 12 01:38:34" 
	   and don't require a day of week or the timezone

	   the most specific format we expect:  "Fri, 03 Dec 12 01:38:34 CET"
	 */
	/* skip day of week */
	if(NULL != (pos = g_utf8_strchr(date, -1, ',')))
		date = ++pos;

	/* we expect English month names, so we set the locale */
	oldlocale = g_strdup(setlocale(LC_TIME, NULL));
	setlocale(LC_TIME, "C");
	
	/* standard format with seconds and 4 digit year */
	if(NULL != (pos = strptime((const char *)date, " %d %b %Y %T", &tm)))
		success = TRUE;
	/* non-standard format without seconds and 4 digit year */
	else if(NULL != (pos = strptime((const char *)date, " %d %b %Y %H:%M", &tm)))
		success = TRUE;
	/* non-standard format with seconds and 2 digit year */
	else if(NULL != (pos = strptime((const char *)date, " %d %b %y %T", &tm)))
		success = TRUE;
	/* non-standard format without seconds 2 digit year */
	else if(NULL != (pos = strptime((const char *)date, " %d %b %y %H:%M", &tm)))
		success = TRUE;
	
	while(pos && *pos != '\0' && isspace((int)*pos))       /* skip whitespaces before timezone */
		pos++;
	
	if(oldlocale) {
		setlocale(LC_TIME, oldlocale);	/* and reset it again */
		g_free(oldlocale);
	}
	
	if(success) {
		if((time_t)(-1) != (t = mktime(&tm))) {
			/* GMT time, with no daylight savings time
			   correction. (Usually, there is no daylight savings
			   time since the input is GMT.) */
			t = t - common_parse_rfc822_tz(pos);
			t2 = mktime(gmtime(&t));
			t = t - (t2 - t);
			return t;
		} else {
			debug0(DEBUG_PARSING, "internal error! time conversion error! mktime failed!\n");
		}
	}
	
	return 0;
}

static void common_check_dir(gchar *path) {

	if(!g_file_test(path, G_FILE_TEST_IS_DIR)) {
		if(0 != g_mkdir_with_parents(path, S_IRUSR | S_IWUSR | S_IXUSR)) {
			g_error(_("Cannot create cache directory \"%s\"!"), path);
		}
	}
	g_free(path);
}

static void common_init_cache_path(void) {
	gchar *cachePath;

	lifereaUserPath = g_strdup_printf("%s" G_DIR_SEPARATOR_S ".liferea_1.2", g_get_home_dir());
	cachePath = g_strdup_printf("%s" G_DIR_SEPARATOR_S "cache", lifereaUserPath);

	common_check_dir(g_strdup(lifereaUserPath));
	common_check_dir(g_strdup(cachePath));
	common_check_dir(g_strdup_printf("%s" G_DIR_SEPARATOR_S "feeds", cachePath));
	common_check_dir(g_strdup_printf("%s" G_DIR_SEPARATOR_S "favicons", cachePath));
	common_check_dir(g_strdup_printf("%s" G_DIR_SEPARATOR_S "plugins", cachePath));
	common_check_dir(g_strdup_printf("%s" G_DIR_SEPARATOR_S "scripts", cachePath));

	g_free(cachePath);
	/* lifereaUserPath reused globally */
}

const gchar * common_get_cache_path(void) {
	
	if(!lifereaUserPath)
		common_init_cache_path();
		
	return lifereaUserPath;
}

gchar * common_create_cache_filename(const gchar *folder, const gchar *filename, const gchar *extension) {
	gchar *result;

	result = g_strdup_printf("%s" G_DIR_SEPARATOR_S "%s%s%s%s%s", common_get_cache_path(),
	                         (folder != NULL) ? folder : "",
	                         (folder != NULL) ? G_DIR_SEPARATOR_S : "",
	                         filename,
	                         (extension != NULL)? "." : "",
	                         (extension != NULL)? extension : "");

	return result;
}

const gchar * common_http_error_to_str(gint httpstatus) {
	gchar	*tmp = NULL;
	
	switch(httpstatus) {
		case 401:tmp = _("You are unauthorized to download this feed. Please update your username and "
		                 "password in the feed properties dialog box.");break;
		case 402:tmp = _("Payment Required");break;
		case 403:tmp = _("Access Forbidden");break;
		case 404:tmp = _("Resource Not Found");break;
		case 405:tmp = _("Method Not Allowed");break;
		case 406:tmp = _("Not Acceptable");break;
		case 407:tmp = _("Proxy Authentication Required");break;
		case 408:tmp = _("Request Time-Out");break;
		case 410:tmp = _("Gone. Resource doesn't exist. Please unsubscribe!");break;
	}
	
	if(!tmp) {
		switch(httpstatus / 100) {
			case 3:tmp = _("Feed not available: Server requested unsupported redirection!");break;
			case 4:tmp = _("Client Error");break;
			case 5:tmp = _("Server Error");break;
			default:tmp = _("(unknown networking error happened)");break;
		}
	}
	
	return tmp;
}

const gchar * common_netio_error_to_str(gint netstatus) {
	gchar	*tmp;
	
	switch(netstatus) {
		case NET_ERR_URL_INVALID:    tmp = _("URL is invalid"); break;
		case NET_ERR_PROTO_INVALID:  tmp = _("Unsupported network protocol"); break;
		case NET_ERR_UNKNOWN:
		case NET_ERR_CONN_FAILED:
		case NET_ERR_SOCK_ERR:       tmp = _("Error connecting to remote host"); break;
		case NET_ERR_HOST_NOT_FOUND: tmp = _("Hostname could not be found"); break;
		case NET_ERR_CONN_REFUSED:   tmp = _("Network connection was refused by the remote host"); break;
		case NET_ERR_TIMEOUT:        tmp = _("Remote host did not finish sending data"); break;
		/* Transfer errors */
		case NET_ERR_REDIRECT_COUNT_ERR: tmp = _("Too many HTTP redirects were encountered"); break;
		case NET_ERR_REDIRECT_ERR:
		case NET_ERR_HTTP_PROTO_ERR: 
		case NET_ERR_GZIP_ERR:           tmp = _("Remote host sent an invalid response"); break;
		/* These are handled by HTTP error codes...
		   case NET_ERR_HTTP_410:
		   case NET_ERR_HTTP_404:
		   case NET_ERR_HTTP_NON_200:
		*/
		case NET_ERR_AUTH_FAILED:
		case NET_ERR_AUTH_NO_AUTHINFO: tmp = _("Authentication failed"); break;
		case NET_ERR_AUTH_GEN_AUTH_ERR:
		case NET_ERR_AUTH_UNSUPPORTED: tmp = _("Webserver's authentication method incompatible with Liferea"); break;
	}
	
	return tmp;
}

static gchar * byte_to_hex(unsigned char nr) {
	gchar *result = NULL;

	result = g_strdup_printf("%%%x%x", nr / 0x10, nr % 0x10);
	return result;
}

gchar * common_encode_uri_string(gchar *string) {
	gchar		*newURIString;
	gchar		*hex, *tmp = NULL;
	int		i, j, len, bytes;

	/* the UTF-8 string is casted to ASCII to treat
	   the characters bytewise and convert non-ASCII
	   compatible chars to URI hexcodes */
	newURIString = g_strdup("");
	len = strlen(string);
	for(i = 0; i < len; i++) {
		if(g_ascii_isalnum(string[i]) || strchr("-_.!~*'()", (int)string[i]))
		   	tmp = g_strdup_printf("%s%c", newURIString, string[i]);
		else if(string[i] == ' ')
			tmp = g_strdup_printf("%s%%20", newURIString);
		else if((unsigned char)string[i] <= 127) {
			tmp = g_strdup_printf("%s%s", newURIString, hex = byte_to_hex(string[i]));g_free(hex);
		} else {
			bytes = 0;
			if(((unsigned char)string[i] >= 192) && ((unsigned char)string[i] <= 223))
				bytes = 2;
			else if(((unsigned char)string[i] > 223) && ((unsigned char)string[i] <= 239))
				bytes = 3;
			else if(((unsigned char)string[i] > 239) && ((unsigned char)string[i] <= 247))
				bytes = 4;
			else if(((unsigned char)string[i] > 247) && ((unsigned char)string[i] <= 251))
				bytes = 5;
			else if(((unsigned char)string[i] > 247) && ((unsigned char)string[i] <= 251))
				bytes = 6;
				
			if(0 != bytes) {
				if((i + (bytes - 1)) > len) {
					g_warning(_("Unexpected end of character sequence or corrupt UTF-8 encoding! Some characters were dropped!"));
					break;
				}

				for(j=0; j < (bytes - 1); j++) {
					tmp = g_strdup_printf("%s%s", newURIString, hex = byte_to_hex((unsigned char)string[i++]));
					g_free(hex);
					g_free(newURIString);
					newURIString = tmp;
				}
				tmp = g_strdup_printf("%s%s", newURIString, hex = byte_to_hex((unsigned char)string[i]));
				g_free(hex);
			} else {
				/* sh..! */
				g_error("Internal error while converting UTF-8 chars to HTTP URI!");
			}
		}
		g_free(newURIString); 
		newURIString = tmp;
	}
	g_free(string);

	return newURIString;
}

xmlChar * common_uri_escape(const xmlChar *url) {
	xmlChar	*result;

	result = xmlURIEscape(url);
	
	/* workaround for libxml2 problem... */
	if(NULL == result)
		result = g_strdup(url);

	return result;	
}

xmlChar * common_uri_unescape(const xmlChar *url) {

	return xmlURIUnescapeString(url, -1, NULL);
}

/* to correctly escape and expand URLs */
xmlChar * common_build_url(const gchar *url, const gchar *baseURL) {
	xmlChar	*escapedURL, *absURL, *escapedBaseURL;

	escapedURL = common_uri_escape(url);

	if(NULL != baseURL) {
		escapedBaseURL = common_uri_escape(baseURL);	
		absURL = xmlBuildURI(escapedURL, escapedBaseURL);
		xmlFree(escapedURL);
		xmlFree(escapedBaseURL);
	} else {
		absURL = escapedURL;
	}

	return absURL;
}

const gchar * common_get_direction_mark(gchar *text) {
	PangoDirection		pango_direction = PANGO_DIRECTION_NEUTRAL;
	GtkTextDirection	gtk_direction;
	
	if(text)
		pango_direction = pango_find_base_dir(text, -1);
		
	switch(pango_direction) {
		case PANGO_DIRECTION_LTR:
			gtk_direction = GTK_TEXT_DIR_LTR;
			break;
		case PANGO_DIRECTION_RTL:
			gtk_direction = GTK_TEXT_DIR_RTL;
			break;
		default:
			gtk_direction = gtk_widget_get_default_direction();
			break;
	}

	switch(gtk_direction) {
		case GTK_TEXT_DIR_RTL: 
			return "\342\200\217"; /* U+200F RIGHT-TO-LEFT MARK */
		case GTK_TEXT_DIR_LTR: 
			return "\342\200\216"; /* U+200E LEFT-TO-RIGHT MARK */
		default:
			return "";
	}
}

/* code taken from glibc-2.2.1/sysdeps/generic/strsep.c */
char * common_strsep (char **stringp, const char *delim) {
	char *begin, *end;

	begin = *stringp;
	if (begin == NULL)
		return NULL;

	/* A frequent case is when the delimiter string contains only one
	   character.  Here we don't need to call the expensive `strpbrk'
	   function and instead work using `strchr'.  */
	if (delim[0] == '\0' || delim[1] == '\0')
		{
			char ch = delim[0];

			if (ch == '\0')
				end = NULL;
			else
				{
					if (*begin == ch)
						end = begin;
					else if (*begin == '\0')
						end = NULL;
					else
						end = strchr (begin + 1, ch);
				}
		}
	else
		/* Find the end of the token.  */
		end = strpbrk (begin, delim);

	if (end)
		{
			/* Terminate the token and set *STRINGP past NUL character.  */
			*end++ = '\0';
			*stringp = end;
		}
	else
		/* No more delimiters; this is the last token.  */
		*stringp = NULL;
	return begin;
}

/* Taken from gaim 24 June 2004, copyrighted by the gaim developers
   under the GPL, etc.... It was slightly modified to free the passed string */
gchar * common_strreplace(gchar *string, const gchar *delimiter, const gchar *replacement) {
	gchar **split;
	gchar *ret;

	g_return_val_if_fail(string      != NULL, NULL);
	g_return_val_if_fail(delimiter   != NULL, NULL);
	g_return_val_if_fail(replacement != NULL, NULL);

	split = g_strsplit(string, delimiter, 0);
	ret = g_strjoinv(replacement, split);
	g_strfreev(split);
	g_free(string);

	return ret;
}

typedef unsigned chartype;

/* strcasestr is Copyright (C) 1994, 1996-2000, 2004 Free Software
   Foundation, Inc.  It was taken from the GNU C Library, which is
   licenced under the GPL v2.1 or (at your option) newer version. */
char *common_strcasestr (const char *phaystack, const char *pneedle)
{
	register const unsigned char *haystack, *needle;
	register chartype b, c;

	haystack = (const unsigned char *) phaystack;
	needle = (const unsigned char *) pneedle;

	b = tolower(*needle);
	if (b != '\0') {
		haystack--;             /* possible ANSI violation */
		do {
			c = *++haystack;
			if (c == '\0')
				goto ret0;
		} while (tolower(c) != (int) b);
		
		c = tolower(*++needle);
		if (c == '\0')
			goto foundneedle;
		++needle;
		goto jin;
		
		for (;;) {
			register chartype a;
			register const unsigned char *rhaystack, *rneedle;
			
			do {
				a = *++haystack;
				if (a == '\0')
					goto ret0;
				if (tolower(a) == (int) b)
					break;
				a = *++haystack;
				if (a == '\0')
					goto ret0;
			shloop:
				;
			}
			while (tolower(a) != (int) b);
			
		jin:      a = *++haystack;
			if (a == '\0')
				goto ret0;
			
			if (tolower(a) != (int) c)
				goto shloop;
			
			rhaystack = haystack-- + 1;
			rneedle = needle;
			a = tolower(*rneedle);
			
			if (tolower(*rhaystack) == (int) a)
				do {
					if (a == '\0')
						goto foundneedle;
					++rhaystack;
					a = tolower(*++needle);
					if (tolower(*rhaystack) != (int) a)
						break;
					if (a == '\0')
						goto foundneedle;
					++rhaystack;
					a = tolower(*++needle);
				} while (tolower (*rhaystack) == (int) a);
			
			needle = rneedle;             /* took the register-poor approach */
			
			if (a == '\0')
				break;
		}
	}
 foundneedle:
	return (char*) haystack;
 ret0:
	return 0;
}
