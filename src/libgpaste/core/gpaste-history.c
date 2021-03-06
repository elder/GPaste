/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include <gpaste-history.h>
#include <gpaste-image-item.h>
#include <gpaste-gsettings-keys.h>
#include <gpaste-update-enums.h>
#include <gpaste-uris-item.h>
#include <gpaste-util.h>

struct _GPasteHistory
{
    GObject parent_instance;
};

typedef struct
{
    GPasteSettings *settings;
    GList          *history;
    guint64         size;

    gchar          *name;

    /* Note: we never track the first (active) item here */
    guint64         biggest_index;
    guint64         biggest_size;

    gulong          changed_signal;
} GPasteHistoryPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteHistory, g_paste_history, G_TYPE_OBJECT)

enum
{
    SELECTED,
    SWITCH,
    UPDATE,

    LAST_SIGNAL
};

static guint64 signals[LAST_SIGNAL] = { 0 };

static void
g_paste_history_private_elect_new_biggest (GPasteHistoryPrivate *priv)
{
    priv->biggest_index = 0;
    priv->biggest_size = 0;

    GList *history = priv->history;

    if (history)
    {
        guint64 index = 1;

        for (history = history->next; history; history = history->next, ++index)
        {
            GPasteItem *item = history->data;
            guint64 size = g_paste_item_get_size (item);

            if (size > priv->biggest_size)
            {
                priv->biggest_index = index;
                priv->biggest_size = size;
            }
        }
    }
}

static void
g_paste_history_private_remove (GPasteHistoryPrivate *priv,
                                GList                *elem,
                                gboolean              remove_leftovers)
{
    if (!elem)
        return;

    GPasteItem *item = elem->data;

    priv->size -= g_paste_item_get_size (item);

    if (remove_leftovers)
    {
        if (G_PASTE_IS_IMAGE_ITEM (item))
        {
            g_autoptr (GFile) image = g_file_new_for_path (g_paste_item_get_value (item));
            g_file_delete (image,
                           NULL, /* cancellable */
                           NULL); /* error */
        }
        g_object_unref (item);
    }
    priv->history = g_list_delete_link (priv->history, elem);
}

static void
g_paste_history_selected (GPasteHistory *self,
                          GPasteItem    *item)
{
    g_signal_emit (self,
                   signals[SELECTED],
                   0, /* detail */
                   item,
                   NULL);
}

static void
g_paste_history_emit_switch (GPasteHistory *self,
                             const gchar   *name)
{
    g_signal_emit (self,
                   signals[SWITCH],
                   0, /* detail */
                   name,
                   NULL);
}

static void
g_paste_history_update (GPasteHistory     *self,
                        GPasteUpdateAction action,
                        GPasteUpdateTarget target,
                        guint64            position)
{
    g_paste_history_save (self, NULL);

    g_signal_emit (self,
                   signals[UPDATE],
                   0, /* detail */
                   action,
                   target,
                   position,
                   NULL);
}

static void
g_paste_history_activate_first (GPasteHistory *self,
                                gboolean       select)
{
    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    GList *history = priv->history;

    if (!history)
        return;

    GPasteItem *first = history->data;

    priv->size -= g_paste_item_get_size (first);
    g_paste_item_set_state (first, G_PASTE_ITEM_STATE_ACTIVE);
    priv->size += g_paste_item_get_size (first);

    if (select)
        g_paste_history_selected (self, first);
}

static void
g_paste_history_private_check_memory_usage (GPasteHistoryPrivate *priv)
{
    guint64 max_memory = g_paste_settings_get_max_memory_usage (priv->settings) * 1024 * 1024;

    while (priv->size > max_memory && !priv->biggest_index)
    {
        GList *biggest = g_list_nth (priv->history, priv->biggest_index);

        g_return_if_fail (biggest);

        g_paste_history_private_remove (priv, biggest, TRUE);
        g_paste_history_private_elect_new_biggest (priv);
    }
}

static void
g_paste_history_private_check_size (GPasteHistoryPrivate *priv)
{
    GList *history = priv->history;
    guint64 max_history_size = g_paste_settings_get_max_history_size (priv->settings);
    guint64 length = g_list_length (history);

    if (length > max_history_size)
    {
        history = g_list_nth (history, max_history_size);
        g_return_if_fail (history);
        history->prev->next = NULL;
        history->prev = NULL;

        for (GList *_history = history; _history; _history = g_list_next (_history))
            priv->size -= g_paste_item_get_size (_history->data);
        g_list_free_full (history,
                          g_object_unref);
    }
}

static gboolean
g_paste_history_private_is_growing_line (GPasteHistoryPrivate *priv,
                                         GPasteItem           *old,
                                         GPasteItem           *new)
{
    if (!(g_paste_settings_get_growing_lines (priv->settings) &&
        G_PASTE_IS_TEXT_ITEM (old) && G_PASTE_IS_TEXT_ITEM (new) &&
        !G_PASTE_IS_PASSWORD_ITEM (old) && !G_PASTE_IS_PASSWORD_ITEM (new)))
            return FALSE;

    const gchar *n = g_paste_item_get_value (new);
    const gchar *o = g_paste_item_get_value (old);

    return (g_str_has_prefix (n, o) || g_str_has_suffix (n, o));
}

