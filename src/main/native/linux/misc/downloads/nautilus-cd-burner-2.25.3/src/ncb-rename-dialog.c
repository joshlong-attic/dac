/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Copyright (C) 2006 Fabio Bonelli <fabiobonelli@libero.it>
 *
 *  ncb-rename-dialog: Provide a dialog to rename files when their
 *  name is invalid (eg. not encoded properly, too long, etc.).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: Fabio Bonelli <fabiobonelli@libero.it>
 */

#include "config.h"

#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <glade/glade.h>
#include <libgnomeui/libgnomeui.h>

#include "ncb-rename-dialog.h"

static void     ncb_rename_dialog_class_init (NcbRenameDialogClass	*klass);
static void     ncb_rename_dialog_init       (NcbRenameDialog		*dialog);
static void     ncb_rename_dialog_finalize   (GObject			*object);

#define NCB_RENAME_DIALOG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NCB_TYPE_RENAME_DIALOG, NcbRenameDialogPrivate))

struct NcbRenameDialogInvalidFile {
	char	*mime_type;
	char	*filename;
};
typedef struct NcbRenameDialogInvalidFile NcbRenameDialogInvalidFile;

struct NcbRenameDialogPrivate
{
	GladeXML        *xml;

	/* View and model for the list
	 * of files */
	GtkWidget	*view;
	GtkListStore	*store;

	/* Contextual popup menu */
	GtkWidget	*menu;
	GtkWidget	*rename_item;

	/* List of NcbRenameDialogInvalidFile invalid filenames */
	GSList		*invalid_files;

	char		*root_path;
};

/* List store columns */
enum {
	NCB_TREESTORE_PIXBUF,
	NCB_TREESTORE_OLDNAME,
	NCB_TREESTORE_NEWNAME
};

G_DEFINE_TYPE (NcbRenameDialog, ncb_rename_dialog, GTK_TYPE_DIALOG)

static void
ncb_rename_dialog_class_init (NcbRenameDialogClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = ncb_rename_dialog_finalize;

	g_type_class_add_private (klass, sizeof (NcbRenameDialogPrivate));
}

/**
 * treemodel_file_move_cb
 * @model: The #GtkTreeModel
 * @tree_path: The #GtkTreePath
 * @iter: #GtkTreeIter for the file
 *
 * Callback for gtk_tree_model_foreach().
 * Takes the row referred by @iter in the invalid files' model and
 * moves the file in burn:// associated with it to its valid name.
 *
 * Returns: %FALSE
 **/
static gboolean
treemodel_file_move_cb (GtkTreeModel	*model,
			GtkTreePath	*tree_path,
			GtkTreeIter	*iter,
			gpointer	 data)
{
	char		*source_uri = NULL;
	char		*dest_uri;
	char		*path_uri;
	GFile           *source_file;
	GFile           *dest_file;
	char		*shortname  = NULL;
	NcbRenameDialog	*dialog;
	gboolean	 res;
	GError          *error;

	dialog = (NcbRenameDialog *) data;

	gtk_tree_model_get (model, iter,
			    NCB_TREESTORE_OLDNAME, &source_uri,
			    NCB_TREESTORE_NEWNAME, &shortname, -1);

	/* Build @dest_uri with a full path */
	path_uri = g_path_get_dirname (source_uri);
	dest_uri = g_strdup_printf ("%s/%s", path_uri, shortname);
	g_free (shortname);
	g_free (path_uri);

	source_file = g_file_new_for_uri (source_uri);
	dest_file = g_file_new_for_uri (dest_uri);

	error = NULL;
	res = g_file_move (source_file,
			   dest_file,
			   G_FILE_COPY_NONE,
			   NULL,
			   NULL,
			   NULL,
			   &error);
	if (! res) {
		g_warning ("Cannot move `%s' to `%s': %s",
			   source_uri,
			   dest_uri,
			   error->message);
		g_error_free (error);
	}

	g_free (source_uri);
	g_free (dest_uri);

	g_object_unref (source_file);
	g_object_unref (dest_file);

	/* Done, but call me again for other rows.*/
	return FALSE;
}


/**
 * ncb_rename_dialog_response_cb
 * @dialog: The #NcbRenameDialog
 * @id: Dialog's response
 * @user_data: unused
 *
 * Convenience response callback for #NcbRenameDialog.
 * Once called it renames the files in burn:// if the response
 * is a GTK_RESPONSE_OK.
 **/
