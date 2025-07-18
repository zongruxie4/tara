/*
 *  Copyright (C) 2008 Giuseppe Torelli - <colossus73@gmail.com>
 *  Copyright (C) 2016 Ingo Brückl
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

#ifndef XARCHIVER_ADD_DIALOG_H
#define XARCHIVER_ADD_DIALOG_H

#include <gtk/gtk.h>
#include "archive.h"

typedef struct AddDialog
{
	GtkWidget *dialog;
	GtkWidget *notebook;
	GtkWidget *filechooser;
	GtkWidget *full_path;
	GtkWidget *relative_path;
	GtkWidget *update;
	GtkWidget *freshen;
	GtkWidget *recurse;
	GtkWidget *remove;
	GtkWidget *solid;
	GtkWidget *label_least;
	GtkWidget *label_best;
	GtkWidget *alignment;
	GtkWidget *compression_scale;
	GtkWidget *uncompressed;
	GtkWidget *password;
	GtkWidget *password_entry;
	GtkWidget *encrypt;
	gboolean can_encrypt;
} AddDialog;

AddDialog *xa_create_add_dialog();
void xa_execute_add_commands(XArchive *, GSList *, gboolean, gboolean);
void xa_parse_add_dialog_options(XArchive *, AddDialog *, GSList *);
void xa_set_add_dialog_options(AddDialog *, XArchive *);

#endif