/**
 * g_paste_history_add:
 * @self: a #GPasteHistory instance
 * @item: (transfer full): the #GPasteItem to add
 *
 * Add a #GPasteItem to the #GPasteHistory
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_history_add (GPasteHistory *self,
                     GPasteItem    *item)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));
    g_return_if_fail (G_PASTE_IS_ITEM (item));

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    guint64 max_memory = g_paste_settings_get_max_memory_usage (priv->settings) * 1024 * 1024;

    g_return_if_fail (g_paste_item_get_size (item) < max_memory);

    GList *history = priv->history;
    gboolean election_needed = FALSE;
    GPasteUpdateTarget target = G_PASTE_UPDATE_TARGET_ALL;

    if (history)
    {
        GPasteItem *old_first = history->data;

        if (g_paste_item_equals (old_first, item))
            return;

        if (g_paste_history_private_is_growing_line (priv, old_first, item))
        {
            target = G_PASTE_UPDATE_TARGET_POSITION;
            g_paste_history_private_remove (priv, history, FALSE);
        }
        else
        {
            /* size may change when state is idle */
            priv->size -= g_paste_item_get_size (old_first);
            g_paste_item_set_state (old_first, G_PASTE_ITEM_STATE_IDLE);

            guint64 size = g_paste_item_get_size (old_first);

            priv->size += size;

            if (size >= priv->biggest_size)
            {
                priv->biggest_index = 0; /* Current 0, will become 1 */
                priv->biggest_size = size;
            }

            guint64 index = 1;
            for (history = history->next; history; history = history->next, ++index)
            {
                if (g_paste_item_equals (history->data, item) || g_paste_history_private_is_growing_line (priv, history->data, item))
                {
                    g_paste_history_private_remove (priv, history, FALSE);
                    if (index == priv->biggest_index)
                        election_needed = TRUE;
                    break;
                }
            }

            ++priv->biggest_index;
        }
    }

    priv->history = g_list_prepend (priv->history, item);

    g_paste_history_activate_first (self, FALSE);
    priv->size += g_paste_item_get_size (item);

    g_paste_history_private_check_size (priv);

    if (election_needed)
        g_paste_history_private_elect_new_biggest (priv);

    g_paste_history_private_check_memory_usage (priv);
    g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REPLACE, target, 0);
}

/**
 * g_paste_history_remove:
 * @self: a #GPasteHistory instance
 * @index: the index of the #GPasteItem to delete
 *
 * Delete a #GPasteItem from the #GPasteHistory
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_history_remove (GPasteHistory *self,
                        guint64        pos)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    GList *history = priv->history;

    g_return_if_fail (pos < g_list_length (history));

    GList *item = g_list_nth (history, pos);

    g_return_if_fail (item);

    g_paste_history_private_remove (priv, item, TRUE);

    if (!pos)
        g_paste_history_activate_first (self, TRUE);

    if (pos == priv->biggest_index)
        g_paste_history_private_elect_new_biggest (priv);
    else if (pos < priv->biggest_index)
        --priv->biggest_index;

    g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REMOVE, G_PASTE_UPDATE_TARGET_POSITION, pos);
}

static GPasteItem *
g_paste_history_private_get (GPasteHistoryPrivate *priv,
                             guint64               pos)
{
    GList *history = priv->history;

    if (pos >= g_list_length (history))
        return NULL;

    return G_PASTE_ITEM (g_list_nth_data (history, pos));
}

/**
 * g_paste_history_get:
 * @self: a #GPasteHistory instance
 * @index: the index of the #GPasteItem
 *
 * Get a #GPasteItem from the #GPasteHistory
 *
 * Returns: a read-only #GPasteItem
 */
G_PASTE_VISIBLE const GPasteItem *
g_paste_history_get (GPasteHistory *self,
                     guint64        pos)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);

    return g_paste_history_private_get (g_paste_history_get_instance_private (self), pos);
}

/**
 * g_paste_history_dup:
 * @self: a #GPasteHistory instance
 * @index: the index of the #GPasteItem
 *
 * Get a #GPasteItem from the #GPasteHistory
 * free it with g_object_unref
 *
 * Returns: (transfer full): a #GPasteItem
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_history_dup (GPasteHistory *self,
                     guint64        pos)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);

    return g_object_ref (g_paste_history_private_get (g_paste_history_get_instance_private (self), pos));
}

/**
 * g_paste_history_get_value:
 * @self: a #GPasteHistory instance
 * @index: the index of the #GPasteItem
 *
 * Get the value of a #GPasteItem from the #GPasteHistory
 *
 * Returns: the read-only value of the #GPasteItem
 */
G_PASTE_VISIBLE const gchar *
g_paste_history_get_value (GPasteHistory *self,
                           guint64        pos)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);

    GPasteItem *item = g_paste_history_private_get (g_paste_history_get_instance_private (self), pos);
    if (!item)
        return NULL;

    return g_paste_item_get_value (item);
}

