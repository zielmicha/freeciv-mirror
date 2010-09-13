/********************************************************************** 
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

/**************************************************************************
  the idea with this file is to create something similar to the ms-windows
  .ini files functions.
  however the interface is nice. ie:
  secfile_lookup_str(file, "player%d.unit%d.name", plrno, unitno); 
***************************************************************************/

/**************************************************************************
  Description of the file format:
  (This is based on a format by the original authors, with
  various incremental extensions. --dwp)
  
  - Whitespace lines are ignored, as are lines where the first
  non-whitespace character is ';' (comment lines).
  Optionally '#' can also be used for comments.

  - A line of the form:
       *include "filename"
  includes the named file at that point.  (The '*' must be the
  first character on the line.) The file is found by looking in
  FREECIV_PATH.  Non-infinite recursive includes are allowed.
  
  - A line with "[name]" labels the start of a section with
  that name; one of these must be the first non-comment line in
  the file.  Any spaces within the brackets are included in the
  name, but this feature (?) should probably not be used...

  - Within a section, lines have one of the following forms:
      subname = "stringvalue"
      subname = -digits
      subname = digits
      subname = TRUE
      sunname = FALSE
  for a value with given name and string, negative integer, and
  positive integer values, respectively.  These entries are
  referenced in the following functions as "sectionname.subname".
  The section name should not contain any dots ('.'); the subname
  can, but they have no particular significance.  There can be
  optional whitespace before and/or after the equals sign.
  You can put a newline after (but not before) the equals sign.
  
  Backslash is an escape character in strings (double-quoted strings
  only, not names); recognised escapes are \n, \\, and \".
  (Any other \<char> is just treated as <char>.)

  - Gettext markings:  You can surround strings like so:
      foo = _("stringvalue")
  The registry just ignores these extra markings, but this is
  useful for marking strings for translations via gettext tools.

  - Multiline strings:  Strings can have embeded newlines, eg:
    foo = _("
    This is a string
    over multiple lines
    ")
  This is equivalent to:
    foo = _("\nThis is a string\nover multiple lines\n")
  Note that if you missplace the trailing doublequote you can
  easily end up with strange errors reading the file...

  - Vector format: An entry can have multiple values separated
  by commas, eg:
      foo = 10, 11, "x"
  These are accessed by names "foo", "foo,1" and "foo,2"
  (with section prefix as above).  So the above is equivalent to:
      foo   = 10
      foo,1 = 11
      foo,2 = "x"
  As in the example, in principle you can mix integers and strings,
  but the calling program will probably require elements to be the
  same type.   Note that the first element of a vector is not "foo,0",
  in order that the name of the first element is the same whether or
  not there are subsequent elements.  However as a convenience, if
  you try to lookup "foo,0" then you get back "foo".  (So you should
  never have "foo,0" as a real name in the datafile.)

  - Tabular format:  The lines:
      foo = { "bar",  "baz",   "bax"
              "wow",   10,     -5
              "cool",  "str"
              "hmm",    314,   99, 33, 11
      }
  are equivalent to the following:
      foo0.bar = "wow"
      foo0.baz = 10
      foo0.bax = -5
      foo1.bar = "cool"
      foo1.baz = "str"
      foo2.bar = "hmm"
      foo2.baz = 314
      foo2.bax = 99
      foo2.bax,1 = 33
      foo2.bax,2 = 11
  The first line specifies the base name and the column names, and the
  subsequent lines have data.  Again it is possible to mix string and
  integer values in a column, and have either more or less values
  in a row than there are column headings, but the code which uses
  this information (via the registry) may set more stringent conditions.
  If a row has more entries than column headings, the last column is
  treated as a vector (as above).  You can optionally put a newline
  after '=' and/or after '{'.

  The equivalence above between the new and old formats is fairly
  direct: internally, data is converted to the old format.
  In principle it could be a good idea to represent the data
  as a table (2-d array) internally, but the current method
  seems sufficient and relatively simple...
  
  There is a limited ability to save data in tabular:
  So long as the section_file is constructed in an expected way,
  tabular data (with no missing or extra values) can be saved
  in tabular form.  (See section_file_save().)

  - Multiline vectors: if the last non-comment non-whitespace
  character in a line is a comma, the line is considered to
  continue on to the next line.  Eg:
      foo = 10,
            11,
            "x"
  This is equivalent to the original "vector format" example above.
  Such multi-lines can occur for column headings, vectors, or
  table rows, again with some potential for strange errors...

***************************************************************************/

/**************************************************************************
  Hashing registry lookups: (by dwp)
  - Have a hash table direct to entries, bypassing sections division.
  - For convenience, store the key (the full section+entry name)
    in the hash table (some memory overhead).
  - The number of entries is fixed when the hash table is built.
  - Now uses hash.c
**************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* utility */
#include "astring.h"
#include "fcintl.h"
#include "genlist.h"
#include "hash.h"
#include "inputfile.h"
#include "ioz.h"
#include "log.h"
#include "mem.h"
#include "shared.h"
#include "support.h"

#include "registry.h"

#define MAX_LEN_BUFFER 1024

/* Set to FALSE for old-style savefiles. */
#define SAVE_TABLES TRUE
/* Debug function for every new entry. */
#define DEBUG_ENTRIES(...) /* log_debug(__VA_ARGS__); */

#define SPECVEC_TAG astring
#include "specvec.h"


static void secfile_log(const struct section_file *secfile,
                        const struct section *psection,
                        const char *file, const char *function, int line,
                        const char *format, ...)
                        fc__attribute((__format__(__printf__, 6, 7)));
