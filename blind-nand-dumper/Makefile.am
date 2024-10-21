AM_CFLAGS = -I$(top_srcdir)/include $(GLOBAL_CFLAGS) $(libgeneral_CFLAGS) $(libusb_CFLAGS)
AM_CXXFLAGS = $(AM_CFLAGS) $(GLOBAL_CXXFLAGS)
AM_LDFLAGS = $(libgeneral_LIBS) $(libusb_LIBS)

bin_PROGRAMS = bnd

bnd_CFLAGS = $(AM_CFLAGS)
bnd_CXXFLAGS = $(AM_CXXFLAGS)
bnd_LDFLAGS = $(AM_LDFLAGS)
bnd_SOURCES = 	main.cpp \
                ECCCorrection.cpp \
                FileMapping.cpp \
                PicoNandReader.cpp \
                external/bitrev.c \
                external/linux_bch.c