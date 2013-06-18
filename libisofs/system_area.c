/*
 * Copyright (c) 2008 Vreixo Formoso
 * Copyright (c) 2010 - 2013 Thomas Schmitt
 *
 * This file is part of the libisofs project; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include "libisofs.h"
#include "system_area.h"
#include "eltorito.h"
#include "filesrc.h"
#include "ecma119_tree.h"
#include "image.h"
#include "messages.h"
#include "ecma119.h"
#include "writer.h"

#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

/* for gettimeofday() */
#include <sys/time.h>

/* >>> Need ./configure test for uuid_generate() which checks for:
       uuid_t, uuid_generate, the need for -luuid
*/
/*
#define Libisofs_with_uuid_generatE 1
*/
#ifdef Libisofs_with_uuid_generatE
#include <uuid/uuid.h>
#endif


/*
 * Create a MBR for an isohybrid enabled ISOLINUX boot image.
 * See libisofs/make_isohybrid_mbr.c
 * Deprecated.
 */
int make_isohybrid_mbr(int bin_lba, int *img_blocks, char *mbr, int flag);

/*
 * The New ISOLINUX MBR Producer.
 * Be cautious with changing parameters. Only few combinations are tested.
 *
 */
int make_isolinux_mbr(uint32_t *img_blocks, Ecma119Image *t,
                      int part_offset, int part_number, int fs_type,
                      uint8_t *buf, int flag);

/* Find out whether GPT and APM are desired by isohybrid
   flag bit0 = register APM and GPT requests in Ecma119Image
*/
int assess_isohybrid_gpt_apm(Ecma119Image *t, int *gpt_count, int gpt_idx[128],
                             int *apm_count, int flag);


static int precompute_gpt(Ecma119Image *t);


/*
 * @param flag bit0= img_blocks is start address rather than end address:
                     do not subtract 1
               bit1= img_blocks is counted in 512-byte units rather than 2 KiB
 */
static
void iso_compute_cyl_head_sec(uint64_t img_blocks, int hpc, int sph,
                              uint32_t *end_lba, uint32_t *end_sec,
                              uint32_t *end_head, uint32_t *end_cyl, int flag)
{
    uint64_t secs;

    if(flag & 2)
        secs = img_blocks;
    else
        secs = img_blocks * 4;
    if (secs > (uint64_t) 0xfffffffc)
      secs = 0xfffffffc;                   /* truncate rather than roll over */
    if (flag & 1)
        *end_lba = secs;                              /* first valid 512-lba */
    else
        secs = *end_lba = secs - 1;                    /* last valid 512-lba */
    *end_cyl = secs / (sph * hpc);
    secs -= *end_cyl * sph * hpc;
    *end_head = secs / sph;
    *end_sec = secs - *end_head * sph + 1;   /* Sector count starts by 1 */
    if (*end_cyl >= 1024) {
        *end_cyl = 1023;
        *end_head = hpc - 1;
        *end_sec = sph;
    }
}

static uint32_t compute_partition_size(char *disk_path, uint32_t *size,
                                       int flag)
{
    int ret;
    off_t num;
    struct stat stbuf;

    *size = 0;
    ret = stat(disk_path, &stbuf);
    if (ret == -1)
        return ISO_BAD_PARTITION_FILE;
    if (! S_ISREG(stbuf.st_mode))
        return ISO_BAD_PARTITION_FILE;
    num = ((stbuf.st_size + 2047) / 2048);
    if (num > 0x3fffffff || num == 0)
        return ISO_BAD_PARTITION_FILE;
    *size = num;
    return ISO_SUCCESS;
}

/* Compute size and position of appended partitions.
*/
int iso_compute_append_partitions(Ecma119Image *t, int flag)
{
    int ret, i, sa_type, cyl_align, cyl_size = 0;
    uint32_t pos, size, add_pos = 0;

    sa_type = (t->system_area_options >> 2) & 0x3f;
    cyl_align = (t->system_area_options >> 8) & 0x3;
    if (sa_type == 0 && cyl_align == 3) {
        cyl_size = t->partition_heads_per_cyl * t->partition_secs_per_head;
        if (cyl_size % 4)
            cyl_size = 0;
        else
            cyl_size /= 4;
    }
    pos = (t->vol_space_size + t->ms_block);
    for (i = 0; i < ISO_MAX_PARTITIONS; i++) {
        if (t->appended_partitions[i] == NULL)
    continue;
        if (t->appended_partitions[i][0] == 0)
    continue;
        ret = compute_partition_size(t->appended_partitions[i], &size, 0);
        if (ret < 0)
            return ret;
        add_pos = 0;
        if (sa_type == 3 && (pos % ISO_SUN_CYL_SIZE)) {
            add_pos = ISO_SUN_CYL_SIZE - (pos % ISO_SUN_CYL_SIZE);
        } else if (cyl_size > 0 && (pos % cyl_size)) {
            add_pos = cyl_size - (pos % cyl_size);
        }
        t->appended_part_prepad[i] = add_pos;
        t->appended_part_start[i] = pos + add_pos;

        if (cyl_size > 0 && (size % cyl_size)) {
            /* Obey cylinder alignment (missing data will be written as
               zeros by iso_write_partition_file()) */
            size += cyl_size - (size % cyl_size);
        }
        t->appended_part_size[i] = size;
        pos += add_pos + size;
        t->total_size += (add_pos + size) * 2048;
    }
    return ISO_SUCCESS;
}


/* @param flag
                bit1= partition_offset and partition_size are counted in
                      blocks of 512 rather than 2048
 */
static int write_mbr_partition_entry(int partition_number, int partition_type,
                  uint64_t partition_offset, uint64_t partition_size,
                  int sph, int hpc, uint8_t *buf, int flag)
{
    uint8_t *wpt;
    uint32_t end_lba, end_sec, end_head, end_cyl;
    uint32_t start_lba, start_sec, start_head, start_cyl;
    uint32_t after_end;
    int i;

    after_end = partition_offset + partition_size;
    iso_compute_cyl_head_sec((uint64_t) partition_offset, hpc, sph,
                             &start_lba, &start_sec, &start_head, &start_cyl,
                             1 | (flag & 2));
    iso_compute_cyl_head_sec((uint64_t) after_end, hpc, sph,
                             &end_lba, &end_sec, &end_head, &end_cyl,
                             (flag & 2));
    wpt = buf + 446 + (partition_number - 1) * 16;

    /* Not bootable */
    *(wpt++) = 0x00;

    /* C/H/S of the start */
    *(wpt++) = start_head;
    *(wpt++) = start_sec | ((start_cyl & 0x300) >> 2);
    *(wpt++) = start_cyl & 0xff;

    /* (partition type) */
    *(wpt++) = partition_type;

    /* 3 bytes of C/H/S end */
    *(wpt++) = end_head;
    *(wpt++) = end_sec | ((end_cyl & 0x300) >> 2);
    *(wpt++) = end_cyl & 0xff;
    
    /* LBA start in little endian */
    for (i = 0; i < 4; i++)
       *(wpt++) = (start_lba >> (8 * i)) & 0xff;

    /* Number of sectors in partition, little endian */
    end_lba = end_lba - start_lba + 1;
    for (i = 0; i < 4; i++)
       *(wpt++) = (end_lba >> (8 * i)) & 0xff;

    /* Afaik, partition tables are recognize donly with MBR signature */
    buf[510] = 0x55;
    buf[511] = 0xAA;

    return ISO_SUCCESS;
}


/* This is the gesture of grub-mkisofs --protective-msdos-label as explained by
   Vladimir Serbinenko <phcoder@gmail.com>, 2 April 2010, on grub-devel@gnu.org
   "Currently we use first and not last entry. You need to:
    1) Zero-fill 446-510
    2) Put 0x55, 0xAA into 510-512
    3) Put 0x80 (for bootable partition), 0, 2, 0 (C/H/S of the start), 0xcd
      (partition type), [3 bytes of C/H/S end], 0x01, 0x00, 0x00, 0x00 (LBA
      start in little endian), [LBA end in little endian] at 446-462
   "

   "C/H/S end" means the CHS address of the last block in the partition.
   It seems that not "[LBA end in little endian]" but "number of blocks"
   should go into bytes 458-461. But with a start lba of 1, this is the
   same number.
   See also http://en.wikipedia.org/wiki/Master_boot_record

   flag   bit0= do not write 0x55, 0xAA to 510,511
          bit1= do not mark partition as bootable
*/
static
int make_grub_msdos_label(uint32_t img_blocks, int sph, int hpc,
                          uint8_t *buf, int flag)
{
    uint8_t *wpt;
    uint32_t end_lba, end_sec, end_head, end_cyl;
    int i;

    iso_compute_cyl_head_sec((uint64_t) img_blocks, hpc, sph,
                             &end_lba, &end_sec, &end_head, &end_cyl, 0);

    /* 1) Zero-fill 446-510 */
    wpt = buf + 446;
    memset(wpt, 0, 64);

    if (!(flag & 1)) {
        /* 2) Put 0x55, 0xAA into 510-512 (actually 510-511) */
        buf[510] = 0x55;
        buf[511] = 0xAA;
    }
    if (!(flag & 2)) {
      /* 3) Put 0x80 (for bootable partition), */
      *(wpt++) = 0x80;
    } else {
      *(wpt++) = 0;
    }

    /* 0, 2, 0 (C/H/S of the start), */
    *(wpt++) = 0;
    *(wpt++) = 2;
    *(wpt++) = 0;

    /* 0xcd (partition type) */
    *(wpt++) = 0xcd;

    /* [3 bytes of C/H/S end], */
    *(wpt++) = end_head;
    *(wpt++) = end_sec | ((end_cyl & 0x300) >> 2);
    *(wpt++) = end_cyl & 0xff;
    

    /* 0x01, 0x00, 0x00, 0x00 (LBA start in little endian), */
    *(wpt++) = 0x01;
    *(wpt++) = 0x00;
    *(wpt++) = 0x00;
    *(wpt++) = 0x00;

    /* [LBA end in little endian] */
    for (i = 0; i < 4; i++)
       *(wpt++) = (end_lba >> (8 * i)) & 0xff;

    /* at 446-462 */
    if (wpt - buf != 462) {
        fprintf(stderr,
        "libisofs: program error in make_grub_msdos_label: \"assert 462\"\n");
        return ISO_ASSERT_FAILURE;
    }
    return ISO_SUCCESS;
}


