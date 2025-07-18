/*
 *  Copyright (C) 2017 Ingo Brückl
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301 USA.
 */

#include <stdio.h>
#include <string.h>
#include "cpio.h"
#include "date_utils.h"
#include "main.h"
#include "parser.h"
#include "string_utils.h"
#include "support.h"
#include "window.h"

static void relative_path (char *filename, gpointer user_data)
{
	if (*filename == '/')
		strcpy(filename, filename + 1);
}

gboolean xa_cpio_check_version (gchar *path)
{
	gchar *command, *output = NULL;
	int major, minor;
	gboolean result = FALSE;

	command = g_strconcat(path, " --version", NULL);
	g_spawn_command_line_sync(command, &output, NULL, NULL, NULL);
	g_free(command);

	if (output)
	{
		if (strncmp(output, "cpio (GNU cpio) ", 16) == 0)
			if (sscanf(output + 16, "%d.%d", &major, &minor) == 2)
				/* cpio as of version 2.13 is safe */
				result = ((major == 2 && minor > 12) || major > 2);

		g_free(output);
	}

	return result;
}

void xa_cpio_ask (XArchive *archive)
{
	archive->can_extract = TRUE;
	archive->can_add = TRUE;
	archive->can_full_path[0] = TRUE;
	archive->can_full_path[1] = TRUE;   // n.b.: adds leading slash
	archive->can_touch = TRUE;
	archive->can_overwrite = TRUE;
	archive->can_recurse[0] = TRUE;
}

void xa_cpio_parse_output (gchar *line, XArchive *archive)
{
	XEntry *entry;
	gpointer item[7];
	gchar *filename, time[6];
	gboolean dev, dir, link;

	USE_PARSER;

	/* permissions */
	NEXT_ITEM(item[4]);

	dev = (*(char *) item[4] == 'b' || *(char *) item[4] == 'c');
	dir = (*(char *) item[4] == 'd');
	link = (*(char *) item[4] == 'l');

	/* number of links */
	SKIP_ITEM;

	/* owner */
	NEXT_ITEM(item[5]);

	/* group */
	NEXT_ITEM(item[6]);

	/* size */
	if (dev)
	{
		SKIP_ITEM;
		SKIP_ITEM;
		item[1] = "0";
	}
	else
		NEXT_ITEM(item[1]);

	LINE_PEEK(9);

	/* date and time */
	NEXT_ITEMS(3, item[2]);

	/* time */
	if (((char *) item[2])[peek] == ':' && strlen(item[2]) >= 12)
	{
		memcpy(time, item[2] + 7, 5);
		time[5] = 0;
	}
	else
		strcpy(time, "-----");

	item[2] = date_MMM_dD_HourYear(item[2]);
	item[3] = time;

	/* name */
	LAST_ITEM(filename);

	item[0] = NULL;

	if (link)
	{
		gchar *lnk = g_strrstr(filename, " -> ");

		if (lnk)
		{
			item[0] = lnk + 4;
			*lnk = 0;
		}
	}

	entry = xa_set_archive_entries_for_each_row(archive, filename, item, dir);

	if (entry)
	{
		if (!entry->is_dir)
			archive->files++;

		archive->files_size += g_ascii_strtoull(item[1], NULL, 0);
	}
}

void xa_cpio_list (XArchive *archive)
{
	const GType types[] = {GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT64, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER};
	const gchar *titles[] = {_("Points to"), _("Original Size"), _("Date"), _("Time"), _("Permissions"), _("Owner"), _("Group")};
	gchar *command;
	guint i;

	archive->files = 0;
	archive->files_size = 0;

	command = g_strconcat(archiver[archive->type].program[0], " -tv -I ", archive->path[1], NULL);
	archive->parse_output = xa_cpio_parse_output;
	xa_spawn_async_process(archive, command);
	g_free(command);

	archive->columns = 10;
	archive->size_column = 3;
	archive->column_types = g_malloc0(sizeof(types));

	for (i = 0; i < archive->columns; i++)
		archive->column_types[i] = types[i];

	xa_create_liststore(archive, titles);
}

/*
 * Note: cpio lists ' ' as '\ ', '"' as '\"' and '\' as '\\' while it
 * extracts ' ', '"' and '\' respectively, i.e. file names containing
 * one of these three characters can't be handled entirely.
 */

gboolean xa_cpio_extract (XArchive *archive, GSList *file_list)
{
	GString *files;
	gchar *extract_to, *command;
	gboolean result;

	if (archive->do_full_path)
		extract_to = xa_unescape_bad_chars(archive->extraction_dir, ESCAPES);
	else
	{
		extract_to = xa_create_working_subdirectory(archive);

		if (!extract_to)
			return FALSE;
	}

	g_slist_foreach(file_list, (GFunc) relative_path, NULL);
	files = xa_quote_filenames(file_list, "*?[]\"", DIR_WITH_ASTERISK);
	archive->child_dir = g_strdup(extract_to);
	command = g_strconcat(archiver[archive->type].program[0], " -id",
	                      archive->do_touch ? "" : " -m",
	                      archive->do_overwrite ? " -u" : "",
	                      " --no-absolute-filenames -I ", archive->path[1],
	                      " --", files->str, NULL);
	result = xa_run_command(archive, command);
	g_free(command);

	g_free(archive->child_dir);
	archive->child_dir = NULL;

	/* collect all files that have been extracted to move them without full path */
	if (result && !archive->do_full_path)
	{
		GString *all_files = xa_collect_files_in_dir(extract_to);

		archive->child_dir = g_strdup(extract_to);
		command = g_strconcat("mv",
		                      archive->do_overwrite ? " -f" : " -n",
		                      " --", all_files->str, " ", archive->extraction_dir, NULL);
		g_string_free(all_files, TRUE);

		archive->status = XARCHIVESTATUS_EXTRACT;   // restore status
		result = xa_run_command(archive, command);
		g_free(command);

		g_free(archive->child_dir);
		archive->child_dir = NULL;
	}

	g_free(extract_to);
	g_string_free(files, TRUE);

	return result;
}

void xa_cpio_add (XArchive *archive, GSList *file_list)
{
	GString *files;
	gchar *archive_path, *command;

	files = xa_quote_filenames(file_list, "\"", DIR_WITH_SLASH);
	archive_path = xa_quote_shell_command(archive->path[0], TRUE);
	command = g_strconcat("sh -c \"", "exec ls -d --", files->str, " | ",
	                      archiver[archive->type].program[0], " -o",
	                      g_file_test(archive->path[0], G_FILE_TEST_EXISTS) ? " -A" : "",
	                      " -O ", archive_path, "\"", NULL);
	g_free(archive_path);
	g_string_free(files, TRUE);

	xa_run_command(archive, command);
	g_free(command);
}