/**
 * g_paste_history_get_display_string:
 * @self: a #GPasteHistory instance
 * @index: the index of the #GPasteItem
 *
 * Get the display string of a #GPasteItem from the #GPasteHistory
 *
 * Returns: the read-only display string of the #GPasteItem
 */
G_PASTE_VISIBLE const gchar *
g_paste_history_get_display_string (GPasteHistory *self,
                                    guint64        pos)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);

    GPasteItem *item = g_paste_history_private_get (g_paste_history_get_instance_private (self), pos);
    if (!item)
        return NULL;

    return g_paste_item_get_display_string (item);
}

/**
 * g_paste_history_select:
 * @self: a #GPasteHistory instance
 * @index: the index of the #GPasteItem to select
 *
 * Select a #GPasteItem from the #GPasteHistory
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_history_select (GPasteHistory *self,
                        guint64        index)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    GList *history = priv->history;

    g_return_if_fail (index < g_list_length (history));

    GPasteItem *item = g_list_nth_data (history, index);

    g_paste_history_add (self, item);
    g_paste_history_selected (self, item);
}

static void
_g_paste_history_replace (GPasteHistory *self,
                          guint64        index,
                          GPasteItem    *new,
                          GList         *todel)
{
    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    GPasteItem *old = todel->data;

    priv->size -= g_paste_item_get_size (old);
    priv->size += g_paste_item_get_size (new);

    g_object_unref (old);
    todel->data = new;

    if (index == priv->biggest_index)
        g_paste_history_private_elect_new_biggest (priv);

    g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_POSITION, index);
}

/**
 * g_paste_history_replace:
 * @self: a #GPasteHistory instance
 * @index: the index of the #GPasteTextItem to replace
 * @contents: the new contents
 *
 * Replace the contents of text item at index @index
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_history_replace (GPasteHistory *self,
                         guint64        index,
                         const gchar   *contents)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));
    g_return_if_fail (!contents || g_utf8_validate (contents, -1, NULL));

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    GList *history = priv->history;

    g_return_if_fail (index < g_list_length (history));

    GList *todel = g_list_nth (history, index);

    g_return_if_fail (todel);

    GPasteItem *item = todel->data;

    g_return_if_fail (G_PASTE_IS_TEXT_ITEM (item) && !g_strcmp0 (g_paste_item_get_kind (item), "Text"));

    GPasteItem *new = g_paste_text_item_new (contents);

    _g_paste_history_replace (self, index, new, todel);

    if (!index)
        g_paste_history_selected (self, new);
}

/**
 * g_paste_history_set_password:
 * @self: a #GPasteHistory instance
 * @index: the index of the #GPasteTextItem to change as password
 * @name: (nullable): the name to give to the password
 *
 * Mark a text item as password
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_history_set_password (GPasteHistory *self,
                              guint64        index,
                              const gchar   *name)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));
    g_return_if_fail (!name || g_utf8_validate (name, -1, NULL));

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    GList *history = priv->history;

    g_return_if_fail (index < g_list_length (history));

    GList *todel = g_list_nth (history, index);

    g_return_if_fail (todel);

    GPasteItem *item = todel->data;

    g_return_if_fail (G_PASTE_IS_TEXT_ITEM (item) && !g_strcmp0 (g_paste_item_get_kind (item), "Text"));

    GPasteItem *password = g_paste_password_item_new (name, g_paste_item_get_real_value (item));

    _g_paste_history_replace (self, index, password, todel);
}

static GPasteItem *
_g_paste_history_private_get_password (const GPasteHistoryPrivate *priv,
                                       const gchar                *name,
                                       guint64                    *index)
{
    guint64 idx = 0;

    for (GList *h = priv->history; h; h = g_list_next (h), ++idx)
    {
        GPasteItem *i = h->data;
        if (G_PASTE_IS_PASSWORD_ITEM (i) &&
            !g_strcmp0 (g_paste_password_item_get_name ((GPastePasswordItem *) i), name))
        {
            if (index)
                *index = idx;
            return i;
        }
    }

    if (index)
        *index = -1;
    return NULL;
}

/**
 * g_paste_history_get_password:
 * @self: a #GPasteHistory instance
 * @name: the name of the #GPastePasswordItem
 *
 * Get the first password matching name
 *
 * Returns: (optional): a #GPastePasswordItem or %NULL
 */
G_PASTE_VISIBLE const GPastePasswordItem *
g_paste_history_get_password (GPasteHistory *self,
                              const gchar   *name)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);
    g_return_val_if_fail (!name || g_utf8_validate (name, -1, NULL), NULL);

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    GPasteItem *item = _g_paste_history_private_get_password (priv, name, NULL);

    return (item) ? G_PASTE_PASSWORD_ITEM (item) : NULL;
}

