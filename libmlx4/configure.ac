dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.57)
AC_INIT(libmlx4, 1.2.1mlnx1, linux-rdma@vger.kernel.org)
AC_CONFIG_SRCDIR([src/mlx4.h])
AC_CONFIG_AUX_DIR(config)
AC_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE([1.10 foreign tar-ustar silent-rules subdir-objects])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PROG_LIBTOOL
LT_INIT

AC_ARG_WITH([valgrind],
    AC_HELP_STRING([--with-valgrind],
        [Enable Valgrind annotations (small runtime overhead, default NO)]))
if test x$with_valgrind = x || test x$with_valgrind = xno; then
    want_valgrind=no
    AC_DEFINE([NVALGRIND], 1, [Define to 1 to disable Valgrind annotations.])
else
    want_valgrind=yes
    if test -d $with_valgrind; then
        CPPFLAGS="$CPPFLAGS -I$with_valgrind/include"
    fi
fi

#--with-wqe-format
AC_ARG_WITH([wqe-format],
    AC_HELP_STRING([--with-wqe-format],
        [Enable wqe-format annotations (default NO)]),
    AC_DEFINE([MLX4_WQE_FORMAT], 1, [Define to 1 to enable wqe-foramt annotations.]),
)

dnl Checks for programs
AC_PROG_CC

dnl Checks for libraries
AC_CHECK_LIB(ibverbs, ibv_get_device_list, [],
    AC_MSG_ERROR([ibv_get_device_list() not found.  libmlx4 requires libibverbs.]))
AC_CHECK_LIB(rt, shm_open, [],
    AC_MSG_ERROR([shm_open() not found. libibverbs requires librt.]))

dnl Checks for header files.
AC_CHECK_HEADER(infiniband/driver.h, [],
    AC_MSG_ERROR([<infiniband/driver.h> not found.  libmlx4 requires libibverbs.]))
AC_HEADER_STDC

if test x$want_valgrind = xyes; then
	AC_CHECK_HEADER(valgrind/memcheck.h,
	    [AC_DEFINE(HAVE_VALGRIND_MEMCHECK_H, 1,
		[Define to 1 if you have the <valgrind/memcheck.h> header file.])],
	    [if test $want_valgrind = yes; then
		AC_MSG_ERROR([Valgrind memcheck support requested, but <valgrind/memcheck.h> not found.])
	    fi])
fi

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_CHECK_SIZEOF(long)

dnl Checks for library functions
AC_CHECK_FUNC(ibv_read_sysfs_file, [],
    AC_MSG_ERROR([ibv_read_sysfs_file() not found.  libmlx4 requires libibverbs >= 1.0.3.]))
AC_CHECK_FUNCS(ibv_dontfork_range ibv_dofork_range ibv_register_driver)

dnl Now check if for libibverbs 1.0 vs 1.1
dummy=if$$
cat <<IBV_VERSION > $dummy.c
#include <infiniband/driver.h>
IBV_DEVICE_LIBRARY_EXTENSION
IBV_VERSION
IBV_DEVICE_LIBRARY_EXTENSION=`$CC $CPPFLAGS -E $dummy.c 2> /dev/null | tail -1`
rm -f $dummy.c
AM_CONDITIONAL(HAVE_IBV_DEVICE_LIBRARY_EXTENSION,
    test $IBV_DEVICE_LIBRARY_EXTENSION != IBV_DEVICE_LIBRARY_EXTENSION)
AC_SUBST(IBV_DEVICE_LIBRARY_EXTENSION)

AC_CACHE_CHECK(whether ld accepts --version-script, ac_cv_version_script,
    [if test -n "`$LD --help < /dev/null 2>/dev/null | grep version-script`"; then
        ac_cv_version_script=yes
    else
        ac_cv_version_script=no
    fi])

if test $ac_cv_version_script = yes; then
    MLX4_VERSION_SCRIPT='-Wl,--version-script=$(srcdir)/src/mlx4.map'
else
    MLX4_VERSION_SCRIPT=
fi
AC_SUBST(MLX4_VERSION_SCRIPT)

AC_CONFIG_FILES([Makefile libmlx4.spec])
AC_OUTPUT
