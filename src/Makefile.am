INCLUDES =						\
	$(GTK_CFLAGS)					\
	$(COLORD_CFLAGS)				\
	$(COLORHUG_CFLAGS)				\
	$(SQLITE_CFLAGS)				\
	$(CANBERRA_CFLAGS)				\
	-DG_LOG_DOMAIN=\"Ch\"				\
	-DG_USB_API_IS_SUBJECT_TO_CHANGE		\
	-DCH_DATA=\"$(pkgdatadir)\"			\
	-DLOCALEDIR=\""$(localedir)"\"

bin_PROGRAMS =						\
	colorhug-factory				\
	colorhug-shipping

colorhug_factory_SOURCES =				\
	ch-flash-md.c					\
	ch-flash-md.h					\
	ch-database.c					\
	ch-database.h					\
	ch-shipping-common.c				\
	ch-shipping-common.h				\
	ch-factory.c

colorhug_factory_LDADD =				\
	$(GTK_LIBS)					\
	$(COLORD_LIBS)					\
	$(COLORHUG_LIBS)				\
	$(SQLITE_LIBS)					\
	$(CANBERRA_LIBS)				\
	-lm

colorhug_factory_CFLAGS =				\
	$(WARNINGFLAGS_C)

colorhug_shipping_SOURCES =				\
	ch-cell-renderer-date.c				\
	ch-cell-renderer-date.h				\
	ch-cell-renderer-postage.c			\
	ch-cell-renderer-postage.h			\
	ch-cell-renderer-uint32.c			\
	ch-cell-renderer-uint32.h			\
	ch-cell-renderer-order-status.c			\
	ch-cell-renderer-order-status.h			\
	ch-shipping-common.c				\
	ch-shipping-common.h				\
	ch-database.c					\
	ch-database.h					\
	ch-shipping.c

colorhug_shipping_LDADD =				\
	$(GTK_LIBS)					\
	$(SQLITE_LIBS)					\
	$(COLORHUG_LIBS)				\
	$(CANBERRA_LIBS)				\
	-lm

colorhug_shipping_CFLAGS =				\
	$(WARNINGFLAGS_C)

-include $(top_srcdir)/git.mk
