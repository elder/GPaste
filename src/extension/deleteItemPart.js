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

const St = imports.gi.St;

const ExtensionUtils = imports.misc.extensionUtils;
const Me = ExtensionUtils.getCurrentExtension() || imports.ui.appletManager.applets["GPaste@gnome-shell-extensions.gnome.org"];

const DeleteButton = Me.imports.deleteButton;

const GPasteDeleteItemPart = new Lang.Class({
    Name: 'GPasteDeleteItemPart',

    _init: function(client, index) {
        this.actor = new St.Bin({ x_align: St.Align.END });
        this._deleteButton = new DeleteButton.GPasteDeleteButton(client, index);
        this.actor.child = this._deleteButton.actor;
    },

    setIndex: function(index) {
        this._deleteButton.setIndex(index);
    }
});
