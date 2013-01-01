# Process this file with autoconf to produce a configure script.
AC_INIT(nwr, 1.0)
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([play.cpp])
AM_CONFIG_HEADER([config.h])

AC_LANG(C++)

# Checks for programs.
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_AWK

# Checks for libraries.
AC_CHECK_LIB([m], [sin])
AC_CHECK_LIB([pcre], [pcre_compile])

# Checks for header files.
AC_CHECK_HEADER([sys/soundcard.h])
AC_CHECK_HEADER([pcre.h])

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.
AC_PROG_GCC_TRADITIONAL

PKG_CHECK_MODULES(SIGC, sigc++-2.0)
# this is commented because insertdb depends on a very old version of libpq
#PKG_CHECK_MODULES(PQ, pq)

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