/* @param flag bit0= zeroize partitions entries 2, 3, 4
*/
static
int iso_offset_partition_start(uint32_t img_blocks, uint32_t partition_offset,
                               int sph, int hpc, uint8_t *buf, int flag)
{
    uint8_t *wpt;
    uint32_t end_lba, end_sec, end_head, end_cyl;
    uint32_t start_lba, start_sec, start_head, start_cyl;
    int i;

    iso_compute_cyl_head_sec((uint64_t) partition_offset, hpc, sph,
                           &start_lba, &start_sec, &start_head, &start_cyl, 1);
    iso_compute_cyl_head_sec((uint64_t) img_blocks, hpc, sph,
                             &end_lba, &end_sec, &end_head, &end_cyl, 0);
    wpt = buf + 446;

    /* Let pass only legal bootability values */
    if (*wpt != 0 && *wpt != 0x80)
        (*wpt) = 0;
    wpt++;

    /* C/H/S of the start */
    *(wpt++) = start_head;
    *(wpt++) = start_sec | ((start_cyl & 0x300) >> 2);
    *(wpt++) = start_cyl & 0xff;

    /* (partition type) */
    wpt++;

    /* 3 bytes of C/H/S end */
    *(wpt++) = end_head;
    *(wpt++) = end_sec | ((end_cyl & 0x300) >> 2);
    *(wpt++) = end_cyl & 0xff;
    
    /* LBA start in little endian */
    for (i = 0; i < 4; i++)
       *(wpt++) = (start_lba >> (8 * i)) & 0xff;

    /* Number of sectors in partition, little endian */
    end_lba = end_lba - start_lba + 1;
    for (i = 0; i < 4; i++)
       *(wpt++) = (end_lba >> (8 * i)) & 0xff;

    if (wpt - buf != 462) {
        fprintf(stderr,
    "libisofs: program error in iso_offset_partition_start: \"assert 462\"\n");
        return ISO_ASSERT_FAILURE;
    }

    if (flag & 1) /* zeroize the other partition entries */
        memset(wpt, 0, 3 * 16);

    return ISO_SUCCESS;
}


static int boot_nodes_from_iso_path(Ecma119Image *t, char *path, 
                                   IsoNode **iso_node, Ecma119Node **ecma_node,
                                   char *purpose, int flag)
{
    int ret;

    ret = iso_tree_path_to_node(t->image, path, iso_node);
    if (ret <= 0) {
        iso_msg_submit(t->image->id, ISO_BOOT_FILE_MISSING, 0,
                       "Cannot find in ISO image: %s '%s'", purpose, path);
        return ISO_BOOT_FILE_MISSING;
    }
    if ((*iso_node)->type != LIBISO_FILE) {
        iso_msg_submit(t->image->id, ISO_BOOT_IMAGE_NOT_VALID, 0,
                       "Designated boot file is not a data file: '%s'", path);
        return ISO_BOOT_IMAGE_NOT_VALID;
    }

    *ecma_node= ecma119_search_iso_node(t, *iso_node);
    if (*ecma_node == NULL) {
        iso_msg_submit(t->image->id, ISO_BOOT_IMAGE_NOT_VALID, 0,
                      "Program error: IsoFile has no Ecma119Node: '%s'", path);
        return ISO_ASSERT_FAILURE;
    } else {
        if ((*ecma_node)->type != ECMA119_FILE) {
            iso_msg_submit(t->image->id, ISO_BOOT_IMAGE_NOT_VALID, 0,
              "Program error: Ecma119Node of IsoFile is no ECMA119_FILE: '%s'",
                          path);
            return ISO_ASSERT_FAILURE;
        }
    }
    return ISO_SUCCESS;
}


/* This function was implemented according to doc/boot_sectors.txt section
   "MIPS Volume Header" which was derived by Thomas Schmitt from
   cdrkit-1.1.10/genisoimage/boot-mips.c by Steve McIntyre which is based
   on work of Florian Lohoff and Thiemo Seufer who possibly learned from
   documents of MIPS Computer Systems, Inc. and Silicon Graphics Computer
   Systems, Inc.
   This function itself is entirely under copyright (C) 2010 Thomas Schmitt.
*/
static int make_mips_volume_header(Ecma119Image *t, uint8_t *buf, int flag)
{
    char *namept, *name_field;
    uint32_t num_cyl, idx, blocks, num, checksum;
    off_t image_size;
    static uint32_t bps = 512, spt = 32;
    Ecma119Node *ecma_node;
    IsoNode *node;
    IsoStream *stream;
    off_t file_size;
    uint32_t file_lba;
    int ret;

    /* Bytes 512 to 32767 may come from image or external file */
    memset(buf, 0, 512);

    image_size = t->curblock * 2048;

    /* 0 -   3 | 0x0be5a941 | Magic number */
    iso_msb(buf, 0x0be5a941, 4);

    /* 28 -  29 |  num_cyl_l | Number of usable cylinder, lower two bytes */
    num_cyl = (image_size + (bps * spt) - 1) / (bps * spt);
    iso_msb(buf + 28, num_cyl & 0xffff, 2);

    /* 32 -  33 |          1 | Number of tracks per cylinder */
    iso_msb(buf + 32, 1, 2);

    /* 35 -  35 |  num_cyl_h | Number of usable cylinders, high byte */
    buf[35] = (num_cyl >> 16) & 0xff;
    
    /* 38 -  39 |         32 | Sectors per track */
    iso_msb(buf + 38, spt, 2);

    /* 40 -  41 |        512 | Bytes per sector */
    iso_msb(buf + 40, bps, 2);

    /* 44 -  47 | 0x00000034 | Controller characteristics */
    iso_msb(buf + 44, 0x00000034, 4);

    /*  72 -  87 | ========== | Volume Directory Entry 1 */
    /*  72 -  79 |  boot_name | Boot file basename */
    /*  80 -  83 | boot_block | ISO 9660 LBA of boot file * 4 */
    /*  84 -  87 | boot_bytes | File length in bytes */
    /*  88 - 311 |          0 | Volume Directory Entries 2 to 15 */

    for (idx = 0; (int) idx < t->image->num_mips_boot_files; idx++) {
        ret = boot_nodes_from_iso_path(t, t->image->mips_boot_file_paths[idx], 
                                       &node, &ecma_node, "MIPS boot file", 0);
        if (ret < 0)
            return ret;

        namept = (char *) iso_node_get_name(node);
        name_field = (char *) (buf + (72 + 16 * idx));
        strncpy(name_field, namept, 8);

        file_lba = ecma_node->info.file->sections[0].block;

        iso_msb(buf + (72 + 16 * idx) + 8, file_lba * 4, 4);

        stream = iso_file_get_stream((IsoFile *) node);
        file_size = iso_stream_get_size(stream);

        /* Shall i really round up to 2048 ? Steve says yes.*/
        iso_msb(buf + (72 + 16 * idx) + 12,
                ((file_size + 2047) / 2048 ) * 2048, 4);
        
    }

    /* 408 - 411 |  part_blks | Number of 512 byte blocks in partition */
    blocks = (image_size + bps - 1) / bps;
    iso_msb(buf + 408, blocks, 4);
    /* 416 - 419 |          0 | Partition is volume header */
    iso_msb(buf + 416, 0, 4);

    /* 432 - 435 |  part_blks | Number of 512 byte blocks in partition */
    iso_msb(buf + 432, blocks, 4);
    iso_msb(buf + 444, 6, 4);

    /* 504 - 507 |   head_chk | Volume header checksum  
                                The two's complement of bytes 0 to 503 read
                                as big endian unsigned 32 bit:
                                  sum(32-bit-words) + head_chk == 0
    */
    checksum = 0;
    for (idx = 0; idx < 504; idx += 4) {
        num = iso_read_msb(buf + idx, 4);
        /* Addition modulo a natural number is commutative and associative.
           Thus the inverse of a sum is the sum of the inverses of the addends.
        */
        checksum -= num;
    }
    iso_msb(buf + 504, checksum, 4);

    return ISO_SUCCESS;
}


/* The following two functions were implemented according to
   doc/boot_sectors.txt section "MIPS Little Endian" which was derived
   by Thomas Schmitt from
   cdrkit-1.1.10/genisoimage/boot-mipsel.c by Steve McIntyre which is based
   on work of Florian Lohoff and Thiemo Seufer,
   and from <elf.h> by Free Software Foundation, Inc.

   Both functions are entirely under copyright (C) 2010 Thomas Schmitt.
*/

/**
 * Read the necessary ELF information from the first MIPS boot file.
 * This is done before image writing starts.
 */
int iso_read_mipsel_elf(Ecma119Image *t, int flag)
{
    uint32_t phdr_adr, todo, count;
    int ret;
    uint8_t *elf_buf = NULL;
    IsoNode *iso_node;
    Ecma119Node *ecma_node;
    IsoStream *stream;
    
    if (t->image->num_mips_boot_files <= 0)
        {ret = ISO_SUCCESS; goto ex;}

    LIBISO_ALLOC_MEM(elf_buf, uint8_t, 2048);
    ret = boot_nodes_from_iso_path(t, t->image->mips_boot_file_paths[0],
                                   &iso_node, &ecma_node, "MIPS boot file", 0);
    if (ret < 0)
        goto ex;
    stream = iso_file_get_stream((IsoFile *) iso_node);

    ret = iso_stream_open(stream);
    if (ret < 0) {
        iso_msg_submit(t->image->id, ret, 0,
                       "Cannot open designated MIPS boot file '%s'",
                       t->image->mips_boot_file_paths[0]);
        goto ex;
    }
    ret = iso_stream_read(stream, elf_buf, 32);
    if (ret != 32) {
cannot_read:;
        iso_stream_close(stream);
        iso_msg_submit(t->image->id, ret, 0,
                       "Cannot read from designated MIPS boot file '%s'",
                       t->image->mips_boot_file_paths[0]);
        goto ex;
    }


    /*  24 -  27 |    e_entry | Entry point virtual address */
    t->mipsel_e_entry = iso_read_lsb(elf_buf + 24, 4);

    /* 28 -  31 |    e_phoff | Program header table file offset */
    phdr_adr = iso_read_lsb(elf_buf + 28, 4);

    /* Skip stream up to byte address phdr_adr */
    todo = phdr_adr - 32;
    while (todo > 0) {
        if (todo > 2048)
            count = 2048;
        else
            count = todo;
        todo -= count;
        ret = iso_stream_read(stream, elf_buf, count);
        if (ret != (int) count)
            goto cannot_read;
    }
    ret = iso_stream_read(stream, elf_buf, 20);
    if (ret != 20)
        goto cannot_read;

    /*  4 -   7 |   p_offset | Segment file offset */
    t->mipsel_p_offset = iso_read_lsb(elf_buf + 4, 4);

    /*  8 -  11 |    p_vaddr | Segment virtual address */
    t->mipsel_p_vaddr = iso_read_lsb(elf_buf + 8, 4);

    /* 16 -  19 |   p_filesz | Segment size in file */
    t->mipsel_p_filesz = iso_read_lsb(elf_buf + 16, 4);

    iso_stream_close(stream);
    ret = ISO_SUCCESS;
ex:;
    LIBISO_FREE_MEM(elf_buf);
    return ret;
}


