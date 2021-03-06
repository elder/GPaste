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

#include <gpaste-ui-item-action.h>

typedef struct
{
    GPasteClient *client;

    guint64       index;
} GPasteUiItemActionPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GPasteUiItemAction, g_paste_ui_item_action, GTK_TYPE_BUTTON)

/**
 * g_paste_ui_item_action_set_index:
 * @self: a #GPasteUiItemAction instance
 * @index: the index of the corresponding item
 *
 * Track a new index
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_ui_item_action_set_index (GPasteUiItemAction *self,
                             guint64         index)
{
    g_return_if_fail (G_PASTE_IS_UI_ITEM_ACTION (self));

    GPasteUiItemActionPrivate *priv = g_paste_ui_item_action_get_instance_private (self);

    priv->index = index;
}

static gboolean
g_paste_ui_item_action_button_press_event (GtkWidget      *widget,
                                           GdkEventButton *event G_GNUC_UNUSED)
{
    GPasteUiItemAction *self = G_PASTE_UI_ITEM_ACTION (widget);
    GPasteUiItemActionPrivate *priv = g_paste_ui_item_action_get_instance_private (self);
    GPasteUiItemActionClass *klass = G_PASTE_UI_ITEM_ACTION_GET_CLASS (self);

    if (klass->activate)
        klass->activate (self, priv->client, priv->index);

    return TRUE;
}

static void
g_paste_ui_item_action_dispose (GObject *object)
{
    GPasteUiItemActionPrivate *priv = g_paste_ui_item_action_get_instance_private (G_PASTE_UI_ITEM_ACTION (object));

    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_ui_item_action_parent_class)->dispose (object);
}

static void
g_paste_ui_item_action_class_init (GPasteUiItemActionClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_item_action_dispose;
    GTK_WIDGET_CLASS (klass)->button_press_event = g_paste_ui_item_action_button_press_event;
}

static void
g_paste_ui_item_action_init (GPasteUiItemAction *self)
{
    GPasteUiItemActionPrivate *priv = g_paste_ui_item_action_get_instance_private (self);

    priv->index = -1;
}

/**
 * g_paste_ui_item_action_new:
 * @type: the type of the subclass to instantiate
 * @client: a #GPasteClient
 * @icon_name: the name of the icon to use
 * @tooltip: the tooltip to display
 *
 * Create a new instance of #GPasteUiItemAction
 *
 * Returns: a newly allocated #GPasteUiItemAction
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_item_action_new (GType         type,
                            GPasteClient *client,
                            const gchar  *icon_name,
                            const gchar  *tooltip)
{
    g_return_val_if_fail (g_type_is_a (type, G_PASTE_TYPE_UI_ITEM_ACTION), NULL);
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);

    GtkWidget *self = gtk_widget_new (type, NULL);
    GPasteUiItemActionPrivate *priv = g_paste_ui_item_action_get_instance_private (G_PASTE_UI_ITEM_ACTION (self));
    GtkWidget *icon = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_MENU);

    priv->client = g_object_ref (client);

    gtk_widget_set_tooltip_text (self, tooltip);
    gtk_widget_set_margin_start (icon, 5);
    gtk_widget_set_margin_end (icon, 5);

    gtk_container_add (GTK_CONTAINER (self), icon);

    return self;
}
