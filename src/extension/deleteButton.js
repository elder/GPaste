/*
 *      This file is part of GPaste.
 *
 *      Copyright 2014-2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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
/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const Lang = imports.lang;

const Clutter = imports.gi.Clutter;
const St = imports.gi.St;

const GPasteDeleteButton = new Lang.Class({
    Name: 'GPasteDeleteButton',

    _init: function(client, index) {
        this.actor = new St.Button();

        this.actor.child = new St.Icon({
            icon_name: 'edit-delete-symbolic',
            style_class: 'popup-menu-icon'
        });

        this._client = client;
        this.setIndex(index);

        this.actor.connect('clicked', Lang.bind(this, this._onClick));
    },

    setIndex: function(index) {
        this._index = index;
    },

    _onClick: function() {
        this._client.delete(this._index, null);
        return Clutter.EVENT_STOP;
    }
});
