/****************************************************************************
 * tools/wrapsymbol.c
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
#include <stdarg.h>
#include <stdbool.h>
#include <libelf.h>
#include <gelf.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAX_LINE_LENGTH 1024
#define OPTIONS_FROM_FILE 256

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct
{
  char **options;
  size_t count;
  size_t capacity;
} option_list_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int g_verbose = 0;
const char *g_input_filename;
const char *g_output_filename;

/****************************************************************************
 * Private Functions prototypes
 ****************************************************************************/

static int read_option_file(const char *filename, option_list_t *wrap_list);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: log_verbose
 *
 * Description:
 *   Print verbose log messages when verbose mode is enabled
 *
 ****************************************************************************/

static void log_verbose(const char *fmt, ...)
{
  va_list args;

  if (g_verbose)
    {
      va_start(args, fmt);
      vprintf(fmt, args);
      va_end(args);
    }
}

/****************************************************************************
 * Name: print_usage
 *
 * Description:
 *   Print usage information
 *
 ****************************************************************************/

static void print_usage(const char *progname)
{
  fprintf(stderr, "Usage: %s <input-elf-file> [--wrap=<symbol>]... "
                  "[--output=<outputfile> [-v] [@options-file]\n", progname);
  fprintf(stderr, "  --wrap=<symbol>: Wrap symbol references and "
                  "definitions\n");
  fprintf(stderr, "                       - Defined symbols: renamed to "
                  "__real_<symbol>\n");
  fprintf(stderr, "                       - Undefined symbols: renamed to "
                  "__wrap_<symbol>\n");
  fprintf(stderr, "  -o <output-elf-file>: Output to a new file "
                  "(optional)\n");
  fprintf(stderr, "  -v, --verbose:        Enable verbose output\n");
  fprintf(stderr, "  @options-file:        Read options from file "
                  "(one option per line)\n");
  fprintf(stderr, "  If -o is not specified, the input file will be "
                  "modified in place.\n");
  fprintf(stderr, "\nExample:\n");
  fprintf(stderr, "  %s input.o --wrap=malloc --wrap=free "
                  "-o output.o\n", progname);
  fprintf(stderr, "  %s input.o @wrap_options.txt -o output.o -v\n",
          progname);
}

/****************************************************************************
 * Name: free_option_list
 ****************************************************************************/

static void free_option_list(option_list_t *list)
{
  size_t i;

  for (i = 0; i < list->count; i++)
    {
      free(list->options[i]);
    }

  free(list->options);
}

/****************************************************************************
 * Name: add_option
 ****************************************************************************/

static int add_option(option_list_t *list, const char *option)
{
  void *newoption;

  if (list->count >= list->capacity)
    {
      newoption = realloc(list->options,
                          sizeof(char *) * (list->capacity +
                          OPTIONS_FROM_FILE));
      if (!newoption)
        {
          fprintf(stderr, "Error: realloc failed while adding option\n");
          return EXIT_FAILURE;
        }

      list->options = newoption;
      list->capacity += OPTIONS_FROM_FILE;
    }

  list->options[list->count] = strdup(option);
  if (!list->options[list->count])
    {
      perror("Failed to allocate memory for option");
      return EXIT_FAILURE;
    }

  list->count++;
  return 0;
}

/****************************************************************************
 * Name: trim_line
 *
 * Description:
 *   Remove newline character ,trim leading and trailing whitespace
 *   from string
 *
 ****************************************************************************/

static char *trim_line(char *str)
{
  size_t len;

  /* Remove newline character */

  len = strlen(str);
  if (len > 0 && str[len - 1] == '\n')
    {
      str[--len] = '\0';
    }

  /* Remove leading whitespace */

  while (isspace(*str))
    {
      str++;
    }

  if (*str == 0)
    {
      return str;
    }

  /* Remove trailing whitespace */

  while (len > 0 && isspace(str[--len]))
    {
      str[len] = '\0';
    }

  return str;
}