void
ncb_rename_dialog_response_cb (NcbRenameDialog *dialog,
			       int		id,
			       gpointer		user_data)
{
	g_return_if_fail (dialog != NULL);
	g_return_if_fail (NCB_IS_RENAME_DIALOG (dialog));

	if (id == GTK_RESPONSE_OK) {
		gtk_tree_model_foreach (GTK_TREE_MODEL (dialog->priv->store),
					treemodel_file_move_cb,
					NULL);
	}
}


/**
 * cell_editing_done_cb
 * @renderer: The #GtkCellRendererText
 * @tree_path: String representation of the #GtkTreePath
 * @new_string: New name for the file
 * @data: Pointer to the #NcbRenameDialog
 *
 * Callback for the "edited" signal emitted by the text
 * renderer.
 * It updates the name in the treemodel with the name typed in.
 **/
static void
cell_editing_done_cb (GtkCellRendererText	*renderer,
		      char			*tree_path,
		      char			*new_string,
		      gpointer			 data)
{
	NcbRenameDialog *dialog;
	GtkTreeIter	 iter;
	int		 r;

	g_return_if_fail (strlen (new_string) != 0);

	dialog = (NcbRenameDialog *) data;

	r = gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (dialog->priv->store),
						 &iter, tree_path);
	g_return_if_fail (r);

	gtk_list_store_set (dialog->priv->store, &iter,
			    NCB_TREESTORE_NEWNAME, new_string, -1);
}

/**
 * rename_menu_item_cb
 * @widget: #GtkWidget that receives the signal
 * @user_data: Pointer to the #NcbRenameDialog
 *
 * Callback for the "activate" signal emitted by the rename popup menu item.
 * Updates the desired new name for the files represented by
 * the selected row. We disable that menu item when multiple rows are
 * selected.
 **/
static void
rename_menu_item_cb (GtkWidget *widget,
		     gpointer   user_data)
{
	NcbRenameDialog		*dialog;
	GtkTreeSelection	*selection;
	GtkListStore		*store;
	GtkTreeViewColumn	*column;
	GtkTreePath		*path;
	GList			*list;

	dialog = (NcbRenameDialog *) user_data;

	store = GTK_LIST_STORE (dialog->priv->store);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (dialog->priv->view));

	list = gtk_tree_selection_get_selected_rows (selection, NULL);
	if (g_list_length (list) != 1) {

		/* This shouldn't happen. Rename makes no sense if more than
		 * one item is selected. */
		g_warning ("More than one row selected during a rename?");
	}

	path = (GtkTreePath *) list->data;

	column = gtk_tree_view_get_column (GTK_TREE_VIEW (dialog->priv->view), 0);
	g_assert (column != NULL);

	/* Start editing the cell */
	gtk_tree_view_set_cursor (GTK_TREE_VIEW (dialog->priv->view),
				  path, column, TRUE);

	g_list_foreach (list, (GFunc)gtk_tree_path_free, NULL);
	g_list_free (list);
}

/**
 * delete_menu_item_cb
 * @widget: #GtkWidget that receives the signal
 * @user_data: Pointer to The #NcbRenameDialog
 *
 * Callback for the "activate" signal emitted by the delete popup menu item.
 * Removes the file represented by the selected row(s) from burn://.
 **/
