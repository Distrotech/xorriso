/*
 * Copyright (c) 2008 Vreixo Formoso
 * Copyright (c) 2012 Thomas Schmitt
 *
 * This file is part of the libisofs project; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

/*
 * Functions for dealing with the system area, this is, the first 16 blocks
 * of the image.
 *
 * At this time, this is only used for hybrid boot images with isolinux.
 */

#ifndef SYSTEM_AREA_H_
#define SYSTEM_AREA_H_

#include "ecma119.h"

/*
 * Create a MBR for an isohybrid enabled ISOLINUX boot image.
 *
 * It is assumed that the caller has verified the readiness of the boot image
 * by checking for 0xfb 0xc0 0x78 0x70 at bytes 0x40 to 0x43 of isolinux.bin.
 *
 * @param bin_lba    The predicted LBA of isolinux.bin within the emerging ISO.
 * @param img_blocks The predicted number of 2048 byte blocks in the ISO.
 *                   It will get rounded up to full MBs and that many blocks
 *                   must really be written as ISO 9660 image.
 * @param mbr        A buffer of at least 512 bytes to take the result which is
 *                   to be written as the very beginning of the ISO.
 * @param flag    unused yet, submit 0
 * @return  <0 = fatal, 0 = failed , 1 = ok , 2 = ok with size warning
 */
int make_isohybrid_mbr(int bin_lba, int *img_blocks, char *mbr, int flag);

/**
 * Write the system area for the given image to the given buffer.
 *
 * @param buf
 *      A buffer with at least 32 K allocated
 * @return
 *      1 if success, < 0 on error
 */
int iso_write_system_area(Ecma119Image *t, uint8_t *buf);

/**
 * Adjust t->tail_blocks to the eventual alignment needs of isohybrid booting.
 */
int iso_align_isohybrid(Ecma119Image *t, int flag);


/** 
 * Read the necessary ELF information from the first MIPS boot file.
 * See doc/boot_sectors.txt "DEC Boot Block" for "MIPS Little Endian".
 */
int iso_read_mipsel_elf(Ecma119Image *t, int flag);


/* Compute size and position of appended partitions.
*/
int iso_compute_append_partitions(Ecma119Image *t, int flag);


/* The parameter struct for production of a single MBR partition entry.
   See also the description of MBR in doc/boot_sectors.txt.
   No sorting by start sector and gap filling is done before the System Area
   gets written. But the entries may get assigned to a desired slot number
   in the table. 
   Requested entries with block_count == 0 get expanded to the start of
   the next requested entry resp. to image end, if no entry follows.
   start_block of a follwing entry must be at least a high as the sum of
   start_block and block_count of the previous entry.
   Empty requested entries will be represented as 16 bytes of 0.
*/
struct iso_mbr_partition_request {

    /* Always given in blocks of 512 bytes */
    uint64_t start_block;

    /* A block count of 0 means that the partition reaches up to the start of
       the next one.
    */
    uint64_t block_count;

    /* Partition type */
    uint8_t type_byte;

    /* 0x80 = bootable */
    uint8_t status_byte;

    /* If >= 1 && <= 4 : The partition slot number in MBR.
       If more than one partition desires the same slot, then an error
       ISO_BOOT_MBR_COLLISION occurs at registration time.
       Use iso_mbr_entry_slot_is_free() to detect this in advance.
       If desired_slot is 0, then the partition entry is put into the
       lowest MBR slot that is not occupied by an entry with desired_slot > 0
       or by an entry that was registered before this entry.
    */
    int desired_slot;

};

/* Copies the content of req and registers it in t.mbr_req[].
   I.e. after the call the submitted storage of req can be disposed or re-used.
   Submit 0 as value flag.
*/
int iso_register_mbr_entry(Ecma119Image *t,
                           struct iso_mbr_partition_request *req, int flag);

/* Convenience frontend for iso_register_mbr_entry().
   name and type are 0-terminated strings, which may get silently truncated.
*/
int iso_quick_mbr_entry(Ecma119Image *t, 
                        uint64_t start_block, uint64_t block_count,
                        uint8_t type_byte, uint8_t status_byte,
                        int desired_slot);

/* Peek in advance whether a desired slot number is already occupied by a
   registered MBR entry.
   Parameter slot may be between 0 and 4. 0 always returns "free".
   Return value is 0 if occupied, 1 if free, and -1 if the slot number is
   out of range.
*/
int iso_mbr_entry_slot_is_free(Ecma119Image *t, int slot);


/* The parameter struct for production of a single Apple Partition Map entry.
   See also the partial APM description in doc/boot_sectors.txt.
   The list of entries is stored in Ecma119Image.apm_req.
   The size of a block can be chosen by setting Ecma119Image.apm_block_size.
   If an entry has start_block <=1, then its block_count will be adjusted
   to the final size of the partition map.
   If no such entry is requested, then it will be prepended automatically
   with name "Apple" and type "Apple_partition_map".
   The requested entries will get sorted and gaps will be filled by more
   entries.
*/
struct iso_apm_partition_request {

