
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2010 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains declarations of emulators for mkisofs and cdrecord.
*/


#ifndef Xorriso_pvt_emul_includeD
#define Xorriso_pvt_emul_includeD yes

/* micro version of cdrskin */
int Xorriso_cdrskin(struct XorrisO *xorriso, char *whom, int argc, char **argv,
                    int flag);

int Xorriso_cdrskin_help(struct XorrisO *xorriso, int flag);

int Xorriso_cdrskin_uses_stdout(struct XorrisO *xorriso, int argc, char **argv,
                                int flag);

int Xorriso_as_cdrskin(struct XorrisO *xorriso, int argc, char **argv,
                      int *idx, int flag);

/* micro emulation of mkisofs */
int Xorriso_genisofs(struct XorrisO *xorriso, char *whom,
                     int argc, char **argv, int flag);

int Xorriso_genisofs_help(struct XorrisO *xorriso, int flag);

int Xorriso_as_genisofs(struct XorrisO *xorriso, int argc, char **argv,
                      int *idx, int flag);

#endif /* ! Xorriso_pvt_emul_includeD */

