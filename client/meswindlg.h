/********************************************************************** 
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef __MESWINDLG__H
#define __MESWINDLG__H

#include "packets.h"

void popup_meswin_dialog();
void update_meswin_dialog();
void clear_notify_window();
void add_notify_window(struct packet_generic_message *packet);

void meswin_update_delay_on();
void meswin_update_delay_off();
     
#endif
