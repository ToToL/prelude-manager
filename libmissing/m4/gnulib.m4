# Copyright (C) 2004 Free Software Foundation, Inc.
# This file is free software, distributed under the terms of the GNU
# General Public License.  As a special exception to the GNU General
# Public License, this file may be distributed as part of a program
# that contains a configuration script generated by Autoconf, under
# the same distribution terms as the rest of that program.
#
# Generated by gnulib-tool.
#
# Invoked as: gnulib-tool --import --dir=. --lib=libmissing --source-base=libmissing --m4-base=libmissing/m4 --libtool snprintf vsnprintf poll getaddrinfo inet_ntop
# Reproduce by: gnulib-tool --import --dir=. --lib=libmissing --source-base=libmissing --m4-base=libmissing/m4 --libtool  alloca-opt getaddrinfo gettext inet_ntop minmax poll restrict snprintf stdbool vasnprintf vsnprintf xsize

AC_DEFUN([gl_EARLY],
[
])

AC_DEFUN([gl_INIT],
[
  gl_FUNC_ALLOCA
  gl_GETADDRINFO
  dnl you must add AM_GNU_GETTEXT([external]) or similar to configure.ac.
  gl_INET_NTOP
  gl_FUNC_POLL
  gl_C_RESTRICT
  gl_FUNC_SNPRINTF
  AM_STDBOOL_H
  gl_FUNC_VASNPRINTF
  gl_FUNC_VSNPRINTF
  gl_XSIZE
])

dnl Usage: gl_MODULES(module1 module2 ...)
AC_DEFUN([gl_MODULES], [])

dnl Usage: gl_SOURCE_BASE(DIR)
AC_DEFUN([gl_SOURCE_BASE], [])

dnl Usage: gl_M4_BASE(DIR)
AC_DEFUN([gl_M4_BASE], [])

dnl Usage: gl_LIB(LIBNAME)
AC_DEFUN([gl_LIB], [])

dnl Usage: gl_LGPL
AC_DEFUN([gl_LGPL], [])

# gnulib.m4 ends here