#define SECFILE_LOG(secfile, psection, format, ...)                         \
  secfile_log(secfile, psection, __FILE__, __FUNCTION__, __LINE__,          \
              format, ## __VA_ARGS__)
#define SECFILE_RETURN_IF_FAIL(secfile, psection, condition)                \
  if (!(condition)) {                                                       \
    SECFILE_LOG(secfile, psection, "Assertion '%s' failed.", #condition);   \
    return;                                                                 \
  }
#define SECFILE_RETURN_VAL_IF_FAIL(secfile, psection, condition, value)     \
  if (!(condition)) {                                                       \
    SECFILE_LOG(secfile, psection, "Assertion '%s' failed.", #condition);   \
    return value;                                                           \
  }

static inline bool entry_used(const struct entry *pentry);
static inline void entry_use(struct entry *pentry);

static void entry_to_file(const struct entry *pentry, fz_FILE *fs);
static void entry_from_token(struct section *psection, const char *name,
                             const char *tok, struct inputfile *file);

static char error_buffer[MAX_LEN_BUFFER] = "\0";

/**************************************************************************
  Returns the last error which occured in a string.  It never returns NULL.
**************************************************************************/
const char *secfile_error(void)
{
  return error_buffer;
}

/**************************************************************************
  Edit the error_buffer.
**************************************************************************/
static void secfile_log(const struct section_file *secfile,
                        const struct section *psection,
                        const char *file, const char *function, int line,
                        const char *format, ...)
{
  char message[MAX_LEN_BUFFER];
  va_list args;

  va_start(args, format);
  fc_vsnprintf(message, sizeof(message), format, args);
  va_end(args);

  fc_snprintf(error_buffer, sizeof(error_buffer),
              "In %s() [%s:%d]: secfile '%s' in section '%s': %s",
              function, file, line, secfile_name(secfile),
              psection != NULL ? section_name(psection) : "NULL", message);
}

/****************************************************************************
  Copies a string. Backslash followed by a genuine newline always
  removes the newline.
  If full_escapes is TRUE:
    - '\n' -> newline translation.
    - Other '\c' sequences (any character 'c') are just passed
      through with the '\' removed (eg, includes '\\', '\"').
  See also make_escapes().
****************************************************************************/
static void remove_escapes(const char *str, bool full_escapes,
                           char *buf, size_t buf_len)
{
  char *dest = buf;
  const char *const max = buf + buf_len - 1;

  while (*str != '\0' && dest < max) {
    if (*str == '\\' && *(str + 1) == '\n') {
      /* Escape followed by newline. Skip both */
      str += 2;
    } else if (full_escapes && *str == '\\') {
      str++;
      if (*str == 'n') {
        *dest++ = '\n';
        str++;
      }
    } else {
      *dest++ = *str++;
    }
  }
  *dest = '\0';
}

/****************************************************************************
  Copies a string and convert the following characters:
  - '\n' to "\\n".
  - '\\' to "\\\\".
  - '\"' to "\\\"".
  See also remove_escapes().
****************************************************************************/
static void make_escapes(const char *str, char *buf, size_t buf_len)
{
  char *dest = buf;
  /* Sometimes we insert 2 characters at once ('\n' -> "\\n"), so keep
   * place for '\0' and an extra character. */
  const char *const max = buf + buf_len - 2;

  while (*str != '\0' && dest < max) {
    switch (*str) {
    case '\n':
      *dest++ = '\\';
      *dest++ = 'n';
      str++;
      break;
    case '\\':
    case '\"':
      *dest++ = '\\';
      /* Fallthrough. */
    default:
      *dest++ = *str++;
      break;
    }
  }
  *dest = 0;
}

/***************************************************************************
  Simplification of fileinfoname().
***************************************************************************/
static const char *datafilename(const char *filename)
{
  return fileinfoname(get_data_dirs(), filename);
}


/* The section file struct itself. */
struct section_file {
  char *name;                           /* Can be NULL. */
  size_t num_entries;
  struct section_list *sections;
  bool allow_duplicates;
  struct {
    struct hash_table *sections;
    struct hash_table *entries;
  } hash;
};

/**************************************************************************
  Ensure name is correct to use it as section or entry name.
**************************************************************************/
static bool check_name(const char *name)
{
  static const char *const allowed = "_.,-[]";

  while ('\0' != *name) {
    if (!fc_isalnum(*name) && NULL == strchr(allowed, *name)) {
      return FALSE;
    }
    name++;
  }
  return TRUE;
}


/**************************************************************************
  Create a new empty section file.
**************************************************************************/
struct section_file *secfile_new(bool allow_duplicates)
{
  struct section_file *secfile = fc_malloc(sizeof(struct section_file));

  secfile->name = NULL;
  secfile->num_entries = 0;
  secfile->sections = section_list_new_full(section_destroy);
  secfile->allow_duplicates = allow_duplicates;

  secfile->hash.sections = hash_new(hash_fval_string,
                                    hash_fcmp_string);
  /* Maybe alloced after. */
  secfile->hash.entries = NULL;

  return secfile;
}

/**************************************************************************
  Free a section file.
**************************************************************************/
void secfile_destroy(struct section_file *secfile)
{
  SECFILE_RETURN_IF_FAIL(secfile, NULL, secfile != NULL);

  hash_free(secfile->hash.sections);
  /* Mark it NULL to be sure to don't try to make operations when
   * deleting the entries. */
  secfile->hash.sections = NULL;
  if (NULL != secfile->hash.entries) {
    hash_free(secfile->hash.entries);
    /* Mark it NULL to be sure to don't try to make operations when
     * deleting the entries. */
    secfile->hash.entries = NULL;
  }

  section_list_destroy(secfile->sections);

  if (NULL != secfile->name) {
    free(secfile->name);
  }

  free(secfile);
}

/**************************************************************************
  Insert an entry into the hash table.  Returns TRUE on success.
**************************************************************************/
static bool secfile_hash_insert(struct section_file *secfile,
                                struct entry *pentry)
{
  char buf[256];
  struct entry *hentry;

  if (NULL == secfile->hash.entries) {
    /* Consider as success if this secfile doesn't have built the entries
     * hash table. */
    return TRUE;
  }

  entry_path(pentry, buf, sizeof(buf));
  hentry = hash_replace(secfile->hash.entries, strdup(buf), pentry);
  if (hentry) {
    entry_use(hentry);
    if (!secfile->allow_duplicates) {
      SECFILE_LOG(secfile, entry_section(hentry),
                  "Tried to insert same value twice: %s", buf);
      return FALSE;
    }
  }

  return TRUE;
}

/**************************************************************************
  Delete an entry from the hash table.  Returns TRUE on success.
**************************************************************************/
static bool secfile_hash_delete(struct section_file *secfile,
                                struct entry *pentry)
{
  char buf[256];

  if (NULL == secfile->hash.entries) {
    /* Consider as success if this secfile doesn't have built the entries
     * hash table. */
    return TRUE;
  }

  entry_path(pentry, buf, sizeof(buf));
  return (pentry == hash_delete_entry(secfile->hash.entries, buf));
  /* What would happen if it wasn't the right entry? */
}

/**************************************************************************
  Base function to load a section file.  Note it closes the inputfile.
**************************************************************************/
static struct section_file *secfile_from_input_file(struct inputfile *inf,
                                                    const char *filename,
                                                    const char *section,
                                                    bool allow_duplicates)
{
  struct section_file *secfile;
  struct section *psection = NULL;
  bool table_state = FALSE;     /* TRUE when within tabular format. */
  int table_lineno = 0;         /* Row number in tabular, 0 top data row. */
  const char *tok;
  int i;
  struct astring base_name = ASTRING_INIT;    /* for table or single entry */
  struct astring entry_name = ASTRING_INIT;
  struct astring_vector columns;    /* astrings for column headings */
  bool found_my_section = FALSE;
  bool error = FALSE;

  if (!inf) {
    return NULL;
  }

  /* Assign the real value later, to speed up the creation of new entries. */
  secfile = secfile_new(TRUE);
  if (filename) {
    secfile->name = fc_strdup(filename);
  } else {
    secfile->name = NULL;
  }

  astring_vector_init(&columns);

  if (filename) {
    log_verbose("Reading registry from \"%s\"", filename);
  } else {
    log_verbose("Reading registry");
  }

  while (!inf_at_eof(inf)) {
    if (inf_token(inf, INF_TOK_EOL)) {
      continue;
    }
    if (inf_at_eof(inf)) {
      /* may only realise at eof after trying to read eol above */
      break;
    }
    tok = inf_token(inf, INF_TOK_SECTION_NAME);
    if (tok) {
      if (found_my_section) {
        /* This shortcut will stop any further loading after the requested
         * section has been loaded (i.e., at the start of a new section).
         * This is needed to make the behavior useful, since the whole
         * purpose is to short-cut further loading of the file.  However
         * normally a section may be split up, and that will no longer
         * work here because it will be short-cut. */
        SECFILE_LOG(secfile, psection, "%s",
                    inf_log_str(inf, "Found requested section; finishing"));
        goto END;
      }
      if (table_state) {
        SECFILE_LOG(secfile, psection, "%s",
                    inf_log_str(inf, "New section during table"));
        error = TRUE;
        goto END;
      }
      /* Check if we already have a section with this name.
         (Could ignore this and have a duplicate sections internally,
         but then secfile_get_secnames_prefix would return duplicates.)
         Duplicate section in input are likely to be useful for includes.
      */
      psection = secfile_section_by_name(secfile, tok);
      if (!psection) {
        if (!section || strcmp(tok, section) == 0) {
          psection = secfile_section_new(secfile, tok);
          if (section) {
            found_my_section = TRUE;
          }
        }
      }
      if (!inf_token(inf, INF_TOK_EOL)) {
        SECFILE_LOG(secfile, psection, "%s",
                    inf_log_str(inf, "Expected end of line"));
        error = TRUE;
        goto END;
      }
      continue;
    }
    if (inf_token(inf, INF_TOK_TABLE_END)) {
      if (!table_state) {
        SECFILE_LOG(secfile, psection, "%s",
                    inf_log_str(inf, "Misplaced \"}\""));
        error = TRUE;
        goto END;
      }
      if (!inf_token(inf, INF_TOK_EOL)) {
        SECFILE_LOG(secfile, psection, "%s",
                    inf_log_str(inf, "Expected end of line"));
        error = TRUE;
        goto END;
      }
      table_state = FALSE;
      continue;
    }
    if (table_state) {
      i = -1;
      do {
        int num_columns = astring_vector_size(&columns);

        i++;
        inf_discard_tokens(inf, INF_TOK_EOL);   /* allow newlines */
        if (!(tok = inf_token(inf, INF_TOK_VALUE))) {
          SECFILE_LOG(secfile, psection, "%s",
                      inf_log_str(inf, "Expected value"));
          error = TRUE;
          goto END;
        }

        if (i < num_columns) {
          astr_set(&entry_name, "%s%d.%s", astr_str(&base_name),
                   table_lineno, astr_str(&columns.p[i]));
        } else {
          astr_set(&entry_name, "%s%d.%s,%d", astr_str(&base_name),
                   table_lineno, astr_str(&columns.p[num_columns - 1]),
                   (int) (i - num_columns + 1));
        }
        entry_from_token(psection, astr_str(&entry_name), tok, inf);
      } while (inf_token(inf, INF_TOK_COMMA));

      if (!inf_token(inf, INF_TOK_EOL)) {
        SECFILE_LOG(secfile, psection, "%s",
                    inf_log_str(inf, "Expected end of line"));
        error = TRUE;
        goto END;
      }
      table_lineno++;
      continue;
    }

    if (!(tok = inf_token(inf, INF_TOK_ENTRY_NAME))) {
      SECFILE_LOG(secfile, psection, "%s",
                  inf_log_str(inf, "Expected entry name"));
      error = TRUE;
      goto END;
    }

    /* need to store tok before next calls: */
    astr_set(&base_name, "%s", tok);

    inf_discard_tokens(inf, INF_TOK_EOL);       /* allow newlines */

    if (inf_token(inf, INF_TOK_TABLE_START)) {
      i = -1;
      do {
        i++;
        inf_discard_tokens(inf, INF_TOK_EOL);  	/* allow newlines */
        if (!(tok = inf_token(inf, INF_TOK_VALUE))) {
          SECFILE_LOG(secfile, psection, "%s",
                      inf_log_str(inf, "Expected value"));
          error = TRUE;
          goto END;
        }
        if (tok[0] != '\"') {
          SECFILE_LOG(secfile, psection, "%s",
                      inf_log_str(inf, "Table column header non-string"));
          error = TRUE;
          goto END;
        }
        {       /* expand columns: */
          int j, n_prev;
          n_prev = astring_vector_size(&columns);
          for (j = i + 1; j < n_prev; j++) {
            astr_free(&columns.p[j]);
          }
          astring_vector_reserve(&columns, i + 1);
          for (j = n_prev; j < i + 1; j++) {
            astr_init(&columns.p[j]);
          }
        }
        astr_set(&columns.p[i], "%s", tok + 1);

      } while (inf_token(inf, INF_TOK_COMMA));

      if (!inf_token(inf, INF_TOK_EOL)) {
        SECFILE_LOG(secfile, psection, "%s",
                    inf_log_str(inf, "Expected end of line"));
        error = TRUE;
        goto END;
      }
      table_state = TRUE;
      table_lineno = 0;
      continue;
    }
    /* ordinary value: */
    i = -1;
    do {
      i++;
      inf_discard_tokens(inf, INF_TOK_EOL);     /* allow newlines */
      if (!(tok = inf_token(inf, INF_TOK_VALUE))) {
        SECFILE_LOG(secfile, psection, "%s",
                    inf_log_str(inf, "Expected value"));
        error = TRUE;
        goto END;
      }
      if (i == 0) {
        entry_from_token(psection, astr_str(&base_name), tok, inf);
      } else {
        astr_set(&entry_name, "%s,%d", astr_str(&base_name), i);
        entry_from_token(psection, astr_str(&entry_name), tok, inf);
      }
    } while (inf_token(inf, INF_TOK_COMMA));
    if (!inf_token(inf, INF_TOK_EOL)) {
      SECFILE_LOG(secfile, psection, "%s",
                  inf_log_str(inf, "Expected end of line"));
      error = TRUE;
      goto END;
    }
  }

  if (table_state) {
    SECFILE_LOG(secfile, psection,
                "Finished registry before end of table");
    error = TRUE;
  }

END:
  inf_close(inf);
  astr_free(&base_name);
  astr_free(&entry_name);
  for (i = 0; i < astring_vector_size(&columns); i++) {
    astr_free(&columns.p[i]);
  }
  astring_vector_free(&columns);

  if (!error) {
    /* Build the entry hash table. */
    secfile->allow_duplicates = allow_duplicates;
    secfile->hash.entries = hash_new_nentries_full(hash_fval_string,
                                                   hash_fcmp_string,
                                                   free, NULL,
                                                   secfile->num_entries);

    section_list_iterate(secfile->sections, psection) {
      entry_list_iterate(section_entries(psection), pentry) {
        if (!secfile_hash_insert(secfile, pentry)) {
          error = TRUE;
          break;
        }
      } entry_list_iterate_end;
      if (error) {
        break;
      }
    } section_list_iterate_end;
  }
  if (error) {
    secfile_destroy(secfile);
    return NULL;
  } else {
    return secfile;
  }
}

/**************************************************************************
  Create a section file from a file.  Returns NULL on error.
**************************************************************************/
struct section_file *secfile_load(const char *filename,
                                  bool allow_duplicates)
{
  return secfile_load_section(filename, NULL, allow_duplicates);
}

/**************************************************************************
  Create a section file from a file, read only one particular section.
  Returns NULL on error.
**************************************************************************/
struct section_file *secfile_load_section(const char *filename,
                                          const char *section,
                                          bool allow_duplicates)
{
  char real_filename[1024];

  interpret_tilde(real_filename, sizeof(real_filename), filename);
  return secfile_from_input_file(inf_from_file(real_filename, datafilename),
                                 filename, section, allow_duplicates);
}

/**************************************************************************
  Create a section file from a stream.  Returns NULL on error.
**************************************************************************/
struct section_file *secfile_from_stream(fz_FILE *stream,
                                         bool allow_duplicates)
{
  return secfile_from_input_file(inf_from_stream(stream, datafilename),
                                 NULL, NULL, allow_duplicates);
}

/**************************************************************************
  Save the previously filled in section_file to disk.

  There is now limited ability to save in the new tabular format
  (to give smaller savefiles).
  The start of a table is detected by an entry with name of the form:
    (alphabetical_component)(zero)(period)(alphanumeric_component)
  Eg: u0.id, or c0.id, in the freeciv savefile.
  The alphabetical component is taken as the "name" of the table,
  and the component after the period as the first column name.
  This should be followed by the other column values for u0,
  and then subsequent u1, u2, etc, in strict order with no omissions,
  and with all of the columns for all uN in the same order as for u0.

  If compression_level is non-zero, then compress using zlib.  (Should
  only supply non-zero compression_level if already know that HAVE_LIBZ.)
  Below simply specifies FZ_ZLIB method, since fz_fromFile() automatically
  changes to FZ_PLAIN method when level == 0.
**************************************************************************/
bool secfile_save(const struct section_file *secfile, const char *filename,
                  int compression_level, enum fz_method compression_method)
{
  char real_filename[1024];
  char pentry_name[128];
  const char *col_entry_name;
  fz_FILE *fs;
  const struct genlist_link *ent_iter, *save_iter, *col_iter;
  struct entry *pentry, *col_pentry;
  int i;

  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != secfile, FALSE);

  if (NULL == filename) {
    filename = secfile->name;
  }

  interpret_tilde(real_filename, sizeof(real_filename), filename);
  fs = fz_from_file(real_filename, "w",
                    compression_method, compression_level);

  if (!fs) {
    return FALSE;
  }

  section_list_iterate(secfile->sections, psection) {
    fz_fprintf(fs, "\n[%s]\n", section_name(psection));

    /* Following doesn't use entry_list_iterate() because we want to do
     * tricky things with the iterators...
     */
    for (ent_iter = genlist_head(entry_list_base(section_entries(psection)));
         ent_iter && (pentry = genlist_link_data(ent_iter));
         ent_iter = genlist_link_next(ent_iter)) {

      /* Tables: break out of this loop if this is a non-table
            * entry (pentry and ent_iter unchanged) or after table (pentry
       * and ent_iter suitably updated, pentry possibly NULL).
       * After each table, loop again in case the next entry
       * is another table.
       */
      for (;;) {
        char *c, *first, base[64];
        int offset, irow, icol, ncol;

        /* Example: for first table name of "xyz0.blah":
         *  first points to the original string pentry->name
         *  base contains "xyz";
         *  offset = 5 (so first+offset gives "blah")
         *  note strlen(base) = offset - 2
         */

        if (!SAVE_TABLES) {
          break;
        }

        sz_strlcpy(pentry_name, entry_name(pentry));
        c = first = pentry_name;
        if (*c == '\0' || !fc_isalpha(*c)) {
          break;
        }
        for (; *c != '\0' && fc_isalpha(*c); c++) {
          /* nothing */
        }
        if (0 != strncmp(c, "0.", 2)) {
          break;
        }
        c += 2;
        if (*c == '\0' || !fc_isalnum(*c)) {
          break;
        }

        offset = c - first;
        first[offset - 2] = '\0';
        sz_strlcpy(base, first);
        first[offset - 2] = '0';
        fz_fprintf(fs, "%s={", base);

        /* Save an iterator at this first entry, which we can later use
         * to repeatedly iterate over column names:
         */
        save_iter = ent_iter;

        /* write the column names, and calculate ncol: */
        ncol = 0;
        col_iter = save_iter;
        for(; (col_pentry = genlist_link_data(col_iter));
            col_iter = genlist_link_next(col_iter)) {
          col_entry_name = entry_name(col_pentry);
          if (strncmp(col_entry_name, first, offset) != 0) {
            break;
          }
          fz_fprintf(fs, "%s\"%s\"", (ncol == 0 ? "" : ","),
                     col_entry_name + offset);
          ncol++;
        }
        fz_fprintf(fs, "\n");

        /* Iterate over rows and columns, incrementing ent_iter as we go,
         * and writing values to the table.  Have a separate iterator
         * to the column names to check they all match.
         */
        irow = icol = 0;
        col_iter = save_iter;
        for (;;) {
          char expect[128];     /* pentry->name we're expecting */

          pentry = genlist_link_data(ent_iter);
          col_pentry = genlist_link_data(col_iter);

          fc_snprintf(expect, sizeof(expect), "%s%d.%s",
                      base, irow, entry_name(col_pentry) + offset);

          /* break out of tabular if doesn't match: */
          if ((!pentry) || (strcmp(entry_name(pentry), expect) != 0)) {
            if (icol != 0) {
              /* If the second or later row of a table is missing some
               * entries that the first row had, we drop out of the tabular
               * format.  This is inefficient so we print a warning message;
               * the calling code probably needs to be fixed so that it can
               * use the more efficient tabular format.
               *
               * FIXME: If the first row is missing some entries that the
               * second or later row has, then we'll drop out of tabular
               * format without an error message. */
              log_error("In file %s, there is no entry in the registry for\n"
                        "%s.%s (or the entries are out of order). This means\n"
                        "a less efficient non-tabular format will be used.\n"
                        "To avoid this make sure all rows of a table are\n"
                        "filled out with an entry for every column.",
                        real_filename, section_name(psection), expect);
              /* TRANS: No full stop after the URL, could cause confusion. */
              log_error(_("Please report this message at %s"), BUG_URL);
              fz_fprintf(fs, "\n");
            }
            fz_fprintf(fs, "}\n");
            break;
          }

          if (icol > 0) {
            fz_fprintf(fs, ",");
          }
          entry_to_file(pentry, fs);

          ent_iter = genlist_link_next(ent_iter);
          col_iter = genlist_link_next(col_iter);

          icol++;
          if (icol == ncol) {
            fz_fprintf(fs, "\n");
            irow++;
            icol = 0;
            col_iter = save_iter;
          }
        }
        if (!pentry) {
          break;
        }
      }
      if (!pentry) {
        break;
      }

      /* Classic entry. */
      col_entry_name = entry_name(pentry);
      fz_fprintf(fs, "%s=", col_entry_name);
      entry_to_file(pentry, fs);

      /* Check for vector. */
      for (i = 1;; i++) {
        col_iter = genlist_link_next(ent_iter);
        col_pentry = genlist_link_data(col_iter);
        if (NULL == col_pentry) {
          break;
        }
        fc_snprintf(pentry_name, sizeof(pentry_name),
                    "%s,%d", col_entry_name, i);
        if (0 != strcmp(pentry_name, entry_name(col_pentry))) {
          break;
        }
        fz_fprintf(fs, ",");
        entry_to_file(col_pentry, fs);
        ent_iter = col_iter;
      }

      if (entry_comment(pentry)) {
        fz_fprintf(fs, "#%s\n", entry_comment(pentry));
      } else {
        fz_fprintf(fs, "\n");
      }
    }
  } section_list_iterate_end;
  
  if (0 != fz_ferror(fs)) {
    SECFILE_LOG(secfile, NULL, "Error before closing %s: %s", 
                real_filename, fz_strerror(fs));
    fz_fclose(fs);
    return FALSE;
  }
  if (0 != fz_fclose(fs)) {
    SECFILE_LOG(secfile, NULL, "Error closing %s", real_filename);
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
  Print log messages for any entries in the file which have
  not been looked up -- ie, unused or unrecognised entries.
  To mark an entry as used without actually doing anything with it,
  you could do something like:
     section_file_lookup(&file, "foo.bar");  / * unused * /
**************************************************************************/
void secfile_check_unused(const struct section_file *secfile)
{
  bool any = FALSE;

  section_list_iterate(secfile_sections(secfile), psection) {
    entry_list_iterate(section_entries(psection), pentry) {
      if (!entry_used(pentry)) {
        if (!any && secfile->name) {
          log_verbose("Unused entries in file %s:", secfile->name);
          any = TRUE;
        }
        log_verbose("  unused entry: %s.%s",
                    section_name(psection), entry_name(pentry));
      }
    } entry_list_iterate_end;
  } section_list_iterate_end;
}

/**************************************************************************
  Return the filename the section file was loaded as, or "(anonymous)"
  if this sectionfile was created rather than loaded from file.
  The memory is managed internally, and should not be altered,
  nor used after secfile_destroy() called for the section file.
**************************************************************************/
const char *secfile_name(const struct section_file *secfile)
{
  if (NULL == secfile) {
    return "NULL";
  } else if (secfile->name) {
    return secfile->name;
  } else {
    return "(anonymous)";
  }
}

/**************************************************************************
  Seperates the section and entry names.  Create the section if missing.
**************************************************************************/
static struct section *secfile_insert_base(struct section_file *secfile,
                                           const char *path,
                                           const char **pent_name)
{
  char fullpath[MAX_LEN_BUFFER];
  char *ent_name;
  struct section *psection;

  sz_strlcpy(fullpath, path);

  ent_name = strchr(fullpath, '.');
  if (!ent_name) {
    SECFILE_LOG(secfile, NULL,
                "Section and entry names must be separated by a dot.");
    return NULL;
  }

  /* Separates section and entry names. */
  *ent_name = '\0';
  *pent_name = path + (ent_name - fullpath) + 1;
  psection = secfile_section_by_name(secfile, fullpath);
  if (psection) {
    return psection;
  } else {
    return secfile_section_new(secfile, fullpath);
  }
}

/**************************************************************************
  Insert a boolean entry.
**************************************************************************/
struct entry *secfile_insert_bool_full(struct section_file *secfile,
                                       bool value, const char *comment,
                                       bool allow_replace,
                                       const char *path, ...)
{
  char fullpath[MAX_LEN_BUFFER];
  const char *ent_name;
  struct section *psection;
  struct entry *pentry = NULL;
  va_list args;

  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != secfile, NULL);

  va_start(args, path);
  fc_vsnprintf(fullpath, sizeof(fullpath), path, args);
  va_end(args);

  psection = secfile_insert_base(secfile, fullpath, &ent_name);
  if (!psection) {
    return NULL;
  }

  if (allow_replace) {
    pentry = section_entry_by_name(psection, ent_name);
    if (NULL != pentry) {
      if (ENTRY_BOOL == entry_type(pentry)) {
        if (!entry_bool_set(pentry, value)) {
          return NULL;
        }
      } else {
        entry_destroy(pentry);
        pentry = NULL;
      }
    }
  }

  if (NULL == pentry) {
    pentry = section_entry_bool_new(psection, ent_name, value);
  }

  if (NULL != pentry && NULL != comment) {
    entry_set_comment(pentry, comment);
  }

  return pentry;
}

/**************************************************************************
  Insert 'dim' boolean entries at 'path,0', 'path,1' etc.  Returns 
  the number of entries inserted or replaced.
**************************************************************************/
size_t secfile_insert_bool_vec_full(struct section_file *secfile,
                                    const bool *values, size_t dim,
                                    const char *comment, bool allow_replace,
                                    const char *path, ...)
{
  char fullpath[MAX_LEN_BUFFER];
  size_t i, ret = 0;
  va_list args;

  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != secfile, 0);

  va_start(args, path);
  fc_vsnprintf(fullpath, sizeof(fullpath), path, args);
  va_end(args);

  /* NB: 'path,0' is actually 'path'.  See comment in the head
   * of the file. */
  if (dim > 0
      && NULL != secfile_insert_bool_full(secfile, values[0], comment,
                                          allow_replace, "%s", fullpath)) {
    ret++;
  }
  for (i = 1; i < dim; i++) {
    if (NULL != secfile_insert_bool_full(secfile, values[i], comment,
                                         allow_replace, "%s,%d",
                                         fullpath, (int) i)) {
      ret++;
    }
  }

  return ret;
}

/**************************************************************************
  Insert a integer entry.
**************************************************************************/
struct entry *secfile_insert_int_full(struct section_file *secfile,
                                      int value, const char *comment,
                                      bool allow_replace,
                                      const char *path, ...)
{
  char fullpath[MAX_LEN_BUFFER];
  const char *ent_name;
  struct section *psection;
  struct entry *pentry = NULL;
  va_list args;

  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != secfile, NULL);

  va_start(args, path);
  fc_vsnprintf(fullpath, sizeof(fullpath), path, args);
  va_end(args);

  psection = secfile_insert_base(secfile, fullpath, &ent_name);
  if (!psection) {
    return NULL;
  }

  if (allow_replace) {
    pentry = section_entry_by_name(psection, ent_name);
    if (NULL != pentry) {
      if (ENTRY_INT == entry_type(pentry)) {
        if (!entry_int_set(pentry, value)) {
          return NULL;
        }
      } else {
        entry_destroy(pentry);
        pentry = NULL;
      }
    }
  }

  if (NULL == pentry) {
    pentry = section_entry_int_new(psection, ent_name, value);
  }

  if (NULL != pentry && NULL != comment) {
    entry_set_comment(pentry, comment);
  }

  return pentry;
}

/**************************************************************************
  Insert 'dim' integer entries at 'path,0', 'path,1' etc.  Returns 
  the number of entries inserted or replaced.
**************************************************************************/
size_t secfile_insert_int_vec_full(struct section_file *secfile,
                                   const int *values, size_t dim,
                                   const char *comment, bool allow_replace,
                                   const char *path, ...)
{
  char fullpath[MAX_LEN_BUFFER];
  size_t i, ret = 0;
  va_list args;

  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != secfile, 0);

  va_start(args, path);
  fc_vsnprintf(fullpath, sizeof(fullpath), path, args);
  va_end(args);

  /* NB: 'path,0' is actually 'path'.  See comment in the head
   * of the file. */
  if (dim > 0
      && NULL != secfile_insert_int_full(secfile, values[0], comment,
                                         allow_replace, "%s", fullpath)) {
    ret++;
  }
  for (i = 1; i < dim; i++) {
    if (NULL != secfile_insert_int_full(secfile, values[i], comment,
                                        allow_replace, "%s,%d",
                                        fullpath, (int) i)) {
      ret++;
    }
  }

  return ret;
}

