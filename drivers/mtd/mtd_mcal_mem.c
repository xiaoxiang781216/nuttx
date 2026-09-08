/****************************************************************************
 * drivers/mtd/mtd_mcal_mem.c
 *
 * SPDX-License-Identifier: Apache-2.0
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
 * MTD adapter for an AUTOSAR Memory Access (MemAcc) address area.
 *
 * MemAcc (AUTOSAR_SWS_MemoryAccess) uses an asynchronous polling-based job
 * model:
 *   - MemAcc_Read/Write/Erase(areaId, logicalAddr, ...) -> accept a job
 *   - MemAcc_MainFunction()                             -> advance the SM
 *   - MemAcc_GetJobStatus()/GetJobResult()              -> query completion
 *
 * NuttX MTD expects synchronous block-level operations.  Each MTD op below
 * issues a MemAcc job for the device's address area, then polls
 * MainFunction + GetJobStatus in a loop with a timeout, yielding the CPU
 * between polls, and finally maps GetJobResult to a NuttX errno.
 *
 * This relies on the vendor Mem driver being integrated in an INDIRECT
 * invocation mode, where MemAcc_MainFunction() pumps the underlying Mem
 * driver's main function as part of advancing the area's job.  Under a
 * DIRECT_STATIC integration MemAcc would not advance the Mem driver, the job
 * would stay pending forever, and this loop would only ever time out.
 *
 * Each MTD device maps 1:1 onto one MemAcc address area whose logical
 * address space starts at 0, so the logical offset handed to MemAcc is just
 * the in-device byte offset (block index * blocksize).  MemAcc_MainFunction
 * walks all Mem drivers of the partition and is non-reentrant; a single
 * global mutex (g_mtd_mcal_mem_job_lock) serialises every operation (reads included)
 * so only one job is ever in flight.
 *
 * Vendor-specific concerns MemAcc does not cover (the AURIX PRRAM prefetch
 * workaround, and hardware-specific services such as OTA bank swap / ECC
 * trap) are delegated to two small binding-provided hooks on the device.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <debug.h>
#include <assert.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/init.h>
#include <nuttx/mutex.h>
#include <nuttx/signal.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/mtd/mtd.h>

#include <nuttx/mtd/mtd_mcal_mem.h>

/* AUTOSAR MemAcc standard API.  The core drives a MemAcc implementation
 * through its STANDARD API (SWS_MemoryAccess R4.7).  It includes the vendor
 * MemAcc.h directly -- the MemAcc_* types/enums and Read/Write/Erase/... entry
 * points are fixed by the AUTOSAR standard and identical across conformant
 * vendors, so pulling the real header (instead of a hand-maintained mirror)
 * keeps the core in lockstep with the linked MemAcc library's ABI.
 *
 * MemAcc.h transitively needs the vendor Platform (Std_Types.h) and the
 * per-board generated MemAcc configuration headers on the include path, and
 * the vendor Platform Std_Types.h clashes with the autocore one on the
 * arch-global path.  So this TU is NOT built in the generic drivers target;
 * it is compiled as an isolated aux library (see drivers/mtd/CMakeLists.txt)
 * that scopes the vendor include paths to this one file -- the same isolation
 * pattern the vendor binding (aurix_mcal_mem.c) and the EB-generated Mem
 * configuration use.
 */

#include "MemAcc.h"

/* MemAcc_MainFunction() is prototyped in SchM_MemAcc.h (the AUTOSAR
 * scheduler-manager interface), not in MemAcc.h.  The core needs it to pump
 * the MemAcc state machine.  The per-board SchM/inc directory is on the aux
 * library's include path (it is appended to the global
 * NUTTX_INCLUDE_DIRECTORIES / arch include dirs by the board's SchM
 * CMakeLists, both inherited here).
 */

#include "SchM_MemAcc.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Total job poll budget in microseconds before giving up. */

#define MTD_MCAL_MEM_TIMEOUT_US 1000000  /* 1s */

/* Fixed stack bounce-buffer size for unaligned byte reads.  No dynamic
 * allocation is used; a device whose blocksize exceeds this is unsupported
 * and trips a DEBUGASSERT.  All current MCAL Mem targets use a small write
 * page as the blocksize (RRAM = 8, eFlash page-sized), well within 32.
 */

#define MTD_MCAL_MEM_BOUNCE_SIZE 32

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int mtd_mcal_mem_erase(FAR struct mtd_dev_s *dev, off_t startblock,
                              size_t nblocks);
