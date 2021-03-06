/*
 *      This file is part of GPaste.
 *
 *      Copyright 2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <gpaste-ui-item.h>
#include <gpaste-util.h>

struct _GPasteUiItem
{
    GPasteUiItemSkeleton parent_instance;
};

typedef struct
{
    GPasteClient   *client;

    GtkLabel       *index_label;
    GtkLabel       *label;
    guint64         index;
    gboolean        bold;

    gulong          size_id;
} GPasteUiItemPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiItem, g_paste_ui_item, G_PASTE_TYPE_UI_ITEM_SKELETON)

/**
 * g_paste_ui_item_activate:
 * @self: a #GPasteUiItem instance
 *
 * Refresh the item
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_ui_item_activate (GPasteUiItem *self)
{
    g_return_if_fail (G_PASTE_IS_UI_ITEM (self));

    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);

    g_paste_client_select (priv->client, priv->index, NULL, NULL);
}

/**
 * g_paste_ui_item_refresh:
 * @self: a #GPasteUiItem instance
 *
 * Refresh the item
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_ui_item_refresh (GPasteUiItem *self)
{
    g_return_if_fail (G_PASTE_IS_UI_ITEM (self));

    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);

    g_paste_ui_item_set_index (self, priv->index);
}

static void
g_paste_ui_item_on_text_ready (GObject      *source_object G_GNUC_UNUSED,
                               GAsyncResult *res,
                               gpointer      user_data)
{
    GPasteUiItem *self = user_data;
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);
    g_autoptr (GError) error = NULL;
    g_autofree gchar *txt = g_paste_client_get_element_finish (priv->client, res, &error);

    if (!txt || error)
        return;

    g_autofree gchar *oneline = g_paste_util_replace (txt, "\n", " ");

    if (priv->bold)
    {
        g_autofree gchar *markup = g_markup_printf_escaped ("<b>%s</b>", oneline);
        g_paste_ui_item_skeleton_set_markup (G_PASTE_UI_ITEM_SKELETON (self), markup);
    }
    else
    {
        g_paste_ui_item_skeleton_set_text (G_PASTE_UI_ITEM_SKELETON (self), oneline);
    }
}

static void
g_paste_ui_item_on_kind_ready (GObject      *source_object G_GNUC_UNUSED,
                               GAsyncResult *res,
                               gpointer      user_data)
{
    GPasteUiItem *self = user_data;
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);
    g_autoptr (GError) error = NULL;
    GPasteItemKind kind = g_paste_client_get_element_kind_finish (priv->client, res, &error);

    if (!kind || error)
        return;

    GPasteUiItemSkeleton *sk = G_PASTE_UI_ITEM_SKELETON (self);

    g_paste_ui_item_skeleton_set_editable (sk, kind == G_PASTE_ITEM_KIND_TEXT);
    g_paste_ui_item_skeleton_set_uploadable (sk, kind == G_PASTE_ITEM_KIND_TEXT);
}

static void
g_paste_ui_item_reset_text (GPasteUiItem *self)
{
    g_return_if_fail (G_PASTE_IS_UI_ITEM (self));

    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);

    g_paste_client_get_element (priv->client, priv->index, g_paste_ui_item_on_text_ready, self);
    g_paste_client_get_element_kind (priv->client, priv->index, g_paste_ui_item_on_kind_ready, self);
}

/**
 * g_paste_ui_item_set_index:
 * @self: a #GPasteUiItem instance
 * @index: the index of the corresponding item
 *
 * Track a new index
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_ui_item_set_index (GPasteUiItem *self,
                           guint64       index)
{
    g_return_if_fail (G_PASTE_IS_UI_ITEM (self));
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);

    g_paste_ui_item_skeleton_set_index (G_PASTE_UI_ITEM_SKELETON (self), index);

    guint64 old_index = priv->index;
    priv->index = index;

    if (!index)
        priv->bold = TRUE;
    else if (!old_index)
        priv->bold = FALSE;

    if (index != (guint64)-1)
    {
        g_paste_ui_item_reset_text (self);
        gtk_widget_show (GTK_WIDGET (self));
    }
    else
    {
        gtk_label_set_text (priv->label, "");
        gtk_widget_hide (GTK_WIDGET (self));
    }
}

static void
g_paste_ui_item_dispose (GObject *object)
{
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (G_PASTE_UI_ITEM (object));

    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_ui_item_parent_class)->dispose (object);
}

static void
g_paste_ui_item_class_init (GPasteUiItemClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_item_dispose;
}

static void
g_paste_ui_item_init (GPasteUiItem *self)
{
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);

    priv->index = -1;
}

/**
 * g_paste_ui_item_new:
 * @client: a #GPasteClient instance
 * @settings: a #GPasteSettings instance
 * @rootwin: the root #GtkWindow
 * @index: the index of the corresponding item
 *
 * Create a new instance of #GPasteUiItem
 *
 * Returns: a newly allocated #GPasteUiItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_item_new (GPasteClient   *client,
                     GPasteSettings *settings,
                     GtkWindow      *rootwin,
                     guint64         index)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    GtkWidget *self = g_paste_ui_item_skeleton_new (G_PASTE_TYPE_UI_ITEM, client, settings, rootwin);
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (G_PASTE_UI_ITEM (self));

    priv->client = g_object_ref (client);

    g_paste_ui_item_set_index (G_PASTE_UI_ITEM (self), index);

    return self;
}
