SUBDIRS = resources

@INTLTOOL_DESKTOP_RULE@
desktopdir = $(datadir)/applications
desktop_in_files =					\
	colorhug-assemble.desktop.in			\
	colorhug-factory.desktop.in			\
	colorhug-shipping.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

@GSETTINGS_RULES@
@INTLTOOL_XML_NOMERGE_RULE@
gsettings_schemas_in_files = com.hughski.colorhug-tools.gschema.xml.in
gsettings_SCHEMAS = $(gsettings_schemas_in_files:.gschema.xml.in=.gschema.xml)

pkgdata_DATA =						\
	ch-assemble.ui					\
	ch-factory.ui					\
	ch-shipping.ui

EXTRA_DIST =						\
	$(gsettings_schemas_in_files)			\
	$(desktop_in_files)				\
	$(pkgdata_DATA)

clean-local :
	rm -f *~

DISTCLEANFILES =					\
	com.hughski.colorhug-tools.gschema.xml		\
	colorhug-assemble.desktop			\
	colorhug-factory.desktop			\
	colorhug-shipping.desktop

-include $(top_srcdir)/git.mk