/**
 * g_paste_history_delete_password:
 * @self: a #GPasteHistory instance
 * @name: the name of the #GPastePasswordItem
 *
 * Delete the password matching name
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_history_delete_password (GPasteHistory *self,
                                 const gchar   *name)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));
    g_return_if_fail (!name || g_utf8_validate (name, -1, NULL));

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    guint64 index;

    if (_g_paste_history_private_get_password (priv, name, &index))
        g_paste_history_remove (self, index);
}

/**
 * g_paste_history_rename_password:
 * @self: a #GPasteHistory instance
 * @old_name: the old name of the #GPastePasswordItem
 * @new_name: (nullable): the new name of the #GPastePasswordItem
 *
 * Rename the password item
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_history_rename_password (GPasteHistory *self,
                                 const gchar   *old_name,
                                 const gchar   *new_name)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));
    g_return_if_fail (!old_name || g_utf8_validate (old_name, -1, NULL));
    g_return_if_fail (!new_name || g_utf8_validate (new_name, -1, NULL));

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    guint64 index = 0;
    GPasteItem *item = _g_paste_history_private_get_password (priv, old_name, &index);
    if (item)
    {
        g_paste_password_item_set_name (G_PASTE_PASSWORD_ITEM (item), new_name);
        g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_POSITION, index);
    }
}

/**
 * g_paste_history_empty:
 * @self: a #GPasteHistory instance
 *
 * Empty the #GPasteHistory
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_history_empty (GPasteHistory *self)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    g_list_free_full (priv->history,
                      g_object_unref);
    priv->history = NULL;
    priv->size = 0;

    g_paste_history_private_elect_new_biggest (priv);
    g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REMOVE, G_PASTE_UPDATE_TARGET_ALL, 0);
}

static gchar *
g_paste_history_encode (const gchar *text)
{
    g_autofree gchar *_encoded_text = g_paste_util_replace (text, "&", "&amp;");
    return g_paste_util_replace (_encoded_text, ">", "&gt;");
}

static gchar *
g_paste_history_decode (const gchar *text)
{
    g_autofree gchar *_decoded_text = g_paste_util_replace (text, "&gt;", ">");
    return g_paste_util_replace (_decoded_text, "&amp;", "&");
}

static gchar *
g_paste_history_get_history_dir_path (void)
{
    return g_build_filename (g_get_user_data_dir (), "gpaste", NULL);
}

static GFile *
g_paste_history_get_history_dir (void)
{
    g_autofree gchar *history_dir_path = g_paste_history_get_history_dir_path ();
    return g_file_new_for_path (history_dir_path);
}

static gchar *
g_paste_history_get_history_file_path (const gchar *name)
{
    g_return_val_if_fail (name, NULL);

    g_autofree gchar *history_dir_path = g_paste_history_get_history_dir_path ();
    g_autofree gchar *history_file_name = g_strconcat (name, ".xml", NULL);

    return g_build_filename (history_dir_path, history_file_name, NULL);
}

static GFile *
g_paste_history_get_history_file (const gchar *name)
{
    g_autofree gchar *history_file_path = g_paste_history_get_history_file_path (name);
    return g_file_new_for_path (history_file_path);
}

static gboolean
ensure_history_dir_exists (gboolean save_history)
{
    g_autoptr (GFile) history_dir = g_paste_history_get_history_dir ();

    if (!g_file_query_exists (history_dir,
                              NULL)) /* cancellable */
    {
        if (!save_history)
            return TRUE;

        g_autoptr (GError) error = NULL;

        g_file_make_directory_with_parents (history_dir,
                                            NULL, /* cancellable */
                                            &error);
        if (error)
        {
            g_critical ("%s: %s", _("Could not create history dir"), error->message);
            return FALSE;
        }
    }

    return TRUE;
}