static ssize_t mtd_mcal_mem_bread(FAR struct mtd_dev_s *dev,
                                  off_t startblock, size_t nblocks,
                                  FAR uint8_t *buffer);
static ssize_t mtd_mcal_mem_bwrite(FAR struct mtd_dev_s *dev,
                                   off_t startblock, size_t nblocks,
                                   FAR const uint8_t *buffer);
static int mtd_mcal_mem_ioctl(FAR struct mtd_dev_s *dev, int cmd,
                              unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Global MemAcc job serialisation mutex.
 *
 * MemAcc drives a single state machine through one main function, so only
 * one job may be in flight at any time across all registered devices.  Held
 * for the full duration of each job submission + completion wait.
 */

static mutex_t g_mtd_mcal_mem_job_lock = NXMUTEX_INITIALIZER;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mtd_mcal_mem_from_job_result_type
 *
 * Description:
 *   Map a completed MemAcc job result (MemAcc_JobResultType, retrieved via
 *   MemAcc_GetJobResult once the area is IDLE) to a NuttX errno.
 *   This is the completion-stage counterpart of
 *   mtd_mcal_mem_from_std_return_type():  submit_rc
 *   maps the Std_ReturnType "was the job accepted" code, this maps the "how
 *   did the job finish" code.  The two enums are distinct value spaces (e.g.
 *   value 1 is E_NOT_OK vs MEMACC_FAILED, value 3 is E_MEM_SERVICE_NOT_AVAIL
 *   vs MEMACC_CANCELED), so they must NOT share a switch.
 *
 *     - MEMACC_OK / MEMACC_ECC_CORRECTED -> OK (corrected ECC is valid
 *       data; upper layers ignore correctable bit-flips on this RRAM target)
 *     - MEMACC_ECC_UNCORRECTED           -> -EBADMSG (data lost/blank)
 *     - MEMACC_WRITE_ERROR/ERASE_ERROR   -> -EIO (genuine hardware fault)
 *     - MEMACC_CANCELED                  -> -ECANCELED (not an I/O fault)
 *     - MEMACC_INCONSISTENT              -> -EILSEQ (blank-check/compare
 *       mismatch: contents differ from expected, not an ECC/HW fault)
 *     - MEMACC_FAILED / anything else    -> -EFAULT (generic, not I/O)
 *
 * Returned Value:
 *   OK on a successful (or hardware-corrected) job; negated errno otherwise.
 *
 ****************************************************************************/

static int mtd_mcal_mem_from_job_result_type(MemAcc_JobResultType result)
{
  switch (result)
    {
      case MEMACC_OK:
        return OK;

      case MEMACC_ECC_CORRECTED:

        /* Correctable ECC: the data has already been repaired by hardware
         * and is valid.  Treat it as success; upper layers (FTL, NVS) do
         * not act on correctable bit-flips on this RRAM target.
         */

        fwarn("MTD MCAL: ECC corrected\n");
        return -EUCLEAN;

      case MEMACC_ECC_UNCORRECTED:

        /* Uncorrectable ECC: the data is lost.  Always report -EBADMSG so
         * upper layers (FTL, NVS) can recognise a blank/corrupt sector
         * regardless of the access direction.
         */

        return -EBADMSG;

      case MEMACC_WRITE_ERROR:
      case MEMACC_ERASE_ERROR:

        /* Genuine hardware I/O error on the write/erase command. */

        return -EIO;

      case MEMACC_CANCELED:

        /* Job was cancelled (e.g. by a prior timeout's MemAcc_Cancel); not
         * an I/O fault.
         */

        return -ECANCELED;

      case MEMACC_INCONSISTENT:

        /* Blank-check / compare mismatch: the read-back contents differ from
         * what was expected (e.g. a blank-check found the area not erased).
         * This is a data mismatch, not an ECC event or a hardware fault, so
         * -EILSEQ ("illegal/unexpected byte sequence") fits without colliding
         * with the -EBADMSG (ECC) or -EIO (HW) codes used above.  -EUCLEAN is
         * avoided on purpose: in the MTD/UBI stack it specifically signals a
         * corrected bitflip / block degradation, which this is not.
         */

        return -EILSEQ;

      case MEMACC_FAILED:
      default:

        /* Generic failure (or an unknown result code): not an I/O hardware
         * error.
         */

        return -EFAULT;
    }
}

