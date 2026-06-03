/****************************************************************************
 * include/sys/tree_type.h
 *
 * SPDX-License-Identifier: BSD-2-Clause
 * SPDX-FileCopyrightText: 2002 Niels Provos <provos@citi.umich.edu>
 *
 * Subset of tree.h containing only type definitions for use in headers
 * that need RB_ENTRY/SPLAY_ENTRY without full tree implementation.
 * This avoids conflicts with user-space tree.h.
 *
 ****************************************************************************/

#ifndef __INCLUDE_SYS_TREE_TYPE_H
#define __INCLUDE_SYS_TREE_TYPE_H

/****************************************************************************
 * Splay Tree Type Definitions
 ****************************************************************************/

#define SPLAY_HEAD(name, type) \
  struct name \
  { \
    struct type *sph_root; /* root of the tree */ \
  }

#define SPLAY_ENTRY(type) \
  struct \
  { \
    struct type *spe_left;  /* left element */ \
    struct type *spe_right; /* right element */ \
  }

/****************************************************************************
 * Red-Black Tree Type Definitions
 ****************************************************************************/

#define RB_HEAD(name, type) \
  struct name \
  { \
    struct type *rbh_root; /* root of the tree */ \
  }

#define RB_BLACK                       0
#define RB_RED                         1

#define RB_ENTRY(type) \
  struct \
  { \
    struct type *rbe_left;    /* left element */ \
    struct type *rbe_right;   /* right element */ \
    struct type *rbe_parent;  /* parent element */ \
    int rbe_color;            /* node color */ \
  }

#endif /* __INCLUDE_SYS_TREE_TYPE_H */