static void
delete_menu_item_cb (GtkWidget *widget,
		     gpointer   user_data)
{
	NcbRenameDialog		*dialog;
	GtkTreeSelection	*selection;
	GtkTreeModel		*model;
	GList			*list;
	GList			*row_references = NULL;
	GList			*tmp;

	dialog = (NcbRenameDialog *) user_data;

	model = GTK_TREE_MODEL (dialog->priv->store);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (dialog->priv->view));

	list = gtk_tree_selection_get_selected_rows (selection, NULL);

	/* Build a GtkTreeRowReference list out of the GtkTreePath list 'cause
	 * we are going to modify the tree */
	for (tmp = list; tmp; tmp = g_list_next (tmp)) {
		GtkTreeRowReference *reference;

		reference = gtk_tree_row_reference_new (model, (GtkTreePath *)tmp->data);

		row_references = g_list_append (row_references, reference);
	}
	g_list_foreach (list, (GFunc)gtk_tree_path_free, NULL);
	g_list_free (list);


	/* Remove the selected rows and relative files */
	for (tmp = row_references; tmp; tmp = g_list_next (tmp)) {
		GtkTreePath	*path;
		GtkTreeIter	 iter;

		path = gtk_tree_row_reference_get_path ((GtkTreeRowReference *)tmp->data);

		if (gtk_tree_model_get_iter (model, &iter, path)) {
			char		*name = NULL;
			gboolean	 res;
			GFile           *file;
			GError          *error;

			gtk_tree_model_get (model,
					    &iter,
					    NCB_TREESTORE_OLDNAME,
					    &name,
					    -1);

			file = g_file_new_for_uri (name);
			error = NULL;
			res = g_file_delete (file, NULL, &error);
			if (res) {
				gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
			} else {
				g_warning ("Cannot remove %s (%s)",
					   name,
					   error->message);
				g_error_free (error);
			}
			g_free (name);
			g_object_unref (file);
		}
	}
	g_list_foreach (row_references, (GFunc)gtk_tree_row_reference_free, NULL);
	g_list_free (row_references);
}

/**
 * show_popup_menu
 * @dialog: the #NcbRenameDialog.
 * @event: #GdkEventButton that triggered the dialog, it can be NULL.
 * @show_rename: wheter to show the rename menu item.
 *
 * Shows the contextual menu.
 **/
static void
show_popup_menu (NcbRenameDialog *dialog,
		 GdkEventButton  *event,
		 gboolean	  show_rename)
{
	GtkWidget *menu;
	GtkWidget *rename_item;

	menu = dialog->priv->menu;
	rename_item = dialog->priv->rename_item;

	gtk_widget_set_sensitive (rename_item, show_rename);

	gtk_widget_show_all (menu);

	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL,
			(event != NULL) ? event->button : 0,
			gdk_event_get_time ((GdkEvent *)event));
}

/**
 * treeview_button_pressed_cb
 * @treeview: #GtkWidget
 * @event: #GdkEventButton
 * @user_data: Pointer to the #NcbRenameDialog
 *
 * Callback for the "button-press-event" signal emitted by the treeview.
 * On right click, it selects the row under the cursor if no one is
 * selected and pops up the contextual menu.
 *
 * Returns: %TRUE to stop other handlers from being invoked when we show the menu.
 * %FALSE otherwise, in order to propagate the event further.
 **/
static gboolean
treeview_button_pressed_cb (GtkWidget		*treeview,
			    GdkEventButton	*event,
			    gpointer		 user_data)
{
	GtkTreeSelection *selection;
	NcbRenameDialog  *dialog;

	if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
		int rows;

		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));

		/* Select the row under the cursor if there is no other row selected
		 * or if there is another one selected */
		rows = gtk_tree_selection_count_selected_rows (selection);
		if (rows <= 1) {
			GtkTreePath *path;

			/* Get tree path for row that was clicked */
			if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (treeview),
							  (gint) event->x,
							  (gint) event->y,
							  &path, NULL, NULL, NULL)) {

				gtk_tree_selection_unselect_all (selection);
				gtk_tree_selection_select_path (selection, path);
				gtk_tree_path_free (path);

				rows = 1;
			}
		}

		dialog = (NcbRenameDialog *) user_data;

		/* Show the popup menu and show the "rename" menu item only
		 * if we selected just a single row */
		show_popup_menu (dialog, event, (rows == 1));

		return TRUE;
	}

	return FALSE;
}

/**
 * treeview_popup_menu_cb
 * @widget: #GtkWidget
 * @user_data: Pointer to the #NcbRenameDialog
 *
 * Callback for the "popup-menu" signal.
 * Pops up the contextual menu.
 **/
static void
treeview_popup_menu_cb (GtkWidget	*widget,
			gpointer	 user_data)
{
	NcbRenameDialog *dialog;

	dialog = (NcbRenameDialog *) user_data;

	show_popup_menu (dialog, NULL, TRUE);
}