/**
 * Write DEC Bootblock from previously read ELF parameters.
 * This is done when image writing has already begun.
 */
static int make_mipsel_boot_block(Ecma119Image *t, uint8_t *buf, int flag)
{
    int ret;
    uint32_t seg_size, seg_start;
    IsoNode *iso_node;
    Ecma119Node *ecma_node;
    
    /* Bytes 512 to 32767 may come from image or external file */
    memset(buf, 0, 512);

    if (t->image->num_mips_boot_files <= 0)
        return ISO_SUCCESS;

    ret = boot_nodes_from_iso_path(t, t->image->mips_boot_file_paths[0],
                                   &iso_node, &ecma_node, "MIPS boot file", 0);
    if (ret < 0)
        return ret;

    /*  8 -  11 | 0x0002757a | Magic number */
    iso_lsb(buf + 8, 0x0002757a, 4);

    /* 12 -  15 |          1 | Mode  1: Multi extent boot */
    iso_lsb(buf + 12, 1, 4);

    /* 16 -  19 |   load_adr | Load address */
    iso_lsb(buf + 16, t->mipsel_p_vaddr, 4);

    /* 20 -  23 |   exec_adr | Execution address */
    iso_lsb(buf + 20, t->mipsel_e_entry, 4);

    /* 24 -  27 |   seg_size | Segment size in file. */
    seg_size = (t->mipsel_p_filesz + 511) / 512;
    iso_lsb(buf + 24, seg_size, 4);
    
    /* 28 -  31 |  seg_start | Segment file offset */
    seg_start = ecma_node->info.file->sections[0].block * 4
                + (t->mipsel_p_offset + 511) / 512;
    iso_lsb(buf + 28, seg_start, 4);
    
    return ISO_SUCCESS;
}


/* The following two functions were implemented according to
   doc/boot_sectors.txt section "SUN Disk Label and boot images" which
   was derived by Thomas Schmitt from
     cdrtools-2.01.01a77/mkisofs/sunlabel.h
     cdrtools-2.01.01a77/mkisofs/mkisofs.8
     by Joerg Schilling

   Both functions are entirely under copyright (C) 2010 Thomas Schmitt.
*/

/* @parm flag bit0= copy from next lower valid partition table entry
 */
static int write_sun_partition_entry(int partition_number,
                  char *appended_partitions[],
                  uint32_t partition_offset[], uint32_t partition_size[],
                  uint32_t cyl_size, uint8_t *buf, int flag)
{
    uint8_t *wpt;
    int read_idx, i;

    if (partition_number < 1 || partition_number > 8)
        return ISO_ASSERT_FAILURE;
    
    /* 142 - 173 | ========== | 8 partition entries of 4 bytes */
    wpt = buf + 142 + (partition_number - 1) * 4;
    if (partition_number == 1)
        iso_msb(wpt, 4, 2);                            /* 4 = User partition */
    else
        iso_msb(wpt, 2, 2);            /* 2 = Root partition with boot image */
    iso_msb(wpt + 2, 0x10, 2);              /* Permissions: 0x10 = read-only */

    /* 444 - 507 | ========== | Partition table */
    wpt = buf + 444 + (partition_number - 1) * 8;
    read_idx = partition_number - 1;
    if (flag & 1) {
        /* Search next lower valid partition table entry. #1 is default */
        for (read_idx = partition_number - 2; read_idx > 0; read_idx--)
            if (appended_partitions[read_idx] != NULL)
                if (appended_partitions[read_idx][0] != 0)
        break;
    }
    iso_msb(wpt, partition_offset[read_idx] / (uint32_t) ISO_SUN_CYL_SIZE, 4);
    iso_msb(wpt + 4, partition_size[read_idx] * 4, 4);

    /* 510 - 511 |   checksum | The result of exoring 2-byte words 0 to 254 */
    buf[510] = buf[511] = 0;
    for (i = 0; i < 510; i += 2) {
        buf[510] ^= buf[i];
        buf[511] ^= buf[i + 1];
    }

    return ISO_SUCCESS;
}

/**
 * Write SUN Disk Label with ISO in partition 1 and unused 2 to 8
 */
static int make_sun_disk_label(Ecma119Image *t, uint8_t *buf, int flag)
{
    int ret, i;
    uint64_t blk;

    /* Bytes 512 to 32767 may come from image or external file */
    memset(buf, 0, 512);

    /* 0 - 127 |      label | ASCII Label */
    if (t->ascii_disc_label[0])
        strncpy((char *) buf, t->ascii_disc_label, 128);
    else
        strcpy((char *) buf,
               "CD-ROM Disc with Sun sparc boot created by libisofs");
    
    /* 128 - 131 |          1 | Layout version */
    iso_msb(buf + 128, 1, 4);

    /* 140 - 141 |          8 | Number of partitions */
    iso_msb(buf + 140, 8, 2);

    /* 188 - 191 | 0x600ddeee | vtoc sanity */
    iso_msb(buf + 188, 0x600ddeee, 4);

    /* 420 - 421 |        350 | Rotations per minute */
    iso_msb(buf + 420, 350, 2);

    /* 422 - 423 |       2048 | Number of physical cylinders (fixely 640 MB) */
    iso_msb(buf + 422, 2048, 2);

    /* 430 - 431 |          1 | interleave factor */
    iso_msb(buf + 430, 1, 2);

    /* 432 - 433 |       2048 | Number of data cylinders (fixely 640 MB) */
    iso_msb(buf + 432, 2048, 2);

    /* 436 - 437 |          1 | Number of heads per cylinder (1 cyl = 320 kB)*/
    iso_msb(buf + 436, 1, 2);

    /* 438 - 439 |        640 | Number of sectors per head (1 head = 320 kB) */
    iso_msb(buf + 438, 640, 2);

    /* 508 - 509 |     0xdabe | Magic Number */
    iso_msb(buf + 508, 0xdabe, 2);

    if (t->sparc_core_src != NULL) {
        /* May be used for grub-sparc. */
        blk= ((uint64_t) t->sparc_core_src->sections[0].block) *
             (uint64_t) 2048; 
        for (i = 0; i < 8; i++)
            buf[Libisofs_grub2_sparc_patch_adr_poS + i] = blk >> ((7 - i) * 8);
        iso_msb(buf + Libisofs_grub2_sparc_patch_size_poS,
                t->sparc_core_src->sections[0].size, 4);
    }

    /* Set partition 1 to describe ISO image and compute checksum */
    t->appended_part_start[0] = 0;
    t->appended_part_size[0] = t->curblock;
    ret = write_sun_partition_entry(1, t->appended_partitions,
                  t->appended_part_start, t->appended_part_size,
                  ISO_SUN_CYL_SIZE, buf, 0);
    if (ret < 0)
        return ret;
    return ISO_SUCCESS;
}


/* Convenience frontend for iso_register_apm_entry().
   name and type are 0-terminated strings.
*/
int iso_quick_apm_entry(Ecma119Image *t,
           uint32_t start_block, uint32_t block_count, char *name, char *type)
{
    int ret;
    struct iso_apm_partition_request *entry;

    entry = calloc(1, sizeof(struct iso_apm_partition_request));
    if (entry == NULL)
        return ISO_OUT_OF_MEM;
    entry->start_block = start_block;
    entry->block_count = block_count;
    strncpy((char *) entry->name, name, 32);
    strncpy((char *) entry->type, type, 32);
    ret = iso_register_apm_entry(t, entry, 0);
    free(entry);
    return ret;
}


/* Convenience frontend for iso_register_gpt_entry().
   name has to be already encoded as UTF-16LE.
*/
int iso_quick_gpt_entry(Ecma119Image *t,
                        uint32_t start_block, uint32_t block_count,
                        uint8_t type_guid[16], uint8_t partition_guid[16],
                        uint64_t flags, uint8_t name[72])
{
    int ret;
    struct iso_gpt_partition_request *entry;

    entry = calloc(1, sizeof(struct iso_gpt_partition_request));
    if (entry == NULL)
        return ISO_OUT_OF_MEM;
    entry->start_block = start_block;
    entry->block_count = block_count;
    memcpy(entry->type_guid, type_guid, 16);
    memcpy(entry->partition_guid, partition_guid, 16);
    entry->flags = flags;
    memcpy(entry->name, name, 72);
    ret = iso_register_gpt_entry(t, entry, 0);
    free(entry);
    return ret;
}


int iso_quick_mbr_entry(Ecma119Image *t,
                        uint64_t start_block, uint64_t block_count,
                        uint8_t type_byte, uint8_t status_byte,
                        int desired_slot)
{
    int ret;
    struct iso_mbr_partition_request *entry;

    ret = iso_mbr_entry_slot_is_free(t, desired_slot);
    if (ret < 0)
        desired_slot = 0;
    else if (ret == 0)
        return ISO_BOOT_MBR_COLLISION;

    entry = calloc(1, sizeof(struct iso_mbr_partition_request));
    if (entry == NULL)
        return ISO_OUT_OF_MEM;
    entry->start_block = start_block;
    entry->block_count = block_count;
    entry->type_byte = type_byte;
    entry->status_byte = status_byte;
    entry->desired_slot = desired_slot;
    ret = iso_register_mbr_entry(t, entry, 0);
    free(entry);
    return ret;
}


int iso_mbr_entry_slot_is_free(Ecma119Image *t, int slot)
{
    int i;

    if (slot < 0 || slot > ISO_MBR_ENTRIES_MAX)
        return -1;
    if (slot == 0)
        return 1;
    for (i = 0; i < t->mbr_req_count; i++)
        if (t->mbr_req[i]->desired_slot == slot)
            return 0;
    return 1;
}


/**
 * Compare the block interval positions of two iso_apm_partition_request
 */
static
int cmp_partition_request(const void *f1, const void *f2)
{
    struct iso_partition_request {
       uint32_t start_block;
       uint32_t block_count;
    } *r1, *r2;

    r1 = *((struct iso_partition_request **) f1);
    r2 = *((struct iso_partition_request **) f2);
    if (r1->start_block < r2->start_block)
        return -1;
    if (r1->start_block > r2->start_block)
        return 1;

    /* In case of overlapping the largest partition shall be first */
    if (r1->block_count > r2->block_count)
        return -1;
    if (r1->block_count < r2->block_count)
        return 1;
    return 0;
}


