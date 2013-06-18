/*
 * Copyright (c) 2007 Vreixo Formoso
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
#include "eltorito.h"
#include "fsource.h"
#include "filesrc.h"
#include "image.h"
#include "messages.h"
#include "writer.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * This table should be written with the actual values at offset
 * 8 of boot image, when used ISOLINUX boot loader
 */
struct boot_info_table {
    uint8_t bi_pvd              BP(1, 4);  /* LBA of primary volume descriptor */
    uint8_t bi_file             BP(5, 8);  /* LBA of boot file */
    uint8_t bi_length           BP(9, 12); /* Length of boot file */
    uint8_t bi_csum             BP(13, 16); /* Checksum of boot file */
    uint8_t bi_reserved         BP(17, 56); /* Reserved */
};

/**
 * Structure for each one of the four entries in a partition table on a
 * hard disk image.
 */
struct partition_desc {
    uint8_t boot_ind;
    uint8_t begin_chs[3];
    uint8_t type;
    uint8_t end_chs[3];
    uint8_t start[4];
    uint8_t size[4];
};

/**
 * Structures for a Master Boot Record of a hard disk image.
 */
struct hard_disc_mbr {
    uint8_t code_area[440];
    uint8_t opt_disk_sg[4];
    uint8_t pad[2];
    struct partition_desc partition[4];
    uint8_t sign1;
    uint8_t sign2;
};

/* API */
int el_torito_set_boot_platform_id(ElToritoBootImage *bootimg, uint8_t id)
{
    bootimg->platform_id = id;
    return 1;
}

/* API */
int el_torito_get_boot_platform_id(ElToritoBootImage *bootimg)
{
    return bootimg->platform_id;
}

/**
 * Sets the load segment for the initial boot image. This is only for
 * no emulation boot images, and is a NOP for other image types.
 */
void el_torito_set_load_seg(ElToritoBootImage *bootimg, short segment)
{
    if (bootimg->type != 0)
        return;
    bootimg->load_seg = segment;
}

/* API */
int el_torito_get_load_seg(ElToritoBootImage *bootimg)
{
   if (bootimg->load_seg < 0)
       return 0xffff - bootimg->load_seg;
   return bootimg->load_seg;
}
 
/**
 * Sets the number of sectors (512b) to be load at load segment during
 * the initial boot procedure. This is only for no emulation boot images,
 * and is a NOP for other image types.
 */
void el_torito_set_load_size(ElToritoBootImage *bootimg, short sectors)
{
    if (bootimg->type != 0)
        return;
    bootimg->load_size = sectors;
}

/* API */
int el_torito_get_load_size(ElToritoBootImage *bootimg)
{
   if (bootimg->load_size < 0)
       return 0xffff - bootimg->load_size;
   return bootimg->load_size;
}

/**
 * Marks the specified boot image as not bootable
 */
void el_torito_set_no_bootable(ElToritoBootImage *bootimg)
{
    bootimg->bootable = 0;
}

/* API */
int el_torito_get_bootable(ElToritoBootImage *bootimg)
{   
    return !!bootimg->bootable;
}

/* API */
int el_torito_set_id_string(ElToritoBootImage *bootimg, uint8_t id_string[28])
{
    memcpy(bootimg->id_string, id_string, 28);
    return 1;
}

/* API */
int el_torito_get_id_string(ElToritoBootImage *bootimg, uint8_t id_string[28])
{
    
    memcpy(id_string, bootimg->id_string, 28);
    return 1;
}

/* API */
int el_torito_set_selection_crit(ElToritoBootImage *bootimg, uint8_t crit[20])
{
    memcpy(bootimg->selection_crit, crit, 20);
    return 1;
}

/* API */
int el_torito_get_selection_crit(ElToritoBootImage *bootimg, uint8_t crit[20])
{
    
    memcpy(crit, bootimg->selection_crit, 20);
    return 1;
}

/* API */
int el_torito_seems_boot_info_table(ElToritoBootImage *bootimg, int flag)
{
    switch (flag & 15) {
    case 0:
        return bootimg->seems_boot_info_table;
    case 1:
        return bootimg->seems_grub2_boot_info;
    }
    return 0;
}

/**
 * Specifies that this image needs to be patched. This involves the writing
 * of a 56 bytes boot information table at offset 8 of the boot image file.
 * The original boot image file won't be modified.
 * This is needed for isolinux boot images.
 */
void el_torito_patch_isolinux_image(ElToritoBootImage *bootimg)
{
    bootimg->isolinux_options |= 0x01;
}


/**
 * Specifies options for IsoLinux boot images. This should only be used with
 * isolinux boot images.
 *
 * @param options
 *        bitmask style flag. The following values are defined:
 *
 *        bit 0 -> 1 to path the image, 0 to not
 *                 Patching the image involves the writing of a 56 bytes
 *                 boot information table at offset 8 of the boot image file.
 *                 The original boot image file won't be modified. This is needed
 *                 to allow isolinux images to be bootable.
 *        bit 1 -> 1 to generate an hybrid image, 0 to not
 *                 An hybrid image is a boot image that boots from either CD/DVD
 *                 media or from USB sticks. For that, you should use an isolinux
 *                 image that supports hybrid mode. Recent images support this.
 * @return
 *      1 if success, < 0 on error
 * @since 0.6.12
 */