/**
 * g_paste_history_save:
 * @self: a #GPasteHistory instance
 * @name: (nullable): the name to save the history to (defaults to the configured one)
 *
 * Save the #GPasteHistory to the history file
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_history_save (GPasteHistory *self,
                      const gchar   *name)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    GPasteSettings *settings = priv->settings;
    gboolean save_history = g_paste_settings_get_save_history (settings);
    g_autofree gchar *history_file_path = NULL;
    g_autoptr (GFile) history_file = NULL;

    if (!ensure_history_dir_exists (save_history))
        return;

    history_file_path = g_paste_history_get_history_file_path ((name) ? name : priv->name);
    history_file = g_file_new_for_path (history_file_path);

    if (!save_history)
    {
        g_file_delete (history_file,
                       NULL, /* cancellable*/
                       NULL); /* error */
    }
    else
    {
        g_autoptr (GOutputStream) stream = G_OUTPUT_STREAM (g_file_replace (history_file,
                                                            NULL,
                                                            FALSE,
                                                            G_FILE_CREATE_REPLACE_DESTINATION,
                                                            NULL, /* cancellable */
                                                            NULL)); /* error */

        if (!g_output_stream_write_all (stream, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", 39, NULL, NULL /* cancellable */, NULL /* error */) ||
            !g_output_stream_write_all (stream, "<history version=\"1.0\">\n", 24, NULL, NULL /* cancellable */, NULL /* error */))
                return;

        for (GList *history = priv->history; history; history = g_list_next (history))
        {
            GPasteItem *item = history->data;
            const gchar *kind = g_paste_item_get_kind (item);

            if (!g_strcmp0 (kind, "Password"))
                continue;

            g_autofree gchar *text = g_paste_history_encode (g_paste_item_get_value (item));

            if (!g_output_stream_write_all (stream, "  <item kind=\"", 14, NULL, NULL /* cancellable */, NULL /* error */) ||
                !g_output_stream_write_all (stream, kind, strlen (kind), NULL, NULL /* cancellable */, NULL /* error */) ||
                (G_PASTE_IS_IMAGE_ITEM (item) &&
                    (!g_output_stream_write_all (stream, "\" date=\"", 8, NULL, NULL /* cancellable */, NULL /* error */) ||
                     !g_output_stream_write_all (stream, g_date_time_format ((GDateTime *) g_paste_image_item_get_date (G_PASTE_IMAGE_ITEM (item)), "%s"), 10, NULL, NULL /* cancellable */, NULL /* error */))) ||
                !g_output_stream_write_all (stream, "\"><![CDATA[", 11, NULL, NULL /* cancellable */, NULL /* error */) ||
                !g_output_stream_write_all (stream, text, strlen (text), NULL, NULL /* cancellable */, NULL /* error */) ||
                !g_output_stream_write_all (stream, "]]></item>\n", 11, NULL, NULL /* cancellable */, NULL /* error */))
            {
                g_warning ("Failed to write an item to history");
                continue;
            }
        }

        if (!g_output_stream_write_all (stream, "</history>\n", 11, NULL, NULL /* cancellable */, NULL /* error */) ||
            !g_output_stream_close (stream, NULL /* cancellable */, NULL /* error */))
                return;
    }
}

/********************/
/* Begin XML Parser */
/********************/

typedef enum
{
    BEGIN,
    IN_HISTORY,
    IN_ITEM,
    HAS_TEXT,
    END
} State;

typedef enum
{
    TEXT,
    IMAGE,
    URIS,
    PASSWORD
} Type;

typedef struct
{
    GPasteHistoryPrivate *priv;
    State                 state;
    Type                  type;
    guint64               current_size;
    guint64               max_size;
    gboolean              images_support;
    gchar                *date;
    gchar                *name;
    gchar                *text;
} Data;

#define ASSERT_STATE(x)                                                                           \
    if (data->state != x)                                                                         \
    {                                                                                             \
        g_warning ("Expected state %" G_GINT32_FORMAT ", but got %" G_GINT32_FORMAT, x, data->state); \
        return;                                                                                   \
    }
#define SWITCH_STATE(x, y) \
    ASSERT_STATE (x);      \
    data->state = y

static void
start_tag (GMarkupParseContext *context G_GNUC_UNUSED,
           const gchar         *element_name,
           const gchar        **attribute_names,
           const gchar        **attribute_values,
           gpointer             user_data,
           GError             **error G_GNUC_UNUSED)
{
    Data *data = user_data;

    if (!g_strcmp0 (element_name, "history"))
    {
        SWITCH_STATE (BEGIN, IN_HISTORY);
    }
    else if (!g_strcmp0 (element_name, "item"))
    {
        SWITCH_STATE (IN_HISTORY, IN_ITEM);
        g_clear_pointer (&data->date, g_free);
        g_clear_pointer (&data->text, g_free);
        for (const gchar **a = attribute_names, **v = attribute_values; *a && *v; ++a, ++v)
        {
            if (!g_strcmp0 (*a, "kind"))
            {
                if (!g_strcmp0 (*v, "Text"))
                    data->type = TEXT;
                else if (!g_strcmp0 (*v, "Image"))
                    data->type = IMAGE;
                else if (!g_strcmp0 (*v, "Uris"))
                    data->type = URIS;
                else if (!g_strcmp0 (*v, "Password"))
                    data->type = PASSWORD;
                else
                    g_critical ("Unknown item kind: %s", *v);
            }
            else if (!g_strcmp0 (*a, "date"))
            {
                if (data->type != IMAGE)
                {
                    g_warning ("Expected type %" G_GINT32_FORMAT ", but got %" G_GINT32_FORMAT, IMAGE, data->type);
                    return;
                }
                data->date = g_strdup (*v);
            }
            else if (!g_strcmp0 (*a, "name"))
            {
                if (data->type != PASSWORD)
                {
                    g_warning ("Expected type %" G_GINT32_FORMAT ", but got %" G_GINT32_FORMAT, PASSWORD, data->type);
                    return;
                }
                data->name = g_strdup (*v);
            }
            else
                g_warning ("Unknown item attribute: %s", *a);
        }
    }
    else
        g_warning ("Unknown element: %s", element_name);
}

static void
end_tag (GMarkupParseContext *context G_GNUC_UNUSED,
         const gchar         *element_name,
         gpointer             user_data,
         GError             **error G_GNUC_UNUSED)
{
    Data *data = user_data;

    if (!g_strcmp0 (element_name, "history"))
    {
        SWITCH_STATE (IN_HISTORY, END);
    }
    else if (!g_strcmp0 (element_name, "item"))
    {
        SWITCH_STATE (HAS_TEXT, IN_HISTORY);
    }
    else
        g_warning ("Unknown element: %s", element_name);
}