/****************************************************************************
 * Name: process_single_option
 *
 * Description:
 *   Process a single option (for both command line and file options)
 *
 ****************************************************************************/

static int process_single_option(const char *opt,
                                 option_list_t *wrap_list)
{
  if (strncmp(opt, "@", sizeof("@") - 1) == 0)
    {
      /* Process options file */

      opt += sizeof("@") - 1;
      if (opt[0] == '\0')
        {
          fprintf(stderr, "Error: @ requires a filename\n");
          return EXIT_FAILURE;
        }

      if (read_option_file(opt, wrap_list) != 0)
        {
          return EXIT_FAILURE;
        }
    }
  else if (strncmp(opt, "--wrap=", sizeof("--wrap=") - 1) == 0)
    {
      opt += sizeof("--wrap=") - 1;
      if (opt[0] == '\0')
        {
          fprintf(stderr, "Error: --wrap= requires a symbol\n");
          return EXIT_FAILURE;
        }

      if (add_option(wrap_list, opt) != 0)
        {
          return EXIT_FAILURE;
        }
    }
  else if (strncmp(opt, "--output=", sizeof("--output=") - 1) == 0 &&
           g_output_filename == NULL)
    {
      g_output_filename = strdup(opt + sizeof("--output=") - 1);
      if (g_output_filename && g_output_filename[0] == '\0')
        {
          fprintf(stderr, "Error: --output= requires a filename\n");
          return EXIT_FAILURE;
        }
    }
  else if (strcmp(opt, "-v") == 0 || strcmp(opt, "--verbose") == 0)
    {
      g_verbose = 1;
    }
  else
    {
      fprintf(stderr, "Error: Unknown option '%s'\n", opt);
      return EXIT_FAILURE;
    }

  return 0;
}

/****************************************************************************
 * Name: read_option_file
 *
 * Description:
 *   Read options from file and process them directly
 *
 ****************************************************************************/

static int read_option_file(const char *filename, option_list_t *wrap_list)
{
  FILE *file = fopen(filename, "r");
  char line[MAX_LINE_LENGTH];
  size_t count = 0;
  char *trimmed;

  if (!file)
    {
      fprintf(stderr, "Error: Failed to open options file '%s': ",
              filename);
      perror("");
      return EXIT_FAILURE;
    }

  while (fgets(line, sizeof(line), file))
    {
      /* Trim whitespace */

      trimmed = trim_line(line);

      /* Skip empty lines and comment lines (starting with #) */

      if (trimmed[0] == '\0' || trimmed[0] == '#')
        {
          continue;
        }

      /* Process option directly */

      if (process_single_option(trimmed, wrap_list) != 0)
        {
          fclose(file);
          return EXIT_FAILURE;
        }

      count++;
    }

  fclose(file);
  log_verbose("Read %d options from file '%s'\n", count, filename);
  return 0;
}

/****************************************************************************
 * Name: is_wrap_symbol
 ****************************************************************************/

static bool is_wrap_symbol(const option_list_t *list, const char *symbol)
{
  size_t i;

  for (i = 0; i < list->count; i++)
    {
      if (strcmp(list->options[i], symbol) == 0)
        {
          return true;
        }
    }

  return false;
}

/****************************************************************************
 * Name: copy_file
 ****************************************************************************/

static int copy_file(const char *dst, const char *src)
{
  char buffer[4096];
  FILE *src_file;
  FILE *dst_file;
  size_t bytes;

  src_file = fopen(src, "rb");
  if (!src_file)
    {
      perror("Failed to open source file");
      return EXIT_FAILURE;
    }

  dst_file = fopen(dst, "wb");
  if (!dst_file)
    {
      perror("Failed to create destination file");
      fclose(src_file);
      return EXIT_FAILURE;
    }

  while ((bytes = fread(buffer, 1, sizeof(buffer), src_file)) > 0)
    {
      if (fwrite(buffer, 1, bytes, dst_file) != bytes)
        {
          perror("Failed to write to destination file");
          fclose(src_file);
          fclose(dst_file);
          return EXIT_FAILURE;
        }
    }

  fclose(src_file);
  fclose(dst_file);
  return 0;
}