int el_torito_set_isolinux_options(ElToritoBootImage *bootimg, int options, int flag)
{
    bootimg->isolinux_options = (options & 0x03ff);
    return ISO_SUCCESS;
}

/* API */
int el_torito_get_isolinux_options(ElToritoBootImage *bootimg, int flag)
{
    return bootimg->isolinux_options & 0x03ff;
}

/* API */
int el_torito_get_boot_media_type(ElToritoBootImage *bootimg,
                                  enum eltorito_boot_media_type *media_type)
{
    if (bootimg) {
        switch (bootimg->type) {
        case 1:
        case 2:
        case 3:
            *media_type = ELTORITO_FLOPPY_EMUL;
            return 1;
        case 4:
            *media_type = ELTORITO_HARD_DISC_EMUL;
            return 1;
        case 0:
            *media_type = ELTORITO_NO_EMUL;
            return 1;
        default:
            /* should never happen */
            return ISO_ASSERT_FAILURE;
            break;
        }
    }
    return ISO_WRONG_ARG_VALUE;
}

static
int iso_tree_add_boot_node(IsoDir *parent, const char *name, IsoBoot **boot)
{
    IsoBoot *node;
    IsoNode **pos;
    time_t now;
    int ret;

    if (parent == NULL || name == NULL || boot == NULL) {
        return ISO_NULL_POINTER;
    }
    if (boot) {
        *boot = NULL;
    }

    /* check if the name is valid */
    ret = iso_node_is_valid_name(name);
    if (ret < 0)
        return ret;

    /* find place where to insert */
    pos = &(parent->children);
    while (*pos != NULL && strcmp((*pos)->name, name) < 0) {
        pos = &((*pos)->next);
    }
    if (*pos != NULL && !strcmp((*pos)->name, name)) {
        /* a node with same name already exists */
        return ISO_NODE_NAME_NOT_UNIQUE;
    }

    node = calloc(1, sizeof(IsoBoot));
    if (node == NULL) {
        return ISO_OUT_OF_MEM;
    }

    node->node.refcount = 1;
    node->node.type = LIBISO_BOOT;
    node->node.name = strdup(name);
    if (node->node.name == NULL) {
        free(node);
        return ISO_OUT_OF_MEM;
    }
    node->lba = 0;
    node->size = 0;
    node->content = NULL;

    /* atributes from parent */
    node->node.mode = S_IFREG | (parent->node.mode & 0444);
    node->node.uid = parent->node.uid;
    node->node.gid = parent->node.gid;
    node->node.hidden = parent->node.hidden;

    /* current time */
    now = time(NULL);
    node->node.atime = now;
    node->node.ctime = now;
    node->node.mtime = now;

    /* add to dir */
    node->node.parent = parent;
    node->node.next = *pos;
    *pos = (IsoNode*)node;

    if (boot) {
        *boot = node;
    }
    return ++parent->nchildren;
}


static
int create_image(IsoImage *image, const char *image_path,
                 enum eltorito_boot_media_type type,
                 struct el_torito_boot_image **bootimg)
{
    int ret;
    struct el_torito_boot_image *boot;
    int boot_media_type = 0;
    int load_sectors = 0; /* number of sector to load */
    unsigned char partition_type = 0;
    off_t size;
    IsoNode *imgfile;
    IsoStream *stream;

    ret = iso_tree_path_to_node(image, image_path, &imgfile);
    if (ret < 0) {
        return ret;
    }
    if (ret == 0) {
        iso_msg_submit(image->id, ISO_NODE_DOESNT_EXIST, 0,
                       "El Torito boot image file missing in ISO image: '%s'",
                       image_path);
        return ISO_NODE_DOESNT_EXIST;
    }

    if (imgfile->type != LIBISO_FILE) {
        return ISO_BOOT_IMAGE_NOT_VALID;
    }

    stream = ((IsoFile*)imgfile)->stream;

    /* we need to read the image at least two times */
    if (!iso_stream_is_repeatable(stream)) {
        return ISO_BOOT_IMAGE_NOT_VALID;
    }

    size = iso_stream_get_size(stream);
    if (size <= 0) {
        iso_msg_submit(image->id, ISO_BOOT_IMAGE_NOT_VALID, 0,
                       "Boot image file is empty");
        return ISO_BOOT_IMAGE_NOT_VALID;
    }

