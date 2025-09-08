/****************************************************************************
 * libs/libc/locale/lib_setlocale.c
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

#include <nuttx/config.h>

#include <sys/types.h>
#include <string.h>
#include <locale.h>
#include <stdlib.h>

#ifdef CONFIG_LIBC_LOCALE

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const char * const g_locale_env[] =
{
  "LC_CTYPE",
  "LC_NUMERIC",
  "LC_TIME",
  "LC_COLLATE",
  "LC_MONETARY",
  "LC_MESSAGES",
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: normalize_locale
 *
 * Description:
 *   Maps the locale names "C" and "POSIX" to "C", and maps any other locale
 *   name to "".
 *
 * Input Parameters:
 *   locale - system-specific locale identifier.
 *
 * Returned Value:
 *   Pointer to a null-terminated string identifying the locale of system,
 *   or null pointer on failure.
 ****************************************************************************/

static inline FAR char *normalize_locale(FAR const char *locale)
{
  return (strcmp(locale, "C") == 0 ||
          strcmp(locale, "POSIX") == 0) ? "C" : NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: setlocale
 *
 * Description:
 *   For Nuttx, we only support setting the locale to "C".
 *   So we can assume that a global variables, the member of it
 *   include "LC_CTYPE", "LC_NUMERIC", "LC_TIME", "LC_COLLATE", "LC_MONETARY"
 *   and "LC_MESSAGES". All members have been set as "C".
 *
 *   The setlocale function installs the specified system locale as "C"
 *   locale.
 *
 *   If locale is a null pointer, setlocale queries the current C locale
 *   without modifying it.
 *
 * Input Parameters:
 *   category - locale category identifier, one of the LC_* macros.
 *   locale - system-specific locale identifier. Only support "C".
 *
 * Returned Value:
 *   Pointer to a null-terminated string identifying the locale of system,
 *   or null pointer on failure.
 ****************************************************************************/

FAR char *setlocale(int category, FAR const char *locale)
{
  FAR const char *value;
  int i;

  /* If category is invalid, return NULL. */

  if (category < 0 || category > LC_ALL)
    {
      return NULL;
    }

  /* When the locale is a non empty string, for any category, if the locale
   * is "C" or "POSXI", the valid locale is set to "C"; Otherwise, no valid
   * local setting is specified, and return NULL.
   */

  if (locale != NULL && locale[0] != '\0')
    {
      return normalize_locale(locale);
    }

  /* If locale is NULL or an empty string, get the locale setting from the
   * environment variable. The rules are as follows:
   * When the environment variables are not an empty string and none of the
   * environment variables are "C" or "POSIX", an invalid local setting is
   * found, return NULL;
   * When the environment variables are not an empty string and are one of
   * "C" and "POSIX", then the valid locale is set to "C";
   * When the queried environment variables are all empty strings, the
   * default locale setting is "C";
   *
   * The environment variables query order is LC_ALL, LC_ *, and LANG.
   */

  if ((value = getenv("LC_ALL")) != NULL)
    {
      return normalize_locale(value);
    }

  if (category == LC_ALL)
    {
      for (i = 0; i < LC_ALL; i++)
        {
          if ((value = getenv(g_locale_env[i])) != NULL)
            {
              return normalize_locale(value);
            }
        }
    }
  else
    {
      if ((value = getenv(g_locale_env[category])) != NULL)
        {
          return normalize_locale(value);
        }
    }

  if ((value = getenv("LANG")) != NULL)
    {
      return normalize_locale(value);
    }

  return "C";
}
#endif
