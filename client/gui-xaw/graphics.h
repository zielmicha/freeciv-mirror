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
#ifndef FC__GRAPHICS_H
#define FC__GRAPHICS_H

#include <X11/Xlib.h>

#include "graphics_g.h"

struct Sprite {
  Pixmap pixmap, mask;
  int width, height, ncols;
  unsigned long *pcolorarray;
  int has_mask;
};

struct Sprite *ctor_sprite(Pixmap mypixmap, int width, int height);
struct Sprite *ctor_sprite_mask(Pixmap mypixmap, Pixmap mask, 
				int width, int height);

struct Sprite *load_xpmfile(char *filename);
void free_sprite(struct Sprite *s);

void dtor_sprite(struct Sprite *mysprite);

struct Sprite *get_tile_sprite(int tileno);
void load_tile_gfx_first(void);
void load_tile_gfx_rest(void);

Pixmap create_overlay_unit(int i);

#endif  /* FC__GRAPHICS_H */