static void
ncb_rename_dialog_init (NcbRenameDialog *dialog)
{
	GtkWidget		*vbox;
	GtkWidget		*button;
	GtkTreeSelection	*selection;
	GtkCellRenderer		*renderer;
	GtkCellRenderer		*pixbuf_renderer;
	GtkTreeViewColumn	*newname;

	GtkWidget		*rename_item;
	GtkWidget		*delete_item;

	dialog->priv = NCB_RENAME_DIALOG_GET_PRIVATE (dialog);

	gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);
        gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
	gtk_window_set_title (GTK_WINDOW (dialog), "");
	gtk_window_set_icon_name (GTK_WINDOW (dialog), "nautilus-cd-burner");

	dialog->priv->xml = glade_xml_new (DATADIR "/nautilus-cd-burner.glade", "rename_dialog_vbox", NULL);

	vbox = glade_xml_get_widget (dialog->priv->xml, "rename_dialog_vbox");
        gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
                            vbox,
                            TRUE, TRUE, 0);
        gtk_widget_show_all (vbox);

	gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
	button = gtk_dialog_add_button (GTK_DIALOG (dialog), _("C_ontinue"), GTK_RESPONSE_OK);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_BUTTONS_CANCEL);

	/* Set list store and tree view */
	dialog->priv->store = gtk_list_store_new (3, G_TYPE_OBJECT, G_TYPE_STRING, G_TYPE_STRING);

	dialog->priv->view = glade_xml_get_widget (dialog->priv->xml, "rename_dialog_treeview");
	gtk_tree_view_set_model (GTK_TREE_VIEW (dialog->priv->view), GTK_TREE_MODEL (dialog->priv->store));

	g_signal_connect (dialog->priv->view, "button-press-event",
			  G_CALLBACK (treeview_button_pressed_cb), dialog);
	g_signal_connect (dialog->priv->view, "popup-menu",
			  G_CALLBACK (treeview_popup_menu_cb), dialog);

	/* Setup selection mode */
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (dialog->priv->view));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);

	/* Icon renderer */
	pixbuf_renderer = gtk_cell_renderer_pixbuf_new ();

	/* Filename renderer */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (G_OBJECT (renderer), "editable", TRUE, NULL);
	g_signal_connect (G_OBJECT (renderer), "edited", G_CALLBACK (cell_editing_done_cb), dialog);

	/* Column */
	newname = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_title (newname, _("Filename"));

	gtk_tree_view_column_pack_start (newname, pixbuf_renderer, FALSE);
	gtk_tree_view_column_set_attributes (newname, pixbuf_renderer,
					     "pixbuf", NCB_TREESTORE_PIXBUF, NULL);
	gtk_tree_view_column_pack_start (newname, renderer, FALSE);
	gtk_tree_view_column_set_attributes (newname, renderer,
					     "text", NCB_TREESTORE_NEWNAME, NULL);

	/* Append column to the view */
	gtk_tree_view_append_column (GTK_TREE_VIEW (dialog->priv->view), newname);


	/** Set popup menu **/
	dialog->priv->menu = gtk_menu_new ();

	rename_item = gtk_menu_item_new_with_label (_("Rename file"));
	g_signal_connect (rename_item, "activate",
			  G_CALLBACK (rename_menu_item_cb), dialog);

	delete_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_REMOVE,
							  gtk_accel_group_new ());
	g_signal_connect (delete_item, "activate",
			  G_CALLBACK (delete_menu_item_cb), dialog);

	gtk_menu_shell_append (GTK_MENU_SHELL (dialog->priv->menu), rename_item);
	gtk_menu_shell_append (GTK_MENU_SHELL (dialog->priv->menu), delete_item);

	dialog->priv->rename_item = rename_item;

	gtk_widget_show_all (GTK_WIDGET (GTK_DIALOG (dialog)->vbox));
}

/**
 * make_valid_filename
 * @name: the string containing an invalid filename.
 *
 * Return a filename that can be written to a UTF-8 ISO9660 filesystem.
 * Adapted from gnome_vfs_make_valid_utf8().
 *
 * Returns: a newly allocated string with a valid filename based on @name. Free with g_free ().
 **/
static char *
make_valid_filename (const char *name)
{
	GString		*string;
	const char	*remainder;
	const char	*invalid;
	int		 remaining_bytes;
	int		 valid_bytes;

	string = NULL;
	remainder = name;
	remaining_bytes = strlen (name);

	while (remaining_bytes != 0) {
		if (g_utf8_validate (remainder, remaining_bytes, &invalid)) {
			break;
		}

		valid_bytes = invalid - remainder;

		if (string == NULL) {
			string = g_string_sized_new (remaining_bytes);
		}

		g_string_append_len (string, remainder, valid_bytes);
		/* Use the Unicode replacement character (<?>) as substitute
		 * for invalid chars. */
		g_string_append_unichar (string, 0xFFFD);

		remaining_bytes -= valid_bytes + 1;
		remainder = invalid + 1;
	}

	if (string == NULL) {
		return g_strdup (name);
	}

	string = g_string_append (string, remainder);
	g_assert (g_utf8_validate (string->str, -1, NULL));

	return g_string_free (string, FALSE);
}


