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

#include <string.h>

#include "mem.h"

#include "pf_tools.h"

/* ===================== Move Cost Callbacks ========================= */

/*************************************************************
  SINGLE_MOVE cost function for SEA_MOVING 
  Note: this is a reversable function
*************************************************************/
static int single_seamove(int x, int y, enum direction8 dir,
			  int x1, int y1, struct pf_parameter *param)
{
  /* MOVE_COST_FOR_VALID_SEA_STEP means ships can move between */
  if (map_get_tile(x, y)->move_cost[dir]
      == MOVE_COST_FOR_VALID_SEA_STEP) {
    return SINGLE_MOVE;
  } else {
    return PF_IMPOSSIBLE_MC;
  }
}

/************************************************************
  A cost function for a sea unit which allows going one step 
  into the land (shore bombardment).
  Things to remember: we should prevent going from land to
  anywhere, unless we are leaving a friendly city, in which
  case we can move into the ocean but not into the land.
************************************************************/
static int sea_overlap_move(int x, int y, enum direction8 dir,
			    int x1, int y1, struct pf_parameter *param)
{
  if (map_get_terrain(x, y) == T_OCEAN) {
    return SINGLE_MOVE;
  } else if (is_allied_city_tile(map_get_tile(x, y), param->owner)
	     && map_get_terrain(x1, y1) == T_OCEAN) {
    return SINGLE_MOVE;
  }

  return PF_IMPOSSIBLE_MC;
}

/************************************************************ 
  LAND_MOVE cost function for a unit 
  Must put owner into *data.
************************************************************/
static int normal_move_unit(int x, int y, enum direction8 dir,
			    int x1, int y1, struct pf_parameter *param)
{
  struct tile *ptile = map_get_tile(x, y);
  int terrain1 = map_get_terrain(x1, y1);
  int move_cost;

  if (terrain1 == T_OCEAN) {
    if (ground_unit_transporter_capacity(x1, y1, param->owner) > 0) {
      move_cost = SINGLE_MOVE;
    } else {
      move_cost = PF_IMPOSSIBLE_MC;
    }
  } else if (ptile->terrain == T_OCEAN) {
    move_cost = get_tile_type(terrain1)->movement_cost * SINGLE_MOVE;
  } else {
    move_cost = ptile->move_cost[dir];
  }

  return move_cost;
}

/************************************************************ 
  A cost function for a land unit, which allows going into
  the ocean (with moves costing SINGLE_MOVE).  It is 
  recommended to use dont_cross_ocean TB callback with this 
  one, so we don't venture too far into the ocean ;)

  Alternatively,we can change the flow to
  if (ptile->terrain == T_OCEAN) {
    move_cost = PF_IMPOSSIBLE_MC;
  } else if (terrain1 == T_OCEAN) {
    move_cost = SINGLE_MOVE;
  } else {
    move_cost = ptile->move_cost[dir];
  }
  which will achieve thesame without call-back.
************************************************************/
static int land_overlap_move(int x, int y, enum direction8 dir,
			     int x1, int y1, struct pf_parameter *param)
{
  struct tile *ptile = map_get_tile(x, y);
  int terrain1 = map_get_terrain(x1, y1);
  int move_cost;

  if (terrain1 == T_OCEAN) {
    move_cost = SINGLE_MOVE;
  } else if (ptile->terrain == T_OCEAN) {
    move_cost = get_tile_type(terrain1)->movement_cost * SINGLE_MOVE;
  } else {
    move_cost = ptile->move_cost[dir];
  }

  return move_cost;
}

