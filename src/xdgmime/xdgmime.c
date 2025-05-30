/* -*- mode: C; c-file-style: "gnu" -*- */
/* xdgmime.c: XDG Mime Spec mime resolver.  Based on version 0.11 of the spec.
 *
 * More info can be found at http://www.freedesktop.org/standards/
 * 
 * Copyright (C) 2003,2004  Red Hat, Inc.
 * Copyright (C) 2003,2004  Jonathan Blandford <jrb@alum.mit.edu>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later or AFL-2.0
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "xdgmime.h"
#include "xdgmimeint.h"
#include "xdgmimeglob.h"
#include "xdgmimemagic.h"
#include "xdgmimealias.h"
#include "xdgmimeicon.h"
#include "xdgmimeparent.h"
#include "xdgmimecache.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
//#include <sys/types.h>
#include <sys/time.h>
//#include <unistd.h>
#include <assert.h>

//#ifndef S_ISREG
//#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
//#endif

typedef struct XdgDirTimeList XdgDirTimeList;
typedef struct XdgCallbackList XdgCallbackList;

static int need_reread = TRUE;
static time_t last_stat_time = 0;

static XdgGlobHash *global_hash = NULL;
static XdgMimeMagic *global_magic = NULL;
static XdgAliasList *alias_list = NULL;
static XdgParentList *parent_list = NULL;
static XdgDirTimeList *dir_time_list = NULL;
static XdgCallbackList *callback_list = NULL;
static XdgIconList *icon_list = NULL;
static XdgIconList *generic_icon_list = NULL;

static char **xdg_dirs = NULL;  /* NULL terminated */

XdgMimeCache **_caches = NULL;
static int n_caches = 0;

const char xdg_mime_type_unknown[] = "application/octet-stream";
//const char xdg_mime_type_empty[] = "application/x-zerosize";
//const char xdg_mime_type_textplain[] = "text/plain";


enum
{
  XDG_CHECKED_UNCHECKED,
  XDG_CHECKED_VALID,
  XDG_CHECKED_INVALID
};

struct XdgDirTimeList
{
  time_t mtime;
  char *directory_name;
  int checked;
  XdgDirTimeList *next;
};

struct XdgCallbackList
{
  XdgCallbackList *next;
  XdgCallbackList *prev;
  int              callback_id;
  XdgMimeCallback  callback;
  void            *data;
  XdgMimeDestroy   destroy;
};

/* Function called by xdg_run_command_on_dirs.  If it returns TRUE, further
 * directories aren't looked at */
typedef int (*XdgDirectoryFunc) (const char *directory,
				 void       *user_data);

static void
xdg_dir_time_list_add (char   *file_name, 
		       time_t  mtime)
{
  XdgDirTimeList *list;

  for (list = dir_time_list; list; list = list->next) 
    {
      if (strcmp (list->directory_name, file_name) == 0)
        {
          free (file_name);
          return;
        }
    }
  
  list = calloc (1, sizeof (XdgDirTimeList));
  list->checked = XDG_CHECKED_UNCHECKED;
  list->directory_name = file_name;
  list->mtime = mtime;
  list->next = dir_time_list;
  dir_time_list = list;
}
 
static void
xdg_dir_time_list_free (XdgDirTimeList *list)
{
  XdgDirTimeList *next;

  while (list)
    {
      next = list->next;
      free (list->directory_name);
      free (list);
      list = next;
    }
}

