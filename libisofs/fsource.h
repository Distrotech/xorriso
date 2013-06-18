/*
 * Copyright (c) 2007 Vreixo Formoso
 * Copyright (c) 2009 - 2011 Thomas Schmitt
 * 
 * This file is part of the libisofs project; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

#ifndef LIBISO_FSOURCE_H_
#define LIBISO_FSOURCE_H_

/*
 * Definitions for the file sources. Most functions/structures related with
 * this were moved to libisofs.h.
 */

#include "libisofs.h"

#define ISO_LOCAL_FS_ID        1
#define ISO_IMAGE_FS_ID        2
#define ISO_ELTORITO_FS_ID     3
#define ISO_MEM_FS_ID          4
#define ISO_FILTER_FS_ID       5

/**
 * Create a new IsoFilesystem to deal with local filesystem.
 * 
 * @return
 *     1 sucess, < 0 error
 */
int iso_local_filesystem_new(IsoFilesystem **fs);


/* Rank two IsoFileSource of ifs_class by their eventual old image LBAs.
   Other IsoFileSource classes will be ranked only roughly.
*/
int iso_ifs_sections_cmp(IsoFileSource *s1, IsoFileSource *s2, int flag);


/* Create an independent copy of an ifs_class IsoFileSource.
*/
int iso_ifs_source_clone(IsoFileSource *old_source, IsoFileSource **new_source,
                         int flag);


#endif /*LIBISO_FSOURCE_H_*/
