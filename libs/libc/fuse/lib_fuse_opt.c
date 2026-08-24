/****************************************************************************
 * libs/libc/fuse/lib_fuse_opt.c
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
 * Included Files
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fuse_opt.h>

#include <nuttx/lib/lib.h>

/****************************************************************************
 * Private Type Definitions
 ****************************************************************************/

struct fuse_opt_context
{
  FAR void                  *data;
  FAR const struct fuse_opt *opt;
  fuse_opt_proc_t            proc;
  int                        argctr;
  int                        argc;
  FAR char                 **argv;
  struct fuse_args           outargs;
  FAR char                  *opts;
  int                        nonopt;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline int
fuse_opt_insert_arg_common(FAR struct fuse_args *args,
                           int pos, FAR const char *arg)
{
  int ret = fuse_opt_add_arg(args, arg);

  if (ret < 0)
    {
      return ret;
    }

  if (pos != args->argc - 1)
    {
      FAR char *newarg = args->argv[args->argc - 1];

      memmove(&args->argv[pos + 1], &args->argv[pos],
              sizeof(FAR char *) * (args->argc - pos - 1));
      args->argv[pos] = newarg;
    }

  return 0;
}

static inline int next_arg(FAR struct fuse_opt_context *ctx)
{
  if (ctx->argctr + 1 >= ctx->argc)
    {
      return -EINVAL;
    }

  ctx->argctr++;
  return 0;
}

static inline int add_arg(FAR struct fuse_opt_context *ctx,
                          FAR const char *arg)
{
  return fuse_opt_add_arg(&ctx->outargs, arg);
}

static int add_opt_common(FAR char **opts,
                          FAR const char *opt, bool esc)
{
  size_t oldlen = *opts ? strlen(*opts) : 0;
  FAR char *d = lib_realloc(*opts, oldlen + 1 + strlen(opt) * 2 + 1);

  if (d == NULL)
    {
      return -ENOMEM;
    }

  *opts = d;
  if (oldlen)
    {
      d += oldlen;
      *d++ = ',';
    }

  for (; *opt; opt++)
    {
      if (esc && (*opt == ',' || *opt == '\\'))
        {
          *d++ = '\\';
        }

      *d++ = *opt;
    }

  *d = '\0';
  return 0;
}

static inline int add_opt(FAR struct fuse_opt_context *ctx,
                          FAR const char *opt)
{
  return add_opt_common(&ctx->opts, opt, true);
}

static int call_proc(FAR struct fuse_opt_context *ctx,
                     FAR const char *arg, int key, bool iso)
{
  if (key == FUSE_OPT_KEY_DISCARD)
    {
      return 0;
    }

  if (key != FUSE_OPT_KEY_KEEP && ctx->proc)
    {
      int ret = ctx->proc(ctx->data, arg, key, &ctx->outargs);

      if (ret <= 0)
        {
          return ret;
        }
    }

  if (iso)
    {
      return add_opt(ctx, arg);
    }
  else
    {
      return add_arg(ctx, arg);
    }
}

static inline int
match_template(FAR const char *t, FAR const char *arg,
               FAR size_t *sepp)
{
  FAR const char *sep = strchr(t, '=');

  sep = sep ? sep : strchr(t, ' ');
  if (sep && (!sep[1] || sep[1] == '%'))
    {
      size_t arglen = strlen(arg);
      size_t tlen = sep - t;

      if (sep[0] == '=')
        {
          tlen++;
        }

      if (arglen >= tlen && strncmp(arg, t, tlen) == 0)
        {
          *sepp = sep - t;
          return 1;
        }
    }

  if (strcmp(t, arg) == 0)
    {
      *sepp = 0;
      return 1;
    }

  return 0;
}

static FAR const struct fuse_opt *
find_opt(FAR const struct fuse_opt *opt,
         FAR const char *arg, FAR size_t *sepp)
{
  for (; opt && opt->templ; opt++)
    {
      if (match_template(opt->templ, arg, sepp))
        {
          return opt;
        }
    }

  return NULL;
}

static inline int
process_opt_param(FAR void *var, FAR const char *format,
                  FAR const char *param)
{
  if (format[1] == 's')
    {
      FAR char **s = var;
      FAR char *copy = strdup(param);

      if (copy == NULL)
        {
          return -ENOMEM;
        }

      lib_free(*s);
      *s = copy;
    }
  else
    {
      int ret = sscanf(param, format, var);

      if (ret != 1)
        {
          return ret < 0 ? -get_errno() : -EINVAL;
        }
    }

  return 0;
}

static int process_opt(FAR struct fuse_opt_context *ctx,
                       FAR const struct fuse_opt *opt, size_t sep,
                       FAR const char *arg, bool iso)
{
  if (opt->offset == -1U)
    {
      int ret = call_proc(ctx, arg, opt->value, iso);

      if (ret < 0)
        {
          return ret;
        }
    }
  else
    {
      FAR void *var = (FAR char *)ctx->data + opt->offset;

      if (sep && opt->templ[sep + 1])
        {
          FAR const char *param = arg + sep;
          int ret;

          if (opt->templ[sep] == '=')
            {
              param++;
            }

          ret = process_opt_param(var, opt->templ + sep + 1, param);
          if (ret < 0)
            {
              return ret;
            }
        }
      else
        {
          *(FAR int *)var = opt->value;
        }
    }

  return 0;
}

static inline int
process_opt_sep_arg(FAR struct fuse_opt_context *ctx,
                    FAR const struct fuse_opt *opt,
                    size_t sep, FAR const char *arg,
                    bool iso)
{
  FAR const char *param;
  FAR char *newarg;
  int ret;

  ret = next_arg(ctx);
  if (ret < 0)
    {
      return ret;
    }

  param = ctx->argv[ctx->argctr];
  newarg = lib_malloc(sep + strlen(param) + 1);
  if (newarg == NULL)
    {
      return -ENOMEM;
    }

  memcpy(newarg, arg, sep);
  strcpy(newarg + sep, param);
  ret = process_opt(ctx, opt, sep, newarg, iso);
  lib_free(newarg);

  return ret;
}

static int process_gopt(FAR struct fuse_opt_context *ctx,
                        FAR const char *arg, bool iso)
{
  size_t sep;
  FAR const struct fuse_opt *opt = find_opt(ctx->opt, arg, &sep);

  if (opt)
    {
      for (; opt; opt = find_opt(opt + 1, arg, &sep))
        {
          int ret;

          if (sep && opt->templ[sep] == ' ' && !arg[sep])
            {
              ret = process_opt_sep_arg(ctx, opt, sep, arg, iso);
            }
          else
            {
              ret = process_opt(ctx, opt, sep, arg, iso);
            }

          if (ret < 0)
            {
              return ret;
            }
        }

      return 0;
    }
  else
    {
      return call_proc(ctx, arg, FUSE_OPT_KEY_OPT, iso);
    }
}

static inline int
process_real_option_group(FAR struct fuse_opt_context *ctx,
                          FAR char *opts)
{
  FAR char *s = opts;
  FAR char *d = s;

  do
    {
      if (*s == '\0' || *s == ',')
        {
          int ret;

          *d = '\0';
          ret = process_gopt(ctx, opts, true);
          if (ret < 0)
            {
              return ret;
            }

          d = opts;
        }
      else
        {
          if (s[0] == '\\' && s[1] != '\0')
            {
              s++;
              if (s[0] >= '0' && s[0] <= '3' &&
                  s[1] >= '0' && s[1] <= '7' &&
                  s[2] >= '0' && s[2] <= '7')
                {
                  *d++ = (s[0] - '0') * 0100 +
                         (s[1] - '0') * 0010 +
                         (s[2] - '0');
                  s += 2;
                }
              else
                {
                  *d++ = *s;
                }
            }
          else
            {
              *d++ = *s;
            }
        }
    }
  while (*s++ != '\0');

  return 0;
}

static int process_option_group(FAR struct fuse_opt_context *ctx,
                                FAR const char *opts)
{
  FAR char *copy = strdup(opts);
  int ret;

  if (copy == NULL)
    {
      return -ENOMEM;
    }

  ret = process_real_option_group(ctx, copy);
  lib_free(copy);
  return ret;
}

static inline int
process_one(FAR struct fuse_opt_context *ctx, FAR const char *arg)
{
  if (ctx->nonopt || arg[0] != '-')
    {
      return call_proc(ctx, arg, FUSE_OPT_KEY_NONOPT, false);
    }
  else if (arg[1] == 'o')
    {
      if (arg[2])
        {
          return process_option_group(ctx, arg + 2);
        }
      else
        {
          int ret = next_arg(ctx);

          if (ret < 0)
            {
              return ret;
            }

          return process_option_group(ctx, ctx->argv[ctx->argctr]);
        }
    }
  else if (arg[1] == '-' && !arg[2])
    {
      int ret = add_arg(ctx, arg);

      if (ret < 0)
        {
          return ret;
        }

      ctx->nonopt = ctx->outargs.argc;
      return 0;
    }
  else
    {
      return process_gopt(ctx, arg, false);
    }
}

static inline int opt_parse(FAR struct fuse_opt_context *ctx)
{
  int ret;

  if (ctx->argc)
    {
      ret = add_arg(ctx, ctx->argv[0]);
      if (ret < 0)
        {
          return ret;
        }
    }

  for (ctx->argctr = 1; ctx->argctr < ctx->argc; ctx->argctr++)
    {
      ret = process_one(ctx, ctx->argv[ctx->argctr]);
      if (ret < 0)
        {
          return ret;
        }
    }

  /* If the option separator ("--") is the last argument, remove it */

  if (ctx->nonopt && ctx->nonopt == ctx->outargs.argc &&
      strcmp(ctx->outargs.argv[ctx->outargs.argc - 1], "--") == 0)
    {
      lib_free(ctx->outargs.argv[ctx->outargs.argc - 1]);
      ctx->outargs.argv[--ctx->outargs.argc] = NULL;
    }

  if (ctx->opts)
    {
      ret = fuse_opt_insert_arg(&ctx->outargs, 1, "-o");
      if (ret >= 0)
        {
          ret = fuse_opt_insert_arg(&ctx->outargs, 2, ctx->opts);
        }

      if (ret < 0)
        {
          return ret;
        }
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void fuse_opt_free_args(FAR struct fuse_args *args)
{
  int i;

  if (args == NULL)
    {
      return;
    }

  if (args->allocated && args->argv != NULL)
    {
      for (i = 0; i < args->argc; i++)
        {
          lib_free(args->argv[i]);
        }

      lib_free(args->argv);
    }

  args->argv      = NULL;
  args->argc      = 0;
  args->allocated = 0;
}

int fuse_opt_add_arg(FAR struct fuse_args *args, FAR const char *arg)
{
  FAR char *newarg = strdup(arg);
  FAR char **newargv;

  if (newarg == NULL)
    {
      return -ENOMEM;
    }

  newargv = lib_realloc(args->argv, (args->argc + 2) * sizeof(FAR char *));
  if (newargv == NULL)
    {
      lib_free(newarg);
      return -ENOMEM;
    }

  newargv[args->argc++] = newarg;
  newargv[args->argc]   = NULL;
  args->argv            = newargv;
  args->allocated       = 1;

  return 0;
}

int fuse_opt_insert_arg(FAR struct fuse_args *args, int pos,
                        FAR const char *arg)
{
  return fuse_opt_insert_arg_common(args, pos, arg);
}

int fuse_opt_add_opt(FAR char **opts, FAR const char *opt)
{
  return add_opt_common(opts, opt, false);
}

int fuse_opt_add_opt_escaped(FAR char **opts, FAR const char *opt)
{
  return add_opt_common(opts, opt, true);
}

int fuse_opt_match(FAR const struct fuse_opt opts[], FAR const char *opt)
{
  size_t sep;

  return find_opt(opts, opt, &sep) != NULL;
}

int fuse_opt_parse(FAR struct fuse_args *args, FAR void *data,
                   FAR const struct fuse_opt opts[], fuse_opt_proc_t proc)
{
  struct fuse_opt_context ctx;
  int ret;

  if (args == NULL || args->argv == NULL || !args->argc)
    {
      return 0;
    }

  memset(&ctx, 0, sizeof(ctx));

  ctx.data = data;
  ctx.opt  = opts;
  ctx.proc = proc;
  ctx.argc = args->argc;
  ctx.argv = args->argv;

  ret = opt_parse(&ctx);
  if (ret >= 0)
    {
      struct fuse_args tmp = *args;

      *args = ctx.outargs;
      ctx.outargs = tmp;
    }

  lib_free(ctx.opts);
  fuse_opt_free_args(&ctx.outargs);

  return ret;
}