    switch (type) {
    case ELTORITO_FLOPPY_EMUL:
        switch (size) {
        case 1200 * 1024:
            boot_media_type = 1; /* 1.2 meg diskette */
            break;
        case 1440 * 1024:
            boot_media_type = 2; /* 1.44 meg diskette */
            break;
        case 2880 * 1024:
            boot_media_type = 3; /* 2.88 meg diskette */
            break;
        default:
            iso_msg_submit(image->id, ISO_BOOT_IMAGE_NOT_VALID, 0,
                          "Invalid image size %d Kb. Must be one of 1.2, 1.44"
                          "or 2.88 Mb", iso_stream_get_size(stream) / 1024);
            return ISO_BOOT_IMAGE_NOT_VALID;
            break;
        }
        /* it seems that for floppy emulation we need to load
         * a single sector (512b) */
        load_sectors = 1;
        break;
    case ELTORITO_HARD_DISC_EMUL:
        {
        size_t i;
        struct hard_disc_mbr mbr;
        int used_partition;

        /* read the MBR on disc and get the type of the partition */
        ret = iso_stream_open(stream);
        if (ret < 0) {
            iso_msg_submit(image->id, ISO_BOOT_IMAGE_NOT_VALID, ret,
                          "Can't open image file.");
            return ret;
        }
        ret = iso_stream_read(stream, &mbr, sizeof(mbr));
        iso_stream_close(stream);
        if (ret != sizeof(mbr)) {
            iso_msg_submit(image->id, ISO_BOOT_IMAGE_NOT_VALID, 0,
                          "Can't read MBR from image file.");
            return ret < 0 ? ret : (int) ISO_FILE_READ_ERROR;
        }

        /* check valid MBR signature */
        if ( mbr.sign1 != 0x55 || mbr.sign2 != 0xAA ) {
            iso_msg_submit(image->id, ISO_BOOT_IMAGE_NOT_VALID, 0,
                          "Invalid MBR. Wrong signature.");
            return (int) ISO_BOOT_IMAGE_NOT_VALID;
        }

        /* ensure single partition */
        used_partition = -1;
        for (i = 0; i < 4; ++i) {
            if (mbr.partition[i].type != 0) {
                /* it's an used partition */
                if (used_partition != -1) {
                    iso_msg_submit(image->id, ISO_BOOT_IMAGE_NOT_VALID, 0,
                                  "Invalid MBR. At least 2 partitions: %d and "
                                  "%d, are being used\n", used_partition, i);
                    return ISO_BOOT_IMAGE_NOT_VALID;
                } else
                    used_partition = i;
            }
        }
        partition_type = mbr.partition[used_partition].type;
        }
        boot_media_type = 4;

        /* only load the MBR */
        load_sectors = 1;
        break;
    case ELTORITO_NO_EMUL:
        boot_media_type = 0;
        break;
    }

    boot = calloc(1, sizeof(struct el_torito_boot_image));
    if (boot == NULL) {
        return ISO_OUT_OF_MEM;
    }
    boot->image = (IsoFile*)imgfile;
    iso_node_ref(imgfile); /* get our ref */
    boot->bootable = 1;
    boot->seems_boot_info_table = 0;
    boot->isolinux_options = 0;
    boot->type = boot_media_type;
    boot->partition_type = partition_type;
    boot->load_seg = 0;
    boot->load_size = load_sectors;
    boot->platform_id = 0; /* 80x86 */
    memset(boot->id_string, 0, sizeof(boot->id_string));
    memset(boot->selection_crit, 0, sizeof(boot->selection_crit));
    if (bootimg) {
        *bootimg = boot;
    }

    return ISO_SUCCESS;
}

int iso_image_set_boot_image(IsoImage *image, const char *image_path,
                             enum eltorito_boot_media_type type,
                             const char *catalog_path,
                             ElToritoBootImage **boot)
{
    int ret, i;
    struct el_torito_boot_catalog *catalog;
    ElToritoBootImage *boot_image= NULL;
    IsoBoot *cat_node= NULL;

    if (image == NULL || image_path == NULL || catalog_path == NULL) {
        return ISO_NULL_POINTER;
    }
    if (image->bootcat != NULL) {
        return ISO_IMAGE_ALREADY_BOOTABLE;
    }

    /* create the node for the catalog */
    {
        IsoDir *parent;
        char *catdir = NULL, *catname = NULL;
        catdir = strdup(catalog_path);
        if (catdir == NULL) {
            return ISO_OUT_OF_MEM;
        }

        /* get both the dir and the name */
        catname = strrchr(catdir, '/');
        if (catname == NULL) {
            free(catdir);
            return ISO_WRONG_ARG_VALUE;
        }
        if (catname == catdir) {
            /* we are apending catalog to root node */
            parent = image->root;
        } else {
            IsoNode *p;
            catname[0] = '\0';
            ret = iso_tree_path_to_node(image, catdir, &p);
            if (ret <= 0) {
                iso_msg_submit(image->id, ISO_NODE_DOESNT_EXIST, 0,
         "Cannot find directory for El Torito boot catalog in ISO image: '%s'",
                               catdir);
                free(catdir);
                return ret < 0 ? ret : (int) ISO_NODE_DOESNT_EXIST;
            }
            if (p->type != LIBISO_DIR) {
                free(catdir);
                return ISO_WRONG_ARG_VALUE;
            }
            parent = (IsoDir*)p;
        }
        catname++;
        ret = iso_tree_add_boot_node(parent, catname, &cat_node);
        free(catdir);
        if (ret < 0) {
            return ret;
        }
    }

    /* create the boot image */
    ret = create_image(image, image_path, type, &boot_image);
    if (ret < 0) {
        goto boot_image_cleanup;
    }

    /* creates the catalog with the given image */
    catalog = calloc(1, sizeof(struct el_torito_boot_catalog));
    if (catalog == NULL) {
        ret = ISO_OUT_OF_MEM;
        goto boot_image_cleanup;
    }
    catalog->num_bootimages = 1;
    catalog->bootimages[0] = boot_image;
    for (i = 1; i < Libisofs_max_boot_imageS; i++)
        catalog->bootimages[i] = NULL;
    catalog->node = cat_node;
    catalog->sort_weight = 1000;                            /* slightly high */
    iso_node_ref((IsoNode*)cat_node);
    image->bootcat = catalog;

    if (boot) {
        *boot = boot_image;
    }

    return ISO_SUCCESS;

boot_image_cleanup:;
    if (cat_node) {
        iso_node_take((IsoNode*)cat_node);
        iso_node_unref((IsoNode*)cat_node);
    }
    if (boot_image) {
        iso_node_unref((IsoNode*)boot_image->image);
        free(boot_image);
    }
    return ret;
}

