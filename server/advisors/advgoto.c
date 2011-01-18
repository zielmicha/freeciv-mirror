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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* common */
#include "ai.h"
#include "game.h"
#include "movement.h"
#include "unit.h"
#include "tile.h"

/* aicore */
#include "path_finding.h"

/* server */
#include "srv_log.h"
#include "unithand.h"
#include "unittools.h"

/* ai */
#include "aiunit.h"

#include "advgoto.h"

static bool adv_unit_move(struct unit *punit, struct tile *ptile);

/**************************************************************************
  Move a unit along a path without disturbing its activity, role
  or assigned destination
  Return FALSE iff we died.
**************************************************************************/
bool adv_follow_path(struct unit *punit, struct pf_path *path,
                     struct tile *ptile)
{
  struct tile *old_tile = punit->goto_tile;
  enum unit_activity activity = punit->activity;
  bool alive;

  if (punit->moves_left <= 0) {
    return TRUE;
  }
  punit->goto_tile = ptile;
  unit_activity_handling(punit, ACTIVITY_GOTO);
  alive = adv_unit_execute_path(punit, path);
  if (alive) {
    unit_activity_handling(punit, ACTIVITY_IDLE);
    send_unit_info(NULL, punit); /* FIXME: probably duplicate */
    unit_activity_handling(punit, activity);
    punit->goto_tile = old_tile; /* May be NULL. */
    send_unit_info(NULL, punit);
  }
  return alive;
}


/*************************************************************************
  This is a function to execute paths returned by the path-finding engine,
  for units controlled by advisors.

  Brings our bodyguard along.
  Returns FALSE only if died.
*************************************************************************/
bool adv_unit_execute_path(struct unit *punit, struct pf_path *path)
{
  const bool is_ai = unit_owner(punit)->ai_controlled;
  int i;

  /* We start with i = 1 for i = 0 is our present position */
  for (i = 1; i < path->length; i++) {
    struct tile *ptile = path->positions[i].tile;
    int id = punit->id;

    if (same_pos(punit->tile, ptile)) {
      UNIT_LOG(LOG_DEBUG, punit, "execute_path: waiting this turn");
      return TRUE;
    }

    /* We use ai_unit_move() for everything but the last step
     * of the way so that we abort if unexpected opposition
     * shows up. Any enemy on the target tile is expected to
     * be our target and any attack there intentional.
     * However, do not annoy human players by automatically attacking
     * using units temporarily under AI control (such as auto-explorers)
     */

    if (is_ai) {
      CALL_PLR_AI_FUNC(unit_move, unit_owner(punit), punit, ptile, path, i);
    } else {
      (void) adv_unit_move(punit, ptile);
    }
    if (!game_unit_by_number(id)) {
      /* Died... */
      return FALSE;
    }

    if (!same_pos(punit->tile, ptile) || punit->moves_left <= 0) {
      /* Stopped (or maybe fought) or ran out of moves */
      return TRUE;
    }
  }

  return TRUE;
}

/**************************************************************************
  Move a unit. Do not attack. Do not leave bodyguard.
  For advisor controlled units.

  This function returns only when we have a reply from the server and
  we can tell the calling function what happened to the move request.
  (Right now it is not a big problem, since we call the server directly.)
**************************************************************************/
static bool adv_unit_move(struct unit *punit, struct tile *ptile)
{
  struct player *pplayer = unit_owner(punit);

  /* if enemy, stop and give a chance for the human player to
     handle this case */
  if (is_enemy_unit_tile(ptile, pplayer)
      || is_enemy_city_tile(ptile, pplayer)) {
    UNIT_LOG(LOG_DEBUG, punit, "movement halted due to enemy presence");
    return FALSE;
  }

  /* Try not to end move next to an enemy if we can avoid it by waiting */
  if (punit->moves_left <= map_move_cost_unit(punit, ptile)
      && unit_move_rate(punit) > map_move_cost_unit(punit, ptile)
      && enemies_at(punit, ptile)
      && !enemies_at(punit, punit->tile)) {
    UNIT_LOG(LOG_DEBUG, punit, "ending move early to stay out of trouble");
    return FALSE;
  }

  /* go */
  unit_activity_handling(punit, ACTIVITY_IDLE);
  (void) unit_move_handling(punit, ptile, FALSE, TRUE);

  return TRUE;
}

/**************************************************************************
  Similar to is_my_zoc(), but with some changes:
  - destination (x0,y0) need not be adjacent?
  - don't care about some directions?
  
  Note this function only makes sense for ground units.

  Fix to bizarre did-not-find bug.  Thanks, Katvrr -- Syela
**************************************************************************/
static bool adv_could_be_my_zoc(struct unit *myunit, struct tile *ptile)
{
  fc_assert_ret_val(is_ground_unit(myunit), TRUE);

  if (same_pos(ptile, myunit->tile))
    return FALSE; /* can't be my zoc */
  if (is_tiles_adjacent(ptile, myunit->tile)
      && !is_non_allied_unit_tile(ptile, unit_owner(myunit)))
    return FALSE;

  adjc_iterate(ptile, atile) {
    if (!is_ocean_tile(atile)
	&& is_non_allied_unit_tile(atile, unit_owner(myunit))) {
      return FALSE;
    }
  } adjc_iterate_end;
  
  return TRUE;
}

/**************************************************************************
  returns:
    0 if can't move
    1 if zoc_ok
   -1 if zoc could be ok?

  see also unithand can_unit_move_to_tile_with_notify()
**************************************************************************/
int adv_could_unit_move_to_tile(struct unit *punit, struct tile *dest_tile)
{
  enum unit_move_result reason =
      unit_move_to_tile_test(unit_type(punit), unit_owner(punit),
                             ACTIVITY_IDLE, punit->tile, 
                             dest_tile, unit_has_type_flag(punit, F_IGZOC));
  switch (reason) {
  case MR_OK:
    return 1;

  case MR_ZOC:
    if (adv_could_be_my_zoc(punit, punit->tile)) {
      return -1;
    }
    break;

  default:
    break;
  };
  return 0;
}