/**
 * pixmap_from_mime_type
 * @mime_type: the MIME type
 *
 * Get the icon representing a certain MIME type.
 *
 * Returns: #GdkPixbuf icon or NULL. Unref it with g_object_unref().
 **/
static const GdkPixbuf *
pixmap_from_mime_type (const char *mime_type)
{
	GtkIconTheme    *theme;
	GdkPixbuf	*pixbuf;
	char            *icon;

	g_return_val_if_fail (mime_type != NULL, NULL);

	theme = gtk_icon_theme_get_default ();

	icon = gnome_icon_lookup (theme,
				  NULL,
				  NULL,
				  NULL,
				  NULL,
				  mime_type,
				  GNOME_ICON_LOOKUP_FLAGS_NONE,
				  NULL);

	pixbuf = gtk_icon_theme_load_icon (theme,
					   icon,
					   GTK_ICON_SIZE_SMALL_TOOLBAR,
					   0, NULL);

	g_free (icon);

	return pixbuf;
}

/**
 * add_to_liststore
 * @data: gpointer to a #NcbRenameDialogInvalidFile.
 * @user_data: gpointer to the #NcbRenameDialog.
 *
 * Adds a #NcbRenameDialogInvalidFile to the #NcbRenameDialog's
 * list store.
 **/
static void
add_to_liststore (gpointer data,
		  gpointer user_data)
{
	GtkTreeIter			 iter;
	NcbRenameDialogInvalidFile	*invalid_file;
	char				*invalid_basename;
	NcbRenameDialog			*dialog;
	const GdkPixbuf			*pixbuf;
	char				*newname;

	invalid_file = (NcbRenameDialogInvalidFile *) data;
	dialog = (NcbRenameDialog *) user_data;

	/* Use just the basename for the suggested filenames list */
	invalid_basename = g_path_get_basename (invalid_file->filename);
	newname = make_valid_filename (invalid_basename);
	g_free (invalid_basename);

	pixbuf = pixmap_from_mime_type (invalid_file->mime_type);

	gtk_list_store_append (dialog->priv->store, &iter);
	gtk_list_store_set (dialog->priv->store, &iter,
			    NCB_TREESTORE_PIXBUF, pixbuf,
			    NCB_TREESTORE_OLDNAME, invalid_file->filename,
			    NCB_TREESTORE_NEWNAME, newname, -1);

	g_free (newname);
}

/**
 * ncb_rename_dialog_list_free
 * @dialog: the #NcbRenameDialog.
 *
 * Free the @dialog list of invalid files.
 **/
static void
ncb_rename_dialog_list_free (NcbRenameDialog *dialog)
{
	GSList *l;

	for (l = dialog->priv->invalid_files; l; l= g_slist_next (l)) {
		NcbRenameDialogInvalidFile *invalid;

		invalid = (NcbRenameDialogInvalidFile *) l->data;

		g_free (invalid->mime_type);
		g_free (invalid->filename);
		g_free (invalid);
	}
	g_slist_free (dialog->priv->invalid_files);

	dialog->priv->invalid_files = NULL;
}

static gboolean
check_filename_visitor (GFile             *file,
			const char        *rel_path,
			GFileInfo	  *info,
			NcbRenameDialog   *dialog,
			gboolean          *recurse)
{

	if (! g_utf8_validate (g_file_info_get_name (info), -1, NULL)) {
		NcbRenameDialogInvalidFile *invalid_file;

		invalid_file = g_new0 (NcbRenameDialogInvalidFile, 1);
		invalid_file->filename = g_strdup_printf ("%s/%s",
							  dialog->priv->root_path,
							  rel_path);
		invalid_file->mime_type = g_strdup (g_file_info_get_content_type (info));

		dialog->priv->invalid_files = g_slist_prepend (dialog->priv->invalid_files,
							       invalid_file);
	}

	*recurse = (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY);

	/* Call me again. */
	return TRUE;
}