static int
xdg_mime_init_from_directory (const char *directory,
                              void       *user_data __attribute__((__unused__)))
{
  char *file_name;
  struct stat st;

  assert (directory != NULL);

  file_name = malloc (strlen (directory) + strlen ("/mime.cache") + 1);
  strcpy (file_name, directory); strcat (file_name, "/mime.cache");
  if (stat (file_name, &st) == 0)
    {
      XdgMimeCache *cache = _xdg_mime_cache_new_from_file (file_name);

      if (cache != NULL)
	{
	  xdg_dir_time_list_add (file_name, st.st_mtime);

	  _caches = realloc (_caches, sizeof (XdgMimeCache *) * (n_caches + 2));
	  _caches[n_caches] = cache;
          _caches[n_caches + 1] = NULL;
	  n_caches++;

	  return FALSE;
	}
    }
  free (file_name);

  file_name = malloc (strlen (directory) + strlen ("/globs2") + 1);
  strcpy (file_name, directory); strcat (file_name, "/globs2");
  if (stat (file_name, &st) == 0)
    {
      _xdg_mime_glob_read_from_file (global_hash, file_name, TRUE);
      xdg_dir_time_list_add (file_name, st.st_mtime);
    }
  else
    {
      free (file_name);
      file_name = malloc (strlen (directory) + strlen ("/globs") + 1);
      strcpy (file_name, directory); strcat (file_name, "/globs");
      if (stat (file_name, &st) == 0)
        {
          _xdg_mime_glob_read_from_file (global_hash, file_name, FALSE);
          xdg_dir_time_list_add (file_name, st.st_mtime);
        }
      else
        {
          free (file_name);
        }
    }

  file_name = malloc (strlen (directory) + strlen ("/magic") + 1);
  strcpy (file_name, directory); strcat (file_name, "/magic");
  if (stat (file_name, &st) == 0)
    {
      _xdg_mime_magic_read_from_file (global_magic, file_name);
      xdg_dir_time_list_add (file_name, st.st_mtime);
    }
  else
    {
      free (file_name);
    }

  file_name = malloc (strlen (directory) + strlen ("/aliases") + 1);
  strcpy (file_name, directory); strcat (file_name, "/aliases");
  _xdg_mime_alias_read_from_file (alias_list, file_name);
  free (file_name);

  file_name = malloc (strlen (directory) + strlen ("/subclasses") + 1);
  strcpy (file_name, directory); strcat (file_name, "/subclasses");
  _xdg_mime_parent_read_from_file (parent_list, file_name);
  free (file_name);

  file_name = malloc (strlen (directory) + strlen ("/icons") + 1);
  strcpy (file_name, directory); strcat (file_name, "/icons");
  _xdg_mime_icon_read_from_file (icon_list, file_name);
  free (file_name);

  file_name = malloc (strlen (directory) + strlen ("/generic-icons") + 1);
  strcpy (file_name, directory); strcat (file_name, "/generic-icons");
  _xdg_mime_icon_read_from_file (generic_icon_list, file_name);
  free (file_name);

  return FALSE; /* Keep processing */
}

/* Set @xdg_dirs from the environment. It must not have been set already. */
static void
xdg_init_dirs (void)
{
  const char *xdg_data_home, *home, *xdg_data_dirs;
  const char *ptr;
  size_t n_dirs = 0;
  size_t i, current_dir;

  assert (xdg_dirs == NULL);

  xdg_data_home = getenv ("XDG_DATA_HOME");
  home = getenv ("HOME");
  xdg_data_dirs = getenv ("XDG_DATA_DIRS");

  if (xdg_data_dirs == NULL)
    xdg_data_dirs = "/usr/local/share/:/usr/share/";

  /* Work out how many dirs we’re dealing with. */
  if (xdg_data_home != NULL || home != NULL)
    n_dirs++;
  n_dirs++;  /* initial entry in @xdg_data_dirs */
  for (i = 0; xdg_data_dirs[i] != '\0'; i++)
    if (xdg_data_dirs[i] == ':')
      n_dirs++;

  xdg_dirs = calloc (n_dirs + 1  /* NULL terminator */, sizeof (char *));
  current_dir = 0;

  /* $XDG_DATA_HOME */
  if (xdg_data_home != NULL)
    {
      char *mime_subdir;

      mime_subdir = malloc (strlen (xdg_data_home) + strlen ("/mime/") + 1);
      strcpy (mime_subdir, xdg_data_home);
      strcat (mime_subdir, "/mime/");

      xdg_dirs[current_dir++] = mime_subdir;
    }
  else if (home != NULL)
    {
      char *guessed_xdg_home;

      guessed_xdg_home = malloc (strlen (home) + strlen ("/.local/share/mime/") + 1);
      strcpy (guessed_xdg_home, home);
      strcat (guessed_xdg_home, "/.local/share/mime/");

      xdg_dirs[current_dir++] = guessed_xdg_home;
    }

  /* $XDG_DATA_DIRS */
  ptr = xdg_data_dirs;

  while (*ptr != '\000')
    {
      const char *end_ptr;
      char *dir;
      int len;

      end_ptr = ptr;
      while (*end_ptr != ':' && *end_ptr != '\000')
        end_ptr ++;

      if (end_ptr == ptr)
        {
          ptr++;
          continue;
        }

      if (*end_ptr == ':')
        len = end_ptr - ptr;
      else
        len = end_ptr - ptr + 1;
      dir = malloc (len + strlen ("/mime/") + 1);
      strncpy (dir, ptr, len);
      dir[len] = '\0';
      strcat (dir, "/mime/");

      xdg_dirs[current_dir++] = dir;

      ptr = end_ptr;
    }

  /* NULL terminator */
  xdg_dirs[current_dir] = NULL;

  need_reread = TRUE;
}