/****************************************************************************
 * Name: mtd_mcal_mem_wait_job
 *
 * Description:
 *   Drive the MemAcc state machine for the device's address area until the
 *   in-flight job leaves the pending/queued state or the poll budget is
 *   exhausted.  Maps the MemAcc job result to a NuttX errno.
 *
 * Input Parameters:
 *   dev        - The adapter device (provides the is_busy hook + priv).
 *   area_id    - MemAcc address area whose job to drive (usually
 *                dev->area_id, but the async hwsvc path drives a
 *                binding-reported poll area).
 *   need_sleep - true to yield the CPU (nxsig_usleep) between polls
 *                while the job is still in flight; false to busy-poll
 *                without sleeping.  Reads pass false (a read retires in
 *                a few tight MemAcc_MainFunction() passes, and an
 *                early-boot read storm must not pay a scheduler tick per
 *                block); the asynchronous, millisecond-scale write/erase
 *                jobs pass true to yield.  Even when need_sleep is true
 *                the core only sleeps while the binding's is_busy hook
 *                reports the silicon is genuinely executing; with no hook
 *                (or hardware idle) it busy-polls.
 *
 * Returned Value:
 *   OK on success; negated errno on failure or timeout.
 *
 ****************************************************************************/

static int mtd_mcal_mem_wait_job(FAR struct mtd_mcal_mem_dev_s *dev,
                                 uint32_t area_id, bool need_sleep)
{
  MemAcc_JobStatusType status;

  /* Two timeout regimes sharing one variable (deadline).  Once the OS
   * scheduler is up (and we are not in interrupt context) the system tick
   * advances, so deadline holds a real wall-clock tick deadline: the 1s
   * budget then holds regardless of how each iteration is spent (a tight
   * busy-poll and a POLL_SLEEP_US back-off consume the same amount of the
   * budget).  Before the OS is ready the tick does not advance -- a tick-based
   * deadline would never fire (or fire at once) -- so deadline is instead a
   * raw down-counter of poll iterations as a coarse upper bound.
   */

  bool os_ready = OSINIT_OS_READY() && !up_interrupt_context();
  clock_t deadline;

  if (os_ready)
    {
      deadline = clock_systime_ticks() + USEC2TICK(MTD_MCAL_MEM_TIMEOUT_US);
    }
  else
    {
      deadline = MTD_MCAL_MEM_TIMEOUT_US;
    }

#if CONFIG_MTD_MCAL_MEM_POLL_SLEEP_US > 0
  /* Sleeping is only safe (and only requested) for the asynchronous write/
   * erase jobs, and only once the OS scheduler is up and we are not in
   * interrupt context.  A read passes need_sleep=false: a MemAcc read
   * retires in a few tight MemAcc_MainFunction() passes, and nxsig_usleep()
   * rounds up to one whole system tick (CONFIG_USEC_PER_TICK) regardless of
   * the requested POLL_SLEEP_US, so sleeping per read would make the
   * early-boot mtdlog_detect full-device scan accumulate past the multicore
   * sync-barrier budget and reset the board.  Busy-polling keeps a read in
   * the microsecond range.
   */

  bool can_sleep = need_sleep && os_ready;
#endif

  for (; ; )
    {
      /* Advance the MemAcc state machine and check for completion BEFORE any
       * sleep.  In INDIRECT_STATIC mode a read job completes on the very
       * first MemAcc_MainFunction() (Mem_17_Nvm reads have no multi-cycle
       * state machine), so the common read path must not pay a sleep at all.
       *
       * Sleeping first would add one tick to every block: nxsig_usleep()
       * rounds up to CONFIG_USEC_PER_TICK (100us here), not the requested
       * POLL_SLEEP_US (10us).  During the early-boot mtdlog_detect
       * full-device scan -- one read per block over the whole device --
       * that per-block tick accumulates past the multicore sync-barrier
       * budget (2000ms), so core0 votes late, the other cores time out,
       * and the board resets.  Checking first keeps a one-shot read
       * sleep-free; only a genuinely asynchronous job (erase/write) ever
       * backs off.
       */

      MemAcc_MainFunction();

      status = MemAcc_GetJobStatus(area_id);

      if (status == MEMACC_JOB_IDLE)
        {
          return mtd_mcal_mem_from_job_result_type(
            MemAcc_GetJobResult(area_id));
        }

      /* Not IDLE: the job must be genuinely in flight (PENDING or QUEUED)
       * before we back off and retry.  Any other status is unexpected (a
       * driver fault or out-of-range value) -- bail immediately rather than
       * spin/sleep to the timeout on a status that will never reach IDLE.
       */

      if (status != MEMACC_JOB_PENDING && status != MEMACC_JOB_QUEUED)
        {
          ferr("MTD MCAL: unexpected job status %" PRIu32 "\n", status);
          return -EIO;
        }

      /* Job still in flight.  Give up once the budget is exhausted: with the
       * OS scheduler up we compare against the wall-clock deadline (tick-
       * based, robust to how long each iteration actually took); before the
       * OS is ready the tick is frozen, so we fall back to the raw poll
       * counter as a coarse upper bound.
       */

      if (os_ready)
        {
          if ((sclock_t)(clock_systime_ticks() - deadline) >= 0)
            {
              break;
            }
        }
      else if (deadline-- == 0)
        {
          break;
        }

      /* Back off and retry.  We sleep (yield the CPU) ONLY when the binding's
       * is_busy hook confirms the silicon is genuinely executing a command --
       * i.e. the job is truly blocked on hardware.  In every other case (no
       * is_busy hook, hardware reports idle, or a read that never sets
       * can_sleep) we busy-poll: the MemAcc/Mem state machine still has
       * software work to do (arbitration, command setup) and re-pumping
       * MemAcc_MainFunction() advances it immediately, whereas nxsig_usleep()
       * would waste a whole scheduler tick.  The MemAcc job status
       * (PENDING/QUEUED) is NOT used to gate the sleep: PENDING spans the
       * pre-issue setup sub-states too, so it is not a reliable hardware-busy
       * signal.
       */

#if CONFIG_MTD_MCAL_MEM_POLL_SLEEP_US > 0
      if (can_sleep && dev != NULL && dev->is_busy != NULL &&
          dev->is_busy(dev->priv, area_id))
        {
          nxsig_usleep(CONFIG_MTD_MCAL_MEM_POLL_SLEEP_US);
        }
#endif
    }

  MemAcc_Cancel(area_id);
  return -ETIMEDOUT;
}

