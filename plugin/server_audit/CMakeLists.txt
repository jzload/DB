# Copyright (C) 2013 Alexey Botchkov and SkySQL Ab
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02111-1301 USA

SET(SERVER_AUDIT_SOURCES
   server_audit.cc test_audit_v4.c plugin_audit_v4.h)

 MYSQL_ADD_PLUGIN(server_audit ${SERVER_AUDIT_SOURCES} MODULE_ONLY)