/**
 * Get the boot catalog and the El-Torito default boot image of an ISO image.
 *
 * This can be useful, for example, to check if a volume read from a previous
 * session or an existing image is bootable. It can also be useful to get
 * the image and catalog tree nodes. An application would want those, for
 * example, to prevent the user removing it.
 *
 * Both nodes are owned by libisofs and should not be freed. You can get your
 * own ref with iso_node_ref(). You can can also check if the node is already
 * on the tree by getting its parent (note that when reading El-Torito info
 * from a previous image, the nodes might not be on the tree even if you haven't
 * removed them). Remember that you'll need to get a new ref
 * (with iso_node_ref()) before inserting them again to the tree, and probably
 * you will also need to set the name or permissions.
 *
 * @param image
 *      The image from which to get the boot image.
 * @param boot
 *      If not NULL, it will be filled with a pointer to the boot image, if
 *      any. That  object is owned by the IsoImage and should not be freed by
 *      the user, nor dereferenced once the last reference to the IsoImage was
 *      disposed via iso_image_unref().
 * @param imgnode
 *      When not NULL, it will be filled with the image tree node. No extra ref
 *      is added, you can use iso_node_ref() to get one if you need it.
 * @param catnode
 *      When not NULL, it will be filled with the catnode tree node. No extra
 *      ref is added, you can use iso_node_ref() to get one if you need it.
 * @return
 *      1 on success, 0 is the image is not bootable (i.e., it has no El-Torito
 *      image), < 0 error.
 */
int iso_image_get_boot_image(IsoImage *image, ElToritoBootImage **boot,
                             IsoFile **imgnode, IsoBoot **catnode)
{
    if (image == NULL) {
        return ISO_NULL_POINTER;
    }
    if (image->bootcat == NULL) {
        return 0;
    }

    /* ok, image is bootable */
    if (boot) {
        *boot = image->bootcat->bootimages[0];
    }
    if (imgnode) {
        *imgnode = image->bootcat->bootimages[0]->image;
    }
    if (catnode) {
        *catnode = image->bootcat->node;
    }
    return ISO_SUCCESS;
}

int iso_image_get_bootcat(IsoImage *image, IsoBoot **catnode, uint32_t *lba,
                          char **content, off_t *size)
{
    IsoBoot *bootcat;

    *catnode = NULL;
    *lba = 0;
    *content = NULL;
    *size = 0;
    bootcat = image->bootcat->node;
    if (bootcat == NULL)
        return 0;
    *catnode = bootcat;
    *lba = bootcat->lba;
    *size = bootcat->size;
    if (bootcat->size > 0 && bootcat->content != NULL) {
        *content = calloc(1, bootcat->size);
        if (*content == NULL) 
            return ISO_OUT_OF_MEM;
        memcpy(*content, bootcat->content, bootcat->size);
    }
    return 1;
}

int iso_image_get_all_boot_imgs(IsoImage *image, int *num_boots,
               ElToritoBootImage ***boots, IsoFile ***bootnodes, int flag)
{
    int i;
    struct el_torito_boot_catalog *cat;

    if (image == NULL)
        return ISO_NULL_POINTER;
    if (image->bootcat == NULL)
        return 0;
    cat = image->bootcat;
    *num_boots = cat->num_bootimages;
    *boots = NULL;
    *bootnodes = NULL;
    if (*num_boots <= 0)
        return 0;
    *boots = calloc(*num_boots, sizeof(ElToritoBootImage *));
    *bootnodes = calloc(*num_boots, sizeof(IsoFile *));
    if(*boots == NULL || *bootnodes == NULL) {
        if (*boots != NULL)
            free(*boots);
        if (*bootnodes != NULL)
            free(*bootnodes);
        *boots = NULL;
        *bootnodes = NULL;
        return ISO_OUT_OF_MEM;
    }
    for (i = 0; i < *num_boots; i++) {
        (*boots)[i] = cat->bootimages[i];
        (*bootnodes)[i] = image->bootcat->bootimages[i]->image;
    }
    return 1;
}

