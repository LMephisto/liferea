/**
 * @file update.c feed update request processing
 *
 * Copyright (C) 2003, 2004 Lars Lindner <lars.lindner@gmx.net>
 * Copyright (C) 2004 Nathan J. Conrad <t98502@users.sourceforge.net>
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

#include <unistd.h>
#include "net/netio.h"
#include "debug.h"
#include "update.h"

/* communication queues for requesting updates and sending the results */
GAsyncQueue	*requests = NULL;
GAsyncQueue	*results = NULL;

/* condition mutex for offline mode */
static GMutex	*cond_mutex = NULL;
static GCond	*offline_cond = NULL;
static gboolean	online = TRUE;

/* prototypes */
static void *update_thread_main(void *data);
static char* filter(gchar *cmd, gchar *data);

gpointer update_request_new(feedPtr fp) {
	struct feed_request	*request;

	debug_enter("update_request_new");	
	/* we always reuse one request structure per feed, to
	   allow to reuse the lastmodified attribute of the
	   last request... */
	   
	request = g_new0(struct feed_request, 1);
	request->feedurl = NULL;
	request->lastmodified = NULL;
	request->lasthttpstatus = 0;
	request->fp = fp;
	if(NULL != fp) {
		g_assert(fp->request == NULL);
		fp->request = (gpointer)request;
	}
	debug_exit("update_request_new");
	
	return (gpointer)request;
}

void update_request_free(gpointer request) {

	debug_enter("update_request_free");
	if(NULL != request) {
		g_free(((struct feed_request *)request)->lastmodified);
		g_free(((struct feed_request *)request)->feedurl);
		g_free(((struct feed_request *)request)->filtercmd);
		g_free(request);
	}
	debug_exit("update_request_free");
}

GThread * update_thread_init(void) {

	requests = g_async_queue_new();
	results = g_async_queue_new();
		
	return g_thread_create(update_thread_main, NULL, FALSE, NULL);
}

static void *update_thread_main(void *data) {
	struct feed_request	*request;
	gchar *tmp;

	offline_cond = g_cond_new();
	cond_mutex = g_mutex_new();
	for(;;)	{	
		/* block updating if we are offline */
		if(!online) {
			debug0(DEBUG_UPDATE, "now going offline!");
			g_mutex_lock(cond_mutex);
			g_cond_wait(offline_cond, cond_mutex);
	                g_mutex_unlock(cond_mutex);
			debug0(DEBUG_UPDATE, "going online again!");
		}
		
		/* do update processing */
		debug0(DEBUG_UPDATE, "waiting for request...");
		request = g_async_queue_pop(requests);
		g_assert(NULL != request);
		debug1(DEBUG_UPDATE, "processing received request (%s)", request->feedurl);
		downloadURL(request);

		/* And execute the postfilter */
		if (request->data != NULL && request->filtercmd != NULL) {
			tmp = filter(request->filtercmd, request->data);
			if (tmp != NULL) {
				g_free(request->data);
				request->data = tmp;
			}
		}
		/* return the request so the GUI thread can merge the feeds and display the results... */
		debug0(DEBUG_UPDATE, "request finished");
		g_async_queue_push(results, (gpointer)request);
	}
}

void update_thread_add_request(struct feed_request *new_request) {

	g_assert(NULL != new_request);
	g_async_queue_push(requests, new_request);
}

void update_thread_set_online(gboolean mode) {

	if((online = mode)) {
		g_mutex_lock(cond_mutex);
		g_cond_signal(offline_cond);
                g_mutex_unlock(cond_mutex);
	}
}

gboolean update_thread_is_online(void) {

	return online;
}

struct feed_request * update_thread_get_result(void) {

	g_assert(NULL != results);
	return g_async_queue_try_pop(results);
}

/* filter was taken from Snownews */
static char* filter(gchar *cmd, gchar *data) {
	int fd;
	gchar *command;
	const gchar *tmpdir = g_get_tmp_dir();
	char *tmpfilename;
	char		*out = NULL;
	FILE *file, *p;
	
	tmpfilename = g_strdup_printf("%s" G_DIR_SEPARATOR_S "liferea-XXXXXX", tmpdir);
	
	fd = g_mkstemp(tmpfilename);
	
	if (fd == -1) {
		odebug1("Error opening temp file %s to use for filtering!", tmpfilename);
		g_free(tmpfilename);
		return NULL;
	}
	
	command = g_strdup_printf ("%s < %s", cmd, tmpfilename);
	
	file = fdopen (fd, "w");
	
	fwrite (data, strlen(data), 1, file);
	fclose (file);
	
	out = malloc (1);
     out = '\0';
	
	/* Pipe temp file contents to process and popen it. */
	p = popen(command, "r");

	if(NULL != p) {
		int i = 0, n=0;
		while(!feof(p)) {
			++i;
			out = g_realloc(out, i*1024);
			n = fread(&out[(i-1)*1024], 1, 1024, p);
		}
		pclose(p);
		if (n == 1024)
			out = g_realloc(out, (i+1)*1024);
		out[(i-1)*1024+n] = '\0';
	}

	/* Clean up. */
	unlink (tmpfilename);
	g_free(tmpfilename);
	return out;
}

