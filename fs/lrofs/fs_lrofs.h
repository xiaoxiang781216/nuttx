/****************************************************************************
 * fs/lrofs/fs_lrofs.h
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __FS_LROFS_FS_LROFS_H
#define __FS_LROFS_FS_LROFS_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/nuttx.h>
#include <nuttx/list.h>

#include <stdint.h>
#include <stdbool.h>

#include "inode/inode.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Volume header (multi-byte values are big-endian) */

#define ROMFS_VHDR_ROM1FS   0  /*  0-7:  "-rom1fs-" */
#define ROMFS_VHDR_SIZE     8  /*  8-11: Number of accessible bytes in this fs. */
#define ROMFS_VHDR_CHKSUM  12  /* 12-15: Checksum of the first 512 bytes. */
#define ROMFS_VHDR_VOLNAME 16  /* 16-..: Zero terminated volume name, padded to
                                *        16 byte boundary. */

#define ROMFS_VHDR_MAGIC   "-rom1fs-"

/* File header offset (multi-byte values are big-endian) */

#define ROMFS_FHDR_NEXT     0  /*  0-3:  Offset of the next file header
                                *        (zero if no more files) */
#define ROMFS_FHDR_INFO     4  /*  4-7:  Info for directories/hard links/
                                *        devices */
#define ROMFS_FHDR_SIZE     8  /*  8-11: Size of this file in bytes */
#define ROMFS_FHDR_CHKSUM  12  /* 12-15: Checksum covering the meta data,
                                *        including the file name, and
                                *        padding. */
#define ROMFS_FHDR_NAME    16  /* 16-..: Zero terminated volume name, padded
                                *        to 16 byte boundary. */

/* Bits 0-3 of the rf_next offset provide mode information.  These are the
 * values specified in
 */

#define RFNEXT_MODEMASK    7    /* Bits 0-2: Mode; bit 3: Executable */
#define RFNEXT_ALLMODEMASK 15   /* Bits 0-3: All mode bits */
#define RFNEXT_OFFSETMASK (~15) /* Bits n-3: Offset to next entry */

#define RFNEXT_HARDLINK    0    /* rf_info = Link destination file header */
#define RFNEXT_DIRECTORY   1    /* rf_info = First file's header */
#define RFNEXT_FILE        2    /* rf_info = Unused, must be zero */
#define RFNEXT_SOFTLINK    3    /* rf_info = Unused, must be zero */
#define RFNEXT_BLOCKDEV    4    /* rf_info = 16/16 bits major/minor number */
#define RFNEXT_CHARDEV     5    /* rf_info = 16/16 bits major/minor number */
#define RFNEXT_SOCKET      6    /* rf_info = Unused, must be zero */
#define RFNEXT_FIFO        7    /* rf_info = Unused, must be zero */
#define RFNEXT_EXEC        8    /* Modifier of RFNEXT_DIRECTORY and RFNEXT_FILE */

#define IS_MODE(rfn,mode)  ((((uint32_t)(rfn))&RFNEXT_MODEMASK)==(mode))
#define IS_HARDLINK(rfn)   IS_MODE(rfn,RFNEXT_HARDLINK)
#define IS_DIRECTORY(rfn)  IS_MODE(rfn,RFNEXT_DIRECTORY)
#define IS_FILE(rfn)       IS_MODE(rfn,RFNEXT_FILE)
#define IS_SOFTLINK(rfn)   IS_MODE(rfn,RFNEXT_SOFTLINK)
#define IS_BLOCKDEV(rfn)   IS_MODE(rfn,RFNEXT_BLOCKDEV)
#define IS_CHARDEV(rfn)    IS_MODE(rfn,RFNEXT_CHARDEV)
#define IS_SOCKET(rfn)     IS_MODE(rfn,RFNEXT_SOCKET)
#define IS_FIFO(rfn)       IS_MODE(rfn,RFNEXT_FIFO)
#define IS_EXECUTABLE(rfn) (((rfn) & RFNEXT_EXEC) != 0)

/* RFNEXT_SOFTLINK, RFNEXT_BLOCKDEV, RFNEXT_CHARDEV, RFNEXT_SOCKET, and
 * RFNEXT_FIFO are not presently supported in NuttX.
 */

/* Alignment macros */

#define ROMFS_ALIGNMENT       16
#define ROMFS_MAXPADDING      (ROMFS_ALIGNMENT-1)
#define ROMFS_ALIGNMASK       (~ROMFS_MAXPADDING)
#define ROMFS_ALIGNUP(addr)   ((((uint32_t)(addr))+ROMFS_MAXPADDING)&ROMFS_ALIGNMASK)
#define ROMFS_ALIGNDOWN(addr) (((uint32_t)(addr))&ROMFS_ALIGNMASK)

/* Offset and sector conversions */

#define SEC_NDXMASK(r)       ((r)->lm_hwsectorsize - 1)
#define SEC_NSECTORS(r,o)    ((o) / (r)->lm_hwsectorsize)
#define SEC_ALIGN(r,o)       ((o) & ~SEC_NDXMASK(r))
#define SEC_ALIGNUP(r,o)     (((o) + (r)->lm_hwsectorsize - 1) & ~SEC_NDXMASK(r))

/* Maximum numbr of links that will be followed before we decide that there
 * is a problem.
 */

#define ROMF_MAX_LINKS 64

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* This structure represents the spare list.  An instance of this
 * structure is retained as file header and file data size on each mountpoint
 * that is mounted with a lrofs filesystem.
 */

struct lrofs_sparenode_s
{
  struct list_node node;
  uint32_t start;
  uint32_t end;
};