/****************************************************************************
 * Name: parse_arguments
 *
 * Description:
 *   Parse command line arguments
 *
 ****************************************************************************/

static int parse_arguments(int argc, char **argv,
                           option_list_t *wrap_list)
{
  int i;

  if (argc < 2)
    {
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }

  g_input_filename = argv[1];
  g_output_filename = NULL;

  /* Process command line arguments */

  for (i = 2; i < argc; i++)
    {
      if (process_single_option(argv[i], wrap_list) != 0)
        {
          print_usage(argv[0]);
          return EXIT_FAILURE;
        }
    }

  if (wrap_list->count == 0)
    {
      fprintf(stderr, "Error: At least one --wrap=<symbol> is "
                      "required\n");
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }

  return 0;
}

/****************************************************************************
 * Name: find_symbol_string_tables
 *
 * Description:
 *   Find symbol table and string table sections
 *
 ****************************************************************************/

static int find_symbol_string_tables(Elf *elf,
                                     Elf_Scn **symtab_scn,
                                     Elf_Scn **strtab_scn,
                                     GElf_Shdr *symtab_shdr,
                                     GElf_Shdr *strtab_shdr,
                                     Elf_Data **symtab_data,
                                     Elf_Data **strtab_data)
{
  Elf_Scn *scn = NULL;
  char *section_name;
  size_t shstrndx;
  GElf_Shdr shdr;

  *symtab_scn = NULL;
  *strtab_scn = NULL;

  while ((scn = elf_nextscn(elf, scn)) != NULL)
    {
      if (gelf_getshdr(scn, &shdr) != &shdr)
        {
          fprintf(stderr, "gelf_getshdr failed: %s\n", elf_errmsg(-1));
          return EXIT_FAILURE;
        }

      if (shdr.sh_type == SHT_SYMTAB)
        {
          *symtab_scn = scn;
          *symtab_shdr = shdr;
        }
      else if (shdr.sh_type == SHT_STRTAB)
        {
          if (elf_getshdrstrndx(elf, &shstrndx) == 0)
            {
              section_name = elf_strptr(elf, shstrndx, shdr.sh_name);
              if (section_name && strcmp(section_name, ".strtab") == 0)
                {
                  *strtab_scn = scn;
                  *strtab_shdr = shdr;
                }
            }
        }
    }

  if (!*symtab_scn)
    {
      fprintf(stderr, "Error: Symbol table not found\n");
      return EXIT_FAILURE;
    }

  if (!*strtab_scn)
    {
      fprintf(stderr, "Error: String table not found\n");
      return EXIT_FAILURE;
    }

  /* Get symbol table data */

  *symtab_data = elf_getdata(*symtab_scn, NULL);
  if (!*symtab_data)
    {
      fprintf(stderr, "Error: Failed to get symbol table data: %s\n",
              elf_errmsg(-1));
      return EXIT_FAILURE;
    }

  /* Get string table data */

  *strtab_data = elf_getdata(*strtab_scn, NULL);
  if (!*strtab_data)
    {
      fprintf(stderr, "Error: Failed to get string table data: %s\n",
              elf_errmsg(-1));
      return EXIT_FAILURE;
    }

  return 0;
}

/****************************************************************************
 * Name: compute_strtab_expansion_size
 *
 * Description:
 *   Calculate expansion string table size needed
 *
 ****************************************************************************/

static size_t compute_strtab_expansion_size(Elf_Data *symtab_data,
                                            Elf_Data *strtab_data,
                                            GElf_Shdr *symtab_shdr,
                                            option_list_t *wrap_list)
{
  size_t sym_count = symtab_shdr->sh_size / symtab_shdr->sh_entsize;
  size_t expansion_size = 0;
  GElf_Sym sym;
  char *name;
  size_t i;

  for (i = 0; i < sym_count; i++)
    {
      if (!gelf_getsym(symtab_data, i, &sym))
        {
          continue;
        }

      name = (char *)strtab_data->d_buf + sym.st_name;
      if (is_wrap_symbol(wrap_list, name))
        {
          /* __wrap_ or __real_ prefix (7 chars) + original name +
           * null terminator
           */

          expansion_size += strlen(name) + 8;
        }
    }

  return expansion_size;
}