/**************************************************************************
  Insert a string entry.
**************************************************************************/
struct entry *secfile_insert_str_full(struct section_file *secfile,
                                      const char *string,
                                      const char *comment,
                                      bool allow_replace,
                                      bool no_escape, const char *path, ...)
{
  char fullpath[MAX_LEN_BUFFER];
  const char *ent_name;
  struct section *psection;
  struct entry *pentry = NULL;
  va_list args;

  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != secfile, NULL);

  va_start(args, path);
  fc_vsnprintf(fullpath, sizeof(fullpath), path, args);
  va_end(args);

  psection = secfile_insert_base(secfile, fullpath, &ent_name);
  if (!psection) {
    return NULL;
  }

  if (allow_replace) {
    pentry = section_entry_by_name(psection, ent_name);
    if (NULL != pentry) {
      if (ENTRY_STR == entry_type(pentry)) {
        if (!entry_str_set(pentry, string)) {
          return NULL;
        }
      } else {
        entry_destroy(pentry);
        pentry = NULL;
      }
    }
  }

  if (NULL == pentry) {
    pentry = section_entry_str_new(psection, ent_name, string, !no_escape);
  }

  if (NULL != pentry && NULL != comment) {
    entry_set_comment(pentry, comment);
  }

  return pentry;
}