/* @param flag bit0= This is the entry in block 1. Its blocks are already in
                     the desired apm_block_size unit. Set block_fac to 1.
                     Set flags to 3 rather than 0x13.
*/
static int iso_write_apm_entry(Ecma119Image *t, int apm_block_size,
                               struct iso_apm_partition_request *req,
                               uint8_t *buf, int map_entries, int flag)
{
    uint8_t *wpt;
    uint32_t flags;
    int block_fac;

    if ((flag & 1) || (t->apm_req_flags & 4))
        block_fac = 1;
    else
        block_fac = 2048 / apm_block_size;
        
    memset(buf, 0, apm_block_size);
    wpt = buf;

    /* Signature */
    wpt[0] = 'P'; wpt[1] = 'M';
    wpt+= 2;
    /* reserved */
    wpt += 2;
    /* Number of partition entries */
    iso_msb(wpt, (uint32_t) map_entries, 4);
    wpt += 4;
    /* Physical block start of partition */
    iso_msb(wpt, req->start_block * block_fac, 4);
    wpt += 4;
    /* Physical block count of partition */
    iso_msb(wpt, req->block_count * block_fac, 4);
    wpt += 4;
    /* Partition name */
    memcpy(wpt, req->name, 32);
    wpt += 32;
    /* Type string */
    memcpy(wpt, req->type, 32);
    wpt += 32;
    /* Logical block start */
    iso_msb(wpt, 0, 4);
    wpt += 4;
    /* Logical block count */
    iso_msb(wpt, req->block_count * block_fac, 4);
    wpt += 4;
    /* Status flags : bit0= entry is valid , bit1= entry is allocated
                      bit4= partition is readable , bit5= partition is writable
                      bit30= automatic mount (legacy Mac)
    */
    if (flag & 1) {
        flags = 3;
    } else {
        flags = 0x13;
        if (strncmp((char *) req->type, "Apple_HFS", 9) == 0 &&
            req->type[9] == 0)
            flags |= 0x40000000;
    }
    iso_msb(wpt, flags, 4);
    wpt += 4;

    /* boot_block , boot_bytes , processor , reserved : are all 0 */

    return ISO_SUCCESS;
}


/* Sort and fill gaps in requested APM */
static int fill_apm_gaps(Ecma119Image *t, uint32_t img_blocks)
{
    int i, ret, gap_counter = 0, up_to;
    uint32_t part_end, goal, block_fac = 1;
    char gap_name[33];

    if (t->apm_req_flags & 4)
        block_fac = 2048 / t->apm_block_size;

    /* Find out whether an entry with start_block <= 1 is requested */
    for (i = 0; i < t->apm_req_count; i++) {
        if (t->apm_req[i]->start_block <= 1)
    break;
    }
    if (i >= t->apm_req_count) {
        ret = iso_quick_apm_entry(t, 1, 0, "Apple", "Apple_partition_map");
        if (ret < 0)
            return ret;
    }

    qsort(t->apm_req, t->apm_req_count,
        sizeof(struct iso_apm_partition_request *), cmp_partition_request);

    /* t->apm_req_count will grow during the loop */
    up_to = t->apm_req_count + 1;
    for (i = 1; i < up_to; i++) {
        if (i < up_to - 1)
            goal = t->apm_req[i]->start_block;
        else
            goal = img_blocks * block_fac;
        if (i == 1) {
            /* Description of APM itself */
            /* Actual APM size is not yet known. Protection begins at PVD */
            part_end = 16 * block_fac;
            if (goal < part_end && goal> 1)
                    part_end = goal;
        } else {
            part_end = t->apm_req[i - 1]->start_block +
                       t->apm_req[i - 1]->block_count;
        }
        if (part_end > goal) {
            iso_msg_submit(t->image->id, ISO_BOOT_APM_OVERLAP, 0,
               "Program error: APM partitions %d and %d overlap by %lu blocks",
               i - 1, i, part_end - goal);
            return ISO_BOOT_APM_OVERLAP;
        }

        if (t->apm_req_flags & 2) /* Do not fill gaps */
    continue;

        if (part_end < goal || i == up_to - 1) { /* Always add a final entry */
            sprintf(gap_name, "Gap%d", gap_counter);
            gap_counter++;
            ret = iso_quick_apm_entry(t, part_end, goal - part_end,
                                      gap_name, "ISO9660_data");
            if (ret < 0)
                return ret;
        }
    }

    /* Merge list of gap partitions with list of already sorted entries */
    if (!(t->apm_req_flags & 2)) /* No gaps were filled */
        qsort(t->apm_req, t->apm_req_count,
            sizeof(struct iso_apm_partition_request *), cmp_partition_request);

    return 1;
}


static int rectify_apm(Ecma119Image *t)
{
    int ret;

#ifdef NIX
    /* Disabled */

    /* <<< ts B20526 : Dummy mock-up */
    if (t->apm_req_count <= 0) {
        /*
        ret = iso_quick_apm_entry(t, 16, 20, "Test1_name_16_20", "Test1_type");
        / * >>> Caution: Size 90 causes intentional partition overlap error * /
        ret = iso_quick_apm_entry(t, 30, 90, "BAD_30_90_BAD", "Test1_type");
        */
        ret = iso_quick_apm_entry(t, 30, 20, "Test1_name_30_20", "Test1_type");
        if (ret < 0)
            return ret;
        ret = iso_quick_apm_entry(t, 100, 400, "Test2_name_100_400",
                                  "Test2_type");
        if (ret < 0)
            return ret;
    }
#endif /* NIX */

    if (t->apm_req_count == 0)
        return 1;

    if (t->gpt_req_count > 0 &&
        t->apm_block_size != 2048 && t->apm_req_count > 0) {
        iso_msgs_submit(0,
                "GPT and APM requested. APM block size would have to be 2048.",
                        0, "FAILURE", 0);
        return ISO_BOOT_APM_GPT_BSIZE;
    }
    if (t->apm_req_count > 0) {
        ret = fill_apm_gaps(t, t->curblock);
        if (ret < 0)
            return ret;
    }
    return 1;
}


/* flag bit0= do not write Block0
*/
static int iso_write_apm(Ecma119Image *t, uint32_t img_blocks, uint8_t *buf,
                         int flag)
{
    int i, ret;
    uint32_t block_fac = 1;
    /* This is a micro mock-up of an APM Block0
       and also harmless x86 machine code.
    */
    static uint8_t block0_template[8] = {
        0x45, 0x52, 0x02, 0x00, 0xeb, 0x02, 0xff, 0xff
    };

    if (t->apm_req_flags & 4)
        block_fac = 2048 / t->apm_block_size;

    if (t->apm_req_count <= 0)
        return 2;

    if (!(t->apm_req_flags & 2)) {
        /* Gaps have been filled. Care for the final one */
        /* Adjust last partition to img_size. This size was not known when the
           number of APM partitions was determined.
        */
        t->apm_req[t->apm_req_count - 1]->block_count =
        img_blocks * block_fac - t->apm_req[t->apm_req_count - 1]->start_block;
        /* If it is still empty, remove it */
        if(t->apm_req[t->apm_req_count - 1]->block_count == 0) {
          free(t->apm_req[t->apm_req_count - 1]);
          t->apm_req_count--;
        }
    }

    /* If block size is larger than 512, then not all 63 entries will fit */
    if ((t->apm_req_count + 1) * t->apm_block_size > 32768) 
        return ISO_BOOT_TOO_MANY_APM;

    /* Block 1 describes the APM itself */
    t->apm_req[0]->start_block = 1;
    t->apm_req[0]->block_count = t->apm_req_count;

    if (!(flag & 1)) {
      /* Write APM block 0. Very sparse, not to overwrite much of
         possible MBR.
      */
      memcpy(buf, block0_template, 8);
      buf[2]= (t->apm_block_size >> 8) & 0xff;
      buf[3]= 0;
    }

    /* Write APM Block 1 to t->apm_req_count */
    for (i = 0; i < t->apm_req_count; i++) {
        ret = iso_write_apm_entry(t, t->apm_block_size, t->apm_req[i],
                       buf + (i + 1) * t->apm_block_size, t->apm_req_count,
                       i == 0);
        if (ret < 0)
            return ret;
    }
    return ISO_SUCCESS;
}


static int iso_write_mbr(Ecma119Image *t, uint32_t img_blocks, uint8_t *buf)
{
    int i, ret, req_of_slot[ISO_MBR_ENTRIES_MAX], q, j;

#ifdef NIX
    /* Disabled */

    /* <<< Dummy mock-up */
    if (t->mbr_req_count <= 0) {
        ret = iso_quick_mbr_entry(t, (uint64_t) 0, (uint64_t) 0, 0xee, 0, 0);
        if (ret < 0)
            return ret;
        ret = iso_quick_mbr_entry(t, ((uint64_t) 100) * 4, (uint64_t) 0,
                                  0x0c, 0x80, 1);
        if (ret < 0)
            return ret;
    }
#endif /* NIX */

    if (t->mbr_req_count <= 0)
        return 2;

    /* >>> Sort by start block ? */

    /* Adjust partition ends */
    for (i = 0; i < t->mbr_req_count; i++) {
        if (i > 0) {
            if (t->mbr_req[i]->start_block <= t->mbr_req[i - 1]->start_block &&
                !(t->mbr_req[i]->block_count == 0 &&
                  t->mbr_req[i]->start_block ==
                                             t->mbr_req[i - 1]->start_block))
                return ISO_BOOT_MBR_OVERLAP;
            if (t->mbr_req[i - 1]->start_block +
                   t->mbr_req[i - 1]->block_count > t->mbr_req[i]->start_block)
                return ISO_BOOT_MBR_OVERLAP;
        }
        if (t->mbr_req[i]->block_count != 0)
    continue;
        if (i < t->mbr_req_count - 1)
            t->mbr_req[i]->block_count = t->mbr_req[i + 1]->start_block -
                                         t->mbr_req[i]->start_block;
        else
            t->mbr_req[i]->block_count = ((uint64_t) img_blocks) * 4 -
                                         t->mbr_req[i]->start_block;
    }

    /* Assign requested entries to slot numbers */
    for (i = 0; i < ISO_MBR_ENTRIES_MAX; i++)
        req_of_slot[i] = -1;
    for (i = 0; i < t->mbr_req_count; i++) {
        if (t->mbr_req[i]->desired_slot < 1 ||
            t->mbr_req[i]->desired_slot > ISO_MBR_ENTRIES_MAX)
    continue;
        if (req_of_slot[t->mbr_req[i]->desired_slot - 1] >= 0)
            return ISO_BOOT_MBR_COLLISION;
        req_of_slot[t->mbr_req[i]->desired_slot - 1] = i;
    }
    for (i = 0; i < t->mbr_req_count; i++) {
        if (t->mbr_req[i]->desired_slot > 0)
    continue;
        for (j = 0; j < ISO_MBR_ENTRIES_MAX; j++)
             if (req_of_slot[j] < 0)
        break;
        if (j >= ISO_MBR_ENTRIES_MAX)
            return ISO_BOOT_TOO_MANY_MBR;
        req_of_slot[j] = i;
    }

    /* Write partition slots */
    for (i = 0; i < ISO_MBR_ENTRIES_MAX; i++) {
        memset(buf + 446 + i * 16, 0, 16);
        q = req_of_slot[i];
        if (q < 0) 
    continue;
        if (t->mbr_req[q]->block_count == 0)
    continue;
        ret = write_mbr_partition_entry(i + 1, (int) t->mbr_req[q]->type_byte,
                  t->mbr_req[q]->start_block, t->mbr_req[q]->block_count,
                  t->partition_secs_per_head, t->partition_heads_per_cyl,
                  buf, 2);
        if (ret < 0)
            return ret;
        buf[446 + i * 16] = t->mbr_req[q]->status_byte;
    }
    return ISO_SUCCESS;
}