/**
 * Removes the El-Torito bootable image.
 *
 * The IsoBoot node that acts as placeholder for the catalog is also removed
 * for the image tree, if there.
 * If the image is not bootable (don't have el-torito boot image) this function
 * just returns.
 */
void iso_image_remove_boot_image(IsoImage *image)
{
    if (image == NULL || image->bootcat == NULL)
        return;

    /*
     * remove catalog node from its parent and dispose it
     * (another reference is with the catalog)
     */
    if (iso_node_get_parent((IsoNode*) image->bootcat->node) != NULL) {
        iso_node_take((IsoNode*) image->bootcat->node);
        iso_node_unref((IsoNode*) image->bootcat->node);
    }

    /* free boot catalog and image, including references to nodes */
    el_torito_boot_catalog_free(image->bootcat);
    image->bootcat = NULL;
}

/* API */
int iso_image_add_boot_image(IsoImage *image, const char *image_path,
                             enum eltorito_boot_media_type type, int flag,
                             ElToritoBootImage **boot)
{
    int ret;
    struct el_torito_boot_catalog *catalog = image->bootcat;
    ElToritoBootImage *boot_img;

    if(catalog == NULL)
      return ISO_BOOT_NO_CATALOG;
    if (catalog->num_bootimages >= Libisofs_max_boot_imageS)
        return ISO_BOOT_IMAGE_OVERFLOW;
    ret = create_image(image, image_path, type, &boot_img);
    if (ret < 0) 
        return ret;
    catalog->bootimages[catalog->num_bootimages] = boot_img;
    catalog->num_bootimages++;
    if (boot != NULL)
        *boot = boot_img;
    return 1;
}

/* API */
int iso_image_set_boot_catalog_weight(IsoImage *image, int sort_weight)
{
    if (image->bootcat == NULL)
        return 0;
    image->bootcat->sort_weight = sort_weight;
    return 1;
}

/* API */
int iso_image_set_boot_catalog_hidden(IsoImage *image, int hide_attrs)
{
    if (image->bootcat == NULL)
        return 0;
    if (image->bootcat->node == NULL)
        return 0;
    iso_node_set_hidden((IsoNode *) image->bootcat->node, hide_attrs);
    return 1;
}


void el_torito_boot_catalog_free(struct el_torito_boot_catalog *cat)
{
    struct el_torito_boot_image *image;
    int i;

    if (cat == NULL) {
        return;
    }

    for (i = 0; i < Libisofs_max_boot_imageS; i++) {
        image = cat->bootimages[i];
        if (image == NULL)
    continue;
        if ((IsoNode*)image->image != NULL)
            iso_node_unref((IsoNode*)image->image);
        free(image);
    }
    if ((IsoNode*)cat->node != NULL)
        iso_node_unref((IsoNode*)cat->node);
    free(cat);
}

/**
 * Stream that generates the contents of a El-Torito catalog.
 */
struct catalog_stream
{
    Ecma119Image *target;
    uint8_t buffer[BLOCK_SIZE];
    int offset; /* -1 if stream is not opened */
};

static void
write_validation_entry(uint8_t *buf, uint8_t platform_id,
                       uint8_t id_string[24])
{
    size_t i;
    int checksum;

    struct el_torito_validation_entry *ve =
        (struct el_torito_validation_entry*)buf;
    ve->header_id[0] = 1;
    ve->platform_id[0] = platform_id;
    memcpy(ve->id_string, id_string, sizeof(ve->id_string));
    ve->key_byte1[0] = 0x55;
    ve->key_byte2[0] = 0xAA;
    /* calculate the checksum, to ensure sum of all words is 0 */
    checksum = 0;
    for (i = 0; i < sizeof(struct el_torito_validation_entry); i += 2) {
        checksum -= (int16_t) ((buf[i+1] << 8) | buf[i]);
    }
    iso_lsb(ve->checksum, checksum, 2);
}

static void
write_section_header(uint8_t *buf, Ecma119Image *t, int idx, int num_entries)
{
    char *id_string;

    struct el_torito_section_header *e =
        (struct el_torito_section_header *) buf;

    /* 0x90 = more section headers follow , 0x91 = final section */
    e->header_indicator[0] = 0x90 + (idx == t->catalog->num_bootimages - 1);
    e->platform_id[0] = t->catalog->bootimages[idx]->platform_id;
    e->num_entries[0] = num_entries & 0xff;
    e->num_entries[1] = (num_entries >> 8) & 0xff;;
    id_string = (char *) e->id_string;
    memcpy(id_string,  t->catalog->bootimages[idx]->id_string,
           sizeof(e->id_string));
}

/**
 * Write one section entry.
 * Usable for the Default Entry
 * and for Section Entries with Selection criteria type == 0
 */
