INCLUDES =						\
	$(GTK_CFLAGS)					\
	$(COLORHUG_CFLAGS)				\
	$(LCMS_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
	-DG_LOG_DOMAIN=\"Ch\"				\
	-DG_USB_API_IS_SUBJECT_TO_CHANGE		\
	-DCH_DATA=\"$(pkgdatadir)\"			\
	-DLOCALEDIR=\""$(localedir)"\"

bin_PROGRAMS =						\
	colorhug-factory

colorhug_factory_SOURCES =				\
	ch-sample-window.c				\
	ch-sample-window.h				\
	ch-flash-md.c					\
	ch-flash-md.h					\
	ch-database.c					\
	ch-database.h					\
	ch-shipping-common.h				\
	ch-factory.c

colorhug_factory_LDADD =				\
	$(GTK_LIBS)					\
	$(LCMS_LIBS)					\
	$(COLORHUG_LIBS)				\
	$(SQLITE_LIBS)					\
	-lm

colorhug_factory_CFLAGS =				\
	$(WARNINGFLAGS_C)

-include $(top_srcdir)/git.mk