/* Runs a command on all the directories in the search path (@xdg_dirs). */
static void
xdg_run_command_on_dirs (XdgDirectoryFunc  func,
                         void             *user_data)
{
  size_t i;

  if (xdg_dirs == NULL)
    xdg_init_dirs ();

  for (i = 0; xdg_dirs[i] != NULL; i++)
    {
      if ((func) (xdg_dirs[i], user_data))
        return;
    }
}

///* Allows the calling code to override the directories used by xdgmime, without
// * having to change environment variables in a running process (which is not
// * thread safe). This is intended to be used by tests. The changes will be
// * picked up by xdg_mime_init() next time public API is called.
// *
// * This will set @xdg_dirs. Directories in @dirs must be complete, including
// * the conventional `/mime` subdirectory. This is to allow tests to override
// * them without the need to create a subdirectory. */
//void
//xdg_mime_set_dirs (const char * const *dirs)
//{
//  size_t i;
//
//  for (i = 0; xdg_dirs != NULL && xdg_dirs[i] != NULL; i++)
//    free (xdg_dirs[i]);
//  free (xdg_dirs);
//  xdg_dirs = NULL;
//
//  if (dirs != NULL)
//    {
//      for (i = 0; dirs[i] != NULL; i++);
//      xdg_dirs = calloc (i + 1  /* NULL terminator */, sizeof (char*));
//      for (i = 0; dirs[i] != NULL; i++)
//        xdg_dirs[i] = strdup (dirs[i]);
//      xdg_dirs[i] = NULL;
//    }
//
//  need_reread = TRUE;
//}

/* Checks file_path to make sure it has the same mtime as last time it was
 * checked.  If it has a different mtime, or if the file doesn't exist, it
 * returns FALSE.
 *
 * FIXME: This doesn't protect against permission changes.
 */
static int
xdg_check_file (const char *file_path,
                int        *exists)
{
  struct stat st;

  /* If the file exists */
  if (stat (file_path, &st) == 0)
    {
      XdgDirTimeList *list;

      if (exists)
        *exists = TRUE;

      for (list = dir_time_list; list; list = list->next)
	{
	  if (! strcmp (list->directory_name, file_path))
	    {
	      if (st.st_mtime == list->mtime)
		list->checked = XDG_CHECKED_VALID;
	      else 
		list->checked = XDG_CHECKED_INVALID;

	      return (list->checked != XDG_CHECKED_VALID);
	    }
	}
      return TRUE;
    }

  if (exists)
    *exists = FALSE;

  return FALSE;
}

