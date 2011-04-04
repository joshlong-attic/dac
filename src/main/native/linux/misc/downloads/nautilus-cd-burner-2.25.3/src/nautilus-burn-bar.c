/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2005 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include "nautilus-burn-bar.h"

static void nautilus_burn_bar_finalize   (GObject *object);

#define NAUTILUS_BURN_BAR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NAUTILUS_TYPE_BURN_BAR, NautilusBurnBarPrivate))

struct NautilusBurnBarPrivate
{
        GtkTooltips *tooltips;
        GtkWidget   *button;
};

enum {
       ACTIVATE,
       LAST_SIGNAL
};

static guint           signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (NautilusBurnBar, nautilus_burn_bar, GTK_TYPE_HBOX)

GtkWidget *
nautilus_burn_bar_get_button (NautilusBurnBar *bar)
{
        GtkWidget *button;

        g_return_val_if_fail (bar != NULL, NULL);

        button = bar->priv->button;

        return button;
}

static void
nautilus_burn_bar_set_property (GObject            *object,
                                guint               prop_id,
                                const GValue       *value,
                                GParamSpec         *pspec)
{
        NautilusBurnBar *self;

        self = NAUTILUS_BURN_BAR (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
nautilus_burn_bar_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
        NautilusBurnBar *self;

        self = NAUTILUS_BURN_BAR (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
nautilus_burn_bar_class_init (NautilusBurnBarClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize     = nautilus_burn_bar_finalize;
        object_class->get_property = nautilus_burn_bar_get_property;
        object_class->set_property = nautilus_burn_bar_set_property;

        g_type_class_add_private (klass, sizeof (NautilusBurnBarPrivate));

        signals [ACTIVATE] = g_signal_new ("activate",
                                           G_TYPE_FROM_CLASS (klass),
                                           G_SIGNAL_RUN_LAST,
                                           G_STRUCT_OFFSET (NautilusBurnBarClass, activate),
                                           NULL, NULL,
                                           g_cclosure_marshal_VOID__VOID,
                                           G_TYPE_NONE, 0);

}

static void
button_clicked_cb (GtkWidget       *button,
                   NautilusBurnBar *bar)
{
        g_signal_emit (bar, signals [ACTIVATE], 0);
}

static void
nautilus_burn_bar_init (NautilusBurnBar *bar)
{
        GtkWidget   *label;
        GtkWidget   *hbox;
        GtkWidget   *vbox;
        GtkWidget   *image;
        char        *hint;

        bar->priv = NAUTILUS_BURN_BAR_GET_PRIVATE (bar);

        bar->priv->tooltips = gtk_tooltips_new ();
        g_object_ref (bar->priv->tooltips);
        gtk_object_sink (GTK_OBJECT (bar->priv->tooltips));

        hbox = GTK_WIDGET (bar);

        vbox = gtk_vbox_new (FALSE, 6);
        gtk_widget_show (vbox);
        gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);

        label = gtk_label_new (_("CD/DVD Creator Folder"));
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_widget_show (label);
        gtk_box_pack_start (GTK_BOX (vbox), label, TRUE, TRUE, 0);

        label = gtk_label_new ("");
        hint = g_strdup_printf ("<i>%s</i>", _("Drag files to write to disc here"));
        gtk_label_set_markup (GTK_LABEL (label), hint);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_widget_show (label);
        gtk_box_pack_start (GTK_BOX (vbox), label, TRUE, TRUE, 0);

        bar->priv->button = gtk_button_new_with_label (_("Write to Disc"));
        gtk_widget_show (bar->priv->button);
        gtk_box_pack_end (GTK_BOX (hbox), bar->priv->button, FALSE, FALSE, 0);

        image = gtk_image_new_from_icon_name ("nautilus-cd-burner", GTK_ICON_SIZE_BUTTON);
        gtk_widget_show (image);
        gtk_button_set_image (GTK_BUTTON (bar->priv->button), image);

        g_signal_connect (bar->priv->button, "clicked",
                          G_CALLBACK (button_clicked_cb),
                          bar);

        gtk_tooltips_set_tip (GTK_TOOLTIPS (bar->priv->tooltips),
                              bar->priv->button,
                              _("Write contents to a CD or DVD disc"),
                              NULL);
}

static void
nautilus_burn_bar_finalize (GObject *object)
{
        NautilusBurnBar *bar;

        g_return_if_fail (object != NULL);
        g_return_if_fail (NAUTILUS_IS_BURN_BAR (object));

        bar = NAUTILUS_BURN_BAR (object);

        g_return_if_fail (bar->priv != NULL);

        if (bar->priv->tooltips != NULL) {
                g_object_unref (bar->priv->tooltips);
        }

        G_OBJECT_CLASS (nautilus_burn_bar_parent_class)->finalize (object);
}

GtkWidget *
nautilus_burn_bar_new (void)
{
        GObject *result;

        result = g_object_new (NAUTILUS_TYPE_BURN_BAR,
                               NULL);

        return GTK_WIDGET (result);
}
