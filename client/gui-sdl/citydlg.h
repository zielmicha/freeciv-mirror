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

/**********************************************************************
                          citydlg.h  -  description
                             -------------------
    begin                : Wed Sep 04 2002
    copyright            : (C) 2002 by Rafa� Bursig
    email                : Rafa� Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifndef FC__CITYDLG_H
#define FC__CITYDLG_H

#include "citydlg_g.h"

void load_city_icons(void);
void refresh_city_dlg_background(void);
SDL_Surface *get_colb_surface(void);
SDL_Rect *get_citydlg_rect(void);

#endif				/* FC__CITYDLG_H */
