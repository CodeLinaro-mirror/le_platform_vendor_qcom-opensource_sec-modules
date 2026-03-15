# stub KM contract library: weak symbols
lib_LTLIBRARIES = libkm_contract_stub.la

libkm_contract_stub_la_SOURCES = \
    src/km_contract/km_contract_stub.c \
    inc/km_contract/km_contract.h
libkm_contract_stub_la_LDFLAGS = -version-info 1:0:0 -no-undefined

# Bin program for fde_utils
AM_CFLAGS = -Wall -g \
        -Wundef \
        -Wstrict-prototypes \
        -Wno-trigraphs \
        -DOE

ACLOCAL_AMFLAGS = -I m4

LOCAL_INCLUDE_PATHS = \
    -I$(srcdir)/inc/ \
    -I$(srcdir)/inc/km_contract \
    -I$(srcdir)/inc/dm

AM_CPPFLAGS = -D__packed__= \
    -DFEATURE_LE_DIAG \
    -DOE \
    $(LOCAL_INCLUDE_PATHS) \
    $(EXT4_UTILS_CFLAGS)

h_sources = \
    inc/fde_core.h \
    inc/fde_km.h \
    inc/key_utils.h \
    inc/encrypt_inplace.h \
    inc/utils \
    inc/km_contract/km_contract.h \
    inc/dm/dmctl_inlinecrypt.h

c_sources = \
    src/fde_utils.c \
    src/fde_core.c \
    src/fde_km.c \
    src/key_utils.c \
    src/encrypt_inplace.c \
    src/utils.c \
    src/dm/dmctl_inlinecrypt.c

bin_PROGRAMS = fde_utils

fde_utils_CC = @CC@
fde_utils_SOURCES = $(c_sources) $(h_sources)
fde_utils_CPPFLAGS = $(AM_CPPFLAGS)
fde_utils_CFLAGS = $(AM_CFLAGS)
fde_utils_LDFLAGS = -lpthread -lrt -ldl libkm_contract_stub.la $(EXT4_UTILS_LIBS)
fde_utils_LDADD = -ldl libkm_contract_stub.la $(EXT4_UTILS_LIBS)

if USE_GLIB
fde_utils_CFLAGS += -DUSE_GLIB $(GLIB_CFLAGS)
fde_utils_LDFLAGS += $(GLIB_LIBS)
endif

# Install headers
pkgincludedir = $(includedir)/km_contract
pkginclude_HEADERS = $(srcdir)/inc/km_contract/km_contract.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = fde-utils.pc
EXTRA_DIST = $(pkgconfig_DATA)