/**************************************************************************
  Insert 'dim' string entries at 'path,0', 'path,1' etc.  Returns 
  the number of entries inserted or replaced.
**************************************************************************/
size_t secfile_insert_str_vec_full(struct section_file *secfile,
                                   const char *const *strings, size_t dim,
                                   const char *comment, bool allow_replace,
                                   bool no_escape, const char *path, ...)
{
  char fullpath[MAX_LEN_BUFFER];
  size_t i, ret = 0;
  va_list args;

  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != secfile, 0);

  va_start(args, path);
  fc_vsnprintf(fullpath, sizeof(fullpath), path, args);
  va_end(args);

  /* NB: 'path,0' is actually 'path'.  See comment in the head
   * of the file. */
  if (dim > 0
      && NULL != secfile_insert_str_full(secfile, strings[0], comment,
                                         allow_replace, no_escape,
                                         "%s", fullpath)) {
    ret++;
  }
  for (i = 1; i < dim; i++) {
    if (NULL != secfile_insert_str_full(secfile, strings[i], comment,
                                        allow_replace, no_escape,
                                        "%s,%d", fullpath, (int) i)) {
      ret++;
    }
  }

  return ret;
}

/**************************************************************************
  Returns the entry by the name or NULL if not matched.
**************************************************************************/
struct entry *secfile_entry_by_path(const struct section_file *secfile,
                                    const char *entry_path)
{
  char fullpath[MAX_LEN_BUFFER];
  char *ent_name;
  size_t len;
  struct section *psection;

  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != secfile, NULL);

  sz_strlcpy(fullpath, entry_path);

  /* treat "sec.foo,0" as "sec.foo": */
  len = strlen(fullpath);
  if (len > 2 && fullpath[len - 2] == ',' && fullpath[len - 1] == '0') {
    fullpath[len - 2] = '\0';
  }

  if (NULL != secfile->hash.entries) {
    struct entry *pentry = hash_lookup_data(secfile->hash.entries, fullpath);

    if (NULL != pentry) {
      entry_use(pentry);
    }
    return pentry;
  }

  /* I dont like strtok.
   * - Me neither! */
  ent_name = strchr(fullpath, '.');
  if (!ent_name) {
    return NULL;
  }

  /* Separates section and entry names. */
  *ent_name++ = '\0';
  psection = secfile_section_by_name(secfile, fullpath);
  if (psection) {
    return section_entry_by_name(psection, ent_name);
  } else {
    return NULL;
  }
}

