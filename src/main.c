/*
 *  Copyright (c) 2006 Stephan Arts <psybsd@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <glib.h>
#include <glib-object.h>
#include <libxarchiver/archive.h>
#include <libxarchiver/support.h>
#include <libxarchiver/libxarchiver.h>
#include <gtk/gtk.h> 
#include <getopt.h>
#include <unistd.h>
#include <libintl.h>

#include "main.h"
#include "property-dialog.h"
#include "main-window.h"

#define _(String) gettext(String)

GtkWidget *main_window;
XAArchive *xa_archive = NULL;
XAArchive *xa_sub_archive = NULL;
XASupport *xa_support = NULL;
XASupport *xa_sub_support = NULL;

void
xa_update_rows(GObject *object, gpointer data)
{
	XAArchive *archive = data;
	gint n_columns = 0;
	gchar **column_names = NULL;
	GType *column_types = NULL;
	xa_main_window_clear_list(XA_MAIN_WINDOW(main_window), TRUE);
	xa_support_get_columns(xa_support, &n_columns, &column_names, &column_types);
	xa_main_window_set_list_interface(XA_MAIN_WINDOW(main_window), 
		n_columns, 
		column_names, 
		column_types);
}

void
xa_append_rows(GObject *object, gpointer data)
{
	XAArchive *archive = data;
	archive->row = g_list_reverse ( archive->row );
	xa_main_window_append_list(XA_MAIN_WINDOW(main_window), archive->row);
	g_list_foreach(archive->row, (GFunc)g_value_unset, NULL);
	g_list_foreach(archive->row, (GFunc)g_free, NULL);
	g_list_free(archive->row);
	archive->row = NULL;
	xa_main_window_set_progressbar_value(XA_MAIN_WINDOW(main_window), 101);
}
void
xa_operation_complete(GtkWidget *widget, gpointer data)
{
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
		"xa-button-open", 
		TRUE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
		"xa-button-new", 
		TRUE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
		"xa-button-close", 
		TRUE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
		"xa-button-cancel", 
		FALSE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
		"xa-button-remove", 
		TRUE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
		"xa-button-add-file", 
		TRUE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
		"xa-button-add-folder", 
		TRUE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
		"xa-button-extract", 
		TRUE);
	xa_main_window_set_statusbar_value(XA_MAIN_WINDOW(main_window), _("Done"));
	xa_main_window_set_progressbar_value(XA_MAIN_WINDOW(main_window), -1);
}

void
xa_archive_modified(GObject *object, gpointer data)
{
	XAArchive *archive = data;
	if(object == G_OBJECT(xa_support))
	{
		if(xa_sub_archive)
		{
			GSList *files = g_slist_alloc();
			files->data = xa_archive->path;
			xa_sub_support->add(xa_sub_support, xa_sub_archive, files);
			g_slist_free(files);
		} 
		xa_support->open(xa_support, xa_archive);
	}
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
		"xa-button-cancel", 
		FALSE);
}

void
xa_cancel_operation(GtkWidget *widget, gpointer data)
{
	xa_support_cancel(xa_support);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
		"xa-button-open", 
		TRUE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
		"xa-button-new", 
		TRUE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
		"xa-button-close", 
		TRUE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
		"xa-button-cancel", 
		FALSE);
	xa_main_window_set_statusbar_value(XA_MAIN_WINDOW(main_window), "Cancelled");
}

void
xa_show_about(GtkWidget *widget, gpointer data)
{
	GtkWidget *dialog;
	dialog = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "TODO");
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

void
xa_extract_archive(GtkWidget *widget, gpointer folder, gpointer files)
{
 	if((xa_archive) && (xa_support))
 	{
 		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
 			"xa-button-cancel", 
 			TRUE);
		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
			"xa-button-open", 
			FALSE);
		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
			"xa-button-remove", 
			FALSE);
		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
			"xa-button-add-file", 
			FALSE);
		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
			"xa-button-add-folder", 
			FALSE);
		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
			"xa-button-new", 
			FALSE);
		xa_main_window_set_statusbar_value(XA_MAIN_WINDOW(main_window), _("Extracting archive"));
		xa_main_window_set_progressbar_value(XA_MAIN_WINDOW(main_window), 101); 
 		xa_support->extract(xa_support, xa_archive, folder, files, FALSE);
	}

}
void
xa_close_archive(GtkWidget *widget, gpointer data)
{
	xa_main_window_clear_list(XA_MAIN_WINDOW(main_window), TRUE);

	if(xa_archive)
		g_object_unref(xa_archive);
	if(xa_sub_archive)
		g_object_unref(xa_sub_archive);
	xa_archive = NULL;
	xa_sub_archive = NULL;
	xa_support = NULL;
	xa_sub_support = NULL;

	if ( g_file_test ("/tmp/xarchiver.tmp",G_FILE_TEST_EXISTS) )
		unlink ("/tmp/xarchiver.tmp");
	xa_main_window_set_property_window(XA_MAIN_WINDOW(main_window), NULL);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), "xa-button-test", FALSE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), "xa-button-extract", FALSE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), "xa-button-add-folder", FALSE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), "xa-button-add-file", FALSE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), "xa-button-cancel", FALSE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), "xa-button-close", FALSE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), "xa-button-remove", FALSE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), "xa-button-properties", FALSE);
}

void
xa_open_archive(GtkWidget *widget, gpointer data)
{
	gchar *filename = data;
	if(xa_archive)
		g_object_unref(xa_archive);
	if(xa_sub_archive)
		g_object_unref(xa_sub_archive);
	xa_archive = xarchiver_archive_new(filename, XARCHIVETYPE_UNKNOWN);
	if(xa_archive == NULL)
	{
		gchar *_filename = g_path_get_basename(filename);
		GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
				GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_MESSAGE_ERROR,
				GTK_BUTTONS_CLOSE,
				_("File \"%s\" could not be opened, archive not supported\n"),
				_filename);
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
		return;
	}
	else
	{
		xa_main_window_set_statusbar_value(XA_MAIN_WINDOW(main_window), _("Opening archive..."));
		if((xa_archive->type == XARCHIVETYPE_BZIP2) || (xa_archive->type == XARCHIVETYPE_GZIP))
		{
			xa_sub_archive = xa_archive;
			xa_support = xarchiver_find_archive_support(xa_sub_archive);
			xa_support->extract(xa_support, xa_sub_archive, "/tmp/xarchiver.tmp", NULL, FALSE);
			xa_archive = NULL;
			xa_archive = xarchiver_archive_new("/tmp/xarchiver.tmp", XARCHIVETYPE_UNKNOWN);
			if(!xa_archive)
			{
				xa_archive = xa_sub_archive;
				xa_sub_archive = NULL;
			}
			else
				xa_sub_support = xarchiver_find_archive_support(xa_sub_archive);
		}
	}
	if(xa_archive)
	{
		xa_support = xarchiver_find_archive_support(xa_archive);
		xa_support->open(xa_support, xa_archive);
		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
			"xa-button-cancel", 
			TRUE);
		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
			"xa-button-open", 
			FALSE);
		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
			"xa-button-new", 
			FALSE);
		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
			"xa-button-extract", 
			FALSE);
		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
			"xa-button-add-file", 
			FALSE);
		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
			"xa-button-add-folder", 
			FALSE);
		
		if(xa_archive->has_passwd)
			xa_main_window_set_widget_visible(XA_MAIN_WINDOW(main_window), "xa-passwd", TRUE);
		else
			xa_main_window_set_widget_visible(XA_MAIN_WINDOW(main_window), "xa-passwd", FALSE);

	}
}

void
xa_add_files(GtkWidget *widget, gpointer data)
{
 	if((xa_archive) && (xa_support))
 	{
 		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
 			"xa-button-cancel", 
 			TRUE);
		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
			"xa-button-open", 
			FALSE);
		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
			"xa-button-remove", 
			FALSE);
		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
			"xa-button-add-file", 
			FALSE);
		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
			"xa-button-add-folder", 
			FALSE);
		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
			"xa-button-new", 
			FALSE);
		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
			"xa-button-extract", 
			FALSE);
 		xa_support->add(xa_support, xa_archive, data);
	}
}

void
xa_remove_files(GtkWidget *widget, gpointer data)
{
 	if((xa_archive) && (xa_support))
 	{
 		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
 			"xa-button-cancel", 
 			TRUE);
		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
			"xa-button-open", 
			FALSE);
		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
			"xa-button-remove", 
			FALSE);
		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
			"xa-button-add-file", 
			FALSE);
		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
			"xa-button-add-folder", 
			FALSE);
		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
			"xa-button-extract", 
			FALSE);
		xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), 
			"xa-button-new", 
			FALSE);
 		xa_support->remove(xa_support, xa_archive, data);
	}
}


int main(int argc, char **argv)
{
	GError *error = NULL;
	gchar **filenames = NULL;
	g_type_init();
	xarchiver_init();
	GOptionEntry options[] = {
		{ "add-file", 'a', 0, G_OPTION_ARG_FILENAME_ARRAY, &filenames, _("Open Archive"), NULL},
		{ NULL }
	};

	GOptionGroup *gtk_options = gtk_get_option_group(FALSE);

	GOptionContext *ctx = g_option_context_new(_("archiving app"));

	g_option_context_add_group(ctx, gtk_options);
	g_option_context_add_main_entries(ctx, options, "xarchiver options");

	/* parse gtk+ options */
	gtk_init(&argc, &argv);
	
	/* parse g_option options */
	g_option_context_parse(ctx, &argc, &argv, NULL);
	g_option_context_free(ctx);

	/* use remaining option as filename to open */


	main_window = xa_main_window_new();
	gtk_widget_set_size_request(main_window, 620, 400);
	GtkWidget *prop_dialog = xa_property_dialog_new(GTK_WINDOW(main_window));
	xa_property_dialog_add_property(XA_PROPERTY_DIALOG(prop_dialog), "filename", "/etc/passwd");

	g_signal_connect(G_OBJECT(main_window), "destroy", gtk_main_quit, NULL);
	//g_signal_connect(G_OBJECT(main_window), "xa_new_archive", G_CALLBACK(open_archive), NULL);
	g_signal_connect(G_OBJECT(main_window), "xa_open_archive", G_CALLBACK(xa_open_archive), NULL);
	//g_signal_connect(G_OBJECT(main_window), "xa_test_archive", G_CALLBACK(xa_test_archive), NULL);
	g_signal_connect(G_OBJECT(main_window), "xa_close_archive", G_CALLBACK(xa_close_archive), NULL);
	g_signal_connect(G_OBJECT(main_window), "xa_extract_archive", G_CALLBACK(xa_extract_archive), NULL);
	g_signal_connect(G_OBJECT(main_window), "xa_add_folders", G_CALLBACK(xa_add_files), NULL);
	g_signal_connect(G_OBJECT(main_window), "xa_add_files", G_CALLBACK(xa_add_files), NULL);
	g_signal_connect(G_OBJECT(main_window), "xa_remove_files", G_CALLBACK(xa_remove_files), NULL);
	g_signal_connect(G_OBJECT(main_window), "xa_cancel_operation", G_CALLBACK(xa_cancel_operation), NULL);
	g_signal_connect(G_OBJECT(main_window), "xa_show_about", G_CALLBACK(xa_show_about), NULL);
		

	xarchiver_support_connect("xa_rows_updated", G_CALLBACK(xa_update_rows));
	xarchiver_support_connect("xa_rows_appended", G_CALLBACK(xa_append_rows));
	xarchiver_support_connect("xa_archive_modified", G_CALLBACK(xa_archive_modified));
	xarchiver_support_connect("xa_operation_complete", G_CALLBACK(xa_operation_complete));

	gtk_widget_show_all(main_window);
	
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), "xa-button-test", FALSE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), "xa-button-extract", FALSE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), "xa-button-add-folder", FALSE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), "xa-button-add-file", FALSE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), "xa-button-cancel", FALSE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), "xa-button-close", FALSE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), "xa-button-remove", FALSE);
	xa_main_window_set_widget_sensitive(XA_MAIN_WINDOW(main_window), "xa-button-properties", FALSE);

	xa_main_window_set_statusbar_value(XA_MAIN_WINDOW(main_window), PACKAGE_STRING);
	//xa_main_window_set_property_window(XA_MAIN_WINDOW(main_window), XA_PROPERTY_DIALOG(prop_dialog));
	
	gtk_main();
	if ( g_file_test ("/tmp/xarchiver.tmp",G_FILE_TEST_EXISTS) )
		unlink ("/tmp/xarchiver.tmp");

	xarchiver_destroy();
	return 0;
}