/****************************************************************************
 * Name: modify_symbols
 *
 * Description:
 *   Modify symbols in the symbol table
 *
 ****************************************************************************/

static size_t modify_symbols(Elf_Data *symtab_data,
                             Elf_Data *strtab_data,
                             GElf_Shdr *symtab_shdr,
                             option_list_t *wrap_list)
{
  size_t sym_count = symtab_shdr->sh_size / symtab_shdr->sh_entsize;
  size_t strtab_offset = strtab_data->d_size;
  size_t modified = 0;
  size_t new_name_len;
  char new_name[512];
  const char *name;
  GElf_Sym sym;
  size_t i;

  for (i = 0; i < sym_count; i++)
    {
      if (!gelf_getsym(symtab_data, i, &sym))
        {
          fprintf(stderr, "gelf_getsym failed: %s\n", elf_errmsg(-1));
          continue;
        }

      name = (const char *)strtab_data->d_buf + sym.st_name;
      if (!is_wrap_symbol(wrap_list, name))
        {
          continue;
        }

      /* Check if symbol is defined or undefined
       * SHN_UNDEF (0) indicates undefined symbol
       */

      if (sym.st_shndx == SHN_UNDEF)
        {
          /* Undefined symbol -> __wrap_xxxx */

          snprintf(new_name, sizeof(new_name), "__wrap_%s", name);
          log_verbose("Renaming undefined symbol '%s' to '%s'\n",
                      name, new_name);
        }
      else
        {
          /* Defined symbol -> __real_xxxx */

          snprintf(new_name, sizeof(new_name), "__real_%s", name);
          log_verbose("Renaming defined symbol '%s' to '%s'\n",
                      name, new_name);
        }

      /* Add new symbol name to string table */

      new_name_len = strlen(new_name) + 1;
      strcpy((char *)strtab_data->d_buf + strtab_offset,
              new_name);

      /* Update symbol's string index */

      sym.st_name = strtab_offset;
      strtab_offset += new_name_len;

      /* Update symbol table */

      if (!gelf_update_sym(symtab_data, i, &sym))
        {
          fprintf(stderr, "gelf_update_sym failed: %s\n",
                  elf_errmsg(-1));
          return EXIT_FAILURE;
        }

      modified++;
    }

  return modified;
}

/****************************************************************************
 * Name: process_elf_file
 *
 * Description:
 *   Process the ELF file and modify symbols
 *
 ****************************************************************************/