static void
on_text (GMarkupParseContext *context G_GNUC_UNUSED,
         const gchar         *text,
         guint64              text_len,
         gpointer             user_data,
         GError             **error G_GNUC_UNUSED)
{
    Data *data = user_data;

    g_autofree gchar *txt = g_strndup (text, text_len);
    switch (data->state)
    {
    case IN_HISTORY:
    case HAS_TEXT:
        if (*g_strstrip (txt))
        {
            g_warning ("Unexpected text: %s", txt);
            return;
        }
        break;
    case IN_ITEM:
    {
        g_autofree gchar *value = g_paste_history_decode (txt);
        if (*g_strstrip (txt))
        {
            if (data->current_size < data->max_size)
            {
                GPasteItem *item = NULL;

                switch (data->type)
                {
                case TEXT:
                    item = g_paste_text_item_new (value);
                    break;
                case URIS:
                    item = g_paste_uris_item_new (value);
                    break;
                case PASSWORD:
                    item = g_paste_password_item_new (data->name, value);
                    break;
                case IMAGE:
                    if (data->images_support && data->date)
                    {
                        g_autoptr (GDateTime) date_time = g_date_time_new_from_unix_local (g_ascii_strtoll (data->date,
                                                                                                            NULL, /* end */
                                                                                                            0)); /* base */
                        item = g_paste_image_item_new_from_file (value, date_time);
                    }
                    else
                    {
                        g_autoptr (GFile) img_file = g_file_new_for_path (value);

                        if (g_file_query_exists (img_file,
                                                 NULL)) /* cancellable */
                        {
                            g_file_delete (img_file,
                                           NULL, /* cancellable */
                                           NULL); /* error */
                        }
                    }
                    break;
                }

                if (item)
                {
                    GPasteHistoryPrivate *priv = data->priv;
                    priv->size += g_paste_item_get_size (item);
                    priv->history = g_list_append (priv->history, item);
                    ++data->current_size;;
                }
            }

            SWITCH_STATE (IN_ITEM, HAS_TEXT);
        }
        break;
    }
    default:
        g_warning ("Unexpected state: %" G_GINT32_FORMAT, data->state);
    }
}

static void on_error (GMarkupParseContext *context   G_GNUC_UNUSED,
                      GError              *error,
                      gpointer             user_data G_GNUC_UNUSED)
{
    g_critical ("error: %s", error->message);
}

/******************/
/* End XML Parser */
/******************/

/**
 * g_paste_history_load:
 * @self: a #GPasteHistory instance
 * @name: (nullable): the name of the history to load, defaults to the configured one
 *
 * Load the #GPasteHistory from the history file
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_history_load (GPasteHistory *self,
                      const gchar   *name)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));
    g_return_if_fail (!name || g_utf8_validate (name, -1, NULL));

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    GPasteSettings *settings = priv->settings;

    if (priv->name && !g_strcmp0(name, priv->name))
        return;

    g_list_free_full (priv->history,
                      g_object_unref);
    priv->history = NULL;

    g_free (priv->name);
    priv->name = g_strdup ((name) ? name : g_paste_settings_get_history_name (priv->settings));

    g_autofree gchar *history_file_path = g_paste_history_get_history_file_path (priv->name);
    g_autoptr (GFile) history_file = g_file_new_for_path (history_file_path);

    if (g_file_query_exists (history_file,
                             NULL)) /* cancellable */
    {
        GMarkupParser parser = {
            start_tag,
            end_tag,
            on_text,
            NULL,
            on_error
        };
        Data data = {
            priv,
            BEGIN,
            TEXT,
            0,
            g_paste_settings_get_max_history_size (settings),
            g_paste_settings_get_images_support (settings),
            NULL,
            NULL,
            NULL
        };
        GMarkupParseContext *ctx = g_markup_parse_context_new (&parser,
                                                               G_MARKUP_TREAT_CDATA_AS_TEXT,
                                                               &data,
                                                               NULL);
        gchar *text;
        guint64 text_length;

        g_file_get_contents (history_file_path, &text, &text_length, NULL);
        g_markup_parse_context_parse (ctx, text, text_length, NULL);
        g_markup_parse_context_end_parse (ctx, NULL);

        if (data.state != END)
            g_warning ("Unexpected state adter parsing history: %" G_GINT32_FORMAT, data.state);
        g_markup_parse_context_unref (ctx);
    }
    else
    {
        /* Create the empty file to be listed as an available history */
        if (ensure_history_dir_exists (g_paste_settings_get_save_history (settings)))
            g_object_unref (g_file_create (history_file, G_FILE_CREATE_NONE, NULL, NULL));
    }

    if (priv->history)
    {
        g_paste_history_activate_first (self, TRUE);
        g_paste_history_private_elect_new_biggest (priv);
    }
}

