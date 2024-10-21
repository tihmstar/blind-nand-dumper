AC_PREREQ([2.69])
AC_INIT([bnd], m4_esyscmd([git rev-list --count HEAD | tr -d '\n']), [tihmstar@gmail.com])

AC_CANONICAL_SYSTEM
AC_CANONICAL_HOST
AM_PROG_LIBTOOL

AM_INIT_AUTOMAKE([subdir-objects])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIRS([m4])


AC_DEFINE([VERSION_COMMIT_COUNT], "m4_esyscmd([git rev-list --count HEAD | tr -d '\n'])", [Git commit count])
AC_DEFINE([VERSION_COMMIT_SHA], "m4_esyscmd([git rev-parse HEAD | tr -d '\n'])", [Git commit sha])
AC_SUBST([VERSION_COMMIT_COUNT], ["m4_esyscmd([git rev-list --count HEAD | tr -d '\n'])"])
AC_SUBST([VERSION_COMMIT_SHA], ["m4_esyscmd([git rev-parse HEAD | tr -d '\n'])"])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC

CXXFLAGS+=" -std=c++20"
CFLAGS+=" -std=c17"
# Versioning.

# Checks for libraries.
LIBGENERAL_REQUIRES_STR="libgeneral >= 80"
LIBUSB_REQUIRES_STR="libusb-1.0 >= 1.0.27"
PKG_CHECK_MODULES(libgeneral, $LIBGENERAL_REQUIRES_STR)
PKG_CHECK_MODULES(libusb, $LIBUSB_REQUIRES_STR)

AC_SUBST([libgeneral_requires], [$LIBGENERAL_REQUIRES_STR])
AC_SUBST([libusb_requires], [$LIBUSB_REQUIRES_STR])

AC_ARG_ENABLE([debug],
            [AS_HELP_STRING([--enable-debug],
            [enable debug build(default is no)])],
            [debug_build=true],
            [debug_build=false])

if test "$debug_build" = true; then
  echo "*** Note: debug build requested ***"
  CFLAGS+=" -g -O0 -DDEBUG=1"
  CXXFLAGS+=" -g -O0 -DDEBUG=1"
fi

# Checks for header files.
# AC_CHECK_HEADERS([arpa/inet.h stdint.h stdlib.h string.h unistd.h winsock.h])

if test "x$win32" == "xtrue"; then
  #this needs to be done after AC_CHECK_LIB, because -no-undefined breaks that!
  LDFLAGS+=" -no-undefined -lws2_32"
fi

AC_CHECK_FUNCS([fls])

AC_CONFIG_FILES([Makefile
                 blind-nand-dumper/Makefile])

AC_OUTPUT


echo "
Configuration for $PACKAGE-$VERSION:
-------------------------------------------

  install prefix ..........: $prefix
  debug build .............: $debug_build"

echo "  compiler ................: ${CC}

  Now type 'make' to build $PACKAGE-$VERSION,
  and then 'make install' for installation.
"
