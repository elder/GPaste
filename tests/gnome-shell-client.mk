## This file is part of GPaste.
##
## Copyright 2013-2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

TESTS+=                             \
	bin/test-gnome-shell-client \
	$(NULL)

bin_test_gnome_shell_client_SOURCES =                    \
	%D%/gnome-shell-client/test-gnome-shell-client.c \
	$(NULL)

bin_test_gnome_shell_client_CFLAGS = \
	$(AM_CFLAGS)                 \
	$(NULL)

bin_test_gnome_shell_client_LDADD =      \
	$(builddir)/$(libgpaste_la_file) \
	$(AM_LIBS)                       \
	$(NULL)