static int
xdg_check_dir (const char *directory,
	       void       *user_data)
{
  int invalid, exists;
  char *file_name;
  int* invalid_dir_list = user_data;

  assert (directory != NULL);

  /* Check the mime.cache file */
  file_name = malloc (strlen (directory) + strlen ("/mime.cache") + 1);
  strcpy (file_name, directory); strcat (file_name, "/mime.cache");
  invalid = xdg_check_file (file_name, &exists);
  free (file_name);
  if (invalid)
    {
      *invalid_dir_list = TRUE;
      return TRUE;
    }
  else if (exists)
    {
      return FALSE;
    }

  /* Check the globs file */
  file_name = malloc (strlen (directory) + strlen ("/globs") + 1);
  strcpy (file_name, directory); strcat (file_name, "/globs");
  invalid = xdg_check_file (file_name, NULL);
  free (file_name);
  if (invalid)
    {
      *invalid_dir_list = TRUE;
      return TRUE;
    }

  /* Check the magic file */
  file_name = malloc (strlen (directory) + strlen ("/magic") + 1);
  strcpy (file_name, directory); strcat (file_name, "/magic");
  invalid = xdg_check_file (file_name, NULL);
  free (file_name);
  if (invalid)
    {
      *invalid_dir_list = TRUE;
      return TRUE;
    }

  return FALSE; /* Keep processing */
}

/* Walks through all the mime files stat()ing them to see if they've changed.
 * Returns TRUE if they have. */
static int
xdg_check_dirs (void)
{
  XdgDirTimeList *list;
  int invalid_dir_list = FALSE;

  for (list = dir_time_list; list; list = list->next)
    list->checked = XDG_CHECKED_UNCHECKED;

  xdg_run_command_on_dirs (xdg_check_dir, &invalid_dir_list);

  if (invalid_dir_list)
    return TRUE;

  for (list = dir_time_list; list; list = list->next)
    {
      if (list->checked != XDG_CHECKED_VALID)
	return TRUE;
    }

  return FALSE;
}

/* We want to avoid stat()ing on every single mime call, so we only look for
 * newer files every 5 seconds.  This will return TRUE if we need to reread the
 * mime data from disk.
 */
static int
xdg_check_time_and_dirs (void)
{
  struct timeval tv;
  time_t current_time;
  int retval = FALSE;

  gettimeofday (&tv, NULL);
  current_time = tv.tv_sec;

  if (current_time >= last_stat_time + 5)
    {
      retval = xdg_check_dirs ();
      last_stat_time = current_time;
    }

  return retval;
}

/* Called in every public function.  It reloads the hash function if need be.
 */
static void
xdg_mime_init (void)
{
  if (xdg_check_time_and_dirs ())
    {
      xdg_mime_shutdown ();
    }

  if (need_reread)
    {
      global_hash = _xdg_glob_hash_new ();
      global_magic = _xdg_mime_magic_new ();
      alias_list = _xdg_mime_alias_list_new ();
      parent_list = _xdg_mime_parent_list_new ();
      icon_list = _xdg_mime_icon_list_new ();
      generic_icon_list = _xdg_mime_icon_list_new ();

      xdg_run_command_on_dirs (xdg_mime_init_from_directory, NULL);

      need_reread = FALSE;
    }
}

//const char *
//xdg_mime_get_mime_type_for_data (const void *data,
//				 size_t      len,
//				 int        *result_prio)
//{
//  const char *mime_type;
//
//  if (len == 0)
//    {
//      if (result_prio != NULL)
//        *result_prio = 100;
//      return XDG_MIME_TYPE_EMPTY;
//    }
//
//  xdg_mime_init ();
//
//  if (_caches)
//    mime_type = _xdg_mime_cache_get_mime_type_for_data (data, len, result_prio);
//  else
//    mime_type = _xdg_mime_magic_lookup_data (global_magic, data, len, result_prio, NULL, 0);
//
//  if (mime_type)
//    return mime_type;
//
//  return _xdg_binary_or_text_fallback(data, len);
//}

