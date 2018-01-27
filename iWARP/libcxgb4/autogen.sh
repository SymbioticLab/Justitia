#! /bin/sh

set -x
aclocal -I config -W none
libtoolize --force --copy --no-warn
autoheader
automake --foreign --add-missing --copy -W none
autoconf
