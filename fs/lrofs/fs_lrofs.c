/****************************************************************************
 * fs/lrofs/fs_lrofs.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <sys/statfs.h>
#include <sys/stat.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/kmalloc.h>
#include <nuttx/fs/fs.h>
#include <nuttx/fs/ioctl.h>
#include <libgen.h>

#include "fs_lrofs.h"
#include "fs_heap.h"

/****************************************************************************
 * Pre-processor Declarations
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* This structure represents one entry node in the lrofs file system */

struct lrofs_dir_s
{
  struct     fs_dirent_s        base;       /* Vfs directory structure */
  FAR struct lrofs_nodeinfo_s **firstnode;  /* The address of first node in the directory */
  FAR struct lrofs_nodeinfo_s **currnode;   /* The address of current node into the directory */
  FAR struct lrofs_nodeinfo_s  *nodeinfo;   /* The address of the directory */
  uint16_t                      count;      /* Number of nodes in the directory */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int     lrofs_open(FAR struct file *filep, FAR const char *relpath,
                          int oflags, mode_t mode);
static int     lrofs_close(FAR struct file *filep);
static ssize_t lrofs_read(FAR struct file *filep, FAR char *buffer,
                          size_t buflen);
static ssize_t lrofs_write(FAR struct file *filep, FAR const char *buffer,
                           size_t buflen);
static off_t   lrofs_seek(FAR struct file *filep, off_t offset, int whence);
static int     lrofs_ioctl(FAR struct file *filep, int cmd,
                           unsigned long arg);
static int     lrofs_mmap(FAR struct file *filep,
                          FAR struct mm_map_entry_s *map);
static int     lrofs_truncate(FAR struct file *filep, off_t length);
static int     lrofs_sync(FAR struct file *filep);
static int     lrofs_dup(FAR const struct file *oldp,
                         FAR struct file *newp);
static int     lrofs_fstat(FAR const struct file *filep,
                           FAR struct stat *buf);

static int     lrofs_opendir(FAR struct inode *mountpt,
                             FAR const char *relpath,
                             FAR struct fs_dirent_s **dir);
static int     lrofs_closedir(FAR struct inode *mountpt,
                              FAR struct fs_dirent_s *dir);
static int     lrofs_readdir(FAR struct inode *mountpt,
                             FAR struct fs_dirent_s *dir,
                             FAR struct dirent *entry);
static int     lrofs_rewinddir(FAR struct inode *mountpt,
                               FAR struct fs_dirent_s *dir);

static int     lrofs_bind(FAR struct inode *blkdriver, FAR const void *data,
                          FAR void **handle);
static int     lrofs_unbind(FAR void *handle, FAR struct inode **blkdriver,
                            unsigned int flags);
static int     lrofs_statfs(FAR struct inode *mountpt,
                            FAR struct statfs *buf);

static int     lrofs_unlink(FAR struct inode *mountpt,
                            FAR const char *relpath);
static int     lrofs_mkdir(FAR struct inode *mountpt,
                           FAR const char *relpath, mode_t mode);
static int     lrofs_rmdir(FAR struct inode *mountpt,
                           FAR const char *relpath);
static int     lrofs_rename(FAR struct inode *mountpt,
                            FAR const char *oldrelpath,
                            FAR const char *newrelpath);
static int     lrofs_stat_common(uint8_t type, uint32_t size,
                                 uint16_t sectorsize, FAR struct stat *buf);
static int     lrofs_stat(FAR struct inode *mountpt, FAR const char *relpath,
                          FAR struct stat *buf);

/****************************************************************************
 * Public Data
 ****************************************************************************/

const struct mountpt_operations g_lrofs_operations =
{
  lrofs_open,      /* open */
  lrofs_close,     /* close */
  lrofs_read,      /* read */
  lrofs_write,     /* write */
  lrofs_seek,      /* seek */
  lrofs_ioctl,     /* ioctl */
  lrofs_mmap,      /* mmap */
  lrofs_truncate,  /* truncate */
  NULL,            /* poll */
  NULL,            /* readv */
  NULL,            /* writev */

  lrofs_sync,      /* sync */
  lrofs_dup,       /* dup */
  lrofs_fstat,     /* fstat */
  NULL,            /* fchstat */

  lrofs_opendir,   /* opendir */
  lrofs_closedir,  /* closedir */
  lrofs_readdir,   /* readdir */
  lrofs_rewinddir, /* rewinddir */

  lrofs_bind,      /* bind */
  lrofs_unbind,    /* unbind */
  lrofs_statfs,    /* statfs */

  lrofs_unlink,    /* unlink */
  lrofs_mkdir,     /* mkdir */
  lrofs_rmdir,     /* rmdir */
  lrofs_rename,    /* rename */
  lrofs_stat,      /* stat */
  NULL             /* chstat */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lrofs_open
 ****************************************************************************/

static int lrofs_open(FAR struct file *filep, FAR const char *relpath,
                      int oflags, mode_t mode)
{
  FAR struct lrofs_mountpt_s  *lm;
  FAR struct lrofs_file_s     *lf;
  FAR struct lrofs_nodeinfo_s *ln;
  size_t                       len;
  int                          ret;

  DEBUGASSERT(filep->f_priv == NULL);

  /* Get mountpoint private data from the inode reference from the file
   * structure
   */

  lm = filep->f_inode->i_private;

  /* Check if the mount is still healthy */

  nxrmutex_lock(&lm->lm_lock);

  if (oflags & (O_WRONLY | O_APPEND | O_TRUNC | O_CREAT))
    {
      nxrmutex_unlock(&lm->lm_lock);
      nxsem_wait_uninterruptible(&lm->lm_sem);
      nxrmutex_lock(&lm->lm_lock);
    }

  ret = lrofs_checkmount(lm);
  if (ret < 0)
    {
      ferr("ERROR: lrofs_checkmount failed: %d\n", ret);
      goto errout_with_sem;
    }

  /* Locate the directory entry for this path */

  ln = lrofs_finddirentry(lm, relpath);
  if (ln == NULL)
    {
      if (oflags & O_CREAT)
        {
          /* Create a new file */

          ret = lrofs_create(lm, &ln, relpath, false);
          if (ret < 0)
            {
              ferr("ERROR: Failed to create file '%s': %d\n",
                  relpath, ret);
              goto errout_with_sem;
            }
        }
      else
        {
          finfo("ERROR: Failed to find directory entry for '%s'.\n",
                relpath);
          ret = -ENOENT;
          goto errout_with_sem;
        }
    }

  /* The full path exists -- but is the final component a file
   * or a directory?  Or some other Unix file type that is not
   * appropriate in this context.
   *
   * REVISIT: This logic should follow hard/soft link file
   * types.  At present, it returns the ENXIO.
   */

  if (IS_DIRECTORY(ln->ln_next))
    {
      /* It is a directory */

      ret = -EISDIR;
      ferr("ERROR: '%s' is a directory\n", relpath);
      goto errout_with_sem;
    }
  else if (!IS_FILE(ln->ln_next))
    {
      /* ENXIO indicates "The named file is a character special or
       * block special file, and the device associated with this
       * special file does not exist."
       *
       * Here we also return ENXIO if the file is not a directory
       * or a regular file.
       */

      ret = -ENXIO;
      ferr("ERROR: '%s' is a special file\n", relpath);
      goto errout_with_sem;
    }

  /* Create an instance of the file private data to describe the opened
   * file.
   */

  len = strlen(relpath);
  lf = fs_heap_zalloc(sizeof(struct lrofs_file_s) + len);
  if (!lf)
    {
      ferr("ERROR: Failed to allocate private data\n");
      ret = -ENOMEM;
      goto errout_with_sem;
    }

  /* Initialize the file private data (only need to initialize
   * non-zero elements)
   */

  lf->lf_ln   = ln;
  lf->lf_size = ln->ln_size;
  lf->lf_type = (uint8_t)(ln->ln_next & RFNEXT_ALLMODEMASK);
  memcpy(lf->lf_path, relpath, len + 1);

  /* Get the start of the file data */

  ret = lrofs_datastart(lm, ln, &lf->lf_startoffset);
  if (ret < 0)
    {
      ferr("ERROR: Failed to locate start of file data: %d\n", ret);
      fs_heap_free(lf);
      goto errout_with_sem;
    }

  /* Configure buffering to support access to this file */

  ret = lrofs_fileconfigure(lm, lf);
  if (ret < 0)
    {
      ferr("ERROR: Failed configure buffering: %d\n", ret);
      fs_heap_free(lf);
      goto errout_with_sem;
    }

  /* Attach the private date to the struct file instance */

  filep->f_priv = lf;
  lm->lm_refs++;

  if (oflags & O_TRUNC)
    {
      ret = lrofs_truncate_file(filep, 0);
      if (ret < 0)
        {
          ferr("ERROR: Failed to truncate file '%s': %d\n",
               lf->lf_path, ret);
        }
    }

  /* If the file is only created for read */

  if ((oflags & (O_WRONLY | O_APPEND | O_TRUNC | O_CREAT)) == O_CREAT)
    {
      nxsem_post(&lm->lm_sem);
    }

  nxrmutex_unlock(&lm->lm_lock);
  return ret;

errout_with_sem:
  if (oflags & (O_WRONLY | O_APPEND | O_TRUNC | O_CREAT))
    {
      nxsem_post(&lm->lm_sem);
    }

  nxrmutex_unlock(&lm->lm_lock);
  return ret;
}

/****************************************************************************
 * Name: lrofs_close
 ****************************************************************************/

static int lrofs_close(FAR struct file *filep)
{
  FAR struct lrofs_mountpt_s *lm;
  FAR struct lrofs_file_s    *lf;

  DEBUGASSERT(filep->f_priv != NULL);

  /* Recover our private data from the struct file instance */

  lf = filep->f_priv;
  lm = filep->f_inode->i_private;

  nxrmutex_lock(&lm->lm_lock);

  lm->lm_refs--;

  if (filep->f_oflags & (O_WRONLY | O_APPEND | O_TRUNC))
    {
      nxsem_post(&lm->lm_sem);
    }

  nxrmutex_unlock(&lm->lm_lock);

  /* Do not check if the mount is healthy.  We must support closing of
   * the file even when there is healthy mount.
   */

  /* Deallocate the memory structures created when the open method
   * was called.
   *
   * Free the sector buffer that was used to manage partial sector
   * accesses.
   */

  if (!lm->lm_xipbase && lf->lf_buffer)
    {
      fs_heap_free(lf->lf_buffer);
    }

  /* Then free the file structure itself. */

  fs_heap_free(lf);
  filep->f_priv = NULL;
  return 0;
}

/****************************************************************************
 * Name: lrofs_read
 ****************************************************************************/

static ssize_t lrofs_read(FAR struct file *filep, FAR char *buffer,
                          size_t buflen)
{
  FAR struct lrofs_mountpt_s *lm;
  FAR struct lrofs_file_s    *lf;
  unsigned int                bytesread;
  unsigned int                readsize = 0;
  unsigned int                nsectors;
  uint32_t                    offset;
  size_t                      bytesleft;
  off_t                       sector;
  FAR uint8_t                *userbuffer = (FAR uint8_t *)buffer;
  int                         sectorndx;
  int                         ret;

  DEBUGASSERT(filep->f_priv != NULL);

  /* Recover our private data from the struct file instance */

  lf = filep->f_priv;
  lm = filep->f_inode->i_private;

  /* Make sure that the mount is still healthy */

  nxrmutex_lock(&lm->lm_lock);
  ret = lrofs_checkmount(lm);
  if (ret < 0)
    {
      ferr("ERROR: lrofs_checkmount failed: %d\n", ret);
      goto errout_with_lock;
    }

  /* Get the number of bytes left in the file */

  bytesleft = lf->lf_size - filep->f_pos;

  /* Truncate read count so that it does not exceed the number
   * of bytes left in the file.
   */

  if (buflen > bytesleft)
    {
      buflen = bytesleft;
    }

  /* Loop until either (1) all data has been transferred, or (2) an
   * error occurs.
   */

  while (buflen > 0)
    {
      /* Get the first sector and index to read from. */

      offset    = lf->lf_startoffset + filep->f_pos;
      sector    = SEC_NSECTORS(lm, offset);
      sectorndx = offset & SEC_NDXMASK(lm);

      /* Check if the user has provided a buffer large enough to
       * hold one or more complete sectors -AND- the read is
       * aligned to a sector boundary.
       */

      nsectors = SEC_NSECTORS(lm, buflen);
      if (nsectors >= lf->lf_ncachesector && sectorndx == 0)
        {
          /* Read maximum contiguous sectors directly to the user's
           * buffer without using our tiny read buffer.
           */

          /* Read all of the sectors directly into user memory */

          ret = lrofs_hwread(lm, userbuffer, sector, nsectors);
          if (ret < 0)
            {
              ferr("ERROR: lrofs_hwread failed: %d\n", ret);
              goto errout_with_lock;
            }

          bytesread = nsectors * lm->lm_hwsectorsize;
        }
      else
        {
          /* We are reading a partial sector.  First, read the whole sector
           * into the file data buffer.  This is a caching buffer so if
           * it is already there then all is well.
           */

          ret = lrofs_filecacheread(lm, lf, sector);
          if (ret < 0)
            {
              ferr("ERROR: lrofs_filecacheread failed: %d\n", ret);
              goto errout_with_lock;
            }

          /* Copy the partial sector into the user buffer */

          bytesread = (lf->lf_cachesector + lf->lf_ncachesector - sector) *
                       lm->lm_hwsectorsize - sectorndx;
          sectorndx = lf->lf_ncachesector * lm->lm_hwsectorsize - bytesread;
          if (bytesread > buflen)
            {
              /* We will not read to the end of the buffer */

              bytesread = buflen;
            }

          memcpy(userbuffer, &lf->lf_buffer[sectorndx], bytesread);
        }

      /* Set up for the next sector read */

      userbuffer   += bytesread;
      filep->f_pos += bytesread;
      readsize     += bytesread;
      buflen       -= bytesread;
    }

errout_with_lock:
  nxrmutex_unlock(&lm->lm_lock);
  return readsize ? readsize : ret;
}

/****************************************************************************
 * Name: lrofs_write
 ****************************************************************************/

static ssize_t lrofs_write(FAR struct file *filep, FAR const char *buffer,
                           size_t buflen)
{
  FAR struct lrofs_mountpt_s *lm;
  FAR struct lrofs_file_s *lf;
  ssize_t ret;

  DEBUGASSERT(filep->f_priv != NULL);

  /* Recover our private data from the struct file instance */

  lf = filep->f_priv;
  lm = filep->f_inode->i_private;

  /* Make sure that the mount is still healthy */

  nxrmutex_lock(&lm->lm_lock);
  ret = lrofs_checkmount(lm);
  if (ret < 0)
    {
      ferr("ERROR: lrofs_checkmount failed: %d\n", ret);
      goto errout_with_lock;
    }

  if (filep->f_oflags & O_APPEND)
    {
      filep->f_pos = lf->lf_size;
    }

  if (filep->f_pos > lf->lf_size)
    {
      ret = lrofs_truncate_file(filep, filep->f_pos);
      if (ret < 0)
        {
          ferr("ERROR: Failed to truncate file '%s': %d\n",
               lf->lf_path, ret);
          goto errout_with_lock;
        }
    }

  ret = lrofs_write_file(filep, buffer, buflen);
  if (ret < 0)
    {
      ferr("ERROR: Failed to write file '%s': %d\n",
           lf->lf_path, ret);
    }

errout_with_lock:
  nxrmutex_unlock(&lm->lm_lock);
  return ret;
}

/****************************************************************************
 * Name: lrofs_seek
 ****************************************************************************/

static off_t lrofs_seek(FAR struct file *filep, off_t offset, int whence)
{
  FAR struct lrofs_mountpt_s *lm;
  FAR struct lrofs_file_s    *lf;
  off_t                       position;
  int                         ret;

  DEBUGASSERT(filep->f_priv != NULL);

  /* Recover our private data from the struct file instance */

  lf = filep->f_priv;
  lm = filep->f_inode->i_private;

  /* Map the offset according to the whence option */

  switch (whence)
    {
    case SEEK_SET: /* The offset is set to offset bytes. */
        position = offset;
        break;

    case SEEK_CUR: /* The offset is set to its current location plus
                    * offset bytes. */

        position = offset + filep->f_pos;
        break;

    case SEEK_END: /* The offset is set to the size of the file plus
                    * offset bytes. */

        position = offset + lf->lf_size;
        break;

    default:
        ferr("ERROR: Whence is invalid: %d\n", whence);
        return -EINVAL;
    }

  /* Make sure that the mount is still healthy */

  nxrmutex_lock(&lm->lm_lock);
  ret = lrofs_checkmount(lm);
  if (ret < 0)
    {
       ferr("ERROR: lrofs_checkmount failed: %d\n", ret);
       goto errout_with_lock;
    }

  /* Set file position and return success */

  filep->f_pos = position;

errout_with_lock:
  nxrmutex_unlock(&lm->lm_lock);
  return ret;
}

/****************************************************************************
 * Name: lrofs_ioctl
 ****************************************************************************/

static int lrofs_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  FAR struct lrofs_file_s *lf;

  DEBUGASSERT(filep->f_priv != NULL);

  /* Recover our private data from the struct file instance */

  lf = filep->f_priv;

  if (cmd == FIOC_FILEPATH)
    {
      FAR char *ptr = (FAR char *)((uintptr_t)arg);
      inode_getpath(filep->f_inode, ptr, PATH_MAX);
      strlcat(ptr, lf->lf_path, PATH_MAX);
      return 0;
    }
  else if (cmd == FIOC_XIPBASE)
    {
      FAR struct lrofs_mountpt_s *lm = filep->f_inode->i_private;
      FAR uintptr_t *ptr = (FAR uintptr_t *)arg;

      if (lm->lm_xipbase != 0)
        {
          *ptr = (uintptr_t)lm->lm_xipbase + lf->lf_startoffset;
          return 0;
        }
      else
        {
          return -ENXIO;
        }
    }

  return -ENOTTY;
}

/****************************************************************************
 * Name: lrofs_mmap
 ****************************************************************************/

static int lrofs_mmap(FAR struct file *filep, FAR struct mm_map_entry_s *map)
{
  FAR struct lrofs_mountpt_s *lm;
  FAR struct lrofs_file_s *lf;

  DEBUGASSERT(filep->f_priv != NULL);

  /* Recover our private data from the struct file instance */

  lf = filep->f_priv;
  lm = filep->f_inode->i_private;

  /* Return the address on the media corresponding to the start of
   * the file.
   */

  if (lm->lm_xipbase && map->offset >= 0 && map->offset < lf->lf_size &&
      map->length != 0 && map->offset + map->length <= lf->lf_size)
    {
      map->vaddr = lm->lm_xipbase + lf->lf_startoffset + map->offset;
      return 0;
    }

  return -ENOTTY;
}

/****************************************************************************
 * Name: lrofs_truncate
 ****************************************************************************/

static int lrofs_truncate(FAR struct file *filep, off_t length)
{
  FAR struct lrofs_mountpt_s *lm;
  FAR struct lrofs_file_s *lf;
  int ret;

  DEBUGASSERT(filep->f_priv != NULL);

  /* Recover our private data from the struct file instance */

  lf = filep->f_priv;
  lm = filep->f_inode->i_private;

  /* Make sure that the mount is still healthy */

  nxrmutex_lock(&lm->lm_lock);
  ret = lrofs_checkmount(lm);
  if (ret < 0)
    {
      ferr("ERROR: lrofs_checkmount failed: %d\n", ret);
      goto errout_with_lock;
    }

  ret = lrofs_truncate_file(filep, length);
  if (ret < 0)
    {
      ferr("ERROR: Failed to truncate file '%s': %d\n",
           lf->lf_path, ret);
    }

errout_with_lock:
  nxrmutex_unlock(&lm->lm_lock);
  return ret;
}

/****************************************************************************
 * Name: lrofs_sync
 ****************************************************************************/

static int lrofs_sync(FAR struct file *filep)
{
  /* There is nothing to do here. Data will be write to flash directly */

  return OK;
}

/****************************************************************************
 * Name: lrofs_dup
 ****************************************************************************/

static int lrofs_dup(FAR const struct file *oldp, FAR struct file *newp)
{
  FAR struct lrofs_mountpt_s *lm;
  FAR struct lrofs_file_s *old_lf;
  FAR struct lrofs_file_s *new_lf;
  size_t len;
  int ret;

  DEBUGASSERT(oldp->f_priv != NULL &&
              newp->f_priv == NULL &&
              newp->f_inode != NULL);

  /* Get mountpoint private data from the inode reference from the file
   * structure
   */

  lm = newp->f_inode->i_private;

  /* Check if the mount is still healthy */

  nxrmutex_lock(&lm->lm_lock);
  ret = lrofs_checkmount(lm);
  if (ret < 0)
    {
      ferr("ERROR: lrofs_checkmount failed: %d\n", ret);
      goto errout_with_lock;
    }

  /* Recover the old private data from the old struct file instance */

  old_lf = oldp->f_priv;

  /* Create an new instance of the file private data to describe the new
   * dup'ed file.
   */

  len    = strlen(old_lf->lf_path);
  new_lf = fs_heap_malloc(sizeof(struct lrofs_file_s) + len);
  if (!new_lf)
    {
      ferr("ERROR: Failed to allocate private data\n");
      ret = -ENOMEM;
      goto errout_with_lock;
    }

  /* Copy all file private data (except for the buffer) */

  new_lf->lf_ln          = old_lf->lf_ln;
  new_lf->lf_startoffset = old_lf->lf_startoffset;
  new_lf->lf_size        = old_lf->lf_size;
  new_lf->lf_type        = old_lf->lf_type;
  memcpy(new_lf->lf_path, old_lf->lf_path, len + 1);

  /* Configure buffering to support access to this file */

  ret = lrofs_fileconfigure(lm, new_lf);
  if (ret < 0)
    {
      fs_heap_free(new_lf);
      ferr("ERROR: Failed configure buffering: %d\n", ret);
      goto errout_with_lock;
    }

  /* Attach the new private date to the new struct file instance */

  newp->f_priv = new_lf;
  lm->lm_refs++;

errout_with_lock:
  nxrmutex_unlock(&lm->lm_lock);
  return ret;
}

/****************************************************************************
 * Name: lrofs_fstat
 ****************************************************************************/

static int lrofs_fstat(FAR const struct file *filep, FAR struct stat *buf)
{
  FAR struct lrofs_mountpt_s *lm;
  FAR struct lrofs_file_s *lf;
  int ret;

  DEBUGASSERT(filep->f_priv != NULL);

  /* Get mountpoint private data from the inode reference from the file
   * structure
   */

  lf = filep->f_priv;
  lm = filep->f_inode->i_private;

  /* Check if the mount is still healthy */

  nxrmutex_lock(&lm->lm_lock);
  ret = lrofs_checkmount(lm);
  if (ret >= 0)
    {
      /* Return information about the directory entry */

      ret = lrofs_stat_common(lf->lf_type, lf->lf_size,
                              lm->lm_hwsectorsize, buf);
    }

  nxrmutex_unlock(&lm->lm_lock);
  return ret;
}

/****************************************************************************
 * Name: lrofs_opendir
 ****************************************************************************/

static int lrofs_opendir(FAR struct inode *mountpt, FAR const char *relpath,
                         FAR struct fs_dirent_s **dir)
{
  FAR struct lrofs_mountpt_s  *lm;
  FAR struct lrofs_dir_s      *ldir;
  FAR struct lrofs_nodeinfo_s *ln;
  int                          ret;

  DEBUGASSERT(mountpt != NULL && mountpt->i_private != NULL);

  /* Recover our private data from the inode instance */

  lm = mountpt->i_private;

  ldir = fs_heap_zalloc(sizeof(*ldir));
  if (ldir == NULL)
    {
      return -ENOMEM;
    }

  /* Make sure that the mount is still healthy */

  nxrmutex_lock(&lm->lm_lock);
  ret = lrofs_checkmount(lm);
  if (ret < 0)
    {
      ferr("ERROR: lrofs_checkmount failed: %d\n", ret);
      goto errout_with_lock;
    }

  /* Find the requested directory */

  ln = lrofs_finddirentry(lm, relpath);
  if (ln == NULL)
    {
      ferr("ERROR: Failed to find directory '%s': %d\n", relpath, ret);
      ret = -ENOENT;
      goto errout_with_lock;
    }

  /* Verify that it is some kind of directory */

  if (!IS_DIRECTORY(ln->ln_next))
    {
      /* The entry is not a directory */

      ferr("ERROR: '%s' is not a directory\n", relpath);
      ret = -ENOTDIR;
      goto errout_with_lock;
    }

  /* The entry is a directory */

  ldir->firstnode   = ln->ln_child;
  ldir->currnode    = ln->ln_child;
  ldir->count       = ln->ln_count;
  ldir->nodeinfo    = ln;
  *dir              = &ldir->base;
  nxrmutex_unlock(&lm->lm_lock);
  return 0;

errout_with_lock:
  nxrmutex_unlock(&lm->lm_lock);
  fs_heap_free(ldir);
  return ret;
}

/****************************************************************************
 * Name: lrofs_closedir
 ****************************************************************************/

static int lrofs_closedir(FAR struct inode *mountpt,
                          FAR struct fs_dirent_s *dir)
{
  DEBUGASSERT(dir);
  fs_heap_free(dir);
  return 0;
}

/****************************************************************************
 * Name: lrofs_readdir
 ****************************************************************************/

static int lrofs_readdir(FAR struct inode *mountpt,
                         FAR struct fs_dirent_s *dir,
                         FAR struct dirent *entry)
{
  FAR struct lrofs_mountpt_s  *lm;
  FAR struct lrofs_dir_s      *ldir;
  FAR struct lrofs_nodeinfo_s *ln;
  uint32_t                     next;
  int                          ret;

  DEBUGASSERT(mountpt != NULL && mountpt->i_private != NULL);

  /* Recover our private data from the inode instance */

  lm = mountpt->i_private;
  ldir = (FAR struct lrofs_dir_s *)dir;
  ln = ldir->nodeinfo;

  /* Make sure that the mount is still healthy */

  nxrmutex_lock(&lm->lm_lock);
  ret = lrofs_checkmount(lm);
  if (ret < 0)
    {
      ferr("ERROR: omfs_checkmount failed: %d\n", ret);
      goto errout_with_lock;
    }

  /* Loop, skipping over unsupported items in the file system */

  for (; ; )
    {
      if (ldir->count != ln->ln_count)
        {
          ldir->currnode = ldir->firstnode;
          ldir->count = ln->ln_count;
        }

      /* Have we reached the end of the directory */

      if (!ln->ln_count || !ldir->currnode || !(*ldir->currnode))
        {
          /* We signal the end of the directory by returning the
           * special error -ENOENT
           */

          ret = -ENOENT;
          goto errout_with_lock;
        }

      next = (*ldir->currnode)->ln_next;
      strlcpy(entry->d_name, (*ldir->currnode)->ln_name,
              sizeof(entry->d_name));
      ldir->currnode++;

      /* Check the file type */

      if (IS_DIRECTORY(next))
        {
          entry->d_type = DTYPE_DIRECTORY;
          break;
        }
      else if (IS_FILE(next))
        {
          entry->d_type = DTYPE_FILE;
          break;
        }
      else if (IS_SOFTLINK(next))
        {
          entry->d_type = DTYPE_LINK;
          break;
        }
    }

errout_with_lock:
  nxrmutex_unlock(&lm->lm_lock);
  return ret;
}

/****************************************************************************
 * Name: lrofs_rewinddir
 ****************************************************************************/

static int lrofs_rewinddir(FAR struct inode *mountpt,
                           FAR struct fs_dirent_s *dir)
{
  FAR struct lrofs_mountpt_s *lm;
  FAR struct lrofs_dir_s *ldir;
  int ret;

  DEBUGASSERT(mountpt != NULL && mountpt->i_private != NULL);

  /* Recover our private data from the inode instance */

  lm = mountpt->i_private;
  ldir = (FAR struct lrofs_dir_s *)dir;

  /* Make sure that the mount is still healthy */

  nxrmutex_lock(&lm->lm_lock);
  ret = lrofs_checkmount(lm);
  if (ret >= 0)
    {
      ldir->currnode = ldir->firstnode;
    }

  nxrmutex_unlock(&lm->lm_lock);
  return ret;
}

/****************************************************************************
 * Name: lrofs_bind
 ****************************************************************************/

static int lrofs_bind(FAR struct inode *blkdriver, FAR const void *data,
                      FAR void **handle)
{
  FAR struct lrofs_mountpt_s *lm;
  int ret;

  if (blkdriver == NULL)
    {
      ferr("ERROR: No block driver/ops\n");
      return -ENODEV;
    }

  if (blkdriver->u.i_bops->open != NULL &&
      (ret = blkdriver->u.i_bops->open(blkdriver)) < 0)
    {
      ferr("ERROR: No open method\n");
      return ret;
    }

  /* Create an instance of the mountpt state structure */

  lm = fs_heap_zalloc(sizeof(struct lrofs_mountpt_s));
  if (!lm)
    {
      ferr("ERROR: Failed to allocate mountpoint structure\n");
      ret = -ENOMEM;
      goto errout;
    }

  /* Initialize the allocated mountpt state structure.  The filesystem is
   * responsible for one reference on the blkdriver inode and does not
   * have to addref() here (but does have to release in ubind().
   */

  nxrmutex_init(&lm->lm_lock);  /* Initialize the mutex that controls access */
  lm->lm_blkdriver = blkdriver; /* Save the block driver reference */

  /* Get the hardware configuration and setup buffering appropriately */

  ret = lrofs_hwconfigure(lm);
  if (ret < 0)
    {
      ferr("ERROR: lrofs_hwconfigure failed: %d\n", ret);
      goto errout_with_mount;
    }

  if (data && strstr(data, "forceformat"))
    {
      ret = lrofs_mkfs(lm);
      if (ret < 0)
        {
          ferr("ERROR: lrofs_mkfs failed: %d\n", ret);
          goto errout_with_buffer;
        }
    }

  /* Then complete the mount by getting the ROMFS configuratrion from
   * the ROMF header
   */

  ret = lrofs_fsconfigure(lm, data);
  if (ret < 0)
    {
      if (data && strstr(data, "autoformat"))
        {
          ret = lrofs_mkfs(lm);
          if (ret < 0)
            {
              ferr("ERROR: lrofs_mkfs failed: %d\n", ret);
              goto errout_with_buffer;
            }

          ret = lrofs_fsconfigure(lm, data);
          if (ret < 0)
            {
              ferr("ERROR: lrofs_fsconfigure failed: %d\n", ret);
              goto errout_with_buffer;
            }
        }
      else
        {
          ferr("ERROR: lrofs_fsconfigure failed: %d\n", ret);
          goto errout_with_buffer;
        }
    }

  nxsem_init(&lm->lm_sem, 0, 1);

  /* Mounted! */

  *handle = lm;
  return 0;

errout_with_buffer:
  fs_heap_free(lm->lm_devbuffer);

errout_with_mount:
  nxrmutex_destroy(&lm->lm_lock);
  fs_heap_free(lm);

errout:
  if (blkdriver->u.i_bops->close != NULL)
    {
      blkdriver->u.i_bops->close(blkdriver);
    }

  return ret;
}

/****************************************************************************
 * Name: lrofs_unbind
 ****************************************************************************/

static int lrofs_unbind(FAR void *handle, FAR struct inode **blkdriver,
                        unsigned int flags)
{
  FAR struct lrofs_mountpt_s *lm = handle;
  int ret;

  /* Check if there are sill any files opened on the filesystem. */

  nxrmutex_lock(&lm->lm_lock);

  if (lm->lm_refs)
    {
      /* We cannot unmount now.. there are open files */

      fwarn("WARNING: There are open files\n");

      /* This implementation currently only supports unmounting if there are
       * no open file references.
       */

      ret = flags ? -ENOSYS : -EBUSY;
    }
  else
    {
      /* Unmount ... close the block driver */

      if (lm->lm_blkdriver)
        {
          FAR struct inode *inode = lm->lm_blkdriver;
          if (inode)
            {
              if (INODE_IS_BLOCK(inode) && inode->u.i_bops->close != NULL)
                {
                  inode->u.i_bops->close(inode);
                }

              /* We hold a reference to the block driver but should
               * not but mucking with inodes in this context.  So, we will
               * just return our contained reference to the block driver
               * inode and let the umount logic dispose of it.
               */

              if (blkdriver)
                {
                  *blkdriver = inode;
                }
            }
        }

      /* Release the mountpoint private data */

      lrofs_freenode(lm->lm_root);
      nxrmutex_destroy(&lm->lm_lock);
      nxsem_destroy(&lm->lm_sem);
      lrofs_free_sparelist(&lm->lm_sparelist);
      fs_heap_free(lm->lm_devbuffer);
      fs_heap_free(lm);
      return 0;
    }

  nxrmutex_unlock(&lm->lm_lock);
  return ret;
}

/****************************************************************************
 * Name: lrofs_statfs
 ****************************************************************************/

static int lrofs_statfs(FAR struct inode *mountpt, FAR struct statfs *buf)
{
  FAR struct lrofs_mountpt_s *lm;
  int ret;

  DEBUGASSERT(mountpt && mountpt->i_private);

  /* Get the mountpoint private data from the inode structure */

  lm = mountpt->i_private;

  /* Check if the mount is still healthy */

  nxrmutex_lock(&lm->lm_lock);
  ret = lrofs_checkmount(lm);
  if (ret < 0)
    {
      ferr("ERROR: lrofs_checkmount failed: %d\n", ret);
      goto errout_with_lock;
    }

  /* Fill in the statfs info */

  buf->f_type    = LROFS_MAGIC;

  /* We will claim that the optimal transfer size is the size of one sector */

  buf->f_bsize   = lm->lm_hwsectorsize;

  /* Everything else follows in units of sectors */

  buf->f_blocks  = lm->lm_hwnsectors;
  buf->f_bfree   = buf->f_blocks -
                   SEC_NSECTORS(lm, lm->lm_volsize + SEC_NDXMASK(lm));
  buf->f_bavail  = buf->f_bfree;
  buf->f_namelen = NAME_MAX;

errout_with_lock:
  nxrmutex_unlock(&lm->lm_lock);
  return ret;
}

/****************************************************************************
 * Name: lrofs_stat_common
 ****************************************************************************/

static int lrofs_stat_common(uint8_t type, uint32_t size,
                             uint16_t sectorsize, FAR struct stat *buf)
{
  memset(buf, 0, sizeof(struct stat));
  if (IS_DIRECTORY(type))
    {
      /* It's a read-execute directory name */

      buf->st_mode = S_IFDIR | S_IROTH | S_IXOTH | S_IRGRP | S_IXGRP |
                     S_IRUSR | S_IXUSR;
    }
  else if (IS_FILE(type) || IS_SOFTLINK(type))
    {
      if (IS_FILE(type))
        {
          buf->st_mode = S_IFREG;
        }
      else
        {
          buf->st_mode = S_IFLNK;
        }

      /* It's a read-only file name */

      buf->st_mode |= S_IROTH | S_IRGRP | S_IRUSR;
      if (IS_EXECUTABLE(type))
        {
          /* It's a read-execute file name */

          buf->st_mode |= S_IXOTH | S_IXGRP | S_IXUSR;
        }
    }
  else
    {
      /* Otherwise, pretend like the unsupported type does not exist */

      finfo("Unsupported type: %d\n", type);
      return -ENOENT;
    }

  /* File/directory size, access block size */

  buf->st_size    = size;
  buf->st_blksize = sectorsize;
  buf->st_blocks  = (buf->st_size + sectorsize - 1) / sectorsize;
  return 0;
}

/****************************************************************************
 * Name: lrofs_stat
 ****************************************************************************/

static int lrofs_stat(FAR struct inode *mountpt, FAR const char *relpath,
                      FAR struct stat *buf)
{
  FAR struct lrofs_mountpt_s  *lm;
  FAR struct lrofs_nodeinfo_s *ln;
  uint8_t type;
  int ret;

  DEBUGASSERT(mountpt && mountpt->i_private);

  /* Get the mountpoint private data from the inode structure */

  lm = mountpt->i_private;

  /* Check if the mount is still healthy */

  nxrmutex_lock(&lm->lm_lock);
  ret = lrofs_checkmount(lm);
  if (ret < 0)
    {
      ferr("ERROR: lrofs_checkmount failed: %d\n", ret);
      goto errout_with_lock;
    }

  /* Find the directory entry corresponding to relpath. */

  ln = lrofs_finddirentry(lm, relpath);

  /* If nothing was found, then we fail with EEXIST */

  if (ln == NULL)
    {
      finfo("Failed to find directory: %d\n", ret);
      ret = -ENOENT;
      goto errout_with_lock;
    }

  /* Return information about the directory entry */

  type = (uint8_t)(ln->ln_next & RFNEXT_ALLMODEMASK);
  ret  = lrofs_stat_common(type, ln->ln_size, lm->lm_hwsectorsize, buf);

errout_with_lock:
  nxrmutex_unlock(&lm->lm_lock);
  return ret;
}

/****************************************************************************
 * Name: lrofs_unlink
 ****************************************************************************/

static int lrofs_unlink(FAR struct inode *mountpt, FAR const char *relpath)
{
  FAR struct lrofs_mountpt_s  *lm;
  FAR struct lrofs_nodeinfo_s *ln;
  int ret;

  DEBUGASSERT(mountpt && mountpt->i_private);

  /* Get the mountpoint private data from the inode structure */

  lm = mountpt->i_private;

  /* Check if the mount is still healthy */

  nxrmutex_lock(&lm->lm_lock);
  ret = lrofs_checkmount(lm);
  if (ret < 0)
    {
      ferr("ERROR: lrofs_checkmount failed: %d\n", ret);
      goto errout_with_lock;
    }

  /* Find the directory entry corresponding to relpath. */

  ln = lrofs_finddirentry(lm, relpath);

  /* If nothing was found, then we fail with EEXIST */

  if (ln == NULL)
    {
      finfo("Failed to find directory: %d\n", ret);
      ret = -ENOENT;
      goto errout_with_lock;
    }

  /* Verify that it is some kind of file */

  if (IS_DIRECTORY(ln->ln_next))
    {
      /* It is a directory */

      ferr("ERROR: '%s' is a directory\n", relpath);
      ret = -EISDIR;
      goto errout_with_lock;
    }
  else if (!IS_FILE(ln->ln_next))
    {
      /* ENXIO indicates "The named file is a character special or
       * block special file, and the device associated with this
       * special file does not exist."
       *
       * Here we also return ENXIO if the file is not a directory
       * or a regular file.
       */

      ferr("ERROR: '%s' is a special file\n", relpath);
      ret = -ENXIO;
      goto errout_with_lock;
    }

  /* Make sure that only one writer operating on the filesystem */

  nxrmutex_unlock(&lm->lm_lock);
  nxsem_wait_uninterruptible(&lm->lm_sem);
  nxrmutex_lock(&lm->lm_lock);

  /* Remove the file */

  ret = lrofs_remove(lm, ln->ln_parent, relpath, false);
  nxsem_post(&lm->lm_sem);

errout_with_lock:
  nxrmutex_unlock(&lm->lm_lock);
  return ret;
}

/****************************************************************************
 * Name: lrofs_mkdir
 ****************************************************************************/

static int lrofs_mkdir(FAR struct inode *mountpt, FAR const char *relpath,
                       mode_t mode)
{
  FAR struct lrofs_mountpt_s  *lm;
  FAR struct lrofs_nodeinfo_s *ln;
  int ret;

  DEBUGASSERT(mountpt && mountpt->i_private);

  /* Get the mountpoint private data from the inode structure */

  lm = mountpt->i_private;

  /* Check if the mount is still healthy */

  nxrmutex_lock(&lm->lm_lock);
  ret = lrofs_checkmount(lm);
  if (ret < 0)
    {
      ferr("ERROR: lrofs_checkmount failed: %d\n", ret);
      goto errout_with_lock;
    }

  /* Find the nodeinfo corresponding to relpath. */

  ln = lrofs_finddirentry(lm, relpath);

  /* If something was found, then we fail with EEXIST */

  if (ln != NULL)
    {
      finfo("The directory is exist: %d\n", ret);
      ret = -EEXIST;
      goto errout_with_lock;
    }

  nxrmutex_unlock(&lm->lm_lock);
  nxsem_wait_uninterruptible(&lm->lm_sem);
  nxrmutex_lock(&lm->lm_lock);

  /* Create the directory */

  ret = lrofs_create(lm, &ln, relpath, true);
  nxsem_post(&lm->lm_sem);

errout_with_lock:
  nxrmutex_unlock(&lm->lm_lock);
  return ret;
}

/****************************************************************************
 * Name: lrofs_rmdir
 ****************************************************************************/

static int lrofs_rmdir(FAR struct inode *mountpt, FAR const char *relpath)
{
  FAR struct lrofs_mountpt_s  *lm;
  FAR struct lrofs_nodeinfo_s *ln;
  int ret;

  DEBUGASSERT(mountpt && mountpt->i_private);

  /* Get the mountpoint private data from the inode structure */

  lm = mountpt->i_private;

  /* Check if the mount is still healthy */

  nxrmutex_lock(&lm->lm_lock);
  ret = lrofs_checkmount(lm);
  if (ret < 0)
    {
      ferr("ERROR: lrofs_checkmount failed: %d\n", ret);
      goto errout_with_lock;
    }

  /* Find the directory entry corresponding to relpath. */

  ln = lrofs_finddirentry(lm, relpath);
  if (ln == NULL)
    {
      finfo("Failed to find directory:%s,%d\n", relpath, ret);
      ret = -ENOENT;
      goto errout_with_lock;
    }

  /* Verify that it is some kind of directory */

  if (!IS_DIRECTORY(ln->ln_next))
    {
      ferr("ERROR: '%s' is not a directory\n", relpath);
      ret = -ENOTDIR;
      goto errout_with_lock;
    }

  if (ln->ln_count > 2)
    {
      ferr("ERROR: '%s' is not empty\n", relpath);
      ret = -ENOTEMPTY;
      goto errout_with_lock;
    }

  nxrmutex_unlock(&lm->lm_lock);
  nxsem_wait_uninterruptible(&lm->lm_sem);
  nxrmutex_lock(&lm->lm_lock);

  /* Remove the directory */

  ret = lrofs_remove(lm, ln->ln_parent, relpath, true);
  nxsem_post(&lm->lm_sem);

errout_with_lock:
  nxrmutex_unlock(&lm->lm_lock);
  return ret;
}

/****************************************************************************
 * Name: lrofs_rename
 ****************************************************************************/

static int lrofs_rename(FAR struct inode *mountpt,
                        FAR const char *oldrelpath,
                        FAR const char *newrelpath)
{
  FAR struct lrofs_nodeinfo_s *ln_old;
  FAR struct lrofs_nodeinfo_s *ln_new;
  FAR struct lrofs_nodeinfo_s *ln_newpath;
  FAR struct lrofs_mountpt_s *lm;
  FAR char *newpath;
  FAR char *newname;
  int ret;

  DEBUGASSERT(mountpt && mountpt->i_private);

  /* Get the mountpoint private data from the inode structure */

  lm = mountpt->i_private;

  /* Check if the mount is still healthy */

  nxrmutex_lock(&lm->lm_lock);
  ret = lrofs_checkmount(lm);
  if (ret < 0)
    {
      ferr("ERROR: lrofs_checkmount failed: %d\n", ret);
      goto errout_with_lock;
    }

  /* Find the file entry corresponding to old relpath. */

  ln_old = lrofs_finddirentry(lm, oldrelpath);
  if (ln_old == NULL)
    {
      ferr("Failed to find the source file: %d\n", ret);
      ret = -ENOENT;
      goto errout_with_lock;
    }

  /* Find the file entry corresponding to new relpath. */

  ln_new = lrofs_finddirentry(lm, newrelpath);
  if ((ln_new != NULL) && !IS_DIRECTORY(ln_new->ln_next))
    {
      ferr("The dist file is exist: %d\n", ret);
      ret = -EEXIST;
      goto errout_with_lock;
    }

  if (ln_new == NULL)
    {
      FAR struct lrofs_nodeinfo_s *ln_oldpath = ln_old->ln_parent;
      newname = basename((FAR char *)newrelpath);
      newpath = dirname((FAR char *)newrelpath);
      if (strcmp(newpath, ".") == 0)
        {
          ln_newpath = lm->lm_root;
        }
      else
        {
          ln_new = lrofs_finddirentry(lm, newpath);
          if (ln_new == NULL)
            {
              finfo("Failed to find new path: %d\n", ret);
              ret = -ENOENT;
              goto errout_with_lock;
            }

          if (ln_new->ln_offset == ln_oldpath->ln_offset)
            {
              /* Move file to same dir, just rename */

              ln_newpath = ln_oldpath;
            }
          else
            {
              ln_newpath = ln_new;
            }
        }
    }
  else
    {
      /* Move file to a new dir */

      newname = NULL;
      ln_newpath = ln_new;
    }

  nxrmutex_unlock(&lm->lm_lock);
  nxsem_wait_uninterruptible(&lm->lm_sem);
  nxrmutex_lock(&lm->lm_lock);

  /* Rename the file or directory */

  ret = lrofs_rename_file(lm, ln_old, ln_newpath, newname);
  nxsem_post(&lm->lm_sem);

errout_with_lock:
  nxrmutex_unlock(&lm->lm_lock);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/