/**
 * g_paste_history_switch:
 * @self: a #GPasteHistory instance
 * @name: the name of the new history
 *
 * Switch to a new history
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_history_switch (GPasteHistory *self,
                        const gchar   *name)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));
    g_return_if_fail (name);
    g_return_if_fail (g_utf8_validate (name, -1, NULL));

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    g_paste_settings_set_history_name (priv->settings, name);
}

/**
 * g_paste_history_delete:
 * @self: a #GPasteHistory instance
 * @name: (nullable): the history to delete (defaults to the configured one)
 * @error: a #GError
 *
 * Delete the current #GPasteHistory
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_history_delete (GPasteHistory *self,
                        const gchar   *name,
                        GError       **error)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    g_autoptr (GFile) history_file = g_paste_history_get_history_file ((name) ? name : priv->name);

    if (!g_strcmp0 (name, priv->name))
        g_paste_history_empty (self);

    if (g_file_query_exists (history_file,
                             NULL)) /* cancellable */
    {
        g_file_delete (history_file,
                       NULL, /* cancellable */
                       error);
    }
}

static void
g_paste_history_history_name_changed (GPasteHistory *self)
{
    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    g_paste_history_load (self, NULL);
    g_paste_history_emit_switch (self, priv->name);
    g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, 0);
}

static void
g_paste_history_settings_changed (GPasteSettings *settings G_GNUC_UNUSED,
                                  const gchar    *key,
                                  gpointer        user_data)
{
    GPasteHistory *self = user_data;
    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    if (!g_strcmp0(key, G_PASTE_MAX_HISTORY_SIZE_SETTING))
        g_paste_history_private_check_size (priv);
    else if (!g_strcmp0 (key, G_PASTE_MAX_MEMORY_USAGE_SETTING))
        g_paste_history_private_check_memory_usage (priv);
    else if (!g_strcmp0 (key, G_PASTE_HISTORY_NAME_SETTING))
        g_paste_history_history_name_changed (self);
}

static void
g_paste_history_dispose (GObject *object)
{
    GPasteHistory *self = G_PASTE_HISTORY (object);
    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    GPasteSettings *settings = priv->settings;

    if (settings)
    {
        g_signal_handler_disconnect (settings, priv->changed_signal);
        g_clear_object (&priv->settings);
    }

    G_OBJECT_CLASS (g_paste_history_parent_class)->dispose (object);
}

static void
g_paste_history_finalize (GObject *object)
{
    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (G_PASTE_HISTORY (object));

    g_free (priv->name);
    g_list_free_full (priv->history,
                      g_object_unref);

    G_OBJECT_CLASS (g_paste_history_parent_class)->finalize (object);
}

static void
g_paste_history_class_init (GPasteHistoryClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_history_dispose;
    object_class->finalize = g_paste_history_finalize;

    /**
     * GPasteHistory::selected:
     * @history: the object on which the signal was emitted
     * @item: the new selected item
     *
     * The "selected" signal is emitted when the user has just
     * selected a new item form the history.
     */
    signals[SELECTED] = g_signal_new ("selected",
                                      G_PASTE_TYPE_HISTORY,
                                      G_SIGNAL_RUN_LAST,
                                      0, /* class offset */
                                      NULL, /* accumulator */
                                      NULL, /* accumulator data */
                                      g_cclosure_marshal_VOID__OBJECT,
                                      G_TYPE_NONE,
                                      1, /* number of params */
                                      G_PASTE_TYPE_ITEM);

    /**
     * GPasteHistory::switch:
     * @history: the object on which the signal was emitted
     * @name: the new history name
     *
     * The "switch" signal is emitted when the user has just
     * switched to a new history
     */
    signals[SWITCH] = g_signal_new ("switch",
                                    G_PASTE_TYPE_HISTORY,
                                    G_SIGNAL_RUN_LAST,
                                    0, /* class offset */
                                    NULL, /* accumulator */
                                    NULL, /* accumulator data */
                                    g_cclosure_marshal_VOID__STRING,
                                    G_TYPE_NONE,
                                    1, /* number of params */
                                    G_TYPE_STRING);

    /**
     * GPasteHistory::update:
     * @history: the object on which the signal was emitted
     * @action: the kind of update
     * @target: the items which need updating
     * @index: the index of the item, when the target is POSITION
     *
     * The "update" signal is emitted whenever anything changed
     * in the history (something was added, removed, selected, replaced...).
     */
    signals[UPDATE] = g_signal_new ("update",
                                    G_PASTE_TYPE_HISTORY,
                                    G_SIGNAL_RUN_LAST,
                                    0, /* class offset */
                                    NULL, /* accumulator */
                                    NULL, /* accumulator data */
                                    g_cclosure_marshal_generic,
                                    G_TYPE_NONE,
                                    3, /* number of params */
                                    G_PASTE_TYPE_UPDATE_ACTION,
                                    G_PASTE_TYPE_UPDATE_TARGET,
                                    G_TYPE_UINT64);
}

static void
g_paste_history_init (GPasteHistory *self)
{
    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    priv->history = NULL;
    priv->size = 0;

    g_paste_history_private_elect_new_biggest (priv);
}

