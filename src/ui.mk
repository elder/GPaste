## This file is part of GPaste.
##
## Copyright 2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

pkglibexec_PROGRAMS += \
	bin/gpaste-ui  \
	$(NULL)

bin_gpaste_ui_SOURCES =    \
	%D%/ui/gpaste-ui.c \
	$(NULL)

bin_gpaste_ui_CFLAGS = \
	$(AM_CFLAGS)   \
	$(NULL)

bin_gpaste_ui_LDADD =                    \
	$(builddir)/$(libgpaste_la_file) \
	$(GTK_LIBS)                      \
	$(AM_LIBS)                       \
	$(NULL)
