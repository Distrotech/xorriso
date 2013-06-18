
# Copyright (c) 2007 - 2012 Thomas Schmitt <scdbackup@gmx.net>
# Provided under GPL version 2 or later.

# ts A90315 : LIBBURNIA_PKGCONFDIR is defined OS specific in acinclude.m4
# was: pkgconfigdir=$(libdir)/pkgconfig
pkgconfigdir=$(LIBBURNIA_PKGCONFDIR)

libincludedir=

lib_LTLIBRARIES =
ACLOCAL_AMFLAGS = -I ./

## ========================================================================= ##

libinclude_HEADERS =

## ========================================================================= ##

bin_PROGRAMS = \
	xorriso/xorriso

xorriso_xorriso_CPPFLAGS = -I./libburn -I./libisofs -I./libisoburn -I./xorriso

# ts B00518 : The configuration macros are now transmitted via config.h 
# xorriso_xorriso_CFLAGS = -DXorriso_standalonE \
#                          $(READLINE_DEF) $(LIBACL_DEF) $(XATTR_DEF) \
#                          $(EXTF_DEF) $(EXTF_SUID_DEF) $(ZLIB_DEF) \
#                          $(LIBCDIO_DEF) \
#                          $(XORRISO_DVD_OBS_64K) $(LIBBURN_O_DIRECT_DEF)
xorriso_xorriso_CFLAGS = 

xorriso_xorriso_LDADD = $(THREAD_LIBS) $(LIBBURN_ARCH_LIBS)