static void
write_section_entry(uint8_t *buf, Ecma119Image *t, int idx)
{
    struct el_torito_boot_image *img;
    struct el_torito_section_entry *se =
        (struct el_torito_section_entry*)buf;

    img = t->catalog->bootimages[idx];

    se->boot_indicator[0] = img->bootable ? 0x88 : 0x00;
    se->boot_media_type[0] = img->type;
    iso_lsb(se->load_seg, img->load_seg, 2);
    se->system_type[0] = img->partition_type;
    iso_lsb(se->sec_count, img->load_size, 2);
    iso_lsb(se->block, t->bootsrc[idx]->sections[0].block, 4);
    se->selec_criteria[0] = img->selection_crit[0];
    memcpy(se->vendor_sc, img->selection_crit + 1, 19);
}

static
int catalog_open(IsoStream *stream)
{
    int i, j, k, num_entries;
    struct catalog_stream *data;
    uint8_t *wpt;
    struct el_torito_boot_catalog *cat;
    struct el_torito_boot_image **boots;

    if (stream == NULL) {
        return ISO_NULL_POINTER;
    }
    data = stream->data;
    cat = data->target->catalog;
    boots = cat->bootimages;

    if (data->offset != -1) {
        return ISO_FILE_ALREADY_OPENED;
    }

    memset(data->buffer, 0, BLOCK_SIZE);

    /* fill the buffer with the catalog contents */
    write_validation_entry(data->buffer,
                           boots[0]->platform_id, boots[0]->id_string);

    /* write default entry = first boot image */
    write_section_entry(data->buffer + 32, data->target, 0);

    /* IMPORTANT: The maximum number of boot images must fit into BLOCK_SIZE */
    wpt = data->buffer + 64;
    for (i = 1; i < cat->num_bootimages; ) {
        /* Look ahead and put images of same platform_id and id_string
           into the same section */
        for (j = i + 1; j < cat->num_bootimages; j++) {
             if (boots[i]->platform_id != boots[j]->platform_id)
        break;
             for (k = 0; k < (int) sizeof(boots[i]->id_string); k++)
                 if (boots[i]->id_string[k] != boots[j]->id_string[k])
             break;
             if (k < (int) sizeof(boots[i]->id_string))
        break;
        }
        num_entries = j - i;

        write_section_header(wpt, data->target, i, num_entries);
        wpt += 32;
        for (j = 0; j < num_entries; j++) {
            write_section_entry(wpt,  data->target, i);
            wpt += 32;
            i++;
        }
    }
    data->offset = 0;
    return ISO_SUCCESS;
}

static
int catalog_close(IsoStream *stream)
{
    struct catalog_stream *data;
    if (stream == NULL) {
        return ISO_NULL_POINTER;
    }
    data = stream->data;

    if (data->offset == -1) {
        return ISO_FILE_NOT_OPENED;
    }
    data->offset = -1;
    return ISO_SUCCESS;
}

static
off_t catalog_get_size(IsoStream *stream)
{
    return BLOCK_SIZE;
}

static
int catalog_read(IsoStream *stream, void *buf, size_t count)
{
    size_t len;
    struct catalog_stream *data;
    if (stream == NULL || buf == NULL) {
        return ISO_NULL_POINTER;
    }
    if (count == 0) {
        return ISO_WRONG_ARG_VALUE;
    }
    data = stream->data;

    if (data->offset == -1) {
        return ISO_FILE_NOT_OPENED;
    }

    len = MIN(count, (size_t) (BLOCK_SIZE - data->offset));
    memcpy(buf, data->buffer + data->offset, len);
    return len;
}

static
int catalog_is_repeatable(IsoStream *stream)
{
    return 1;
}

/**
 * fs_id will be the id reserved for El-Torito
 * dev_id will be 0 for catalog, 1 for boot image (if needed)
 * we leave ino_id for future use when we support multiple boot images
 */
static
void catalog_get_id(IsoStream *stream, unsigned int *fs_id, dev_t *dev_id,
                   ino_t *ino_id)
{
    *fs_id = ISO_ELTORITO_FS_ID;
    *dev_id = 0;
    *ino_id = 0;
}

static
void catalog_free(IsoStream *stream)
{
    free(stream->data);
}

IsoStreamIface catalog_stream_class = {
    0,
    "boot",
    catalog_open,
    catalog_close,
    catalog_get_size,
    catalog_read,
    catalog_is_repeatable,
    catalog_get_id,
    catalog_free,
    NULL,
    NULL,
    NULL,
    NULL
};

/**
 * Create an IsoStream for writing El-Torito catalog for a given target.
 */
static
int catalog_stream_new(Ecma119Image *target, IsoStream **stream)
{
    IsoStream *str;
    struct catalog_stream *data;

    if (target == NULL || stream == NULL || target->catalog == NULL) {
        return ISO_NULL_POINTER;
    }

    str = calloc(1, sizeof(IsoStream));
    if (str == NULL) {
        return ISO_OUT_OF_MEM;
    }
    data = calloc(1, sizeof(struct catalog_stream));
    if (data == NULL) {
        free(str);
        return ISO_OUT_OF_MEM;
    }

    /* fill data */
    data->target = target;
    data->offset = -1;

    str->refcount = 1;
    str->data = data;
    str->class = &catalog_stream_class;

    *stream = str;
    return ISO_SUCCESS;
}

