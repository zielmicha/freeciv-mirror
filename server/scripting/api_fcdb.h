/**********************************************************************
 Freeciv - Copyright (C) 2005 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC__API_FCDB_H
#define FC__API_FCDB_H

/* server */
#include "fcdb.h"

const char *api_fcdb_option(enum fcdb_option_type type);
void api_fcdb_error(const char *err_msg);

#endif /* FC__API_FCDB_H */