/**************************************************************************
  Returns the entry at "fullpath" or NULL if not matched.
**************************************************************************/
struct entry *secfile_entry_lookup(const struct section_file *secfile,
                                   const char *path, ...)
{
  char fullpath[MAX_LEN_BUFFER];
  va_list args;

  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != secfile, NULL);

  va_start(args, path);
  fc_vsnprintf(fullpath, sizeof(fullpath), path, args);
  va_end(args);

  return secfile_entry_by_path(secfile, fullpath);
}

/**************************************************************************
  Lookup a boolean value in the secfile.  Returns TRUE on success.
**************************************************************************/
bool secfile_lookup_bool(const struct section_file *secfile, bool *bval,
                         const char *path, ...)
{
  char fullpath[MAX_LEN_BUFFER];
  const struct entry *pentry;
  va_list args;

  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != secfile, FALSE);

  va_start(args, path);
  fc_vsnprintf(fullpath, sizeof(fullpath), path, args);
  va_end(args);

  if (!(pentry = secfile_entry_by_path(secfile, fullpath))) {
    SECFILE_LOG(secfile, NULL, "\"%s\" entry doesn't exist.", fullpath);
    return FALSE;
  }

  return entry_bool_get(pentry, bval);
}

/**************************************************************************
  Lookup a boolean value in the secfile.  On failure, use the default
  value.
**************************************************************************/
bool secfile_lookup_bool_default(const struct section_file *secfile,
                                 bool def, const char *path, ...)
{
  char fullpath[MAX_LEN_BUFFER];
  const struct entry *pentry;
  bool bval;
  va_list args;

  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != secfile, def);

  va_start(args, path);
  fc_vsnprintf(fullpath, sizeof(fullpath), path, args);
  va_end(args);

  if (!(pentry = secfile_entry_by_path(secfile, fullpath))) {
    return def;
  }

  if (entry_bool_get(pentry, &bval)) {
    return bval;
  }

  return def;
}

/**************************************************************************
  Lookup a boolean vector in the secfile.  Returns NULL on error.  This
  vector is not owned by the registry module, and should be free by the
  user.
**************************************************************************/
bool *secfile_lookup_bool_vec(const struct section_file *secfile,
                              size_t *dim, const char *path, ...)
{
  char fullpath[MAX_LEN_BUFFER];
  size_t i = 0;
  bool *vec;
  va_list args;

  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != secfile, NULL);
  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != dim, NULL);

  va_start(args, path);
  fc_vsnprintf(fullpath, sizeof(fullpath), path, args);
  va_end(args);

  /* Check size. */
  while (NULL != secfile_entry_lookup(secfile, "%s,%d", fullpath, (int) i)) {
    i++;
  }
  *dim = i;

  if (0 == i) {
    /* Doesn't exist. */
    SECFILE_LOG(secfile, NULL, "\"%s\" entry doesn't exist.", fullpath);
    return NULL;
  }

  vec = fc_malloc(i * sizeof(bool));
  for(i = 0; i < *dim; i++) {
    if (!secfile_lookup_bool(secfile, vec + i, "%s,%d", fullpath, (int) i)) {
      SECFILE_LOG(secfile, NULL,
                  "An error occured when looking up to \"%s,%d\" entry.",
                  fullpath, (int) i);
      free(vec);
      *dim = 0;
      return NULL;
    }
  }

  return vec;
}

/**************************************************************************
  Lookup a integer value in the secfile.  Returns TRUE on success.
**************************************************************************/
bool secfile_lookup_int(const struct section_file *secfile, int *ival,
                        const char *path, ...)
{
  char fullpath[MAX_LEN_BUFFER];
  const struct entry *pentry;
  va_list args;

  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != secfile, FALSE);

  va_start(args, path);
  fc_vsnprintf(fullpath, sizeof(fullpath), path, args);
  va_end(args);

  if (!(pentry = secfile_entry_by_path(secfile, fullpath))) {
    SECFILE_LOG(secfile, NULL, "\"%s\" entry doesn't exist.", fullpath);
    return FALSE;
  }

  return entry_int_get(pentry, ival);
}

/**************************************************************************
  Lookup a integer value in the secfile.  On failure, use the default
  value.
**************************************************************************/
int secfile_lookup_int_default(const struct section_file *secfile, int def,
                               const char *path, ...)
{
  char fullpath[MAX_LEN_BUFFER];
  const struct entry *pentry;
  int ival;
  va_list args;

  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != secfile, def);

  va_start(args, path);
  fc_vsnprintf(fullpath, sizeof(fullpath), path, args);
  va_end(args);

  if (!(pentry = secfile_entry_by_path(secfile, fullpath))) {
    return def;
  }

  if (entry_int_get(pentry, &ival)) {
    return ival;
  }

  return def;
}

/**************************************************************************
  Lookup a integer value in the secfile.  The value will be arranged to
  match the interval [minval, maxval].  On failure, use the default
  value.
**************************************************************************/
int secfile_lookup_int_def_min_max(const struct section_file *secfile,
                                   int defval, int minval, int maxval,
                                   const char *path, ...)
{
  char fullpath[MAX_LEN_BUFFER];
  const struct entry *pentry;
  int value;
  va_list args;

  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != secfile, defval);

  va_start(args, path);
  fc_vsnprintf(fullpath, sizeof(fullpath), path, args);
  va_end(args);

  if (!(pentry = secfile_entry_by_path(secfile, fullpath))) {
    return defval;
  }

  if (!entry_int_get(pentry, &value)) {
    return defval;
  }

  if (value < minval) {
    SECFILE_LOG(secfile, entry_section(pentry),
                "\"%s\" should be in the interval [%d, %d] but is %d;"
                "using the minimal value.",
                fullpath, minval, maxval, value);
    value = minval;
  }

  if (value > maxval) {
    SECFILE_LOG(secfile, entry_section(pentry),
                "\"%s\" should be in the interval [%d, %d] but is %d;"
                "using the maximal value.",
                fullpath, minval, maxval, value);
    value = maxval;
  }

  return value;
}