//const char *
//xdg_mime_get_mime_type_for_file (const char  *file_name,
//                                 struct stat *statbuf)
//{
//  const char *mime_type;
//  /* currently, only a few globs occur twice, and none
//   * more often, so 5 seems plenty.
//   */
//  const char *mime_types[5];
//  FILE *file;
//  unsigned char *data;
//  int max_extent;
//  int bytes_read;
//  struct stat buf;
//  const char *base_name;
//  int n;
//
//  if (file_name == NULL)
//    return NULL;
//  if (! _xdg_utf8_validate (file_name))
//    return NULL;
//
//  xdg_mime_init ();
//
//  if (_caches)
//    return _xdg_mime_cache_get_mime_type_for_file (file_name, statbuf);
//
//  base_name = _xdg_get_base_name (file_name);
//  n = _xdg_glob_hash_lookup_file_name (global_hash, base_name, mime_types, 5);
//
//  if (n == 1)
//    return mime_types[0];
//
//  if (!statbuf)
//    {
//      if (stat (file_name, &buf) != 0)
//	return XDG_MIME_TYPE_UNKNOWN;
//
//      statbuf = &buf;
//    }
//
//  if (!S_ISREG (statbuf->st_mode))
//    return XDG_MIME_TYPE_UNKNOWN;
//
//  /* FIXME: Need to make sure that max_extent isn't totally broken.  This could
//   * be large and need getting from a stream instead of just reading it all
//   * in. */
//  max_extent = _xdg_mime_magic_get_buffer_extents (global_magic);
//  data = malloc (max_extent);
//  if (data == NULL)
//    return XDG_MIME_TYPE_UNKNOWN;
//        
//  file = fopen (file_name, "r");
//  if (file == NULL)
//    {
//      free (data);
//      return XDG_MIME_TYPE_UNKNOWN;
//    }
//
//  bytes_read = fread (data, 1, max_extent, file);
//  if (ferror (file))
//    {
//      free (data);
//      fclose (file);
//      return XDG_MIME_TYPE_UNKNOWN;
//    }
//
//  mime_type = _xdg_mime_magic_lookup_data (global_magic, data, bytes_read, NULL,
//					   mime_types, n);
//
//  if (!mime_type)
//    mime_type = _xdg_binary_or_text_fallback (data, bytes_read);
//
//  free (data);
//  fclose (file);
//
//  return mime_type;
//}

const char *
xdg_mime_get_mime_type_from_file_name (const char *file_name)
{
  const char *mime_type;

  xdg_mime_init ();

  if (_caches)
    return _xdg_mime_cache_get_mime_type_from_file_name (file_name);

  if (_xdg_glob_hash_lookup_file_name (global_hash, file_name, &mime_type, 1))
    return mime_type;
  else
    return XDG_MIME_TYPE_UNKNOWN;
}

//int
//xdg_mime_get_mime_types_from_file_name (const char *file_name,
//					const char  *mime_types[],
//					int          n_mime_types)
//{
//  xdg_mime_init ();
//  
//  if (_caches)
//    return _xdg_mime_cache_get_mime_types_from_file_name (file_name, mime_types, n_mime_types);
//  
//  return _xdg_glob_hash_lookup_file_name (global_hash, file_name, mime_types, n_mime_types);
//}

//int
//xdg_mime_is_valid_mime_type (const char *mime_type)
//{
//  /* FIXME: We should make this a better test
//   */
//  return _xdg_utf8_validate (mime_type);
//}

void
xdg_mime_shutdown (void)
{
  XdgCallbackList *list;

  /* FIXME: Need to make this (and the whole library) thread safe */
  if (dir_time_list)
    {
      xdg_dir_time_list_free (dir_time_list);
      dir_time_list = NULL;
    }
	
  if (global_hash)
    {
      _xdg_glob_hash_free (global_hash);
      global_hash = NULL;
    }
  if (global_magic)
    {
      _xdg_mime_magic_free (global_magic);
      global_magic = NULL;
    }

  if (alias_list)
    {
      _xdg_mime_alias_list_free (alias_list);
      alias_list = NULL;
    }

  if (parent_list)
    {
      _xdg_mime_parent_list_free (parent_list);
      parent_list = NULL;
    }

  if (icon_list)
    {
      _xdg_mime_icon_list_free (icon_list);
      icon_list = NULL;
    }

  if (generic_icon_list)
    {
      _xdg_mime_icon_list_free (generic_icon_list);
      generic_icon_list = NULL;
    }
  
  if (_caches)
    {
      int i;

      for (i = 0; i < n_caches; i++)
        _xdg_mime_cache_unref (_caches[i]);
      free (_caches);
      _caches = NULL;
      n_caches = 0;
    }

  for (list = callback_list; list; list = list->next)
    (list->callback) (list->data);

  need_reread = TRUE;
}

