/*****
*
* Copyright (C) 1998-2016 CS-SI. All Rights Reserved.
* Author: Yoann Vandoorselaere <yoann.v@prelude-ids.com>
*
* This file is part of the Prelude-Manager program.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by 
* the Free Software Foundation; either version 2, or (at your option)
* any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along
* with this program; if not, write to the Free Software Foundation, Inc.,
* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*
*****/

#ifndef _MANAGER_PLUGIN_REPORT_H
#define _MANAGER_PLUGIN_REPORT_H

prelude_bool_t report_plugins_available(void);

int report_plugin_activate_failover(const char *plugin);

int report_plugins_init(const char *dirname, void *data);

void report_plugins_run(idmef_message_t *message);

void report_plugins_close(void);

#endif /* _MANAGER_PLUGIN_REPORT_H */