struct lrofs_mountpt_s
{
  FAR struct inode            *lm_blkdriver;    /* The block driver inode that hosts the lrofs */
  FAR struct lrofs_nodeinfo_s *lm_root;         /* The node for root node */
  bool                         lm_mounted;      /* true: The file system is ready */
  uint16_t                     lm_hwsectorsize; /* HW: Sector size reported by block driver */
  rmutex_t                     lm_lock;         /* Used to assume thread-safe access */
  uint32_t                     lm_refs;         /* The references for all files opened on this mountpoint */
  uint32_t                     lm_hwnsectors;   /* HW: The number of sectors reported by the hardware */
  uint32_t                     lm_volsize;      /* Size of the ROMFS volume */
  uint32_t                     lm_cachesector;  /* Current sector in the rm_buffer */
  FAR uint8_t                 *lm_xipbase;      /* Base address of directly accessible media */
  FAR uint8_t                 *lm_buffer;       /* Device sector buffer, allocated if rm_xipbase==0 */
  FAR uint8_t                 *lm_devbuffer;    /* Device sector buffer, allocated for write if rm_xipbase != 0 */
  sem_t                        lm_sem;          /* The semaphore to assume write safe */
  struct list_node             lm_sparelist;    /* The list of spare space */
};

struct lrofs_nodeinfo_s
{
  uint32_t                      ln_origoffset; /* Offset of origin file header */
  uint32_t                      ln_offset;     /* Offset of real file header */
  uint32_t                      ln_next;       /* Offset of the next file header+flags */
  uint32_t                      ln_size;       /* Size (if file) */
  FAR struct lrofs_nodeinfo_s  *ln_parent;     /* The parent node in the upper level */
  FAR struct lrofs_nodeinfo_s **ln_child;      /* The node array for link to lower level */
  uint16_t                      ln_count;      /* The count of node in rn_child level */
  uint16_t                      ln_max;        /* The max count of node in rn_child level */
  uint8_t                       ln_namesize;   /* The length of name of the entry */
  char                          ln_name[1];    /* The name to the entry */
};

/* This structure represents on open file under the mountpoint.  An instance
 * of this structure is retained as struct file specific information on each
 * opened file.
 */

struct lrofs_file_s
{
  uint32_t                     lf_startoffset;  /* Offset to the start of the file data */
  uint32_t                     lf_endsector;    /* Last sector of the file data */
  uint32_t                     lf_size;         /* Size of the file in bytes */
  uint32_t                     lf_cachesector;  /* First sector in the rf_buffer */
  uint32_t                     lf_ncachesector; /* Number of sectors in the rf_buffer */
  FAR struct lrofs_nodeinfo_s *lf_ln;           /* The node struct addr of the file */
  FAR uint8_t                 *lf_buffer;       /* File sector buffer, allocated if rm_xipbase==0 */
  uint8_t                      lf_type;         /* File type (for fstat()) */
  char                         lf_path[1];      /* Path of open file */
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int lrofs_checkmount(FAR struct lrofs_mountpt_s *lm);
int lrofs_datastart(FAR struct lrofs_mountpt_s *lm,
                    FAR struct lrofs_nodeinfo_s *ln,
                    FAR uint32_t *start);
int lrofs_fileconfigure(FAR struct lrofs_mountpt_s *lm,
                        FAR struct lrofs_file_s *lf);
int lrofs_hwread(FAR struct lrofs_mountpt_s *lm, FAR uint8_t *buffer,
                 uint32_t sector, unsigned int nsectors);
void lrofs_freenode(FAR struct lrofs_nodeinfo_s *nodeinfo);
int lrofs_parsedirentry(FAR struct lrofs_mountpt_s *lm, uint32_t offset,
                        FAR uint32_t *poffset, uint32_t *pnext,
                        FAR uint32_t *pinfo, FAR uint32_t *psize);
int lrofs_parsefilename(FAR struct lrofs_mountpt_s *lm, uint32_t offset,
                        FAR char *pname);
FAR struct lrofs_nodeinfo_s *
lrofs_finddirentry(FAR struct lrofs_mountpt_s *lm,
                   FAR const char *path);
int lrofs_filecacheread(FAR struct lrofs_mountpt_s *lm,
                        FAR struct lrofs_file_s *lf,
                        uint32_t sector);
int lrofs_hwconfigure(FAR struct lrofs_mountpt_s *lm);
int lrofs_fsconfigure(FAR struct lrofs_mountpt_s *lm,
                      FAR const void *data);
int lrofs_mkfs(FAR struct lrofs_mountpt_s *lm);
int lrofs_create(FAR struct lrofs_mountpt_s *lm,
                 FAR struct lrofs_nodeinfo_s **ln,
                 FAR const char *relpath,
                 bool isdir);
int lrofs_remove(FAR struct lrofs_mountpt_s *lm,
                 FAR struct lrofs_nodeinfo_s *ln_parent,
                 FAR const char *relpath,
                 bool isdir);
int lrofs_write_file(FAR struct file *filep,
                     FAR const char *buffer,
                     size_t buflen);
int lrofs_truncate_file(FAR struct file *filep,
                        off_t length);
int lrofs_rename_file(FAR struct lrofs_mountpt_s *lm,
                      FAR struct lrofs_nodeinfo_s *ln_old,
                      FAR struct lrofs_nodeinfo_s *ln_newpath,
                      FAR const char *newname);
void lrofs_free_sparelist(FAR struct list_node *list);
#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __FS_LROFS_FS_LROFS_H */