xorriso_xorriso_SOURCES = \
	\
	xorriso/xorriso.h \
	xorriso/xorriso_private.h \
	xorriso/xorriso_main.c \
	xorriso/sfile.h \
	xorriso/sfile.c \
	xorriso/aux_objects.h \
	xorriso/aux_objects.c \
	xorriso/findjob.h \
	xorriso/findjob.c \
	xorriso/check_media.h \
	xorriso/check_media.c \
	xorriso/misc_funct.h \
	xorriso/misc_funct.c \
	xorriso/text_io.h \
	xorriso/text_io.c \
	xorriso/match.h \
	xorriso/match.c \
	xorriso/emulators.h \
	xorriso/emulators.c \
	xorriso/disk_ops.h \
	xorriso/disk_ops.c \
	xorriso/cmp_update.h \
	xorriso/cmp_update.c \
	xorriso/parse_exec.h \
	xorriso/parse_exec.c \
	xorriso/opts_a_c.c \
	xorriso/opts_d_h.c \
	xorriso/opts_i_o.c \
	xorriso/opts_p_z.c \
	\
	xorriso/xorrisoburn.h \
	xorriso/base_obj.h \
	xorriso/base_obj.c \
	xorriso/lib_mgt.h \
	xorriso/lib_mgt.c \
	xorriso/sort_cmp.h \
	xorriso/sort_cmp.c \
	xorriso/drive_mgt.h \
	xorriso/drive_mgt.c \
	xorriso/iso_img.h \
	xorriso/iso_img.c \
	xorriso/iso_tree.h \
	xorriso/iso_tree.c \
	xorriso/iso_manip.h \
	xorriso/iso_manip.c \
	xorriso/write_run.h \
	xorriso/write_run.c \
	xorriso/read_run.h \
	xorriso/read_run.c \
	xorriso/filters.h \
	xorriso/filters.c \
	\
	xorriso/xorriso_timestamp.h \
	xorriso/xorriso_buildstamp.h \
	\
	libisoburn/libisoburn.h \
	libisoburn/isoburn.h \
	libisoburn/isoburn.c \
	libisoburn/isofs_wrap.c \
	libisoburn/burn_wrap.c \
	libisoburn/data_source.c \
	\
	libisofs/libisofs.h \
	libisofs/builder.h \
	libisofs/builder.c \
	libisofs/node.h \
	libisofs/node.c \
	libisofs/tree.h \
	libisofs/tree.c \
	libisofs/image.h \
	libisofs/image.c \
	libisofs/iso1999.h \
	libisofs/iso1999.c \
	libisofs/fsource.h \
	libisofs/fsource.c \
	libisofs/fs_local.c \
	libisofs/fs_image.c \
	libisofs/messages.h \
	libisofs/messages.c \
	libisofs/libiso_msgs.h \
	libisofs/libiso_msgs.c \
	libisofs/stream.h \
	libisofs/stream.c \
	libisofs/util.h \
	libisofs/util.c \
	libisofs/util_rbtree.c \
	libisofs/util_htable.c \
	libisofs/filesrc.h \
	libisofs/filesrc.c \
	libisofs/ecma119.h \
	libisofs/ecma119.c \
	libisofs/ecma119_tree.h \
	libisofs/ecma119_tree.c \
	libisofs/writer.h \
	libisofs/buffer.h \
	libisofs/buffer.c \
	libisofs/rockridge.h \
	libisofs/rockridge.c \
	libisofs/rockridge_read.c \
	libisofs/joliet.h \
	libisofs/joliet.c \
	libisofs/hfsplus.h \
	libisofs/hfsplus.c \
	libisofs/hfsplus_decompose.c \
	libisofs/hfsplus_classes.c \
	libisofs/hfsplus_case.c \
	libisofs/eltorito.h \
	libisofs/eltorito.c \
	libisofs/data_source.c \
        libisofs/find.c \
        libisofs/filter.h \
        libisofs/filter.c \
        libisofs/filters/external.c \
        libisofs/filters/zisofs.c \
        libisofs/filters/gzip.c \
        libisofs/system_area.h \
        libisofs/system_area.c \
        libisofs/make_isohybrid_mbr.c \
	libisofs/aaip_0_2.h \
	libisofs/aaip_0_2.c \
	libisofs/md5.h \
	libisofs/md5.c \
	\
	libjte/libjte.h \
	libjte/libjte_private.h \
	libjte/libjte.c \
	libjte/jte.h \
	libjte/jte.c \
	libjte/checksum.h \
	libjte/checksum.c \
	libjte/endian.c \
	libjte/endianconv.h \
	libjte/md5.h \
	libjte/md5.c \
	libjte/rsync.h \
	libjte/rsync.c \
	libjte/sha1.h \
	libjte/sha1.c \
	libjte/sha256.h \
	libjte/sha256.c \
	libjte/sha512.h \
	libjte/sha512.c \
	\
	libburn/async.c \
	libburn/async.h \
	libburn/back_hacks.h \
	libburn/cdtext.c \
	libburn/cleanup.c \
	libburn/cleanup.h \
	libburn/crc.h \
	libburn/debug.c \
	libburn/debug.h \
	libburn/drive.c \
	libburn/drive.h \
	libburn/ecma130ab.c \
	libburn/ecma130ab.h \
	libburn/error.h \
	libburn/file.c \
	libburn/file.h \
	libburn/init.c \
	libburn/init.h \
	libburn/libburn.h \
	libburn/libdax_audioxtr.h \
	libburn/libdax_audioxtr.c \
	libburn/libdax_msgs.h \
	libburn/libdax_msgs.c \
	libburn/mmc.c \
	libburn/mmc.h \
	libburn/null.c \
	libburn/null.h \
	libburn/options.c \
	libburn/options.h \
	libburn/os.h \
	libburn/read.c \
	libburn/read.h \
	libburn/sbc.c \
	libburn/sbc.h \
	libburn/sector.c \
	libburn/sector.h \
	libburn/sg.c \
	libburn/sg.h \
	libburn/source.h \
	libburn/source.c \
	libburn/spc.c \
	libburn/spc.h \
	libburn/structure.c \
	libburn/structure.h \
	libburn/toc.c \
	libburn/toc.h \
	libburn/transport.h \
	libburn/util.c \
	libburn/util.h \
	libburn/write.c \
	libburn/write.h

# Disabled because unneeded and of unclear ancestry
#	libburn/crc.c 

noinst_PROGRAMS = \
	test/compare_file \
	xorriso/make_xorriso_1