/****************************************************************************
 * Name: mtd_mcal_mem_from_std_return_type
 *
 * Description:
 *   Map the synchronous return code of a MemAcc_Read/Write/Erase job
 *   submission to a NuttX errno.  These MemAcc APIs are asynchronous: the
 *   return value reports only whether the job was ACCEPTED, never the media
 *   I/O outcome (that is retrieved later via GetJobResult in
 *   mtd_mcal_mem_wait_job).  A rejection here is therefore a caller/config
 *   fault, not an I/O error:
 *     - E_OK                    -> OK      (accepted; caller waits for it)
 *     - E_MEM_SERVICE_NOT_AVAIL -> -ENOSYS (underlying Mem service absent)
 *     - E_NOT_OK / anything else -> -EINVAL (job rejected: bad params/state)
 *
 *   The genuine hardware I/O failures (WRITE_ERROR / ERASE_ERROR) surface
 *   from mtd_mcal_mem_wait_job() as -EIO, so this submit stage never returns
 *   -EIO.
 *
 * Returned Value:
 *   OK if the job was accepted; negated errno otherwise.
 *
 ****************************************************************************/

static int mtd_mcal_mem_from_std_return_type(Std_ReturnType rc)
{
  switch (rc)
    {
      case E_OK:
        return OK;

      case E_MEM_SERVICE_NOT_AVAIL:
        return -ENOSYS;

      case E_NOT_OK:
      default:
        return -EINVAL;
    }
}

/****************************************************************************
 * Name: mtd_mcal_mem_do_read
 *
 * Description:
 *   Run one read against the device's single MemAcc area.  off/len are
 *   in-device byte units; because the device maps 1:1 onto one area whose
 *   logical address starts at 0, the area-logical offset is simply off.
 *   Optionally bracketed by the pre/after-read hooks
 *   (CONFIG_MCAL_MEM_READ_HOOKS) -- an escape hatch for a read-side vendor
 *   workaround.  Reads never sleep (need_sleep=false): a read retires in a
 *   few tight MainFunction passes and an early-boot read storm must not pay
 *   a scheduler tick.  Caller holds the job lock.
 *
 * Returned Value:
 *   OK on success; negated errno on failure (including an access past the
 *   end of the device).
 *
 ****************************************************************************/