/**************************************************************************
  Lookup a integer vector in the secfile.  Returns NULL on error.  This
  vector is not owned by the registry module, and should be free by the
  user.
**************************************************************************/
int *secfile_lookup_int_vec(const struct section_file *secfile,
                            size_t *dim, const char *path, ...)
{
  char fullpath[MAX_LEN_BUFFER];
  size_t i = 0;
  int *vec;
  va_list args;

  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != secfile, NULL);
  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != dim, NULL);

  va_start(args, path);
  fc_vsnprintf(fullpath, sizeof(fullpath), path, args);
  va_end(args);

  /* Check size. */
  while (NULL != secfile_entry_lookup(secfile, "%s,%d", fullpath, (int) i)) {
    i++;
  }
  *dim = i;

  if (0 == i) {
    /* Doesn't exist. */
    SECFILE_LOG(secfile, NULL, "\"%s\" entry doesn't exist.", fullpath);
    return NULL;
  }

  vec = fc_malloc(i * sizeof(int));
  for(i = 0; i < *dim; i++) {
    if (!secfile_lookup_int(secfile, vec + i, "%s,%d", fullpath, (int) i)) {
      SECFILE_LOG(secfile, NULL,
                  "An error occured when looking up to \"%s,%d\" entry.",
                  fullpath, (int) i);
      free(vec);
      *dim = 0;
      return NULL;
    }
  }

  return vec;
}

/**************************************************************************
  Lookup a string value in the secfile.  Returns NULL on error.
**************************************************************************/
const char *secfile_lookup_str(const struct section_file *secfile,
                               const char *path, ...)
{
  char fullpath[MAX_LEN_BUFFER];
  const struct entry *pentry;
  const char *str;
  va_list args;

  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != secfile, NULL);

  va_start(args, path);
  fc_vsnprintf(fullpath, sizeof(fullpath), path, args);
  va_end(args);

  if (!(pentry = secfile_entry_by_path(secfile, fullpath))) {
    SECFILE_LOG(secfile, NULL, "\"%s\" entry doesn't exist.", fullpath);
    return NULL;
  }

  if (entry_str_get(pentry, &str)) {
    return str;
  }

  return NULL;
}

/**************************************************************************
  Lookup a string value in the secfile.  On failure, use the default
  value.
**************************************************************************/
const char *secfile_lookup_str_default(const struct section_file *secfile,
                                       const char *def,
                                       const char *path, ...)
{
  char fullpath[MAX_LEN_BUFFER];
  const struct entry *pentry;
  const char *str;
  va_list args;

  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != secfile, def);

  va_start(args, path);
  fc_vsnprintf(fullpath, sizeof(fullpath), path, args);
  va_end(args);

  if (!(pentry = secfile_entry_by_path(secfile, fullpath))) {
    return def;
  }

  if (entry_str_get(pentry, &str)) {
    return str;
  }

  return def;
}

/**************************************************************************
  Lookup a string vector in the secfile.  Returns NULL on error.  This
  vector is not owned by the registry module, and should be free by the
  user, but the string pointers stored inside the vector shouldn't be
  free.
**************************************************************************/
const char **secfile_lookup_str_vec(const struct section_file *secfile,
                                    size_t *dim, const char *path, ...)
{
  char fullpath[MAX_LEN_BUFFER];
  size_t i = 0;
  const char **vec;
  va_list args;

  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != secfile, NULL);
  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != dim, NULL);

  va_start(args, path);
  fc_vsnprintf(fullpath, sizeof(fullpath), path, args);
  va_end(args);

  /* Check size. */
  while (NULL != secfile_entry_lookup(secfile, "%s,%d", fullpath, (int) i)) {
    i++;
  }
  *dim = i;

  if (0 == i) {
    /* Doesn't exist. */
    SECFILE_LOG(secfile, NULL, "\"%s\" entry doesn't exist.", fullpath);
    return NULL;
  }

  vec = fc_malloc(i * sizeof(const char *));
  for(i = 0; i < *dim; i++) {
    if (!(vec[i] = secfile_lookup_str(secfile, "%s,%d",
                                      fullpath, (int) i))) {
      SECFILE_LOG(secfile, NULL,
                  "An error occured when looking up to \"%s,%d\" entry.",
                  fullpath, (int) i);
      free(vec);
      *dim = 0;
      return NULL;
    }
  }

  return vec;
}

/**************************************************************************
  Returns the first section matching the name.
**************************************************************************/
struct section *secfile_section_by_name(const struct section_file *secfile,
                                        const char *name)
{
  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != secfile, NULL);

  section_list_iterate(secfile->sections, psection) {
    if (0 == strcmp(section_name(psection), name)) {
      return psection;
    }
  } section_list_iterate_end;

  return NULL;
}

/**************************************************************************
  Find a section by path.
**************************************************************************/
struct section *secfile_section_lookup(const struct section_file *secfile,
                                       const char *path, ...)
{
  char fullpath[MAX_LEN_BUFFER];
  va_list args;

  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != secfile, NULL);

  va_start(args, path);
  fc_vsnprintf(fullpath, sizeof(fullpath), path, args);
  va_end(args);

  return secfile_section_by_name(secfile, fullpath);
}

/**************************************************************************
  Returns the list of sections.  This list is owned by the registry module
  and shouldn't be modified and destroyed.
**************************************************************************/
const struct section_list *
secfile_sections(const struct section_file *secfile)
{
  return (NULL != secfile ? secfile->sections : NULL);
}

/**************************************************************************
  Returns the list of sections which match the name prefix.  Returns NULL
  if no section was found.  This list is not owned by the registry module
  and the user must destroy it when he finished to work with it.
**************************************************************************/
struct section_list *
secfile_sections_by_name_prefix(const struct section_file *secfile,
                                const char *prefix)
{
  struct section_list *matches = NULL;
  size_t len;

  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != secfile, NULL);
  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != prefix, NULL);

  len = strlen(prefix);
  if (0 == len) {
    return NULL;
  }

  section_list_iterate(secfile->sections, psection) {
    if (0 == strncmp(section_name(psection), prefix, len)) {
      if (NULL == matches) {
        matches = section_list_new();
      }
      section_list_append(matches, psection);
    }
  } section_list_iterate_end;

  return matches;
}


/* Section structure. */
struct section {
  struct section_file *secfile; /* Parent structure. */
  char *name;                   /* Name of the section. */
  struct entry_list *entries;   /* The list of the children. */
};

/**************************************************************************
  Create a new section in the secfile.
**************************************************************************/
struct section *secfile_section_new(struct section_file *secfile,
                                    const char *name)
{
  struct section *psection;

  SECFILE_RETURN_VAL_IF_FAIL(secfile, NULL, NULL != secfile, NULL);

  if (NULL == name || '\0' == name[0]) {
    SECFILE_LOG(secfile, NULL, "Cannot create a section without name.");
    return NULL;
  }

  if (!check_name(name)) {
    SECFILE_LOG(secfile, NULL, "\"%s\" is not a valid section name.",
                name);
    return NULL;
  }

  if (NULL != secfile_section_by_name(secfile, name)) {
    /* We cannot duplicate sections in any case! */
    SECFILE_LOG(secfile, NULL, "Section \"%s\" already exists.", name);
    return NULL;
  }

  psection = fc_malloc(sizeof(struct section));
  psection->name = fc_strdup(name);
  psection->entries = entry_list_new_full(entry_destroy);

  /* Append to secfile. */
  psection->secfile = secfile;
  section_list_append(secfile->sections, psection);
  if (NULL != secfile->hash.sections) {
    hash_insert(secfile->hash.sections, psection->name, psection);
  }

  return psection;
}

/**************************************************************************
  Remove this section from the secfile.
**************************************************************************/
void section_destroy(struct section *psection)
{
  struct section_file *secfile;

  SECFILE_RETURN_IF_FAIL(NULL, psection, NULL != psection);

  section_clear_all(psection);

  if ((secfile = psection->secfile)) {
    /* Detach from secfile. */
    if (section_list_remove(secfile->sections, psection)) {
      /* This has called section_destroy() already then. */
      return;
    }
    if (NULL != secfile->hash.sections) {
      hash_delete_entry(secfile->hash.sections, psection->name);
    }
  }

  entry_list_destroy(psection->entries);
  free(psection->name);
  free(psection);
}