static void iso_write_gpt_entry(Ecma119Image *t, uint8_t *buf,
                                uint8_t type_guid[16], uint8_t part_uuid[16],
                                uint64_t start_lba, uint64_t end_lba,
                                uint64_t flags, uint8_t name[72])
{
    char *wpt;
    int i;

    wpt = (char *) buf;
    memcpy(wpt, type_guid, 16);
    wpt += 16;
    for (i = 0; i < 16; i++)
        if (part_uuid[i])
    break;
    if (i < 16) {
        memcpy(wpt, part_uuid, 16);
    } else {
        iso_random_uuid(t, (uint8_t *) wpt);
    }
    wpt += 16;
    iso_lsb_to_buf(&wpt, start_lba & 0xffffffff, 4, 0);
    iso_lsb_to_buf(&wpt, (start_lba >> 32) & 0xffffffff, 4, 0);
    iso_lsb_to_buf(&wpt, end_lba & 0xffffffff, 4, 0);
    iso_lsb_to_buf(&wpt, (end_lba >> 32) & 0xffffffff, 4, 0);
    iso_lsb_to_buf(&wpt, flags & 0xffffffff, 4, 0);
    iso_lsb_to_buf(&wpt, (flags >> 32) & 0xffffffff, 4, 0);
    memcpy(wpt, name, 72);
}


int iso_write_gpt_header_block(Ecma119Image *t, uint32_t img_blocks,
                               uint8_t *buf, uint32_t max_entries,
                               uint32_t part_start, uint32_t p_arr_crc)
{
    static char *sig = "EFI PART";
    static char revision[4] = {0x00, 0x00, 0x01, 0x00};
    char *wpt;
    uint32_t crc;
    off_t back_lba;

    memset(buf, 0, 128);
    wpt = (char *) buf;

    /* >>> Make signature adjustable */
    memcpy(wpt, sig, 8); /* no trailing 0 */
    wpt += 8;

    memcpy(wpt, revision, 4);
    wpt += 4;
    iso_lsb_to_buf(&wpt, 92, 4, 0);

    /* CRC will be inserted later */
    wpt += 4;

    /* reserved */
    iso_lsb_to_buf(&wpt, 0, 4, 0);
    /* Own LBA low 32 */
    iso_lsb_to_buf(&wpt, 1, 4, 0);
    /* Own LBA high 32 */
    iso_lsb_to_buf(&wpt, 0, 4, 0);

    /* Backup header LBA is 1 hd block before backup GPT area end */
    back_lba = t->gpt_backup_end * 4 - 1;
    iso_lsb_to_buf(&wpt, (uint32_t) (back_lba & 0xffffffff), 4, 1);
    iso_lsb_to_buf(&wpt, (uint32_t) (back_lba >> 32), 4, 1);

    /* First usable LBA for partitions (4 entries per hd block) */
    iso_lsb_to_buf(&wpt, part_start + max_entries / 4, 4, 0);
    iso_lsb_to_buf(&wpt, 0, 4, 0);

    /* Last usable LBA for partitions is 1 hd block before first backup entry*/
    iso_lsb_to_buf(&wpt,
                   (uint32_t) ((back_lba - max_entries / 4 - 1) & 0xffffffff),
                   4, 1);
    iso_lsb_to_buf(&wpt,
                   (uint32_t) ((back_lba - max_entries / 4 - 1) >> 32), 4, 1);

    /* Disk GUID */
    /* >>> Make adjustable */
    iso_random_uuid(t, (uint8_t *) wpt);
    wpt += 16;

    /* Partition entries start */
    iso_lsb_to_buf(&wpt, part_start, 4, 0);
    iso_lsb_to_buf(&wpt, 0, 4, 0);

    /* Number of partition entries */
    iso_lsb_to_buf(&wpt, max_entries, 4, 0);

    /* Size of a partition entry */
    iso_lsb_to_buf(&wpt, 128, 4, 0);

    /* CRC-32 of the partition array */
    iso_lsb_to_buf(&wpt, p_arr_crc, 4, 0);


    /* <<< Only for a first test */
    if (wpt - (char *) buf != 92) {
        iso_msgs_submit(0,
                   "program error : write_gpt_header_block : wpt != 92",
                   0, "FATAL", 0);
        return ISO_ISOLINUX_CANT_PATCH;
    }

    /* CRC-32 of this header while head_crc is 0 */
    crc = iso_crc32_gpt((unsigned char *) buf, 92, 0); 
    wpt = ((char *) buf) + 16;
    iso_lsb_to_buf(&wpt, crc, 4, 0);

    return ISO_SUCCESS;
}


/* Only for up to 36 characters ISO-8859-1 (or ASCII) input */
void iso_ascii_utf_16le(uint8_t gap_name[72])
{
    int i;

    for (i = strlen((char *) gap_name) - 1; i >= 0; i--) {
        gap_name[2 * i] = gap_name[i];
        gap_name[2 * i + 1] = 0;
    }
}