static int process_elf_file(const char *filename, option_list_t *wrap_list)
{
  size_t expansion_strtab_size;
  char *new_strtab = NULL;
  int ret = EXIT_FAILURE;
  GElf_Shdr symtab_shdr;
  GElf_Shdr strtab_shdr;
  Elf_Data *symtab_data;
  Elf_Data *strtab_data;
  Elf_Scn *symtab_scn;
  Elf_Scn *strtab_scn;
  size_t modified;
  Elf *elf;
  int fd;

  /* Initialize libelf */

  if (elf_version(EV_CURRENT) == EV_NONE)
    {
      fprintf(stderr, "Error: ELF library initialization failed: %s\n",
              elf_errmsg(-1));
      return EXIT_FAILURE;
    }

  /* Open ELF file */

  fd = open(filename, O_RDWR);
  if (fd < 0)
    {
      perror("Failed to open file");
      return EXIT_FAILURE;
    }

  elf = elf_begin(fd, ELF_C_RDWR, NULL);
  if (!elf)
    {
      fprintf(stderr, "Error: elf_begin failed: %s\n", elf_errmsg(-1));
      goto cleanup;
    }

  /* Find symbol table and string table */

  if (find_symbol_string_tables(elf, &symtab_scn, &strtab_scn,
                                &symtab_shdr, &strtab_shdr,
                                &symtab_data, &strtab_data) != 0)
    {
      goto cleanup;
    }

  /* Calculate additional string table size needed */

  expansion_strtab_size = compute_strtab_expansion_size(symtab_data,
                                                        strtab_data,
                                                        &symtab_shdr,
                                                        wrap_list);

  /* Expand string table if needed */

  if (expansion_strtab_size > 0)
    {
      new_strtab = malloc(strtab_data->d_size + expansion_strtab_size);
      if (!new_strtab)
        {
          fprintf(stderr, "Error: Failed to allocate memory for new "
                  "string table\n");
          goto cleanup;
        }

      memcpy(new_strtab, strtab_data->d_buf,
             strtab_data->d_size);
      memset(new_strtab + strtab_data->d_size, 0,
             expansion_strtab_size);

      strtab_data->d_buf = new_strtab;
    }

  /* Modify symbols */

  modified = modify_symbols(symtab_data, strtab_data,
                            &symtab_shdr, wrap_list);
  if (modified == 0)
    {
      log_verbose("No symbols were modified.\n");
      ret = EXIT_SUCCESS;
    }
  else if (modified > 0)
    {
      log_verbose("Modified %d symbol(s).\n", modified);

      /* Update string table size */

      strtab_data->d_size += expansion_strtab_size;
      strtab_shdr.sh_size = strtab_data->d_size;

      /* Mark data as modified */

      elf_flagdata(strtab_data, ELF_C_SET, ELF_F_DIRTY);
      elf_flagdata(symtab_data, ELF_C_SET, ELF_F_DIRTY);

      /* Update string table section header */

      if (!gelf_update_shdr(strtab_scn, &strtab_shdr))
        {
          fprintf(stderr, "Error: Failed to update string table header: "
                          "%s\n", elf_errmsg(-1));
          goto cleanup;
        }

      /* Write modifications */

      if (elf_update(elf, ELF_C_WRITE) < 0)
        {
          fprintf(stderr, "Error: elf_update failed: %s\n",
                  elf_errmsg(-1));
          goto cleanup;
        }

      log_verbose("Successfully wrote changes to '%s'\n", filename);
      ret = EXIT_SUCCESS;
    }

cleanup:
  if (new_strtab)
    {
      free(new_strtab);
    }

  if (elf)
    {
      elf_end(elf);
    }

  close(fd);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: main
 ****************************************************************************/

int main(int argc, char **argv)
{
  int ret = EXIT_FAILURE;
  const char *filename;
  size_t i;
  option_list_t wrap_list =
    {
      0
    };

  /* Parse command line arguments */

  if (parse_arguments(argc, argv, &wrap_list) != 0)
    {
      free_option_list(&wrap_list);
      return EXIT_FAILURE;
    }

  /* Print wrap symbols list */

  log_verbose("Processing %d wrap symbol(s):\n", wrap_list.count);
  for (i = 0; i < wrap_list.count; i++)
    {
      log_verbose("  - %s\n", wrap_list.options[i]);
    }

  /* Copy input file to output file if specified */

  if (g_output_filename)
    {
      log_verbose("Copying '%s' to '%s'...\n", g_input_filename,
                  g_output_filename);

      if (copy_file(g_output_filename, g_input_filename) != 0)
        {
          fprintf(stderr, "Error: Failed to copy file\n");
          free_option_list(&wrap_list);
          return EXIT_FAILURE;
        }

      filename = g_output_filename;
    }
  else
    {
      filename = g_input_filename;
    }

  /* Process ELF file */

  ret = process_elf_file(filename, &wrap_list);

  /* If processing failed and output file was created, delete it */

  if (ret != EXIT_SUCCESS && g_output_filename)
    {
      log_verbose("Error occurred, removing output file '%s'\n",
                  g_output_filename);
      unlink(g_output_filename);
    }

  /* Cleanup */

  free_option_list(&wrap_list);
  if (g_output_filename)
    {
      free((void *)g_output_filename);
    }

  return ret;
}