/**************************************************************************
  Remove all entries.
**************************************************************************/
void section_clear_all(struct section *psection)
{
  SECFILE_RETURN_IF_FAIL(NULL, psection, NULL != psection);

  /* This include the removing of the hash datas. */
  entry_list_clear(psection->entries);

  if (0 < entry_list_size(psection->entries)) {
    SECFILE_LOG(psection->secfile, psection,
                "After clearing all, %d entries are still remaining.",
                entry_list_size(psection->entries));
  }
}

/**************************************************************************
  Returns the section name.
**************************************************************************/
const char *section_name(const struct section *psection)
{
  return (NULL != psection ? psection->name : NULL);
}

/**************************************************************************
  Change the section name.  Returns TRUE on success.
**************************************************************************/
bool section_set_name(struct section *psection, const char *name)
{
  struct section_file *secfile;
  struct section *pother;

  SECFILE_RETURN_VAL_IF_FAIL(NULL, psection, NULL != psection, FALSE);
  secfile = psection->secfile;
  SECFILE_RETURN_VAL_IF_FAIL(secfile, psection, NULL != secfile, FALSE);

  if (NULL == name || '\0' == name[0]) {
    SECFILE_LOG(secfile, psection, "No new name for section \"%s\".",
                psection->name);
    return FALSE;
  }

  if (!check_name(name)) {
    SECFILE_LOG(secfile, psection,
                "\"%s\" is not a valid section name for section \"%s\".",
                name, psection->name);
    return FALSE;
  }

  if ((pother = secfile_section_by_name(secfile, name))
      && pother != psection) {
    /* We cannot duplicate sections in any case! */
    SECFILE_LOG(secfile, psection, "Section \"%s\" already exists.", name);
    return FALSE;
  }

  /* Remove old references in the hash tables. */
  if (NULL != secfile->hash.sections) {
    hash_delete_entry(secfile->hash.sections, psection->name);
  }
  if (NULL != secfile->hash.entries) {
    entry_list_iterate(psection->entries, pentry) {
      secfile_hash_delete(secfile, pentry);
    } entry_list_iterate_end;
  }

  /* Really rename. */
  free(psection->name);
  psection->name = fc_strdup(name);

  /* Reinsert new references into the hash tables. */
  if (NULL != secfile->hash.sections) {
    hash_insert(secfile->hash.sections, psection->name, psection);
  }
  if (NULL != secfile->hash.entries) {
    entry_list_iterate(psection->entries, pentry) {
      secfile_hash_insert(secfile, pentry);
    } entry_list_iterate_end;
  }

  return TRUE;
}

/**************************************************************************
  Returns a list containing all the entries.  This list is owned by the
  secfile, so don't modify or destroy it.
**************************************************************************/
const struct entry_list *section_entries(const struct section *psection)
{
  return (NULL != psection ? psection->entries : NULL);
}

/**************************************************************************
  Returns the the first entry matching the name.
**************************************************************************/
struct entry *section_entry_by_name(const struct section *psection,
                                    const char *name)
{
  SECFILE_RETURN_VAL_IF_FAIL(NULL, psection, NULL != psection, NULL);

  entry_list_iterate(psection->entries, pentry) {
    if (0 == strcmp(entry_name(pentry), name)) {
      entry_use(pentry);
      return pentry;
    }
  } entry_list_iterate_end;

  return NULL;
}

/**************************************************************************
  Returns the entry matching the path.
**************************************************************************/
struct entry *section_entry_lookup(const struct section *psection,
                                   const char *path, ...)
{
  char fullpath[MAX_LEN_BUFFER];
  struct entry *pentry;
  va_list args;

  SECFILE_RETURN_VAL_IF_FAIL(NULL, psection, NULL != psection, NULL);

  va_start(args, path);
  fc_vsnprintf(fullpath, sizeof(fullpath), path, args);
  va_end(args);

  if ((pentry = section_entry_by_name(psection, fullpath))) {
    return pentry;
  }

  /* Try with full path. */
  if ((pentry = secfile_entry_by_path(psection->secfile, fullpath))
      && psection == entry_section(pentry)) {
    /* Unsure this is really owned by this section. */
    return pentry;
  }

  return NULL;
}


/* An 'entry' is a string, integer, boolean or string vector;
 * See enum entry_type in registry.h.
 */
struct entry {
  struct section *psection;     /* Parent section. */
  char *name;                   /* Name, not including section prefix. */
  enum entry_type type;         /* The type of the entry. */
  int used;                     /* Number of times entry looked up. */
  char *comment;                /* Comment, may be NULL. */

  union {
    /* ENTRY_BOOL */
    struct {
      bool value;
    } boolean;
    /* ENTRY_INT */
    struct {
      int value;
    } integer;
    /* ENTRY_STR */
    struct {
      char *value;              /* Malloced string. */
      bool escaped;             /* " or $. Usually TRUE */
    } string;
  };
};

/**************************************************************************
  Returns a new entry.
**************************************************************************/
static struct entry *entry_new(struct section *psection, const char *name)
{
  struct section_file *secfile;
  struct entry *pentry;

  SECFILE_RETURN_VAL_IF_FAIL(NULL, psection, NULL != psection, NULL);

  secfile = psection->secfile;
  if (NULL == name || '\0' == name[0]) {
    SECFILE_LOG(secfile, psection, "Cannot create an entry without name.");
    return NULL;
  }

  if (!check_name(name)) {
    SECFILE_LOG(secfile, psection, "\"%s\" is not a valid entry name.",
                name);
    return NULL;
  }

  if (!secfile->allow_duplicates
      && NULL != section_entry_by_name(psection, name)) {
    SECFILE_LOG(secfile, psection, "Entry \"%s\" already exists.", name);
    return NULL;
  }

  pentry = fc_malloc(sizeof(struct entry));
  pentry->name = fc_strdup(name);
  pentry->type = -1;    /* Invalid case. */
  pentry->used = 0;
  pentry->comment = NULL;

  /* Append to section. */
  pentry->psection = psection;
  entry_list_append(psection->entries, pentry);

  /* Notify secfile. */
  secfile->num_entries++;
  secfile_hash_insert(secfile, pentry);

  return pentry;
}

/**************************************************************************
  Returns a new entry of type ENTRY_INT.
**************************************************************************/
struct entry *section_entry_int_new(struct section *psection,
                                    const char *name, int value)
{
  struct entry *pentry = entry_new(psection, name);

  if (NULL != pentry) {
    pentry->type = ENTRY_INT;
    pentry->integer.value = value;
  }

  return pentry;
}

/**************************************************************************
  Returns a new entry of type ENTRY_BOOL.
**************************************************************************/
struct entry *section_entry_bool_new(struct section *psection,
                                     const char *name, bool value)
{
  struct entry *pentry = entry_new(psection, name);

  if (NULL != pentry) {
    pentry->type = ENTRY_BOOL;
    pentry->boolean.value = value;
  }

  return pentry;
}

/**************************************************************************
  Returns a new entry of type ENTRY_STR.
**************************************************************************/
struct entry *section_entry_str_new(struct section *psection,
                                    const char *name, const char *value,
                                    bool escaped)
{
  struct entry *pentry = entry_new(psection, name);

  if (NULL != pentry) {
    pentry->type = ENTRY_STR;
    pentry->string.value = fc_strdup(NULL != value ? value : "");
    pentry->string.escaped = escaped;
  }

  return pentry;
}

/**************************************************************************
  Entry structure destructor.
**************************************************************************/
void entry_destroy(struct entry *pentry)
{
  struct section_file *secfile;
  struct section *psection;

  if (NULL == pentry) {
    return;
  }

  if ((psection = pentry->psection)) {
    /* Detach from section. */
    if (entry_list_remove(psection->entries, pentry)) {
      /* This has called entry_destroy() already then. */
      return;
    }
    if ((secfile = psection->secfile)) {
      /* Detach from secfile. */
      secfile->num_entries--;
      secfile_hash_delete(secfile, pentry);
    }
  }

  /* Specific type free. */
  switch (pentry->type) {
  case ENTRY_BOOL:
  case ENTRY_INT:
    break;

  case ENTRY_STR:
    free(pentry->string.value);
    break;
  }

  /* Common free. */
  free(pentry->name);
  if (NULL != pentry->comment) {
    free(pentry->comment);
  }
  free(pentry);
}

/**************************************************************************
  Returns the parent section of this entry.
**************************************************************************/
struct section *entry_section(const struct entry *pentry)
{
  return (NULL != pentry ? pentry->psection : NULL);
}

/**************************************************************************
  Returns the type of this entry or -1 or error.
**************************************************************************/
enum entry_type entry_type(const struct entry *pentry)
{
  return (NULL != pentry ? pentry->type : -1);
}

/**************************************************************************
  Build the entry path.  Returns like snprintf().
**************************************************************************/
int entry_path(const struct entry *pentry, char *buf, size_t buf_len)
{
  return fc_snprintf(buf, buf_len, "%s.%s",
                     section_name(entry_section(pentry)),
                     entry_name(pentry));
}

/**************************************************************************
  Returns the name of this entry.
**************************************************************************/
const char *entry_name(const struct entry *pentry)
{
  return (NULL != pentry ? pentry->name : NULL);
}