//int
//xdg_mime_get_max_buffer_extents (void)
//{
//  xdg_mime_init ();
//  
//  if (_caches)
//    return _xdg_mime_cache_get_max_buffer_extents ();
//
//  return _xdg_mime_magic_get_buffer_extents (global_magic);
//}

//const char *
//_xdg_mime_unalias_mime_type (const char *mime_type)
//{
//  const char *lookup;
//
//  if (_caches)
//    return _xdg_mime_cache_unalias_mime_type (mime_type);
//
//  if ((lookup = _xdg_mime_alias_list_lookup (alias_list, mime_type)) != NULL)
//    return lookup;
//
//  return mime_type;
//}

//const char *
//xdg_mime_unalias_mime_type (const char *mime_type)
//{
//  xdg_mime_init ();
//
//  return _xdg_mime_unalias_mime_type (mime_type);
//}

//int
//_xdg_mime_mime_type_equal (const char *mime_a,
//			   const char *mime_b)
//{
//  const char *unalias_a, *unalias_b;
//
//  unalias_a = _xdg_mime_unalias_mime_type (mime_a);
//  unalias_b = _xdg_mime_unalias_mime_type (mime_b);
//
//  if (strcmp (unalias_a, unalias_b) == 0)
//    return 1;
//
//  return 0;
//}

//int
//xdg_mime_mime_type_equal (const char *mime_a,
//			  const char *mime_b)
//{
//  xdg_mime_init ();
//
//  return _xdg_mime_mime_type_equal (mime_a, mime_b);
//}

//int
//xdg_mime_media_type_equal (const char *mime_a,
//			   const char *mime_b)
//{
//  char *sep;
//
//  sep = strchr (mime_a, '/');
//  
//  if (sep && strncmp (mime_a, mime_b, sep - mime_a + 1) == 0)
//    return 1;
//
//  return 0;
//}

//#if 1
//static int
//ends_with (const char *str,
//           const char *suffix)
//{
//  int length;
//  int suffix_length;
//
//  length = strlen (str);
//  suffix_length = strlen (suffix);
//  if (length < suffix_length)
//    return 0;
//
//  if (strcmp (str + length - suffix_length, suffix) == 0)
//    return 1;
//
//  return 0;
//}

//static int
//xdg_mime_is_super_type (const char *mime)
//{
//  return ends_with (mime, "/*");
//}
//#endif

//int
//_xdg_mime_mime_type_subclass (const char *mime,
//			      const char *base,
//			      const char ***seen)
//{
//  const char *umime, *ubase, *parent;
//  const char **parents, **first_seen = NULL, **new_seen;
//
//  int i, ret = 0;
//
//  if (_caches)
//    return _xdg_mime_cache_mime_type_subclass (mime, base, NULL);
//
//  umime = _xdg_mime_unalias_mime_type (mime);
//  ubase = _xdg_mime_unalias_mime_type (base);
//
//  if (strcmp (umime, ubase) == 0)
//    return 1;
//
//#if 1  
//  /* Handle supertypes */
//  if (xdg_mime_is_super_type (ubase) &&
//      xdg_mime_media_type_equal (umime, ubase))
//    return 1;
//#endif
//
//  /*  Handle special cases text/plain and application/octet-stream */
//  if (strcmp (ubase, "text/plain") == 0 && 
//      strncmp (umime, "text/", 5) == 0)
//    return 1;
//
//  if (strcmp (ubase, "application/octet-stream") == 0 &&
//      strncmp (umime, "inode/", 6) != 0)
//    return 1;
//
//  if (!seen)
//    {
//      first_seen = calloc (1, sizeof (char *));
//      seen = &first_seen;
//    }
//
//  parents = _xdg_mime_parent_list_lookup (parent_list, umime);
//  for (; parents && *parents; parents++)
//    {
//      parent = *parents;
//
//      /* Detect and avoid buggy circular relationships */
//      for (i = 0; (*seen)[i] != NULL; i++)
//        if (parent == (*seen)[i])
//          goto next_parent;
//      new_seen = realloc (*seen, (i + 2) * sizeof (char *));
//      if (!new_seen)
//        goto done;
//      new_seen[i] = parent;
//      new_seen[i + 1] = NULL;
//      *seen = new_seen;
//
//      if (_xdg_mime_mime_type_subclass (parent, ubase, seen))
//        {
//          ret = 1;
//          goto done;
//        }
//
//    next_parent:
//      continue;
//    }
//
//done:
//  free (first_seen);
//  return ret;
//}

