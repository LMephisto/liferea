/**
 * @file ns_fm.c freshmeat namespace support
 *
 * Copyright (C) 2003-2006 Lars Lindner <lars.lindner@gmx.net>
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

#include "ns_fm.h"
#include "common.h"

/* you can find the fm DTD under http://freshmeat.net/backend/fm-releases-0.1.dtd

  it defines a lot of entities and one tag "screenshot_url", which we
  output as a HTML image in the item view footer
*/

static void parse_item_tag(feedParserCtxtPtr ctxt, xmlNodePtr cur) {
	gchar	*tmp;
	
	if(!xmlStrcmp("screenshot_url", cur->name)) {
 		if(tmp = common_utf8_fix(xmlNodeListGetString(cur->doc, cur->xmlChildrenNode, 1))) {
			if(g_utf8_strlen(tmp, -1) > 0)
				metadata_list_set(&(ctxt->item->metadata), "fmScreenshot", tmp);
			g_free(tmp);
		}
	}
}

static void ns_fm_register_ns(NsHandler *nsh, GHashTable *prefixhash, GHashTable *urihash) {
	g_hash_table_insert(prefixhash, "fm", nsh);
	g_hash_table_insert(urihash, "http://freshmeat.net/backend/fm-releases-0.1.dtd", nsh);
}

NsHandler *ns_fm_getRSSNsHandler(void) {
	NsHandler 	*nsh;
	
	nsh = g_new0(NsHandler, 1);
	nsh->registerNs		= ns_fm_register_ns;
	nsh->prefix		= "fm";
	nsh->parseItemTag	= parse_item_tag;

	return nsh;
}