/**************************************************************************
  Sets the name of the entry.  Returns TRUE on success.
**************************************************************************/
bool entry_set_name(struct entry *pentry, const char *name)
{
  struct section *psection;
  struct section_file *secfile;

  SECFILE_RETURN_VAL_IF_FAIL(NULL, NULL, NULL != pentry, FALSE);
  psection = pentry->psection;
  SECFILE_RETURN_VAL_IF_FAIL(NULL, NULL, NULL != psection, FALSE);
  secfile = psection->secfile;
  SECFILE_RETURN_VAL_IF_FAIL(NULL, psection, NULL != secfile, FALSE);

  if (NULL == name || '\0' == name[0]) {
    SECFILE_LOG(secfile, psection, "No new name for entry \"%s\".",
                pentry->name);
    return FALSE;
  }

  if (!check_name(name)) {
    SECFILE_LOG(secfile, psection,
                "\"%s\" is not a valid entry name for entry \"%s\".",
                name, pentry->name);
    return FALSE;
  }

  if (!secfile->allow_duplicates) {
    struct entry *pother = section_entry_by_name(psection, name);

    if (NULL != pother && pother != pentry) {
      SECFILE_LOG(secfile, psection, "Entry \"%s\" already exists.", name);
      return FALSE;
    }
  }

  /* Remove from hash table the old path. */
  secfile_hash_delete(secfile, pentry);

  /* Really rename the entry. */
  free(pentry->name);
  pentry->name = fc_strdup(name);

  /* Insert into hash table the new path. */
  secfile_hash_insert(secfile, pentry);
  return TRUE;
}

/**************************************************************************
  Returns the comment associated to this entry.
**************************************************************************/
const char *entry_comment(const struct entry *pentry)
{
  return (NULL != pentry ? pentry->comment : NULL);
}

/**************************************************************************
  Sets a comment for the entry.  Pass NULL to remove the current one.
**************************************************************************/
void entry_set_comment(struct entry *pentry, const char *comment)
{
  if (NULL == pentry) {
    return;
  }

  if (NULL != pentry->comment) {
    free(pentry->comment);
  }

  pentry->comment = (NULL != comment ? fc_strdup(comment) : NULL);
}

/**************************************************************************
  Returns TRUE if this entry has been used.
**************************************************************************/
static inline bool entry_used(const struct entry *pentry)
{
  return (0 < pentry->used);
}

/**************************************************************************
  Increase the used count.
**************************************************************************/
static inline void entry_use(struct entry *pentry)
{
  pentry->used++;
}

/**************************************************************************
  Gets an boolean value.  Returns TRUE on success.
  On old saved files, 0 and 1 can also be considered as bool.
**************************************************************************/
bool entry_bool_get(const struct entry *pentry, bool *value)
{
  SECFILE_RETURN_VAL_IF_FAIL(NULL, NULL, NULL != pentry, FALSE);

  if (ENTRY_INT == pentry->type
      && (pentry->integer.value == 0
          || pentry->integer.value == 1)) {
    *value = (0 != pentry->integer.value);
    return TRUE;
  }

  SECFILE_RETURN_VAL_IF_FAIL(pentry->psection->secfile, pentry->psection,
                             ENTRY_BOOL == pentry->type, FALSE);

  if (NULL != value) {
    *value = pentry->boolean.value;
  }
  return TRUE;
}

/**************************************************************************
  Sets an boolean value.  Returns TRUE on success.
**************************************************************************/
bool entry_bool_set(struct entry *pentry, bool value)
{
  SECFILE_RETURN_VAL_IF_FAIL(NULL, NULL, NULL != pentry, FALSE);
  SECFILE_RETURN_VAL_IF_FAIL(pentry->psection->secfile, pentry->psection,
                             ENTRY_BOOL == pentry->type, FALSE);

  pentry->boolean.value = value;
  return TRUE;
}

/**************************************************************************
  Gets an integer value.  Returns TRUE on success.
**************************************************************************/
bool entry_int_get(const struct entry *pentry, int *value)
{
  SECFILE_RETURN_VAL_IF_FAIL(NULL, NULL, NULL != pentry, FALSE);
  SECFILE_RETURN_VAL_IF_FAIL(pentry->psection->secfile, pentry->psection,
                             ENTRY_INT == pentry->type, FALSE);

  if (NULL != value) {
    *value = pentry->integer.value;
  }
  return TRUE;
}

/**************************************************************************
  Sets an integer value.  Returns TRUE on success.
**************************************************************************/
bool entry_int_set(struct entry *pentry, int value)
{
  SECFILE_RETURN_VAL_IF_FAIL(NULL, NULL, NULL != pentry, FALSE);
  SECFILE_RETURN_VAL_IF_FAIL(pentry->psection->secfile, pentry->psection,
                             ENTRY_INT == pentry->type, FALSE);

  pentry->integer.value = value;
  return TRUE;
}

/**************************************************************************
  Gets an string value.  Returns TRUE on success.
**************************************************************************/
bool entry_str_get(const struct entry *pentry, const char **value)
{
  SECFILE_RETURN_VAL_IF_FAIL(NULL, NULL, NULL != pentry, FALSE);
  SECFILE_RETURN_VAL_IF_FAIL(pentry->psection->secfile, pentry->psection,
                             ENTRY_STR == pentry->type, FALSE);

  if (NULL != value) {
    *value = pentry->string.value;
  }
  return TRUE;
}

/**************************************************************************
  Sets an string value.  Returns TRUE on success.
**************************************************************************/
bool entry_str_set(struct entry *pentry, const char *value)
{
  SECFILE_RETURN_VAL_IF_FAIL(NULL, NULL, NULL != pentry, FALSE);
  SECFILE_RETURN_VAL_IF_FAIL(pentry->psection->secfile, pentry->psection,
                             ENTRY_STR == pentry->type, FALSE);

  free(pentry->string.value);
  pentry->string.value = fc_strdup(NULL != value ? value : "");
  return TRUE;
}

/**************************************************************************
  Returns if the string would be escaped.
**************************************************************************/
bool entry_str_escaped(const struct entry *pentry)
{
  SECFILE_RETURN_VAL_IF_FAIL(NULL, NULL, NULL != pentry, FALSE);
  SECFILE_RETURN_VAL_IF_FAIL(pentry->psection->secfile, pentry->psection,
                             ENTRY_STR == pentry->type, FALSE);

  return pentry->string.escaped;
}

/**************************************************************************
  Sets if the string would be escaped.  Returns TRUE on success.
**************************************************************************/
bool entry_str_set_escaped(struct entry *pentry, bool escaped)
{
  SECFILE_RETURN_VAL_IF_FAIL(NULL, NULL, NULL != pentry, FALSE);
  SECFILE_RETURN_VAL_IF_FAIL(pentry->psection->secfile, pentry->psection,
                             ENTRY_STR == pentry->type, FALSE);

  pentry->string.escaped = escaped;
  return TRUE;
}

/**************************************************************************
  Push an entry into a file stream.
**************************************************************************/
static void entry_to_file(const struct entry *pentry, fz_FILE *fs)
{
  static char buf[8192];

  switch (pentry->type) {
  case ENTRY_BOOL:
    fz_fprintf(fs, "%s", pentry->boolean.value ? "TRUE" : "FALSE");
    break;
  case ENTRY_INT:
    fz_fprintf(fs, "%d", pentry->integer.value);
    break;
  case ENTRY_STR:
    if (pentry->string.escaped) {
      make_escapes(pentry->string.value, buf, sizeof(buf));
      fz_fprintf(fs, "\"%s\"", buf);
    } else {
      fz_fprintf(fs, "$%s$", pentry->string.value);
    }
    break;
  }
}

/**************************************************************************
  Creates a new entry from the token.
**************************************************************************/
static void entry_from_token(struct section *psection, const char *name,
                             const char *tok, struct inputfile *inf)
{
  if ('$' == tok[0] || '"' == tok[0]) {
    char buf[strlen(tok) + 1];
    bool escaped = ('"' == tok[0]);

    remove_escapes(tok + 1, escaped, buf, sizeof(buf));
    (void) section_entry_str_new(psection, name, buf, escaped);
    DEBUG_ENTRIES("entry %s '%s'", name, buf);
    return;
  }

  if (fc_isdigit(tok[0]) || ('-' == tok[0] && fc_isdigit(tok[1]))) {
    int value;

    if (1 == sscanf(tok, "%d", &value)) {
      (void) section_entry_int_new(psection, name, value);
      DEBUG_ENTRIES("entry %s %d", name, value);
      return;
    }
  }

  if (0 == fc_strncasecmp(tok, "FALSE", 5)
      || 0 == fc_strncasecmp(tok, "TRUE", 4)) {
    bool value = (0 == fc_strncasecmp(tok, "TRUE", 4));

    (void) section_entry_bool_new(psection, name, value);
    DEBUG_ENTRIES("entry %s %s", name, value ? "TRUE" : "FALSE");
    return;
  }

  log_error("%s", inf_log_str(inf, "Entry value not recognized: %s", tok));
}