static int mtd_mcal_mem_do_read(FAR struct mtd_mcal_mem_dev_s *dev,
                                off_t off, size_t len, FAR uint8_t *buf)
{
  size_t total = dev->geo.neraseblocks * dev->geo.erasesize;
  Std_ReturnType rc;
  int ret;

  if (!len || off > total || len > total - off)
    {
      return -EINVAL;
    }

#ifdef CONFIG_MCAL_MEM_READ_HOOKS
  if (dev->pre_read != NULL)
    {
      dev->pre_read(dev->priv, dev->area_id, off, len);
    }
#endif

  rc  = MemAcc_Read(dev->area_id, off, buf, len);
  ret = rc ? mtd_mcal_mem_from_std_return_type(rc)
           : mtd_mcal_mem_wait_job(dev, dev->area_id, false);

#ifdef CONFIG_MCAL_MEM_READ_HOOKS
  if (dev->after_read != NULL)
    {
      dev->after_read(dev->priv, dev->area_id, off, len);
    }
#endif

  return ret;
}

/****************************************************************************
 * Name: mtd_mcal_mem_do_write
 *
 * Description:
 *   Run one write against the device's single MemAcc area (off/len in-device
 *   byte units == area-logical, see mtd_mcal_mem_do_read).  Optionally
 *   bracketed by the pre/after-write hooks (CONFIG_MCAL_MEM_WRITE_HOOKS): a
 *   memory that needs a silicon workaround around its write sequence (AURIX
 *   PRRAM erratum NVMR_TC.003 -- disable the written bank's CPU prefetch
 *   buffer across the write and read the range back while still disabled,
 *   else a later read may return stale prefetched data) implements these;
 *   everything else leaves them NULL.  The asynchronous write yields the CPU
 *   while the silicon is busy (need_sleep=true).  Caller holds the job lock.
 *
 * Returned Value:
 *   OK on success; negated errno on failure (including an access past the
 *   end of the device).
 *
 ****************************************************************************/

static int mtd_mcal_mem_do_write(FAR struct mtd_mcal_mem_dev_s *dev,
                                 off_t off, size_t len,
                                 FAR const uint8_t *buf)
{
  size_t total = dev->geo.neraseblocks * dev->geo.erasesize;
  Std_ReturnType rc;
  int ret;

  if (!len || off > total || len > total - off)
    {
      return -EINVAL;
    }

#ifdef CONFIG_MCAL_MEM_WRITE_HOOKS
  if (dev->pre_write != NULL)
    {
      dev->pre_write(dev->priv, dev->area_id, off, len);
    }
#endif

  rc  = MemAcc_Write(dev->area_id, off, buf, len);
  ret = rc ? mtd_mcal_mem_from_std_return_type(rc)
           : mtd_mcal_mem_wait_job(dev, dev->area_id, true);

#ifdef CONFIG_MCAL_MEM_WRITE_HOOKS
  if (dev->after_write != NULL)
    {
      dev->after_write(dev->priv, dev->area_id, off, len);
    }
#endif

  return ret;
}

/****************************************************************************
 * Name: mtd_mcal_mem_erase
 ****************************************************************************/

static int mtd_mcal_mem_erase(FAR struct mtd_dev_s *dev, off_t startblock,
                              size_t nblocks)
{
  FAR struct mtd_mcal_mem_dev_s *priv =
      (FAR struct mtd_mcal_mem_dev_s *)dev;
  off_t off;
  size_t len;
  size_t total;
  Std_ReturnType rc;
  int ret = OK;

  /* erasesize == blocksize marks a memory with no erase operation (RRAM):
   * the binding collapses erasesize to the write page for such a device,
   * whereas a real erasable flash always has erasesize > blocksize.
   */

  if (priv->geo.erasesize == priv->geo.blocksize)
    {
      return -ENOSYS;
    }

  off   = startblock * priv->geo.erasesize;
  len   = nblocks * priv->geo.erasesize;
  total = priv->geo.neraseblocks * priv->geo.erasesize;

  /* Reject an erase that runs past the end of the area (overflow-safe).
   * An out-of-range offset/length is a caller parameter fault, not a media
   * I/O error, so report -EINVAL (reserve -EIO for genuine hardware faults).
   */

  if (off > total || len > total - off)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_mtd_mcal_mem_job_lock);
  if (ret < 0)
    {
      return ret;
    }

  /* 1:1 device -> one area: the area-logical offset is simply off.  Bracket
   * the erase with the optional pre/after-erase hooks (gated by
   * CONFIG_MCAL_MEM_ERASE_HOOKS) -- an escape hatch for an erase-side vendor
   * workaround; NULL/disabled otherwise.
   */

#ifdef CONFIG_MCAL_MEM_ERASE_HOOKS
  if (priv->pre_erase != NULL)
    {
      priv->pre_erase(priv->priv, priv->area_id, off, len);
    }
