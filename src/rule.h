/**
 * @file rule.h feed/vfolder rule handling
 *
 * Copyright (C) 2003-2005 Lars Lindner <lars.lindner@gmx.net>
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
 
#ifndef _RULE_H
#define _RULE_H

#include <glib.h>
#include "item.h"

/** rule info structure */
typedef struct ruleInfo {
	gpointer		ruleFunc;	/* the rules test function */
	gchar			*ruleId;	/* rule id for cache file storage */
	gchar			*title;		/* rule type title for dialogs */
	gchar			*positive;	/* text for positive logic selection */
	gchar			*negative;	/* text for negative logic selection */
	gboolean		needsParameter;	/* some rules may require no parameter... */
} *ruleInfoPtr;

/** structure to store a rule instance */
typedef struct rule {
	struct vfolder	*vp;		/* the vfolder the rule belongs to */
	gchar		*value;		/* the value of the rule, e.g. a search text */
	ruleInfoPtr	ruleInfo;	/* info structure about rule check function */
	gboolean	additive;	/* is the rule positive logic */
} *rulePtr;

/** the list of implemented rules */
extern struct ruleInfo *ruleFunctions;
extern gint nrOfRuleFunctions;

/** initializes the rule handling */
void rule_init(void);

/** 
 * Looks up the given rule id and sets up a new rule
 * structure with for the given vfolder and rule value 
 *
 * @param vp		vfolder the rule belongs to
 * @param ruleId	id string for this rule type
 * @param value		argument string for this rule
 * @param additive	indicates positive or negative logic
 */
rulePtr rule_new(struct vfolder *vp, const gchar *ruleId, const gchar *value, gboolean additive);

/**
 * Checks a new item against all additive rules of all feeds
 * except the addition rules of the parent feed. In the second
 * step the function checks wether there are parent feed rules,
 * which do exclude this item. If there is such a rule the 
 * function returns FALSE, otherwise TRUE to signalize if 
 * this new item should be added. 
 *
 * @param rp	rule to check against
 * @param ip	item to check
 */
gboolean rule_check_item(rulePtr rp, itemPtr ip);

/** 
 * Free's the given rule structure 
 *
 * @param rp	rule to free
 */
void rule_free(rulePtr rp);

#endif