    /* Given in blocks of 2 KiB unless (Ecma119Image.apm_req_flags & 4).
       Written to the ISO image according to Ecma119Image.apm_block_size.
    */
    uint32_t start_block;
    uint32_t block_count;

    /* All 32 bytes get copied to the system area.
       Take care to pad up short strings by 0.
    */
    uint8_t name[32];
    uint8_t type[32];
};

/* Copies the content of req and registers it in t.apm_req[].
   I.e. after the call the submitted storage of req can be disposed or re-used.
   Submit 0 as value flag.
*/
int iso_register_apm_entry(Ecma119Image *t,
                           struct iso_apm_partition_request *req, int flag);

/* Convenience frontend for iso_register_apm_entry().
   name and type are 0-terminated strings, which may get silently truncated.
*/
int iso_quick_apm_entry(Ecma119Image *t, 
           uint32_t start_block, uint32_t block_count, char *name, char *type);


/* CRC-32 as of GPT and Ethernet.
*/
unsigned int iso_crc32_gpt(unsigned char *data, int count, int flag);


/* These two pseudo-random generators produce byte strings which will
   surely not duplicate in the first 256 calls. If more calls are necessary
   in the same process, then one must wait until the output of
   gettimeofday(2) changes.
   It is advised to obtain them as late as possible, so that Ecma119Image *t
   can distinguish itself from other image production setups which might be
   run on other machines with the same process number at the same time.
*/

/* Produces a weakly random variation of a hardcoded real random uuid
*/
void iso_random_uuid(Ecma119Image *t, uint8_t uuid[16]);


/* The parameter struct for production of a single GPT entry.
   See also the partial GPT description in doc/boot_sectors.txt.
   The list of entries is stored in Ecma119Image.gpt_req.
   The GPT header block at byte 0x200 will get produced automatically.
   The requested entries will get sorted and gaps will be filled by more
   entries. Overlapping partitions are allowed only if
   (Ecma119Image.gpt_req_flags & 1).
   The block_count will be truncated to the image size before the GPT backup.

   The GPT entries will be stored after the Apple Partition Map, if such
   gets generated too. Both partition descriptions must fit into the 32 KiB
   of the ISO 9660 System Area.
   GPT can be combined with APM only if (Ecma119Image.apm_block_size > 512).
   Otherwise, block 1 of APM and GPT header block would collide.
   So Ecma119Image.apm_block_size is set automatically to 2048 if at least
   one GPT entry is requested. (One could try 1024 ...).
*/
struct iso_gpt_partition_request {

    /* Always given in blocks of 2 KiB.
       Written to the ISO image in blocks of 512.
    */
    uint32_t start_block;
    uint32_t block_count;

    /* The registered GUID which defines the partition type */
    uint8_t type_guid[16];

    /* An individual GUID which shall be unique to the partition.
       If the caller submits 0...0 then a (weak) random uuid will be generated.
    */
    uint8_t partition_guid[16];

    /* bit0= "System Partition" Do not alter,
       bit2= Legacy BIOS bootable (MBR partition type 0x80)
       bit60= read-only
    */
    uint64_t flags;

    /* Fill with text encoded as UTF-16LE.
       All 72 bytes get copied to the system area.
       Take care to pad up short strings by 0.
    */
    uint8_t name[72];
};

/* Copies the content of req and registers it in t.gpt_req[].
   I.e. after the call the submitted storage of req can be disposed or re-used.
   Submit 0 as value flag.
*/
int iso_register_gpt_entry(Ecma119Image *t,
                           struct iso_gpt_partition_request *req, int flag);

/* Convenience frontend for iso_register_gpt_entry().
   name has to be already encoded as UTF-16LE.
*/
int iso_quick_gpt_entry(Ecma119Image *t, 
                        uint32_t start_block, uint32_t block_count, 
                        uint8_t type_guid[16], uint8_t partition_guid[16],
                        uint64_t flags, uint8_t name[72]);


/* Internal helper that will be used by system_area.c and make_isohybrid_mbr.c
*/
int iso_write_gpt_header_block(Ecma119Image *t, uint32_t img_blocks,
                               uint8_t *buf, uint32_t max_entries,
                               uint32_t part_start, uint32_t p_arr_crc);

/* Creates the Partition Prepend writer.
*/
int partprepend_writer_create(Ecma119Image *target);

/* Creates the GPT backup tail writer.
*/
int gpt_tail_writer_create(Ecma119Image *target);

/* Only for up to 36 characters ISO-8859-1 (or ASCII) input */
void iso_ascii_utf_16le(uint8_t gap_name[72]);


/* Parameters of MBR patching for GRUB2
   Might later become variables in Ecma119Image
*/
#define Libisofs_grub2_mbr_patch_poS    0x1b0
#define Libisofs_grub2_mbr_patch_offsT      4

/* Parameters of SUN Disk Label patching for GRUB2
   See API iso_image_set_sparc_core().
*/
#define Libisofs_grub2_sparc_patch_adr_poS    0x228
#define Libisofs_grub2_sparc_patch_size_poS   0x230    

#endif /* SYSTEM_AREA_H_ */