int el_torito_catalog_file_src_create(Ecma119Image *target, IsoFileSrc **src)
{
    int ret;
    IsoFileSrc *file;
    IsoStream *stream;

    if (target == NULL || src == NULL || target->catalog == NULL) {
        return ISO_OUT_OF_MEM;
    }

    if (target->cat != NULL) {
        /* catalog file src already created */
        *src = target->cat;
        return ISO_SUCCESS;
    }

    file = calloc(1, sizeof(IsoFileSrc));
    if (file == NULL) {
        return ISO_OUT_OF_MEM;
    }

    ret = catalog_stream_new(target, &stream);
    if (ret < 0) {
        free(file);
        return ret;
    }

    /* fill fields */
    file->no_write = 0; /* TODO allow copy of old img catalog???? */
    file->checksum_index = 0;
    file->nsections = 1;
    file->sections = calloc(1, sizeof(struct iso_file_section));
    file->sort_weight = target->catalog->sort_weight;
    file->stream = stream;

    ret = iso_file_src_add(target, file, src);
    if (ret <= 0) {
        iso_stream_unref(stream);
        free(file);
    } else {
        target->cat = *src;
    }
    return ret;
}

/******************* EL-TORITO WRITER *******************************/

/**
 * Insert boot info table content into buf.
 *
 * @return
 *      1 on success, 0 error (but continue), < 0 error
 */
int make_boot_info_table(uint8_t *buf, uint32_t pvd_lba,
                         uint32_t boot_lba, uint32_t imgsize)
{
    struct boot_info_table *info;
    uint32_t checksum;
    uint32_t offset;

    info = (struct boot_info_table *) (buf + 8);
    if (imgsize < 64)
        return ISO_ISOLINUX_CANT_PATCH;

    /* compute checksum, as the the sum of all 32 bit words in boot image
     * from offset 64 */
    checksum = 0;
    offset = 64;

    while (offset <= imgsize - 4) {
        checksum += iso_read_lsb(buf + offset, 4);
        offset += 4;
    }
    if (offset != imgsize) {
        /*
         * file length not multiple of 4
         * empty space in isofs is padded with zero;
         * assume same for last dword
         */
        checksum += iso_read_lsb(buf + offset, imgsize - offset);
    }

    /*memset(info, 0, sizeof(struct boot_info_table));*/
    iso_lsb(info->bi_pvd, pvd_lba, 4);
    iso_lsb(info->bi_file, boot_lba, 4);
    iso_lsb(info->bi_length, imgsize, 4);
    iso_lsb(info->bi_csum, checksum, 4);
    memset(buf + 24, 0, 40);
    return ISO_SUCCESS;
}

/**
 * Patch an El Torito boot image by a boot info table.
 *
 * @return
 *      1 on success, 0 error (but continue), < 0 error
 */
static
int patch_boot_info_table(uint8_t *buf, Ecma119Image *t,
                               size_t imgsize, int idx)
{
    int ret;

    if (imgsize < 64) {
        return iso_msg_submit(t->image->id, ISO_ISOLINUX_CANT_PATCH, 0,
            "Isolinux image too small. We won't patch it.");
    }
    ret = make_boot_info_table(buf, t->ms_block + (uint32_t) 16,
                               t->bootsrc[idx]->sections[0].block,
                               (uint32_t) imgsize);
    return ret;
}


/**
 * Patch a GRUB2 El Torito boot image.
 */
static
int patch_grub2_boot_image(uint8_t *buf, Ecma119Image *t,
                           size_t imgsize, int idx,
                           size_t pos, int offst)
{
    uint64_t blk;

    if (imgsize < pos + 8)
        return iso_msg_submit(t->image->id, ISO_ISOLINUX_CANT_PATCH, 0,
                     "Isolinux image too small for GRUB2. Will not patch it.");
    blk = ((uint64_t) t->bootsrc[idx]->sections[0].block) * 4 + offst;
    iso_lsb((buf + pos), blk & 0xffffffff, 4);
    iso_lsb((buf + pos + 4), blk >> 32, 4);
    return ISO_SUCCESS;
}


/* Patch the boot images if indicated */
int iso_patch_eltoritos(Ecma119Image *t)
{
    int ret, idx;
    size_t size;
    uint8_t *buf;
    IsoStream *new = NULL;
    IsoStream *original = NULL;

    if (t->catalog == NULL)
        return ISO_SUCCESS;

    for (idx = 0; idx < t->catalog->num_bootimages; idx++) {
        if (!(t->catalog->bootimages[idx]->isolinux_options & 0x201))
    continue;
        original = t->bootsrc[idx]->stream;
        size = (size_t) iso_stream_get_size(original);

        /* >>> BOOT ts B00428 :
               check whether size is not too large for buffering */;

        buf = calloc(1, size);
        if (buf == NULL) {
            return ISO_OUT_OF_MEM;
        }
        ret = iso_stream_open(original);
        if (ret < 0) {
            free(buf);
            return ret;
        }
        ret = iso_stream_read(original, buf, size);
        iso_stream_close(original);
        if (ret != (int) size) {
            return (ret < 0) ? ret : (int) ISO_FILE_READ_ERROR;
        }

        /* ok, patch the read buffer */
        if (t->catalog->bootimages[idx]->isolinux_options & 0x200) {
            /* GRUB2 boot provisions */
            ret = patch_grub2_boot_image(buf, t, size, idx,
                                         Libisofs_grub2_elto_patch_poS,
                                         Libisofs_grub2_elto_patch_offsT);
            if (ret < 0)
                return ret;
	}
        /* Must be done as last patching */
        if (t->catalog->bootimages[idx]->isolinux_options & 0x01) {
            /* Boot Info Table */
            ret = patch_boot_info_table(buf, t, size, idx);
            if (ret < 0)
                return ret;
        }

        /* replace the original stream with a memory stream that reads from
         * the patched buffer */
        ret = iso_memory_stream_new(buf, size, &new);
        if (ret < 0) {
            return ret;
        }
        t->bootsrc[idx]->stream = new;
        iso_stream_unref(original);
    }
    return ISO_SUCCESS;
}