# A program to compare two trees of files in mounted filesystems
# To compare tree /media/dvd and /original/dir :
#   find /media/dvd -exec test/compare_file '{}' /media/dvd /original/dir ';'
#
test_compare_file_CPPFLAGS =
test_compare_file_CFLAGS =
test_compare_file_LDADD =
test_compare_file_SOURCES = test/compare_file.c

# Specialized converter from xorriso/xorriso.texi to xorriso/xorriso.1
#
xorriso_make_xorriso_1_CPPFLAGS =
xorriso_make_xorriso_1_CFLAGS =
xorriso_make_xorriso_1_LDADD =
xorriso_make_xorriso_1_SOURCES = xorriso/make_xorriso_1.c


# A Proof-of-concept for frontends
bin_SCRIPTS = \
	frontend/xorriso-tcltk


# Install symbolic links to the xorriso binary
#
install-exec-hook:
	if test -e "$(DESTDIR)$(bindir)"/xorrisofs ; then rm "$(DESTDIR)$(bindir)"/xorrisofs ; else echo ; fi
	ln -s xorriso "$(DESTDIR)$(bindir)"/xorrisofs
	if test -e "$(DESTDIR)$(bindir)"/osirrox ; then rm "$(DESTDIR)$(bindir)"/osirrox ; else echo ; fi
	ln -s xorriso "$(DESTDIR)$(bindir)"/osirrox
	if test -e "$(DESTDIR)$(bindir)"/xorrecord ; then rm "$(DESTDIR)$(bindir)"/xorrecord ; else echo ; fi
	ln -s xorriso "$(DESTDIR)$(bindir)"/xorrecord



# Trying to create a build timestamp file semi-manually:  make buildstamped
#
buildstamp:
	date -u '+#define Xorriso_build_timestamP "%Y.%m.%d.%H%M%S"' >xorriso/xorriso_buildstamp.h
	cat xorriso/xorriso_buildstamp.h

# For now   make buildstamped  has to be performed manually.
buildstamped: buildstamp
	make

# "make clean" shall remove a few stubborn .libs directories
# which George Danchev reported Dec 03 2011.
# Learned from: http://www.gnu.org/software/automake/manual/automake.html#Clean
clean-local:
	-rm -rf xorriso/.libs test/.libs

## ========================================================================= ##

# Indent source files
indent_files = 
	

indent: $(indent_files)
	indent -bad -bap -nbbb -nbbo -nbc -bli0 -br -bls   \
		-cdw -ce -cli0 -ncs -nbfda -i8 -l79 -lc79   \
		-lp -saf -sai -nprs -npsl -saw -sob -ss -ut \
		-sbi0 -nsc -ts8 -npcs -ncdb -fca            \
		$^

.PHONY: indent

## ========================================================================= ##

# Extra things

man_MANS = \
	xorriso/xorriso.1 \
	xorriso/xorrisofs.1 \
	xorriso/xorrecord.1

info_TEXINFOS = \
	xorriso/xorriso.texi \
	xorriso/xorrisofs.texi \
	xorriso/xorrecord.texi

EXTRA_DIST = \
	xorriso.pc.in \
	version.h.in \
	README \
	AUTHORS \
	CONTRIBUTORS \
	COPYRIGHT \
	COPYING \
	INSTALL \
	xorriso/changelog.txt \
	xorriso/xorriso_buildstamp_none.h \
	xorriso/make_docs.sh \
	$(man_MANS) \
	doc/susp_aaip_2_0.txt \
	doc/susp_aaip_isofs_names.txt \
	doc/zisofs_format.txt \
	doc/startup_file.txt \
	frontend/frontend_pipes_xorriso.c \
	frontend/README-tcltk \
	frontend/xorriso-tcltk \
	libisofs/aaip-os-dummy.c \
	libisofs/aaip-os-linux.c \
	libisofs/aaip-os-freebsd.c \
	libburn/os-dummy.h \
	libburn/os-freebsd.h \
	libburn/os-libcdio.h \
	libburn/os-linux.h \
	libburn/os-solaris.h \
	libburn/sg-dummy.c \
	libburn/sg-freebsd.c \
	libburn/sg-libcdio.c \
	libburn/sg-linux.c \
	libburn/sg-solaris.c