#endif

  rc  = MemAcc_Erase(priv->area_id, off, len);
  ret = rc ? mtd_mcal_mem_from_std_return_type(rc)
           : mtd_mcal_mem_wait_job(priv, priv->area_id, true);

#ifdef CONFIG_MCAL_MEM_ERASE_HOOKS
  if (priv->after_erase != NULL)
    {
      priv->after_erase(priv->priv, priv->area_id, off, len);
    }
#endif

  nxmutex_unlock(&g_mtd_mcal_mem_job_lock);
  return ret;
}

/****************************************************************************
 * Name: mtd_mcal_mem_bread
 ****************************************************************************/

static ssize_t mtd_mcal_mem_bread(FAR struct mtd_dev_s *dev,
                                  off_t startblock, size_t nblocks,
                                  FAR uint8_t *buffer)
{
  FAR struct mtd_mcal_mem_dev_s *priv =
      (FAR struct mtd_mcal_mem_dev_s *)dev;
  size_t nblk = (priv->geo.neraseblocks * priv->geo.erasesize) /
                priv->geo.blocksize;
  size_t len;
  off_t off;
  int ret;

  /* Validate in the block domain before converting to a byte offset/length:
   * startblock * blocksize can overflow off_t for a wildly out-of-range
   * index and wrap to a small (or negative) value that would slip past the
   * byte-level bounds check in mtd_mcal_mem_do_read().
   */

  if (startblock < 0 || startblock >= nblk ||
      nblocks > nblk - startblock)
    {
      return -EINVAL;
    }

  off = startblock * priv->geo.blocksize;
  len = nblocks * priv->geo.blocksize;

  ret = nxmutex_lock(&g_mtd_mcal_mem_job_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = mtd_mcal_mem_do_read(priv, off, len, buffer);
  nxmutex_unlock(&g_mtd_mcal_mem_job_lock);

  return ret < 0 ? ret : nblocks;
}

/****************************************************************************
 * Name: mtd_mcal_mem_bwrite
 ****************************************************************************/

static ssize_t mtd_mcal_mem_bwrite(FAR struct mtd_dev_s *dev,
                                   off_t startblock, size_t nblocks,
                                   FAR const uint8_t *buffer)
{
  FAR struct mtd_mcal_mem_dev_s *priv =
      (FAR struct mtd_mcal_mem_dev_s *)dev;
  size_t nblk = (priv->geo.neraseblocks * priv->geo.erasesize) /
                priv->geo.blocksize;
  size_t len;
  off_t off;
  int ret;

  /* Validate in the block domain before converting to a byte offset/length
   * (see mtd_mcal_mem_bread): guards against a startblock * blocksize
   * overflow wrapping past the byte-level bounds check in
   * mtd_mcal_mem_do_write().
   */

  if (startblock < 0 || startblock >= nblk ||
      nblocks > nblk - startblock)
    {
      return -EINVAL;
    }

  off = startblock * priv->geo.blocksize;
  len = nblocks * priv->geo.blocksize;

  ret = nxmutex_lock(&g_mtd_mcal_mem_job_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = mtd_mcal_mem_do_write(priv, off, len, buffer);
  nxmutex_unlock(&g_mtd_mcal_mem_job_lock);

  return ret < 0 ? ret : nblocks;
}

/****************************************************************************
 * Name: mtd_mcal_mem_ioctl
 ****************************************************************************/

static int mtd_mcal_mem_ioctl(FAR struct mtd_dev_s *dev, int cmd,
                              unsigned long arg)
{
  FAR struct mtd_mcal_mem_dev_s *priv =
      (FAR struct mtd_mcal_mem_dev_s *)dev;
  int ret = -EINVAL;

  switch (cmd)
    {
      case MTDIOC_GEOMETRY:
        {
          FAR struct mtd_geometry_s *geo =
              (FAR struct mtd_geometry_s *)arg;
          if (geo != NULL)
            {
              /* priv->geo already holds a self-consistent standard
               * mtd_geometry_s: for RRAM the binding has collapsed erasesize
               * to blocksize and scaled neraseblocks so that
               * erasesize * neraseblocks == total capacity, so a plain copy
               * is all that is needed.
               */

              *geo = priv->geo;
              ret  = OK;
            }
        }
        break;

      case MTDIOC_ERASESTATE:
        {
          FAR uint8_t *result = (FAR uint8_t *)arg;
          if (result != NULL)
            {
              *result = priv->erased_value;
              ret = OK;
            }
        }
        break;

      case MTDIOC_BULKERASE:
        ret = mtd_mcal_mem_erase(dev, 0, priv->geo.neraseblocks);
        break;

      case MTDIOC_ERASESECTORS:
        {
          FAR struct mtd_erase_s *erase = (FAR struct mtd_erase_s *)arg;
          if (erase != NULL)
            {
              ret = mtd_mcal_mem_erase(dev, erase->startblock,
                                       erase->nblocks);
            }
        }
        break;

      case BIOC_XIPBASE:
        {
          FAR void **ppv = (FAR void **)arg;
          if (ppv != NULL)
            {
              *ppv = (FAR void *)(uintptr_t)priv->base_addr;
              ret = OK;
            }
        }
        break;

      case BIOC_PARTINFO:
        {
          FAR struct partition_info_s *info =
              (FAR struct partition_info_s *)arg;
          if (info != NULL)
            {
              info->numsectors  = (priv->geo.erasesize *
                                   priv->geo.neraseblocks) /
                                  priv->geo.blocksize;
              info->sectorsize  = priv->geo.blocksize;
              info->startsector = 0;
              info->parent[0]   = '\0';
              ret = OK;
            }
        }
        break;

      case MTDIOC_MCAL_MEM_HWSVC:
        {
          FAR struct mtd_mcal_mem_hwsvc_s *req =
              (FAR struct mtd_mcal_mem_hwsvc_s *)arg;

          /* Hardware-specific service passthrough.  By default the request is
           * forwarded straight to the standard MemAcc_HwSpecificService: the
           * caller supplies the vendor hwServiceId and the MemAcc area whose
           * Mem instance the service runs on, so the core needs no vendor
           * service enum.  The core resolves that area to its concrete MemAcc
           * hwId via MemAcc_GetMemoryInfo (area and instance are decoupled, so
           * the service may target an instance other than this device's own --
           * e.g. the OTA swap UCB).  The job is issued and driven to completion
           * on this device's own MemAcc area by the shared poll loop; the
           * vendor writes any output into req->data while the job runs.
           *
           * A binding may install an hwsvc hook to override this for a
           * vendor-private service that the standard MemAcc API cannot
           * express; when present it fully replaces the default path.
           */

          if (req == NULL)
            {
              ret = -EINVAL;
              break;
            }

          ret = nxmutex_lock(&g_mtd_mcal_mem_job_lock);
          if (ret < 0)
            {
              break;
            }

          if (priv->hwsvc != NULL)
            {
              ret = priv->hwsvc(priv->priv, req);
            }
          else
            {
              MemAcc_LengthType len = req->length;
              MemAcc_MemoryInfoType info;

              /* Resolve the caller-supplied area id to the Mem instance's
               * MemAcc hwId (offset 0 is always inside the area).
               */

              if (MemAcc_GetMemoryInfo(req->hw_id_area, 0, &info) != E_OK)
                {
                  ret = -EINVAL;
                }
              else if (MemAcc_HwSpecificService(priv->area_id, info.HwId,
                                                req->hw_service_id,
                                                (FAR const MemAcc_DataType *)
                                                req->data, &len) == E_OK)
                {
                  ret = mtd_mcal_mem_wait_job(priv, priv->area_id, true);

                  /* Surface the concrete MemAcc job-result code for diagnostics
                   * (a diagnostic run reads this even on failure to tell the
                   * failure modes apart).
                   */

                  req->result = MemAcc_GetJobResult(priv->area_id);
                }
              else
                {
                  ret = -EIO;
                  req->result = MemAcc_GetJobResult(priv->area_id);
                }
            }

          nxmutex_unlock(&g_mtd_mcal_mem_job_lock);
        }
        break;

      default:
        ret = -ENOTTY;
        break;
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mtd_mcal_mem_register
 ****************************************************************************/

FAR struct mtd_dev_s *
mtd_mcal_mem_register(FAR struct mtd_mcal_mem_dev_s *dev)
{
  if (dev == NULL)
    {
      ferr("ERROR: MTD MCAL: NULL device\n");
      return NULL;
    }

  if (dev->geo.erasesize == 0 || dev->geo.blocksize == 0 ||
      dev->geo.neraseblocks == 0)
    {
      ferr("ERROR: MTD MCAL: zero geometry\n");
      return NULL;
    }

  if (dev->geo.erasesize != dev->geo.blocksize)
    {
      dev->mtd.erase = mtd_mcal_mem_erase;
    }
  else
    {
      dev->mtd.erase = NULL;
    }

  dev->mtd.bread  = mtd_mcal_mem_bread;
  dev->mtd.bwrite = mtd_mcal_mem_bwrite;
  dev->mtd.ioctl  = mtd_mcal_mem_ioctl;

  return &dev->mtd;
}

/****************************************************************************
 * Name: mtd_mcal_mem_geo_from_area
 ****************************************************************************/

bool mtd_mcal_mem_geo_from_area(uint32_t area_id, size_t nsaa,
                                FAR struct mtd_mcal_mem_dev_s *dev,
                                bool no_erase)
{
  /* nsaa (from the binding) bounds the SAA walk so it never probes past the
   * area end, which MemAcc would report as the DET
   * MEMACC_E_PARAM_ADDRESS_LENGTH.  Zero nsaa yields an empty walk.
   */

  MemAcc_MemoryInfoType info;
  size_t   logical     = 0;
  size_t   base        = SIZE_MAX;
  size_t   total       = 0;
  size_t   page_size   = 0;
  size_t   sector_size = 0;
  size_t   count       = 0;

  if (dev == NULL)
    {
      return false;
    }

  /* Walk the area's SAAs by logical address.  MemAcc_GetMemoryInfo returns
   * the SAA containing the queried address (LogicalStartAddress + MaxOffset);
   * stepping to that SAA's end lands on the next SAA's start, aggregating a
   * multi-module area (e.g. an AURIX PFLASH bank of six PFLASHxx SAAs) into
   * one MTD capacity.  An early query failure (malformed area) is caught by
   * the count/total checks below.
   */

  while (count < nsaa &&
         MemAcc_GetMemoryInfo(area_id, logical, &info) == E_OK)
    {
      if (info.MaxOffset == 0 || info.EraseSectorSize == 0 ||
          info.WritePageSize == 0)
        {
          ferr("ERROR: MTD MCAL: bad SAA geometry in area %" PRIu32 "\n",
               area_id);
          return false;
        }

      /* Page / sector size must be uniform across the whole area so a single
       * MTD geometry describes it.
       */

      if (page_size == 0)
        {
          page_size   = info.WritePageSize;
          sector_size = info.EraseSectorSize;
        }
      else if (info.WritePageSize != page_size ||
               info.EraseSectorSize != sector_size)
        {
          ferr("ERROR: MTD MCAL: mixed geometry in area %" PRIu32 "\n",
               area_id);
          return false;
        }

      if (info.PhysicalStartAddress < base)
        {
          base = info.PhysicalStartAddress;
        }

      total  += info.MaxOffset;
      logical = info.LogicalStartAddress + info.MaxOffset;
      count++;
    }

  if (count == 0 || total == 0 ||
      sector_size == 0 || (total % sector_size) != 0)
    {
      return false;
    }

  /* Standard MTD geometry: blocksize = write/read page, erasesize = erase
   * sector, neraseblocks = area size / erase sector.
   */

  dev->geo.blocksize    = page_size;
  dev->geo.erasesize    = sector_size;
  dev->geo.neraseblocks = total / sector_size;
  strlcpy(dev->geo.model, "MCAL-MEM", sizeof(dev->geo.model));

  dev->base_addr    = base;
  dev->erased_value = 0x00;

  if (no_erase)
    {
      /* No erase operation (e.g. RRAM): collapse erasesize to the write page
       * and scale neraseblocks up by the same ratio so that
       * erasesize * neraseblocks stays equal to the total capacity (keeps
       * block-count arithmetic and BIOC_PARTINFO consistent).  erasesize ==
       * blocksize then marks "no erase" to the upper layers -- a real erasable
       * flash always has erasesize > blocksize.
       */

      dev->geo.neraseblocks = total / page_size;
      dev->geo.erasesize    = page_size;
    }

  return true;
}

/****************************************************************************
 * Name: mtd_mcal_mem_unregister
 ****************************************************************************/

void mtd_mcal_mem_unregister(FAR struct mtd_dev_s *dev)
{
  FAR struct mtd_mcal_mem_dev_s *priv =
      (FAR struct mtd_mcal_mem_dev_s *)dev;

  if (priv != NULL)
    {
      /* Statically allocated by the binding: just detach the MTD ops. */

      priv->mtd.erase  = NULL;
      priv->mtd.bread  = NULL;
      priv->mtd.bwrite = NULL;
      priv->mtd.read   = NULL;
      priv->mtd.ioctl  = NULL;
    }
}