static
int eltorito_writer_compute_data_blocks(IsoImageWriter *writer)
{
    /*
     * We have nothing to write.
     */
    return ISO_SUCCESS;
}


/**
 * Write the Boot Record Volume Descriptor (ECMA-119, 8.2)
 */
static
int eltorito_writer_write_vol_desc(IsoImageWriter *writer)
{
    Ecma119Image *t;
    struct ecma119_boot_rec_vol_desc vol;

    if (writer == NULL) {
        return ISO_NULL_POINTER;
    }

    t = writer->target;
    iso_msg_debug(t->image->id, "Write El-Torito boot record");

    memset(&vol, 0, sizeof(struct ecma119_boot_rec_vol_desc));
    vol.vol_desc_type[0] = 0;
    memcpy(vol.std_identifier, "CD001", 5);
    vol.vol_desc_version[0] = 1;
    memcpy(vol.boot_sys_id, "EL TORITO SPECIFICATION", 23);
    iso_lsb(vol.boot_catalog,
            t->cat->sections[0].block - t->eff_partition_offset, 4);
    return iso_write(t, &vol, sizeof(struct ecma119_boot_rec_vol_desc));
}

static
int eltorito_writer_write_data(IsoImageWriter *writer)
{
    /* nothing to do, the files are written by the file writer */
    return ISO_SUCCESS;
}

static
int eltorito_writer_free_data(IsoImageWriter *writer)
{
    /* nothing to do */
    return ISO_SUCCESS;
}

int eltorito_writer_create(Ecma119Image *target)
{
    int ret, idx, outsource_efi = 0;
    IsoImageWriter *writer;
    IsoFile *bootimg;
    IsoFileSrc *src;

    writer = calloc(1, sizeof(IsoImageWriter));
    if (writer == NULL) {
        return ISO_OUT_OF_MEM;
    }

    writer->compute_data_blocks = eltorito_writer_compute_data_blocks;
    writer->write_vol_desc = eltorito_writer_write_vol_desc;
    writer->write_data = eltorito_writer_write_data;
    writer->free_data = eltorito_writer_free_data;
    writer->data = NULL;
    writer->target = target;

    /* add this writer to image */
    target->writers[target->nwriters++] = writer;

    /*
     * get catalog and image file sources.
     * Note that the catalog may be already added, when creating the low
     * level ECMA-119 tree.
     */
    if (target->cat == NULL) {
        ret = el_torito_catalog_file_src_create(target, &src);
        if (ret < 0) {
            return ret;
        }
    }

    if (target->efi_boot_partition != NULL)
        if (strcmp(target->efi_boot_partition, "--efi-boot-image") == 0)
            outsource_efi = 1;
    for (idx = 0; idx < target->catalog->num_bootimages; idx++) {
        bootimg = target->catalog->bootimages[idx]->image;
        ret = iso_file_src_create(target, bootimg, &src);
        if (ret < 0) {
            return ret;
        }
        target->bootsrc[idx] = src;

        /* For patching an image, it needs to be copied always */
        if (target->catalog->bootimages[idx]->isolinux_options & 0x01) {
            src->no_write = 0;
        }

        /* If desired: Recognize first EFI boot image that will be newly
           written, and mark it as claimed for being a partition.
        */
        if (outsource_efi &&
            target->catalog->bootimages[idx]->platform_id == 0xef &&
            src->no_write == 0) {
           target->efi_boot_part_filesrc = src;
           src->sections[0].block = 0xfffffffe;
           ((IsoNode *) bootimg)->hidden |=
                                   LIBISO_HIDE_ON_HFSPLUS | LIBISO_HIDE_ON_FAT;
           outsource_efi = 0;
        }
    }

    /* we need the bootable volume descriptor */
    target->curblock++;

    if (outsource_efi) {
        /* Disable EFI Boot partition and complain */
        free(target->efi_boot_partition);
        target->efi_boot_partition = NULL;
        iso_msg_submit(target->image->id, ISO_BOOT_NO_EFI_ELTO, 0,
"No newly added El Torito EFI boot image found for exposure as GPT partition");
        return ISO_BOOT_NO_EFI_ELTO;
    }

    return ISO_SUCCESS;
}

