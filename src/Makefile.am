## Process this file with automake to produce Makefile.in

if WITH_AVAHI
SYNC = sync
SYNC_LIB = sync/liblisync.a
endif

SUBDIRS = parsers notification ui fl_sources $(SYNC) webkit .

AM_CPPFLAGS = \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_LIB_DIR=\""$(pkglibdir)"\" \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	-DBIN_DIR=\""$(bindir)"\" \
	-I$(top_srcdir)/src \
	$(PACKAGE_CFLAGS) \
	$(SM_CFLAGS) \
	$(DBUS_CFLAGS) \
	$(NM_CFLAGS)

bin_PROGRAMS = liferea
bin_SCRIPTS = liferea-add-feed

liferea_SOURCES = \
	browser.c browser.h \
	comments.c comments.h \
	common.c common.h \
	conf.c conf.h \
	date.c date.h \
	db.c db.h \
	dbus.c dbus.h \
	debug.c debug.h \
	e-date.c e-date.h \
	enclosure.c enclosure.h \
	export.c export.h \
	favicon.c favicon.h \
	feed.c feed.h \
	feed_parser.c feed_parser.h \
	feedlist.c feedlist.h \
	folder.c folder.h \
	html.c html.h \
	htmlview.c htmlview.h \
	item.c item.h \
	item_state.c item_state.h \
	itemset.c itemset.h \
	itemlist.c itemlist.h \
	metadata.c metadata.h \
	migrate.c migrate.h \
	net.c net.h \
	net_monitor.c net_monitor.h \
	newsbin.c newsbin.h \
	node.c node.h \
	node_type.c node_type.h \
	node_view.h \
	render.c render.h \
	rule.c rule.h \
	social.c social.h \
	sqlite3async.c sqlite3async.h \
	subscription.c subscription.h \
	subscription_type.h \
	update.c update.h \
	main.c \
	vfolder.c vfolder.h \
	xml.c xml.h

liferea_LDADD =	parsers/libliparsers.a \
		fl_sources/libliflsources.a \
		ui/libliui.a \
		webkit/libwebkit.a \
		$(SYNC_LIB) \
		$(PACKAGE_LIBS) $(SM_LIBS) \
		$(DBUS_LIBS) $(NM_LIBS) $(INTLLIBS) $(AVAHI_LIBS) \
		$(WEBKIT_LIBS) $(LIBNOTIFY_LIBS)

if WITH_DBUS

EXTRA_DIST = $(srcdir)/liferea_dbus.xml \
		$(srcdir)/liferea-add-feed.in
BUILT_SOURCES = dbus_wrap.c
CLEANFILES = $(BUILT_SOURCES)
DISTCLEANFILES = $(srcdir)/liferea-add-feed
AM_INSTALLCHECK_STD_OPTIONS_EXEMPT = liferea-add-feed

dbus_wrap.c: $(srcdir)/liferea_dbus.xml
	dbus-binding-tool --mode=glib-server --prefix=liferea_dbus $(srcdir)/liferea_dbus.xml > dbus_wrap.c

endif

if WITH_LIBNOTIFY

liferea_LDADD += notification/libnotify.a $(LIBNOTIFY_LIBS)

endif
