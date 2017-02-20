# contrib/pgimportdoc/Makefile

PGFILEDESC = "pgimportdoc - import XML, JSON, BYTEA documents to PostgreSQL"
PGAPPICON = win32

PROGRAM = pgimportdoc
OBJS	= pgimportdoc.o $(WIN32RES)

PG_CPPFLAGS = -I$(libpq_srcdir)
PG_LIBS = $(libpq_pgport)

ifdef NO_PGXS
subdir = contrib/pgimportdoc
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
else
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
endif

