/********************************************************************** 
 Freeciv - Copyright (C) 2003 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "map.h"
#include "shared.h"
#include "support.h"
#include "terrain.h"

struct tile_type tile_types[T_LAST];

/***************************************************************
...
***************************************************************/
struct tile_type *get_tile_type(enum tile_terrain_type type)
{
  return &tile_types[type];
}

/***************************************************************
...
***************************************************************/
enum tile_terrain_type get_terrain_by_name(const char * name)
{
  enum tile_terrain_type tt;
  for (tt = T_FIRST; tt < T_COUNT; tt++) {
    if (0 == strcmp (tile_types[tt].terrain_name, name)) {
      break;
    }
  }
  return tt;
}

/***************************************************************
...
***************************************************************/
const char *get_terrain_name(enum tile_terrain_type type)
{
  assert(type < T_COUNT);
  return tile_types[type].terrain_name;
}

/****************************************************************************
  Return the terrain flag matching the given string, or TER_LAST if there's
  no match.
****************************************************************************/
enum terrain_flag_id terrain_flag_from_str(const char *s)
{
  enum terrain_flag_id flag;
  const char *flag_names[] = {
    /* Must match terrain flags in terrain.h. */
    "NoBarbs"
  };

  assert(ARRAY_SIZE(flag_names) == TER_COUNT);

  for (flag = TER_FIRST; flag < TER_LAST; flag++) {
    if (mystrcasecmp(flag_names[flag], s) == 0) {
      return flag;
    }
  }

  return TER_LAST;
}

/****************************************************************************
  Free memory which is associated with terrain types.
****************************************************************************/
void tile_types_free(void)
{
  terrain_type_iterate(t) {
    struct tile_type *p = get_tile_type(t);

    free(p->helptext);
    p->helptext = NULL;
  } terrain_type_iterate_end;
}

/****************************************************************************
  Returns TRUE iff any adjacent tile contains the given terrain.
****************************************************************************/
bool is_terrain_near_tile(int map_x, int map_y, enum tile_terrain_type t)
{
  adjc_iterate(map_x, map_y, adjc_x, adjc_y) {
    if (map_get_terrain(adjc_x, adjc_y) == t) {
      return TRUE;
    }
  } adjc_iterate_end;

  return FALSE;
}

/****************************************************************************
  Return the number of adjacent tiles that have the given terrain.
****************************************************************************/
int count_terrain_near_tile(int map_x, int map_y, enum tile_terrain_type t)
{
  int count = 0;

  adjc_iterate(map_x, map_y, adjc_x, adjc_y) {
    if (map_get_terrain(adjc_x, adjc_y) == t) {
      count++;
    }
  } adjc_iterate_end;

  return count;
}

/****************************************************************************
  Return the number of cardinally adjacent tiles that have the given terrain.
****************************************************************************/
int adjacent_terrain_tiles4(int map_x, int map_y, enum tile_terrain_type t)
{
  int num_adjacent = 0;

  cartesian_adjacent_iterate(map_x, map_y, adjc_x, adjc_y) {
    if (map_get_terrain(adjc_x, adjc_y) == t) {
      num_adjacent++;
    }
  } cartesian_adjacent_iterate_end;

  return num_adjacent;
}