/************************************************************ 
  Reversed LAND_MOVE cost function for a unit.
  Will be used. DO NOT REMOVE you pricks.
************************************************************/
#ifdef UNUSED
static int reverse_move_unit(int x, int y, enum direction8 dir,
			     int x1, int y1, struct pf_parameter *param)
{
  struct tile *ptile = map_get_tile(x1, y1);
  int terrain0 = map_get_terrain(x, y);
  int terrain1 = ptile->terrain;
  int move_cost = PF_IMPOSSIBLE_MC;

  if (terrain1 == T_OCEAN) {
    if (ground_unit_transporter_capacity(x1, y1, param->owner) > 0) {
      /* Landing */
      move_cost = get_tile_type(terrain0)->movement_cost * SINGLE_MOVE;
    } else {
      /* Nothing to land from */
      move_cost = PF_IMPOSSIBLE_MC;
    }
  } else if (terrain0 == T_OCEAN) {
    /* Boarding */
    move_cost = SINGLE_MOVE;
  } else {
    move_cost = ptile->move_cost[DIR_REVERSE(dir)];
  }

  return move_cost;
}
#endif

/************************************************************ 
  IGTER_MOVE cost function for a unit 
************************************************************/
static int igter_move_unit(int x, int y, enum direction8 dir,
			   int x1, int y1, struct pf_parameter *param)
{
  struct tile *ptile = map_get_tile(x, y);
  int move_cost;

  if (map_get_terrain(x1, y1) == T_OCEAN) {
    if (ground_unit_transporter_capacity(x1, y1, param->owner) > 0) {
      move_cost = MOVE_COST_ROAD;
    } else {
      move_cost = PF_IMPOSSIBLE_MC;
    }
  } else if (ptile->terrain == T_OCEAN) {
    move_cost = MOVE_COST_ROAD;
  } else {
    move_cost = (ptile->move_cost[dir] != 0 ? MOVE_COST_ROAD : 0);
  }
  return move_cost;
}

/************************************************************ 
  Reversed IGTER_MOVE cost function for a unit.
  Will be used. DO NOT REMOVE you pricks.
************************************************************/
#ifdef UNUSED
static int reverse_igter_move_unit(int x, int y, enum direction8 dir,
				   int x1, int y1,
				   struct pf_parameter *param)
{
  struct tile *ptile = map_get_tile(x1, y1);
  int move_cost;

  if (map_get_terrain(x1, y1) == T_OCEAN) {
    if (ground_unit_transporter_capacity(x1, y1, param->owner) > 0) {
      /* Landing */
      move_cost = MOVE_COST_ROAD;
    } else {
      move_cost = PF_IMPOSSIBLE_MC;
    }
  } else if (map_get_terrain(x, y) == T_OCEAN) {
    /* Boarding */
    move_cost = MOVE_COST_ROAD;
  } else {
    move_cost =
	(ptile->move_cost[DIR_REVERSE(dir)] != 0 ? MOVE_COST_ROAD : 0);
  }
  return move_cost;
}
#endif


/* ===================== Extra Cost Callbacks ======================== */

/*********************************************************************
  An example of EC callback.  DO NOT REMOVE you pricks!
*********************************************************************/
#ifdef UNUSED
static int afraid_of_dark_forest(int x, int y, enum known_type known,
				 struct pf_parameter *param)
{
  if (map_get_terrain(x, y) == T_FOREST) {
    /* Willing to spend extra 2 turns to go around a forest tile */
    return PF_TURN_FACTOR * 2;
  }

  return 0;
}
#endif


/* ===================== Tile Behaviour Callbacks ==================== */

/*********************************************************************
  A callback for maps overlapping one square into the ocean.  Insures 
  that we don't continue walking over ocean.
*********************************************************************/
static enum tile_behavior dont_cross_ocean(int x, int y,
					   enum known_type known,
					   struct pf_parameter *param)
{
  if (map_get_terrain(x, y) == T_OCEAN) {
    return TB_DONT_LEAVE;
  }
  return TB_NORMAL;
}


/* =====================  Postion Dangerous Callbacks ================ */

/**********************************************************************
  An example of position-dangerous callback.  For triremes.
  FIXME: it cheats.
***********************************************************************/
static bool trireme_is_pos_dangerous(int x, int y, enum known_type known,
				     struct pf_parameter *param)
{
  return map_get_terrain(x, y) == T_OCEAN && !is_coastline(x, y);
}


/* =====================  Tools for filling parameters =============== */

