## This file is part of GPaste.
##
## Copyright 2012-2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
##
## GPaste is free software: you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation, either version 3 of the License, or
## (at your option) any later version.
##
## GPaste is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with GPaste.  If not, see <http://www.gnu.org/licenses/>.

gnomeshelldir = $(datadir)/gnome-shell/extensions/GPaste@gnome-shell-extensions.gnome.org

gnomeshell_extension_files =          \
	%D%/gnome-shell/extension.js  \
	%D%/gnome-shell/indicator.js  \
	%D%/gnome-shell/prefs.js      \
	%D%/gnome-shell/statusIcon.js \
	$(NULL)

if ENABLE_GNOME_SHELL_EXTENSION
nodist_gnomeshell_DATA =                  \
	$(gpaste_extension_files)         \
	$(gpaste_extension_metadata_file) \
	$(gnomeshell_extension_files)     \
	$(NULL)
endif

EXTRA_DIST +=                         \
	$(gnomeshell_extension_files) \
	$(NULL)
