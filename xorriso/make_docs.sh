#!/bin/sh
#
# Produce man page xorriso/xorriso.1 and info file xorriso/xorriso.info
# from base file xorriso/xorriso.texi.
# Same for xorriso/xorrisofs.texi and xorriso/xorrecord.texi.

( cd xorriso ; makeinfo ./xorriso.texi )
( cd xorriso ; makeinfo ./xorrisofs.texi )
( cd xorriso ; makeinfo ./xorrecord.texi )

xorriso/make_xorriso_1 -auto
xorriso/make_xorriso_1 -auto -xorrisofs
xorriso/make_xorriso_1 -auto -xorrecord