/**
 * g_paste_history_get_history:
 * @self: a #GPasteHistory instance
 *
 * Get the inner history of a #GPasteHistory
 *
 * Returns: (element-type GPasteItem) (transfer none): The inner history
 */
G_PASTE_VISIBLE const GList *
g_paste_history_get_history (const GPasteHistory *self)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    return priv->history;
}

/**
 * g_paste_history_get_length:
 * @self: a #GPasteHistory instance
 *
 * Get the length of a #GPasteHistory
 *
 * Returns: The length of the inner history
 */
G_PASTE_VISIBLE guint64
g_paste_history_get_length (const GPasteHistory *self)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), 0);

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    return g_list_length (priv->history);
}

/**
 * g_paste_history_get_current:
 * @self: a #GPasteHistory instance
 *
 * Get the name of the current history
 *
 * Returns: The name of the current history
 */
G_PASTE_VISIBLE const gchar *
g_paste_history_get_current (const GPasteHistory *self)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), 0);

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    return priv->name;
}

/**
 * g_paste_history_search:
 * @self: a #GPasteHistory instance
 * @pattern: the pattern to match
 *
 * Get the elements matching @pattern in the history
 *
 * Returns: (element-type guint64) (transfer full): The indexes of the matching elements
 */
G_PASTE_VISIBLE GArray *
g_paste_history_search (const GPasteHistory *self,
                        const gchar         *pattern)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);
    g_return_val_if_fail (pattern && g_utf8_validate (pattern, -1, NULL), NULL);

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    g_autoptr (GError) error = NULL;
    g_autoptr (GRegex) regex = g_regex_new (pattern,
                                            G_REGEX_CASELESS|G_REGEX_MULTILINE|G_REGEX_DOTALL|G_REGEX_OPTIMIZE,
                                            G_REGEX_MATCH_NOTEMPTY|G_REGEX_MATCH_NEWLINE_ANY,
                                            &error);

    if (error)
    {
        g_warning ("error while creating regex: %s", error->message);
        return NULL;
    }
    if (!regex)
        return NULL;

    /* Check whether we include the index in the search too */
    gboolean include_idx = FALSE;
    guint64 idx = 0;
    guint64 len = strlen (pattern);

    if (len < 5)
    {
        for (guint64 i = 0; i < len; ++i)
        {
            char c = pattern[i];

            if (c >= '0' && c <= '9')
            {
                include_idx = TRUE;
                idx *= 10;
                idx += (c - '0');
            }
            else
            {
                include_idx = FALSE;
                break;
            }
        }
    }

    GArray *results = g_array_new (FALSE, /* zero-terminated */
                                   TRUE,  /* clear */
                                   sizeof (guint64));
    guint64 index = 0;

    for (GList *history = priv->history; history; history = g_list_next (history), ++index)
    {
        if (include_idx && idx == index)
            g_array_append_val (results, index);
        else if (g_regex_match (regex, g_paste_item_get_value (history->data), G_REGEX_MATCH_NOTEMPTY|G_REGEX_MATCH_NEWLINE_ANY, NULL))
            g_array_append_val (results, index);
    }

    return results;
}

/**
 * g_paste_history_new:
 * @settings: (transfer none): a #GPasteSettings instance
 *
 * Create a new instance of #GPasteHistory
 *
 * Returns: a newly allocated #GPasteHistory
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteHistory *
g_paste_history_new (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    GPasteHistory *self = g_object_new (G_PASTE_TYPE_HISTORY, NULL);
    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    priv->settings = g_object_ref (settings);
    priv->changed_signal = g_signal_connect (settings,
                                             "changed",
                                             G_CALLBACK (g_paste_history_settings_changed),
                                             self);

    return self;
}

/**
 * g_paste_history_list:
 * @error: a #GError
 *
 * Get the list of available histories
 *
 * Returns: (transfer full): The list of history names
 *                           free it with g_array_unref
 */
G_PASTE_VISIBLE GStrv
g_paste_history_list (GError **error)
{
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autoptr (GArray) history_names = g_array_new (TRUE, /* zero-terminated */
                                                    TRUE, /* clear */
                                                    sizeof (gchar *));
    g_autoptr (GFile) history_dir = g_paste_history_get_history_dir ();
    g_autoptr (GFileEnumerator) histories = g_file_enumerate_children (history_dir,
                                                                       G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                                                                       G_FILE_QUERY_INFO_NONE,
                                                                       NULL, /* cancellable */
                                                                       error);
    if (error && *error)
        return NULL;

    GFileInfo *history;

    while ((history = g_file_enumerator_next_file (histories,
                                                   NULL, /* cancellable */
                                                   error))) /* error */
    {
        g_autoptr (GFileInfo) h = history;

        if (error && *error)
        {
            g_array_unref (history_names);
            return NULL;
        }

        const gchar *raw_name = g_file_info_get_display_name (h);

        if (g_str_has_suffix (raw_name, ".xml"))
        {
            gchar *name = g_strdup (raw_name);

            name[strlen (name) - 4] = '\0';
            g_array_append_val (history_names, name);
        }
    }

    return g_strdupv ((GStrv) (gpointer) history_names->data);
}