/**********************************************************************
  Fill unit-dependent parameters
***********************************************************************/
void pft_fill_unit_parameter(struct pf_parameter *parameter,
			     struct unit *punit)
{
  parameter->start_x = punit->x;
  parameter->start_y = punit->y;
  parameter->moves_left_initially = punit->moves_left;
  parameter->move_rate = unit_move_rate(punit);
  parameter->owner = unit_owner(punit);

  switch (unit_type(punit)->move_type) {
  case LAND_MOVING:
    if (unit_flag(punit, F_IGTER)) {
      parameter->get_MC = igter_move_unit;
    } else {
      parameter->get_MC = normal_move_unit;
    }
    break;
  case SEA_MOVING:
    parameter->get_MC = single_seamove;
    break;
  default:
    die("unknown move_type");
  }

  parameter->zoc_used = (unit_type(punit)->move_type == LAND_MOVING
			 && !unit_flag(punit, F_IGZOC));

  if (unit_flag(punit, F_TRIREME)
      && base_trireme_loss_pct(unit_owner(punit)) > 0) {
    parameter->turn_mode = TM_WORST_TIME;
    parameter->is_pos_dangerous = trireme_is_pos_dangerous;
  } else {
    parameter->is_pos_dangerous = NULL;
  }
}

/**********************************************************************
  Switch on one tile overlapping into the sea/land 
  ("sea/land bombardment")
**********************************************************************/
void pft_fill_unit_overlap_param(struct pf_parameter *parameter,
				 struct unit *punit)
{
  parameter->turn_mode = TM_CAPPED;
  parameter->get_TB = NULL;
  parameter->get_EC = NULL;

  parameter->start_x = punit->x;
  parameter->start_y = punit->y;
  parameter->moves_left_initially = punit->moves_left;
  parameter->move_rate = unit_move_rate(punit);
  parameter->owner = unit_owner(punit);

  switch (unit_type(punit)->move_type) {
  case LAND_MOVING:
    parameter->get_MC = land_overlap_move;
    parameter->get_TB = dont_cross_ocean;
    break;
  case SEA_MOVING:
    parameter->get_MC = sea_overlap_move;
  default:
    die("Unsupported move_type");
  }

  parameter->zoc_used = FALSE;

  if (unit_flag(punit, F_TRIREME)
      && base_trireme_loss_pct(unit_owner(punit)) > 0) {
    parameter->is_pos_dangerous = trireme_is_pos_dangerous;
  } else {
    parameter->is_pos_dangerous = NULL;
  }
}

/**********************************************************************
  Fill general use parameters to defaults
***********************************************************************/
void pft_fill_default_parameter(struct pf_parameter *parameter)
{
  parameter->turn_mode = TM_CAPPED;
  parameter->get_TB = NULL;
  parameter->get_EC = NULL;
}

/**********************************************************************
  Concatenate two paths together.  The additional segment (src_path)
  should start where the initial segment (dest_path) stops.  The
  overlapping position is removed.

  If dest_path == NULL, we just copy the src_path and nothing else.
***********************************************************************/
struct pf_path *pft_concat(struct pf_path *dest_path,
			   const struct pf_path *src_path)
{
  if (!dest_path) {
    dest_path = fc_malloc(sizeof(*dest_path));
    dest_path->length = src_path->length;
    dest_path->positions =
	fc_malloc(sizeof(*dest_path->positions) * dest_path->length);
    memcpy(dest_path->positions, src_path->positions,
	   sizeof(*dest_path->positions) * dest_path->length);
  } else {
    int old_length = dest_path->length;

    assert(pf_last_position(dest_path)->x == src_path->positions[0].x);
    assert(pf_last_position(dest_path)->y == src_path->positions[0].y);
    assert(pf_last_position(dest_path)->moves_left ==
	   src_path->positions[0].moves_left);
    dest_path->length += src_path->length - 1;
    dest_path->positions =
	fc_realloc(dest_path->positions,
		   sizeof(*dest_path->positions) * dest_path->length);
    /* Be careful to include the first position of src_path, it contains
     * the direction (it is undefined in the last position of dest_path) */
    memcpy(dest_path->positions + old_length - 1, src_path->positions,
	   src_path->length * sizeof(*dest_path->positions));
  }
  return dest_path;
}