static int iso_write_gpt(Ecma119Image *t, uint32_t img_blocks, uint8_t *buf)
{
    static uint8_t basic_data_uuid[16] = {
        0xa2, 0xa0, 0xd0, 0xeb, 0xe5, 0xb9, 0x33, 0x44,
        0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7
    };

    uint32_t p_arr_crc = 0, part_end, goal, next_end;
    uint64_t start_lba, end_lba;
    int ret, i, gap_counter = 0, up_to;
    struct iso_gpt_partition_request *req;
    uint8_t gpt_name[72];
    static uint8_t zero_uuid[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    static uint64_t gpt_flags = (((uint64_t) 1) << 60) | 1;

    if (t->gpt_req_count == 0)
        return 2;

    /* Sort and fill gaps */
    qsort(t->gpt_req, t->gpt_req_count,
        sizeof(struct iso_gpt_partition_request *), cmp_partition_request);
    /* t->gpt_req_count will grow during the loop */
    up_to = t->gpt_req_count + 1;
    goal = 0;
    part_end = 0;
    for (i = 0; i < up_to; i++) {
        if (i < up_to - 1) {
            goal = t->gpt_req[i]->start_block;
        } else {
            goal = img_blocks;
        }
        if (i == 0) {
            if (goal <= 16)
    continue;
            next_end = 16;
        } else {
            next_end = t->gpt_req[i - 1]->start_block +
                       t->gpt_req[i - 1]->block_count;
        }
        if (next_end > part_end)
            part_end = next_end;
        if (part_end > goal) {
            if (!(t->gpt_req_flags & 1)) {
                iso_msg_submit(t->image->id, ISO_BOOT_GPT_OVERLAP, 0,
               "Program error: GPT partitions %d and %d overlap by %lu blocks",
                               i - 1, i, part_end - goal);
                return ISO_BOOT_GPT_OVERLAP;
            }
        } else if (part_end < goal) {
            memset(gpt_name, 0, 72);
            sprintf((char *) gpt_name, "Gap%d", gap_counter);
            iso_ascii_utf_16le(gpt_name);
            gap_counter++;
            ret = iso_quick_gpt_entry(t, part_end, goal - part_end,
                                      basic_data_uuid, zero_uuid,
                                      gpt_flags, gpt_name);
            if (ret < 0)
                return ret;
        }
    }
    /* Merge list of gap partitions with list of already sorted entries */
    qsort(t->gpt_req, t->gpt_req_count,
        sizeof(struct iso_gpt_partition_request *), cmp_partition_request);
 
    if ((int) t->gpt_max_entries < t->gpt_req_count)
        return ISO_BOOT_TOO_MANY_GPT;

    /* Write the GPT entries to buf */
    for (i = 0; i < t->gpt_req_count; i++) {
        req = t->gpt_req[i];
        start_lba = ((uint64_t) req->start_block) * 4;
        end_lba = ((uint64_t) req->start_block) + req->block_count;
        if (end_lba > t->gpt_backup_end - t->gpt_backup_size)
            end_lba = t->gpt_backup_end - t->gpt_backup_size;
        end_lba = end_lba * 4 - 1;
        iso_write_gpt_entry(t, buf + 512 * t->gpt_part_start + 128 * i,
                            req->type_guid, req->partition_guid, 
                            start_lba, end_lba, req->flags, req->name);
    }
    for (; i < (int) t->gpt_max_entries; i++)
        memset(buf + 512 * t->gpt_part_start + 128 * i, 0, 128);

    p_arr_crc = iso_crc32_gpt((unsigned char *) buf + 512 * t->gpt_part_start,
                              128 * t->gpt_max_entries, 0);
    ret = iso_write_gpt_header_block(t, img_blocks, buf + 512,
                                     t->gpt_max_entries,
                                     t->gpt_part_start, p_arr_crc);
    if (ret < 0)
        return ret;
    return ISO_SUCCESS;
}


int iso_write_system_area(Ecma119Image *t, uint8_t *buf)
{
    int ret, int_img_blocks, sa_type, i, will_append = 0, do_isohybrid = 0;
    int first_partition = 1, last_partition = 4, apm_flag, part_type;
    int gpt_count = 0, gpt_idx[128], apm_count = 0;
    uint32_t img_blocks;
    uint64_t blk;
    uint8_t *wpt;

    if ((t == NULL) || (buf == NULL)) {
        return ISO_NULL_POINTER;
    }

    /* set buf to 0s */
    memset(buf, 0, 16 * BLOCK_SIZE);

    sa_type = (t->system_area_options >> 2) & 0x3f;
    if (sa_type == 3) {
        first_partition = 2;
        last_partition = 8;
    }
    for (i = first_partition - 1; i <= last_partition - 1; i++)
        if (t->appended_partitions[i] != NULL) {
            will_append = 1;
    break;
        }

    img_blocks = t->curblock;
    if (t->system_area_data != NULL) {
        /* Write more or less opaque boot image */
        memcpy(buf, t->system_area_data, 16 * BLOCK_SIZE);

    } else if (sa_type == 0 && t->catalog != NULL &&
               (t->catalog->bootimages[0]->isolinux_options & 0x0a) == 0x02) {
        /* Check for isolinux image with magic number of 3.72 and produce
           an MBR from our built-in template. (Deprecated since 31 Mar 2010)
        */
        if (img_blocks < 0x80000000) {
            int_img_blocks= img_blocks;
        } else {
            int_img_blocks= 0x7ffffff0;
        }
        ret = make_isohybrid_mbr(t->bootsrc[0]->sections[0].block,
                                 &int_img_blocks, (char*)buf, 0);
        if (ret != 1) {
            /* error, it should never happen */
            return ISO_ASSERT_FAILURE;
        }
        return ISO_SUCCESS;
    }

    /* If APM entries were submitted to iso_register_apm_entry(), then they
       get sprinkled over the system area after the system area template was
       loaded and before any other data get inserted.
       Note that if APM and MBR get combined, then the first 8 bytes of the MBR
       template must be replaceable by:
          {0x45, 0x52, 0x08 0x00, 0xeb, 0x02, 0xff, 0xff}

       >>> ts B20526
       >>> This does not care for eventual image enlargements in last minute.
       >>> A sa_type, that does this, will have to adjust the last APM entry
       >>> if exactness matters.
    */

    apm_flag = 0;
    if (sa_type == 0 && (t->system_area_options & 3) == 2) {
        do_isohybrid = 1;

        /* >>> Coordinate with partprepend writer */
        /* <<< provisory trap */
        if (t->mbr_req_count > 0)
            return ISO_BOOT_MBR_OVERLAP;

        /* If own APM is desired, set flag bit0 to prevent writing of Block0
           which would interfere with the own Block0 of isohybrid.
        */
        ret = assess_isohybrid_gpt_apm(t, &gpt_count, gpt_idx, &apm_count, 0);
        if (ret < 0)
            return ret;
        if (apm_count > 0)
            apm_flag |= 1;
    }

    ret = iso_write_apm(t, img_blocks, buf, apm_flag);
    if (ret < 0) {
        iso_msg_submit(t->image->id, ret, 0,
                       "Cannot set up Apple Partition Map");
        return ret;
    }
    ret = iso_write_mbr(t, img_blocks, buf);
    if (ret < 0) {
        iso_msg_submit(t->image->id, ret, 0,
                       "Cannot set up MBR partition table");
        return ret;
    }
    if (t->mbr_req_count > 0 && sa_type != 0)
        return ISO_NON_MBR_SYS_AREA;
    ret = iso_write_gpt(t, img_blocks, buf);
    if (ret < 0) {
        iso_msg_submit(t->image->id, ret, 0, "Cannot set up GPT");
        return ret;
    }

    if (sa_type == 0 && (t->system_area_options & 1)) {
        if (t->mbr_req_count == 0){
            /* Write GRUB protective msdos label, i.e. a simple partition
               table */
            ret = make_grub_msdos_label(img_blocks, t->partition_secs_per_head,
                                      t->partition_heads_per_cyl, buf, 0);
            if (ret != ISO_SUCCESS) /* error should never happen */
               return ISO_ASSERT_FAILURE;
        }
    } else if (do_isohybrid) {
        /* Patch externally provided system area as isohybrid MBR */
        if (t->catalog == NULL || t->system_area_data == NULL) {
            /* isohybrid makes only sense together with ISOLINUX boot image
               and externally provided System Area.
            */
            return ISO_ISOLINUX_CANT_PATCH;
        }

        if (gpt_count > 0 || apm_count > 0)
            part_type = 0x00;
        else
            part_type = 0x17;

        /* >>> ??? Why is partition_offset 0 here ?
                   It gets adjusted later by iso_offset_partition_start()
                   Would it harm to give the real offset here ?
        */;

        ret = make_isolinux_mbr(&img_blocks, t, 0, 1, part_type, buf, 1);
        if (ret != 1)
            return ret;
    } else if (sa_type == 1) {
        ret = make_mips_volume_header(t, buf, 0);
        if (ret != ISO_SUCCESS)
            return ret;
    } else if (sa_type == 2) {
        ret = make_mipsel_boot_block(t, buf, 0);
        if (ret != ISO_SUCCESS)
            return ret;
    } else if (sa_type == 3) {
        ret = make_sun_disk_label(t, buf, 0);
        if (ret != ISO_SUCCESS)
            return ret;
    } else if ((t->partition_offset > 0 || will_append) && sa_type == 0 &&
               t->mbr_req_count == 0) {
        /* Write a simple partition table. */
        ret = make_grub_msdos_label(img_blocks, t->partition_secs_per_head,
                                    t->partition_heads_per_cyl, buf, 2);
        if (ret != ISO_SUCCESS) /* error should never happen */
            return ISO_ASSERT_FAILURE;
        if (t->partition_offset == 0) {
            /* Re-write partion entry 1 : start at 0, type Linux */
            ret = write_mbr_partition_entry(1, 0x83,
                        (uint64_t) 0, (uint64_t) img_blocks,
                        t->partition_secs_per_head, t->partition_heads_per_cyl,
                        buf, 0);
            if (ret < 0)
                return ret;
        }
    }

    if (t->partition_offset > 0 && sa_type == 0 && t->mbr_req_count == 0) {
        /* Adjust partition table to partition offset.
           With t->mbr_req_count > 0 this has already been done,
        */
        img_blocks = t->curblock;                  /* value might be altered */
        ret = iso_offset_partition_start(img_blocks, t->partition_offset,
                                         t->partition_secs_per_head,
                                         t->partition_heads_per_cyl, buf, 0);
        if (ret != ISO_SUCCESS) /* error should never happen */
            return ISO_ASSERT_FAILURE;
    }

    /* This eventually overwrites the non-mbr_req partition table entries
       made so far. Overwriting those from t->mbr_req is not allowed.
    */
    for (i = first_partition - 1; i <= last_partition - 1; i++) {
        if (t->appended_partitions[i] == NULL)
    continue;
        if (i < t->mbr_req_count)
            return ISO_BOOT_MBR_COLLISION;
        if (sa_type == 3) {
            ret = write_sun_partition_entry(i + 1, t->appended_partitions,
                        t->appended_part_start, t->appended_part_size,
                        ISO_SUN_CYL_SIZE,
                        buf, t->appended_partitions[i][0] == 0);
        } else {
            ret = write_mbr_partition_entry(i + 1, t->appended_part_types[i],
                        (uint64_t) t->appended_part_start[i],
                        (uint64_t) t->appended_part_size[i],
                        t->partition_secs_per_head, t->partition_heads_per_cyl,
                        buf, 0);
        }
        if (ret < 0)
            return ret;
    }

    if (sa_type == 0 && (t->system_area_options & 0x4000) && !do_isohybrid) {
        /* Patch MBR for GRUB2 */
        blk = t->bootsrc[0]->sections[0].block * 4 +
                                                Libisofs_grub2_mbr_patch_offsT;
        wpt = buf + Libisofs_grub2_mbr_patch_poS;
        for (i = 0; i < 8; i++)
             wpt[i] = blk >> (i * 8);
    }

    return ISO_SUCCESS;
}

/* Choose *heads_per_cyl so that
   - *heads_per_cyl * secs_per_head * 1024 >= imgsize / 512
   - *heads_per_cyl * secs_per_head is divisible by 4
   - it is as small as possible (to reduce aligment overhead)
   - it is <= 255
   @return 1= success , 0= cannot achieve goals
*/
static
int try_sph(off_t imgsize, int secs_per_head, int *heads_per_cyl, int flag)
{
    off_t hd_blocks, hpc;

    hd_blocks= imgsize / 512;
    hpc = hd_blocks / secs_per_head / 1024;
    if (hpc * secs_per_head * 1024 < hd_blocks)
        hpc++;
    if ((secs_per_head % 4) == 0) {
        ;
    } else if ((secs_per_head % 2) == 0) {
        hpc += (hpc % 2);
    } else if(hpc % 4) {
        hpc += 4 - (hpc % 4);
    }
    if (hpc > 255)
        return 0;
    *heads_per_cyl = hpc;
    return 1;
}

int iso_align_isohybrid(Ecma119Image *t, int flag)
{
    int sa_type, ret, always_align;
    uint32_t img_blocks;
    off_t imgsize, cylsize = 0, frac;
    char *msg = NULL;

    LIBISO_ALLOC_MEM(msg, char, 160);
    sa_type = (t->system_area_options >> 2) & 0x3f;
    if (sa_type != 0)
        {ret = ISO_SUCCESS; goto ex;}
    always_align = (t->system_area_options >> 8) & 3;

    /* Take into account the backup GPT */;
    ret = precompute_gpt(t);
    if (ret < 0)
        goto ex;

    img_blocks = t->curblock + t->tail_blocks + t->gpt_backup_size;
    imgsize = ((off_t) img_blocks) * (off_t) 2048;
    if (((t->system_area_options & 3) || always_align)
        && (off_t) (t->partition_heads_per_cyl * t->partition_secs_per_head
                    * 1024) * (off_t) 512 < imgsize) {
        /* Choose small values which can represent the image size */
        /* First try 32 sectors per head */
        ret = try_sph(imgsize, 32, &(t->partition_heads_per_cyl), 0);
        if (ret == 1) {
            t->partition_secs_per_head = 32;
        } else {
            /* Did not work with 32. Try 63 */
            t->partition_secs_per_head = 63;
            ret = try_sph(imgsize, 63, &(t->partition_heads_per_cyl), 0);
            if (ret != 1)
                t->partition_heads_per_cyl = 255;
        }
        cylsize = t->partition_heads_per_cyl * t->partition_secs_per_head *512;
        frac = imgsize % cylsize;
        sprintf(msg, "Automatically adjusted MBR geometry to %d/%d/%d",
                      (int) (imgsize / cylsize + !!frac),
                      t->partition_heads_per_cyl, t->partition_secs_per_head);
        iso_msgs_submit(0, msg, 0, "NOTE", 0);
    }

    if (always_align == 2)
        {ret = ISO_SUCCESS; goto ex;}

    cylsize = 0;
    if (t->catalog != NULL &&
               (t->catalog->bootimages[0]->isolinux_options & 0x0a) == 0x02) {
        /* Check for isolinux image with magic number of 3.72 and produce
           an MBR from our built-in template. (Deprecated since 31 Mar 2010)
        */
        if (img_blocks >= 0x40000000)
            {ret = ISO_SUCCESS; goto ex;}
        cylsize = 64 * 32 * 512;

    } else if (t->system_area_options & 2) {
        /* Patch externally provided system area as isohybrid MBR */
        if (t->catalog == NULL || t->system_area_data == NULL) {
            /* isohybrid makes only sense together with ISOLINUX boot image
               and externally provided System Area.
            */
            {ret = ISO_ISOLINUX_CANT_PATCH; goto ex;}
        }
        cylsize = t->partition_heads_per_cyl * t->partition_secs_per_head
                  * 512;
    } else if (always_align) {
        cylsize = t->partition_heads_per_cyl * t->partition_secs_per_head
                  * 512;
    } 
    if (cylsize == 0)
        {ret = ISO_SUCCESS; goto ex;}
    if (((double) imgsize) / (double) cylsize > 1024.0) {
        iso_msgs_submit(0,
                  "Image size exceeds 1024 cylinders. Cannot align partition.",
                  0, "WARNING", 0);
        iso_msgs_submit(0,
               "There are said to be BIOSes which will not boot this via MBR.",
               0, "WARNING", 0);
        {ret = ISO_SUCCESS; goto ex;}
    }

    frac = imgsize % cylsize;
    imgsize += (frac > 0 ? cylsize - frac : 0);

    frac = imgsize - ((off_t) img_blocks) * (off_t) 2048;
    if (frac == 0)
        {ret = ISO_SUCCESS; goto ex;}
    if (frac % 2048) {
        sprintf(msg,
             "Cylinder size %d not divisible by 2048. Cannot align partition.",
             (int) cylsize);
        iso_msgs_submit(0, msg, 0, "WARNING", 0);
    } else {
        t->tail_blocks += frac / 2048;
    }
    ret = ISO_SUCCESS;
ex:;
    LIBISO_FREE_MEM(msg);
    return ret;
}


int iso_register_apm_entry(Ecma119Image *t,
                           struct iso_apm_partition_request *req, int flag)
{
    struct iso_apm_partition_request *entry;

    if (t->apm_req_count >= ISO_APM_ENTRIES_MAX)
        return ISO_BOOT_TOO_MANY_APM;
    entry = calloc(1, sizeof(struct iso_apm_partition_request));
    if (entry == NULL)
        return ISO_OUT_OF_MEM;
    
    memcpy(entry, req, sizeof(struct iso_apm_partition_request));
    t->apm_req[t->apm_req_count] = entry;
    t->apm_req_count++;
    return ISO_SUCCESS;
}


int iso_register_mbr_entry(Ecma119Image *t,
                           struct iso_mbr_partition_request *req, int flag)
{
    struct iso_mbr_partition_request *entry;

    if (t->mbr_req_count >= ISO_MBR_ENTRIES_MAX)
        return ISO_BOOT_TOO_MANY_MBR;
    entry = calloc(1, sizeof(struct iso_mbr_partition_request));
    if (entry == NULL)
        return ISO_OUT_OF_MEM;
    
    memcpy(entry, req, sizeof(struct iso_mbr_partition_request));
    t->mbr_req[t->mbr_req_count] = entry;
    t->mbr_req_count++;
    return ISO_SUCCESS;
}


int iso_register_gpt_entry(Ecma119Image *t,
                           struct iso_gpt_partition_request *req, int flag)
{
    struct iso_gpt_partition_request *entry;

    if (t->gpt_req_count >= ISO_GPT_ENTRIES_MAX)
        return ISO_BOOT_TOO_MANY_GPT;
    entry = calloc(1, sizeof(struct iso_gpt_partition_request));
    if (entry == NULL)
        return ISO_OUT_OF_MEM;
    
    memcpy(entry, req, sizeof(struct iso_gpt_partition_request));
    t->gpt_req[t->gpt_req_count] = entry;
    t->gpt_req_count++;
    return ISO_SUCCESS;
}

#ifdef Libisofs_with_uuid_generatE

static void swap_uuid(void *u_pt)
{
     uint8_t tr, *u;

     u = (uint8_t *) u_pt;
     tr = u[0]; u[0] = u[3]; u[3] = tr;
     tr = u[1]; u[1] = u[2]; u[2] = tr;
     tr = u[4]; u[4] = u[5]; u[5] = tr;
     tr = u[6]; u[6] = u[7]; u[7] = tr;
}

#endif /* Libisofs_with_uuid_generatE */


/* CRC-32 as of GPT and Ethernet.
   Parameters are deduced from a table driven implementation in isohybrid.c
*/
unsigned int iso_crc32_gpt(unsigned char *data, int count, int flag)
{   
    unsigned int acc, top, result = 0;
    long int i;

    /* Chosen so that the CRC of 0 bytes of input is 0x00000000 */
    acc = 0x46af6449;

    /* Process data bits and flush numerator by 32 zero bits */
    for (i = 0; i < count * 8 + 32; i++) {
        top = acc & 0x80000000;
        acc = (acc << 1);
        if (i < count * 8)
            /* The least significant bits of input bytes get processed first */
            acc |= ((data[i / 8] >> (i % 8)) & 1);
        if (top)
            /* Division by the generating polynomial */
            acc ^= 0x04c11db7;
    }
    /* Mirror residue bits */
    for (i = 0; i < 32; i++)
        if (acc & (1 << i))
            result |= 1 << (31 - i);
    /* Return bit complement */
    return result ^ 0xffffffff;
}


void iso_random_uuid(Ecma119Image *t, uint8_t uuid[16])
{
#ifdef Libisofs_with_uuid_generatE
    uuid_t u;
#else
    uint8_t u[16];
    /* produced by uuid_generate() and byte-swapped to isohybrid.c habits */
    static uint8_t uuid_template[16] = {
        0xee, 0x29, 0x9d, 0xfc, 0x65, 0xcc, 0x7c, 0x40,
        0x92, 0x61, 0x5b, 0xcd, 0x6f, 0xed, 0x08, 0x34
    };
    static uint8_t uuid_urandom[16];
    uint32_t rnd, salt;
    struct timeval tv;
    struct timezone tz;
    pid_t pid;
    static int counter = 0, use_urandom = 0;
    int i, ret, fd;
#endif

#ifdef Libisofs_with_uuid_generatE

    uuid_generate(u);
    swap_uuid((void *) u);
    memcpy(uuid, u, 16);

#else

    /* First try /dev/urandom.
       (Weakening the result by 8 bit saves a lot of pool entropy.)
    */
    if ((counter & 0xff) == 0) {
        fd = open("/dev/urandom", O_RDONLY);
        if (fd == -1)
            goto fallback;
        ret = read(fd, uuid_urandom, 16);
        if (ret != 16) {
            close(fd);
            goto fallback;
        }
        /* Mark as UUID version 4 */
        uuid_urandom[7] = (uuid_urandom[7] & 0x0f) | 0x40;
        uuid_urandom[8] = (uuid_urandom[8] & 0x3f) | 0x80;
        close(fd);
        use_urandom = 1;
    }
    if (!use_urandom)
        goto fallback;
    memcpy(uuid, uuid_urandom, 16);
    uuid[9] ^= counter & 0xff;
    counter++;
    return;

fallback:;
    pid = getpid();
    salt = iso_crc32_gpt((unsigned char *) t, sizeof(Ecma119Image), 0) ^ pid; 

    /* This relies on the uniqueness of the template and the rareness of
       bootable ISO image production via libisofs. Estimated 53 bits of
       entropy should influence the production of a single day. 
       So first collisions are to be expected with about 100 million images
       per day.
    */
    memcpy(u, uuid_template, 16);
    gettimeofday(&tv, &tz);
    for (i = 0; i < 4; i++)
        u[i] = (salt >> (8 * i)) & 0xff;
    for (i = 0; i < 2; i++)
        u[4 + i] = (pid >> (8 * i)) & 0xff;
    u[6] = ((salt >> 8) ^ (pid >> 16)) & 0xff;
    rnd = ((0xffffff & tv.tv_sec) << 8) |
          (((tv.tv_usec >> 16) ^ (salt & 0xf0)) & 0xff);
    u[9] ^= counter & 0xff;
    for (i = 0; i < 4; i++)
        u[10 + i] ^= (rnd >> (8 * i)) & 0xff;
    u[14] ^= (tv.tv_usec >> 8) & 0xff;
    u[15] ^= tv.tv_usec & 0xff;
    counter++;

    memcpy(uuid, u, 16);

#endif /* ! Libisofs_with_uuid_generatE */    

}


/* Probably already called by tail_writer_compute_data_blocks via
   iso_align_isohybrid
*/
static int precompute_gpt(Ecma119Image *t)
{
    uint32_t gpt_part_start;
    int ret, sa_type;
    int gpt_count, gpt_idx[128], apm_count;

    /* Avoid repetition by  gpt_tail_writer_compute_data_blocks */
    t->gpt_is_computed = 1;


    /* Assess APM and GPT requests of isohybrid */
    sa_type = (t->system_area_options >> 2) & 0x3f;
    if (sa_type == 0 && (t->system_area_options & 3) == 2) {

        /* >>> ISOHYBRID :
               Shall isohybrid be combinable with other APM and GPT requesters ?
        */;
        /* <<< provisorily: Not compatible */
        ret = assess_isohybrid_gpt_apm(t, &gpt_count, gpt_idx, &apm_count, 0);
        if (ret < 0)
            return ret;
        if (t->gpt_req_count > 0 && gpt_count > 0)
            return ISO_BOOT_GPT_OVERLAP;
        if (t->apm_req_count > 0 && apm_count > 0)
            return ISO_BOOT_APM_OVERLAP;
        /* Register the GPT and APM partition entries */
        ret = assess_isohybrid_gpt_apm(t, &gpt_count, gpt_idx, &apm_count, 1);
        if (ret < 0)
            return ret;
    }


    /* Rectify APM requests early in order to learn the size of GPT.
       iso_write_apm() relies on this being already done here.
       So perform even if no GPT is required.
    */
    ret = rectify_apm(t);
    if (ret < 0)
        return ret;

#ifdef NIX
    /* Disabled */
    
    /* <<< ts B20526 : Dummy mock-up */
    if (t->gpt_req_count <= 0) { 

        /* <<< ??? Move to system_area.h and publish as macro ?
                   Or to make_isohybrid_mbr.c ?
        */
        static uint8_t hfs_uuid[16] = {
            0x00, 0x53, 0x46, 0x48, 0x00, 0x00, 0xaa, 0x11,
            0xaa, 0x11, 0x00, 0x30, 0x65, 0x43, 0xec, 0xac
        };
        static uint8_t basic_data_uuid[16] = {
            0xa2, 0xa0, 0xd0, 0xeb, 0xe5, 0xb9, 0x33, 0x44,
            0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7
        };

        uint8_t gpt_name[72];
        static uint8_t zero_uuid[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
        static uint64_t gpt_flags = (((uint64_t) 1) << 60) | 1;

        memset(gpt_name, 0, 72);
        gpt_name[0] = 'T'; gpt_name[2] = '1';
        strcpy((char *) gpt_name, "GPT Test 1");
        iso_ascii_utf_16le(gpt_name);
        /*
        ret = iso_quick_gpt_entry(t, 16, 20, hfs_uuid, zero_uuid,
                                  gpt_flags, gpt_name);
        / * Caution: Size 90 causes intentional partition overlap error * /
        ret = iso_quick_gpt_entry(t, 30, 90, hfs_uuid, zero_uuid,
                                  gpt_flags, gpt_name);
        */ 
        ret = iso_quick_gpt_entry(t, 30, 40, hfs_uuid, zero_uuid,
                                  gpt_flags, gpt_name);
        if (ret < 0)
            return ret;
        strcpy((char *) gpt_name, "GPT Test 2");
        iso_ascii_utf_16le(gpt_name);
        ret = iso_quick_gpt_entry(t, 110,  60, basic_data_uuid, zero_uuid,
                                  gpt_flags, gpt_name);
        if (ret < 0)
            return ret;
    }
#endif /* NIX */

    /* Is a GPT requested ? */
    t->gpt_backup_end = 0;
    t->gpt_max_entries = 0;
    if (t->gpt_req_count == 0)
        return ISO_SUCCESS;

    /* Determine GPT partition start in System Area, */
    gpt_part_start = 0;
    if (t->apm_req_count > 0) 
        gpt_part_start = (t->apm_req_count + 1) * (t->apm_block_size / 512);
    if (gpt_part_start < 2)
        gpt_part_start = 2; 
    else if (gpt_part_start >= 64)
        return ISO_BOOT_TOO_MANY_GPT;
    t->gpt_part_start = gpt_part_start;

    /* Necessary number of 2K blocks */
    t->gpt_max_entries = (64 - t->gpt_part_start) * 4;
    t->gpt_backup_size = ((t->gpt_max_entries / 4 + 1) * 512 + 2047) / 2048;

    return ISO_SUCCESS;
}


static int gpt_tail_writer_compute_data_blocks(IsoImageWriter *writer)
{
    Ecma119Image *t;
    int ret;

    if (writer == NULL)
        return ISO_ASSERT_FAILURE;
    t = writer->target;

    if (! t->gpt_is_computed) {
        ret = precompute_gpt(t);
        if (ret < 0)
            return ret;
    }
    t->curblock += t->gpt_backup_size;
    /* The ISO block number after the backup GPT header */
    t->gpt_backup_end = t->curblock;

    return ISO_SUCCESS;
}


static int gpt_tail_writer_write_vol_desc(IsoImageWriter *writer)
{
    return ISO_SUCCESS;
}


static int gpt_tail_writer_write_data(IsoImageWriter *writer)
{
    Ecma119Image *t;
    uint8_t *head, *new_head, *entries;
    uint8_t *backup_buf = NULL;
    uint32_t crc, i;
    uint64_t part_start;
    int ret;

    t = writer->target;
    if (t->gpt_backup_end == 0 || t->gpt_max_entries == 0)
        return ISO_SUCCESS; /* No backup GPT area reserved by compute_data() */

    backup_buf = calloc(1, t->gpt_backup_size * 2048);
    if (backup_buf == NULL)
        return ISO_OUT_OF_MEM;
    memset(backup_buf, 0, t->gpt_backup_size * 2048);

    /* Check whether GPT header block came through */
    head = t->sys_area_as_written + 512;
    if (strncmp((char *) head, "EFI PART", 8) != 0) {
tampered_head:;
        /* Send error message but do not prevent further image production */
        iso_msgs_submit(0,
                 "GPT header block was altered before writing to System Area.",
                 0, "FAILURE", 0);
        goto write_zeros;
    }
    for (i = 92; i < 512; i++)
        if (head[i])
            goto tampered_head;

    /* Patch memorized header block */
    new_head = backup_buf + t->gpt_backup_size * 2048 - 512;
    memcpy(new_head, head, 512);
    /* Exchange "Location of this header" and "Location of header backup" */
    memcpy(new_head + 24, head + 32, 8);
    memcpy(new_head + 32, head + 24, 8);
    /* Point to the backup partition entries */
    part_start = ((uint64_t) t->gpt_backup_end) * 4
                 - 1 - t->gpt_max_entries / 4;
    iso_lsb(new_head + 72, part_start & 0xffffffff, 4);
    iso_lsb(new_head + 76, (part_start >> 32) & 0xffffffff, 4);

    /* Compute new header CRC */
    memset(new_head + 16, 0, 4);
    crc = iso_crc32_gpt((unsigned char *) new_head, 92, 0); 
    iso_lsb(new_head + 16, crc, 4);

    /* Copy GPT entries */
    entries = t->sys_area_as_written + t->gpt_part_start * 512;
    memcpy(new_head - t->gpt_max_entries * 128,
           entries, t->gpt_max_entries * 128);
              
    ret = iso_write(t, backup_buf, t->gpt_backup_size * 2048);
    free(backup_buf);
    if (ret < 0)
        return ret;
    return ISO_SUCCESS;

write_zeros:;
    ret = iso_write(t, backup_buf, t->gpt_backup_size * 2048);
    free(backup_buf);
    if (ret < 0)
        return ret;
    return ISO_SUCCESS;
}


static int gpt_tail_writer_free_data(IsoImageWriter *writer)
{
    return ISO_SUCCESS;
}


int gpt_tail_writer_create(Ecma119Image *target)
{
    IsoImageWriter *writer;

    writer = calloc(1, sizeof(IsoImageWriter));
    if (writer == NULL) {
        return ISO_OUT_OF_MEM;
    }
    writer->compute_data_blocks = gpt_tail_writer_compute_data_blocks;
    writer->write_vol_desc = gpt_tail_writer_write_vol_desc;
    writer->write_data = gpt_tail_writer_write_data;
    writer->free_data = gpt_tail_writer_free_data;
    writer->data = NULL;
    writer->target = target;

    /* add this writer to image */
    target->writers[target->nwriters++] = writer;

    return ISO_SUCCESS;
}


/* ----------------------  Partition Prepend Writer --------------------- */


static int partprepend_writer_compute_data_blocks(IsoImageWriter *writer)
{
    Ecma119Image *t;
    IsoFileSrc *src;
    int ret, will_have_gpt = 0, with_chrp = 0, i;
    static uint8_t zero_uuid[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    static uint64_t gpt_flags = (((uint64_t) 1) << 60) | 1;
    uint8_t gpt_name[72];

    /* <<< ??? Move to system_area.h and publish as macro ? */
    static uint8_t efi_sys_uuid[16] = {
       0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
       0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b
    };

    if (writer == NULL)
        return ISO_ASSERT_FAILURE;
    t = writer->target;

    with_chrp = ((t->system_area_options & 0x3cff) == 0x0400);
    if (t->efi_boot_partition != NULL || (t->hfsplus && !with_chrp) ||
        t->gpt_req_count > 0)
        will_have_gpt = 1;

    if (t->efi_boot_partition != NULL) {
        if (t->efi_boot_part_filesrc != NULL) {
            /* A file in the emerging ISO image shall store its content
               as prepended partition.
               Install absolute block addresses and determine partition size.
            */
            src = t->efi_boot_part_filesrc;
            t->efi_boot_part_size = 0;
            for (i = 0; i < src->nsections; i++) {
                src->sections[i].block = t->curblock + t->efi_boot_part_size;
                t->efi_boot_part_size += (src->sections[i].size + 2047) / 2048;
            }
        } else {
            ret = compute_partition_size(t->efi_boot_partition,
                                         &(t->efi_boot_part_size), 0);
            if (ret < 0)
                return ret;
        }
        memset(gpt_name, 0, 72);
        strcpy((char *) gpt_name, "EFI boot partition");
        iso_ascii_utf_16le(gpt_name);
        ret = iso_quick_gpt_entry(t, t->curblock, t->efi_boot_part_size,
                                 efi_sys_uuid, zero_uuid, gpt_flags, gpt_name);
        if (ret < 0)
            return ret;
        t->curblock += t->efi_boot_part_size;
    }

    if (with_chrp) {
        /* CHRP is not compatible with any other partition in MBR */
        if (t->prep_partition != NULL || t->fat || will_have_gpt ||
            t->mbr_req_count > 0)
            return ISO_BOOT_MBR_OVERLAP;
        ret = iso_quick_mbr_entry(t, (uint64_t) 0, (uint64_t) 0,
                                  0x96, 0x80, 0);
        if (ret < 0)
            return ret;
        return ISO_SUCCESS;
    }

    if (t->prep_partition != NULL) {
        ret = compute_partition_size(t->prep_partition, &(t->prep_part_size),
                                     0);
        if (ret < 0)
            return ret;
    }
    if (t->prep_part_size > 0 || t->fat || will_have_gpt) {
        /* Protecting MBR entry for ISO start or whole ISO */
        ret = iso_quick_mbr_entry(t, will_have_gpt ? (uint64_t) 1 :
                                  ((uint64_t) t->partition_offset) * 4,
                                  (uint64_t) 0,
                                  will_have_gpt ? 0xee : 0xcd, 0, 0);
        if (ret < 0)
            return ret;
    }
    if (t->prep_part_size > 0) {
        ret = iso_quick_mbr_entry(t, ((uint64_t) t->curblock) * 4,
                                  ((uint64_t) t->prep_part_size) * 4,
                                  0x41, 0, 0);
        if (ret < 0)
            return ret;
        t->curblock += t->prep_part_size;
    }
    if (t->prep_part_size > 0 || t->fat) {
        /* FAT partition or protecting MBR entry for ISO end */
        ret = iso_quick_mbr_entry(t, ((uint64_t) t->curblock) * 4,
                                  (uint64_t) 0,
                                  t->fat ? 0x0c : 0xcd, 0, 0);
        if (ret < 0)
            return ret;
    }

    return ISO_SUCCESS;
}


static int partprepend_writer_write_vol_desc(IsoImageWriter *writer)
{
    return ISO_SUCCESS;
}


static int partprepend_writer_write_data(IsoImageWriter *writer)
{
    Ecma119Image *t;
    int ret;

    t = writer->target;

    if (t->efi_boot_partition != NULL && t->efi_boot_part_size) {

        if (t->efi_boot_part_filesrc != NULL) {
            ret = iso_filesrc_write_data(t, t->efi_boot_part_filesrc,
                                         NULL, NULL, 0);
        } else {
            ret = iso_write_partition_file(t, t->efi_boot_partition,
                                       (uint32_t) 0, t->efi_boot_part_size, 0);
        }
        if (ret < 0)
            return ret;
    }
    if (t->prep_partition != NULL && t->prep_part_size) {
        ret = iso_write_partition_file(t, t->prep_partition, (uint32_t) 0,
                                       t->prep_part_size, 0);
        if (ret < 0)
            return ret;
    }

    return ISO_SUCCESS;
}


static int partprepend_writer_free_data(IsoImageWriter *writer)
{
    return ISO_SUCCESS;
}


int partprepend_writer_create(Ecma119Image *target)
{
    IsoImageWriter *writer;

    writer = calloc(1, sizeof(IsoImageWriter));
    if (writer == NULL) {
        return ISO_OUT_OF_MEM;
    }

    writer->compute_data_blocks = partprepend_writer_compute_data_blocks;
    writer->write_vol_desc = partprepend_writer_write_vol_desc;
    writer->write_data = partprepend_writer_write_data;
    writer->free_data = partprepend_writer_free_data;
    writer->data = NULL;
    writer->target = target;

    /* add this writer to image */
    target->writers[target->nwriters++] = writer;

    return ISO_SUCCESS;
}


