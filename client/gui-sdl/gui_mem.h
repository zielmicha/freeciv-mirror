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
                          gui_mem.h  -  description
                             -------------------
    begin                : Thu Apr 20 2000
    copyright            : (C) 2002 by Rafa� Bursig
    email                : Rafa� Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifndef FC__GUI_MEM_H
#define FC__GUI_MEM_H

#include "mem.h"

/* free with a check */
#define FREE(ptr) do { if (ptr) free(ptr); ptr = NULL; } while(0)

/* dynamic allocate mem with a check and clear */

#define ALLOCA(size)				\
(__extension__					\
	({					\
		void *__new = alloca(size);	\
		if (!__new) abort();		\
		memset(__new, 0, size);		\
	}))

/* malloc with check and clear */
#define MALLOC(size) memset(fc_real_malloc((size), "malloc", \
					__LINE__, __FILE__), 0, size)

/* fc_calloc in gui-sdl style */
#define CALLOC(count , size)   fc_real_calloc((count), (size), \
					"calloc", __LINE__, __FILE__)

/* fc_realloc in gui-sdl style */
#define REALLOC(ptr, size) fc_real_realloc((ptr), (size), "realloc", \
						   __LINE__, __FILE__)


#endif