static gboolean
set_invalid_filenames_internal (GFile           *file,
				const char      *prefix,
				NcbRenameDialog *dialog)
{
	GFileEnumerator          *enumerator;
	gboolean                  stop;

	enumerator = g_file_enumerate_children (file,
						G_FILE_ATTRIBUTE_STANDARD_NAME ","
						G_FILE_ATTRIBUTE_STANDARD_TYPE ","
						G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
						G_FILE_ATTRIBUTE_ACCESS_CAN_READ,
						0,
						NULL, NULL);
	if (enumerator == NULL) {
		return FALSE;
	}

	stop = FALSE;
	while (! stop) {
		GFile     *child;
		GFileInfo *info;
                gboolean   recurse;
                char      *rel_path;

		info = g_file_enumerator_next_file (enumerator, NULL, NULL);
		if (info == NULL) {
			break;
                }

		child = g_file_get_child (file, g_file_info_get_name (info));

		if (prefix == NULL) {
			rel_path = g_strdup (g_file_info_get_name (info));
		} else {
			rel_path = g_strconcat (prefix, g_file_info_get_name (info), NULL);
		}

		stop = ! check_filename_visitor (child, rel_path, info, dialog, &recurse);

		if (! stop
		    && recurse
		    && g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
			char *new_prefix;
			if (prefix == NULL) {
				new_prefix = g_strconcat (g_file_info_get_name (info),
							  "/",
                                                          NULL);
			} else {
				new_prefix = g_strconcat (prefix,
							  g_file_info_get_name (info),
							  "/",
							  NULL);
			}

			set_invalid_filenames_internal (child, new_prefix, dialog);

			g_free (new_prefix);

		}
		g_object_unref (child);
		g_free (rel_path);
		g_object_unref (info);
	}
	g_object_unref (enumerator);

	return TRUE;
}

/**
 * ncb_rename_dialog_set_invalid_filenames
 * @dialog: The #NcbRenameDialog
 * @uri: URI folder to scan for invalid filenames
 *
 * Scan @uri folder for invalid filenames (incorrectly encoded, name too long, etc.)
 * and add them to the dialog
 *
 * Returns: a #GHashTable of invalid filenames in the burn folder where keys are
 * invalid filenames and values are suggested valid filenames. NULL if there aren't invalid filenames.
 **/
gboolean
ncb_rename_dialog_set_invalid_filenames (NcbRenameDialog *dialog,
					 const char	 *uri)
{
	GFile   *file;
	gboolean found;
	gboolean res;

	g_return_val_if_fail (dialog != NULL, FALSE);
	g_return_val_if_fail (NCB_IS_RENAME_DIALOG (dialog), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);

	gtk_list_store_clear (dialog->priv->store);

	dialog->priv->root_path = g_strdup (uri);

	file = g_file_new_for_uri (uri);
	res = set_invalid_filenames_internal (file, NULL, dialog);
	g_object_unref (file);

	g_free (dialog->priv->root_path);

	if (! res) {
		g_warning ("detection of invalid filenames failed for %s",
			   uri);

		return FALSE;
	}

	/* Add invalid files to the liststore */
	g_slist_foreach (dialog->priv->invalid_files, add_to_liststore, dialog);
	found = (dialog->priv->invalid_files != NULL);

	ncb_rename_dialog_list_free (dialog);

	return found;
}

/**
 * ncb_rename_dialog_new:
 *
 * Create a new #NcbRenameDialog.
 *
 * Return value: a new #NcbRenameDialog or NULL. Free with gtk_widget_destroy().
 **/
NcbRenameDialog *
ncb_rename_dialog_new (void)
{
	GObject *object;

	object = g_object_new (NCB_TYPE_RENAME_DIALOG, NULL);
	g_assert (object != NULL);

	return NCB_RENAME_DIALOG (object);
}

static void
ncb_rename_dialog_finalize (GObject *object)
{
	NcbRenameDialog *dialog;

	g_return_if_fail (object != NULL);
	g_return_if_fail (NCB_IS_RENAME_DIALOG (object));

	dialog = NCB_RENAME_DIALOG (object);
	g_return_if_fail (dialog->priv != NULL);

	gtk_list_store_clear (dialog->priv->store);

	G_OBJECT_CLASS (ncb_rename_dialog_parent_class)->finalize (object);
}