//int
//xdg_mime_mime_type_subclass (const char *mime,
//			     const char *base)
//{
//  xdg_mime_init ();
//
//  return _xdg_mime_mime_type_subclass (mime, base, NULL);
//}

//char **
//xdg_mime_list_mime_parents (const char *mime)
//{
//  const char **parents;
//  char **result;
//  int i, n;
//
//  xdg_mime_init ();
//
//  if (_caches)
//    return _xdg_mime_cache_list_mime_parents (mime);
//
//  parents = xdg_mime_get_mime_parents (mime);
//
//  if (!parents)
//    return NULL;
//
//  for (i = 0; parents[i]; i++) ;
//  
//  n = (i + 1) * sizeof (char *);
//  result = (char **) malloc (n);
//  memcpy (result, parents, n);
//
//  return result;
//}

//const char **
//xdg_mime_get_mime_parents (const char *mime)
//{
//  const char *umime;
//
//  xdg_mime_init ();
//
//  umime = _xdg_mime_unalias_mime_type (mime);
//
//  return _xdg_mime_parent_list_lookup (parent_list, umime);
//}

//void 
//xdg_mime_dump (void)
//{
//  xdg_mime_init();
//
//  printf ("*** ALIASES ***\n\n");
//  _xdg_mime_alias_list_dump (alias_list);
//  printf ("\n*** PARENTS ***\n\n");
//  _xdg_mime_parent_list_dump (parent_list);
//  printf ("\n*** CACHE ***\n\n");
//  _xdg_glob_hash_dump (global_hash);
//  printf ("\n*** GLOBS ***\n\n");
//  _xdg_glob_hash_dump (global_hash);
//  printf ("\n*** GLOBS REVERSE TREE ***\n\n");
//  _xdg_mime_cache_glob_dump ();
//}


///* Registers a function to be called every time the mime database reloads its files
// */
//int
//xdg_mime_register_reload_callback (XdgMimeCallback  callback,
//				   void            *data,
//				   XdgMimeDestroy   destroy)
//{
//  XdgCallbackList *list_el;
//  static int callback_id = 1;
//
//  /* Make a new list element */
//  list_el = calloc (1, sizeof (XdgCallbackList));
//  list_el->callback_id = callback_id;
//  list_el->callback = callback;
//  list_el->data = data;
//  list_el->destroy = destroy;
//  list_el->next = callback_list;
//  if (list_el->next)
//    list_el->next->prev = list_el;
//
//  callback_list = list_el;
//  callback_id ++;
//
//  return callback_id - 1;
//}

//void
//xdg_mime_remove_callback (int callback_id)
//{
//  XdgCallbackList *list;
//
//  for (list = callback_list; list; list = list->next)
//    {
//      if (list->callback_id == callback_id)
//	{
//	  if (list->next)
//	    list->next = list->prev;
//
//	  if (list->prev)
//	    list->prev->next = list->next;
//	  else
//	    callback_list = list->next;
//
//	  /* invoke the destroy handler */
//	  (list->destroy) (list->data);
//	  free (list);
//	  return;
//	}
//    }
//}

//const char *
//xdg_mime_get_icon (const char *mime)
//{
//  xdg_mime_init ();
//  
//  if (_caches)
//    return _xdg_mime_cache_get_icon (mime);
//
//  return _xdg_mime_icon_list_lookup (icon_list, mime);
//}

//const char *
//xdg_mime_get_generic_icon (const char *mime)
//{
//  xdg_mime_init ();
//  
//  if (_caches)
//    return _xdg_mime_cache_get_generic_icon (mime);
//
//  return _xdg_mime_icon_list_lookup (generic_icon_list, mime);
//}
