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

#include <assert.h>
#include <stdio.h>

#include "fcintl.h"
#include "log.h"
#include "map.h"
#include "mem.h"

#include "audio.h"
#include "chatline_g.h"
#include "citydlg_g.h"
#include "civclient.h"
#include "climap.h"
#include "climisc.h"
#include "clinet.h"
#include "dialogs_g.h"
#include "goto.h"
#include "gui_main_g.h"
#include "mapctrl_g.h"
#include "mapview_g.h"
#include "menu_g.h"
#include "options.h"
#include "tilespec.h"

#include "control.h"

/* gui-dep code may adjust depending on tile size etc: */
int num_units_below = MAX_NUM_UNITS_BELOW;

/* unit_focus points to the current unit in focus */
static struct unit *punit_focus = NULL;

/* The previously focused unit.  Focus can generally be recalled on this
 * unit with keypad 5.  FIXME: this is not reset when the client
 * disconnects. */
static int previous_focus_id = -1;

/* These should be set via set_hover_state() */
int hover_unit = 0; /* id of unit hover_state applies to */
enum cursor_hover_state hover_state = HOVER_NONE;
/* This may only be here until client goto is fully implemented.
   It is reset each time the hower_state is reset. */
bool draw_goto_line = TRUE;

/* units involved in current combat */
static struct unit *punit_attacking = NULL;
static struct unit *punit_defending = NULL;

/*
 * This variable is TRUE iff a NON-AI controlled unit was focused this
 * turn.
 */
bool non_ai_unit_focus;

/*************************************************************************/

static struct unit *find_best_focus_candidate(bool accept_current);
static void store_focus(void);
static struct unit *quickselect(struct tile *ptile,
                        enum quickselect_type qtype);

/**************************************************************************
...
**************************************************************************/
void set_hover_state(struct unit *punit, enum cursor_hover_state state)
{
  assert(punit != NULL || state == HOVER_NONE);
  draw_goto_line = TRUE;
  if (punit)
    hover_unit = punit->id;
  else
    hover_unit = 0;
  hover_state = state;
  exit_goto_state();
}

/**************************************************************************
Center on the focus unit, if off-screen and auto_center_on_unit is true.
**************************************************************************/
void auto_center_on_focus_unit(void)
{
  if (punit_focus && auto_center_on_unit &&
      !tile_visible_and_not_on_border_mapcanvas(punit_focus->x, punit_focus->y))
    center_tile_mapcanvas(punit_focus->x, punit_focus->y);
}

/**************************************************************************
note: punit can be NULL
We make sure that the previous focus unit is refreshed, if necessary,
_after_ setting the new focus unit (otherwise if the previous unit is
in a city, the refresh code draws the previous unit instead of the city).

 This function can be called directly from packhand.c as a result of
 Wakeup Focus (when a unit goes from Sentried to Idle in the server).
**************************************************************************/
void set_unit_focus(struct unit *punit)
{
  struct unit *punit_old_focus = punit_focus;

  if (punit != punit_focus) {
    store_focus();
  }

  /*
   *  This should be the ONLY place we _modify_ punit_focus.
   */
  punit_focus = punit;

  if(punit) {
    auto_center_on_focus_unit();

    punit->focus_status=FOCUS_AVAIL;
    refresh_tile_mapcanvas(punit->x, punit->y, FALSE);

    if (unit_has_orders(punit)) {
      /* Clear the focus unit's orders. */
      request_orders_cleared(punit);
    }
    if (punit->activity != ACTIVITY_IDLE || punit->ai.control)  {
      punit->activity = ACTIVITY_IDLE;
      punit->ai.control = FALSE;
      refresh_unit_city_dialogs(punit);
      request_new_unit_activity(punit, ACTIVITY_IDLE);
    }
  }
  
  /* avoid the old focus unit disappearing: */
  if (punit_old_focus
      && (!punit || !same_pos(punit_old_focus->x, punit_old_focus->y,
				   punit->x, punit->y))) {
    refresh_tile_mapcanvas(punit_old_focus->x, punit_old_focus->y, FALSE);
  }

  update_unit_info_label(punit);
  update_menus();
}

/**************************************************************************
 The only difference is that here we draw the "cross".
**************************************************************************/
void set_unit_focus_and_select(struct unit *punit)
{
  set_unit_focus(punit);
  if (punit) {
    put_cross_overlay_tile(punit->x, punit->y);
  }
}

/**************************************************************************
  Store the focus unit.  This is used so that we can return to the
  previously focused unit with an appropriate keypress.
**************************************************************************/
static void store_focus(void)
{
  if (punit_focus) {
    previous_focus_id = punit_focus->id;
  }
}

/**************************************************************************
If there is no unit currently in focus, or if the current unit in
focus should not be in focus, then get a new focus unit.
We let GOTO-ing units stay in focus, so that if they have moves left
at the end of the goto, then they are still in focus.
**************************************************************************/
void update_unit_focus(void)
{
  if (!punit_focus
      || (punit_focus->activity != ACTIVITY_IDLE
	  && !unit_has_orders(punit_focus)
	  && punit_focus->activity != ACTIVITY_GOTO)
      || punit_focus->done_moving
      || punit_focus->moves_left == 0 
      || punit_focus->ai.control) {
    advance_unit_focus();
  }
}

/**************************************************************************
...
**************************************************************************/
struct unit *get_unit_in_focus(void)
{
  return punit_focus;
}

/**************************************************************************
 This function may be called from packhand.c, via update_unit_focus(),
 as a result of packets indicating change in activity for a unit. Also
 called when user press the "Wait" command.
 
 FIXME: Add feature to focus only units of a certain category.
**************************************************************************/
void advance_unit_focus(void)
{
  struct unit *punit_old_focus = punit_focus;
  struct unit *candidate = find_best_focus_candidate(FALSE);

  set_hover_state(NULL, HOVER_NONE);

  if(!candidate) {
    /* First try for "waiting" units. */
    unit_list_iterate(game.player_ptr->units, punit) {
      if(punit->focus_status == FOCUS_WAIT) {
        punit->focus_status = FOCUS_AVAIL;
      }
    } unit_list_iterate_end;
    candidate = find_best_focus_candidate(FALSE);
  }

  /* Accept current focus unit as last resort. */
  if (!candidate) {
    candidate = find_best_focus_candidate(TRUE);
  }

  set_unit_focus(candidate);

  /* 
   * Is the unit which just lost focus a non-AI unit? If yes this
   * enables the auto end turn. 
   */
  if (punit_old_focus && !punit_old_focus->ai.control) {
    non_ai_unit_focus = TRUE;
  }

  /* 
   * Handle auto-turn-done mode: If a unit was in focus (did move),
   * but now none are (no more to move) and there was at least one
   * non-AI unit this turn which was focused, then fake a Turn Done
   * keypress.
   */
  if (auto_turn_done && punit_old_focus && !punit_focus && non_ai_unit_focus) {
    key_end_turn();
  }
}

/**************************************************************************
 Find the nearest available unit for focus, excluding any current unit
 in focus unless "accept_current" is TRUE.  If the current focus unit
 is the only possible unit, or if there is no possible unit, returns NULL.
**************************************************************************/
static struct unit *find_best_focus_candidate(bool accept_current)
{
  struct unit *best_candidate;
  int best_dist = 99999;
  int x, y;

  if (punit_focus)  {
    x = punit_focus->x;
    y = punit_focus->y;
  } else {
    get_center_tile_mapcanvas(&x, &y);
  }

  best_candidate = NULL;
  unit_list_iterate(game.player_ptr->units, punit) {
    if ((punit != punit_focus || accept_current)
      && punit->focus_status == FOCUS_AVAIL
      && punit->activity == ACTIVITY_IDLE
	&& !unit_has_orders(punit)
      && punit->moves_left > 0
      && !punit->done_moving
      && !punit->ai.control) {
        int d = sq_map_distance(punit->x, punit->y, x, y);
        if (d < best_dist) {
          best_candidate = punit;
          best_dist = d;
        }
    }
  } unit_list_iterate_end;
  return best_candidate;
}

/**************************************************************************
Return a pointer to a visible unit, if there is one.
**************************************************************************/
struct unit *find_visible_unit(struct tile *ptile)
{
  struct unit *panyowned = NULL, *panyother = NULL, *ptptother = NULL;

  /* If no units here, return nothing. */
  if (unit_list_size(&ptile->units)==0) {
    return NULL;
  }

  /* If a unit is attacking we should show that on top */
  if (punit_attacking && map_get_tile(punit_attacking->x,punit_attacking->y) == ptile) {
    unit_list_iterate(ptile->units, punit)
      if(punit == punit_attacking) return punit;
    unit_list_iterate_end;
  }

  /* If a unit is defending we should show that on top */
  if (punit_defending && map_get_tile(punit_defending->x,punit_defending->y) == ptile) {
    unit_list_iterate(ptile->units, punit)
      if(punit == punit_defending) return punit;
    unit_list_iterate_end;
  }

  /* If the unit in focus is at this tile, show that on top */
  if (punit_focus && map_get_tile(punit_focus->x,punit_focus->y) == ptile) {
    unit_list_iterate(ptile->units, punit)
      if(punit == punit_focus) return punit;
    unit_list_iterate_end;
  }

  /* If a city is here, return nothing (unit hidden by city). */
  if (ptile->city) {
    return NULL;
  }

  /* Iterate through the units to find the best one we prioritize this way:
       1: owned transporter.
       2: any owned unit
       3: any transporter
       4: any unit
     (always return first in stack). */
  unit_list_iterate(ptile->units, punit)
    if (unit_owner(punit) == game.player_ptr) {
      if (!punit->transported_by) {
        if (get_transporter_capacity(punit) > 0) {
	  return punit;
        } else if (!panyowned) {
	  panyowned = punit;
        }
      }
    } else if (!ptptother && !punit->transported_by &&
	       player_can_see_unit(game.player_ptr, punit)) {
      if (get_transporter_capacity(punit) > 0) {
	ptptother = punit;
      } else if (!panyother) {
	panyother = punit;
      }
    }
  unit_list_iterate_end;

  return (panyowned ? panyowned : (ptptother ? ptptother : panyother));
}

/**************************************************************************
...
**************************************************************************/
void blink_active_unit(void)
{
  static bool is_shown;
  static struct unit *pblinking_unit;
  struct unit *punit;

  if ((punit = punit_focus)) {
    if (punit != pblinking_unit) {
      /* When the focus unit changes, we reset the is_shown flag. */
      pblinking_unit = punit;
      is_shown = TRUE;
    }
    if(is_shown) {
      set_focus_unit_hidden_state(TRUE);
      refresh_tile_mapcanvas(punit->x, punit->y, TRUE);
      set_focus_unit_hidden_state(FALSE);
    } else {
      refresh_tile_mapcanvas(punit->x, punit->y, TRUE);
    }
    is_shown=!is_shown;
  }
}

/**************************************************************************
  Update unit icons (and arrow) in the information display, for specified
  punit as the active unit and other units on the same square.  In practice
  punit is almost always (or maybe strictly always?) the focus unit.
  
  Static vars store some info on current (ie previous) state, to avoid
  unnecessary redraws; initialise to "flag" values to always redraw first
  time.  In principle we _might_ need more info (eg ai.control, connecting),
  but in practice this is enough?
  
  Used to store unit_ids for below units, to use for callbacks (now done
  inside gui-dep set_unit_icon()), but even with ids here they would not
  be enough information to know whether to redraw -- instead redraw every
  time.  (Could store enough info to know, but is it worth it?)
**************************************************************************/
void update_unit_pix_label(struct unit *punit)
{
  static enum unit_activity prev_activity = ACTIVITY_UNKNOWN;
  static bool prev_has_orders = FALSE;
  static Unit_Type_id prev_unit_type = U_LAST;
  static int prev_hp = -1;	         /* or could store ihp cf tilespec.c */
  
  int i;

  /* Check for any change in the unit's state.  This assumes that a unit's
   * orders cannot be changed directly but must be removed and then reset. */
  if (punit && get_client_state() != CLIENT_GAME_OVER_STATE) {
    if (punit->type != prev_unit_type
       || punit->activity != prev_activity
       || punit->has_orders != prev_has_orders
       || punit->hp != prev_hp) {
      set_unit_icon(-1, punit);
      prev_unit_type = punit->type;
      prev_activity = punit->activity;
      prev_has_orders = punit->has_orders;
      prev_hp = punit->hp;
    }

    i = 0;			/* index into unit_below_canvas */
    unit_list_iterate(map_get_tile(punit->x, punit->y)->units, aunit) {
      if (aunit != punit) {
	if (i < num_units_below) {
	  set_unit_icon(i, aunit);
	}
	i++;
      }
    }
    unit_list_iterate_end;
    
    if (i > num_units_below) {
      set_unit_icons_more_arrow(TRUE);
    } else {
      set_unit_icons_more_arrow(FALSE);
      for(; i < num_units_below; i++) {
	set_unit_icon(i, NULL);
      }
    }
  }
  else {
    prev_unit_type = U_LAST;
    prev_activity = ACTIVITY_UNKNOWN;
    prev_has_orders = FALSE;
    prev_hp = -1;
    for(i=-1; i<num_units_below; i++) {
      set_unit_icon(i, NULL);
    }
    set_unit_icons_more_arrow(FALSE);
  }
}

/**************************************************************************
...
**************************************************************************/
void set_units_in_combat(struct unit *pattacker, struct unit *pdefender)
{
  punit_attacking = pattacker;
  punit_defending = pdefender;
}

/**************************************************************************
  Add punit to queue of caravan arrivals, and popup a window for the
  next arrival in the queue, if there is not already a popup, and
  re-checking that a popup is appropriate.
  If punit is NULL, just do for the next arrival in the queue.
**************************************************************************/
void process_caravan_arrival(struct unit *punit)
{
  static struct genlist arrival_queue;
  static bool is_init_arrival_queue = FALSE;
  int *p_id;

  /* arrival_queue is a list of individually malloc-ed ints with
     punit.id values, for units which have arrived. */

  if (!is_init_arrival_queue) {
    genlist_init(&arrival_queue);
    is_init_arrival_queue = TRUE;
  }

  if (punit) {
    p_id = fc_malloc(sizeof(int));
    *p_id = punit->id;
    genlist_insert(&arrival_queue, p_id, -1);
  }

  /* There can only be one dialog at a time: */
  if (caravan_dialog_is_open()) {
    return;
  }
  
  while (genlist_size(&arrival_queue) > 0) {
    int id;
    
    p_id = genlist_get(&arrival_queue, 0);
    genlist_unlink(&arrival_queue, p_id);
    id = *p_id;
    free(p_id);
    p_id = NULL;
    punit = player_find_unit_by_id(game.player_ptr, id);

    if (punit && (unit_can_help_build_wonder_here(punit)
		  || unit_can_est_traderoute_here(punit))
	&& (!game.player_ptr->ai.control || ai_popup_windows)) {
      struct city *pcity_dest = map_get_city(punit->x, punit->y);
      struct city *pcity_homecity = find_city_by_id(punit->homecity);
      if (pcity_dest && pcity_homecity) {
	popup_caravan_dialog(punit, pcity_homecity, pcity_dest);
	return;
      }
    }
  }
}

/**************************************************************************
  Add punit/pcity to queue of diplomat arrivals, and popup a window for
  the next arrival in the queue, if there is not already a popup, and
  re-checking that a popup is appropriate.
  If punit is NULL, just do for the next arrival in the queue.
**************************************************************************/
void process_diplomat_arrival(struct unit *pdiplomat, int victim_id)
{
  static struct genlist arrival_queue;
  static bool is_init_arrival_queue = FALSE;
  int *p_ids;

  /* arrival_queue is a list of individually malloc-ed int[2]s with
     punit.id and pcity.id values, for units which have arrived. */

  if (!is_init_arrival_queue) {
    genlist_init(&arrival_queue);
    is_init_arrival_queue = TRUE;
  }

  if (pdiplomat && victim_id != 0) {
    p_ids = fc_malloc(2*sizeof(int));
    p_ids[0] = pdiplomat->id;
    p_ids[1] = victim_id;
    genlist_insert(&arrival_queue, p_ids, -1);
  }

  /* There can only be one dialog at a time: */
  if (diplomat_dialog_is_open()) {
    return;
  }

  while (genlist_size(&arrival_queue) > 0) {
    int diplomat_id, victim_id;
    struct city *pcity;
    struct unit *punit;

    p_ids = genlist_get(&arrival_queue, 0);
    genlist_unlink(&arrival_queue, p_ids);
    diplomat_id = p_ids[0];
    victim_id = p_ids[1];
    free(p_ids);
    p_ids = NULL;
    pdiplomat = player_find_unit_by_id(game.player_ptr, diplomat_id);
    pcity = find_city_by_id(victim_id);
    punit = find_unit_by_id(victim_id);

    if (!pdiplomat || !unit_flag(pdiplomat, F_DIPLOMAT))
      continue;

    if (punit
	&& is_diplomat_action_available(pdiplomat, DIPLOMAT_ANY_ACTION,
					punit->x, punit->y)
	&& diplomat_can_do_action(pdiplomat, DIPLOMAT_ANY_ACTION,
				  punit->x, punit->y)) {
      popup_diplomat_dialog(pdiplomat, punit->x, punit->y);
      return;
    } else if (pcity
	       && is_diplomat_action_available(pdiplomat, DIPLOMAT_ANY_ACTION,
					       pcity->x, pcity->y)
	       && diplomat_can_do_action(pdiplomat, DIPLOMAT_ANY_ACTION,
					 pcity->x, pcity->y)) {
      popup_diplomat_dialog(pdiplomat, pcity->x, pcity->y);
      return;
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_goto(void)
{
  struct unit *punit = punit_focus;

  if (!punit)
    return;

  if (hover_state != HOVER_GOTO) {
    set_hover_state(punit, HOVER_GOTO);
    update_unit_info_label(punit);
    /* Not yet implemented for air units, including helicopters. */
    if (is_air_unit(punit) || is_heli_unit(punit)) {
      draw_goto_line = FALSE;
    } else {
      enter_goto_state(punit);
      create_line_at_mouse_pos();
    }
  } else if (!is_air_unit(punit) && !is_heli_unit(punit)) {
    assert(goto_is_active());
    goto_add_waypoint();
  }
}

/**************************************************************************
prompt player for entering destination point for unit connect
(e.g. connecting with roads)
**************************************************************************/
void request_unit_connect(void)
{
  if (punit_focus && can_unit_do_connect (punit_focus, ACTIVITY_IDLE)) {
    set_hover_state(punit_focus, HOVER_CONNECT);
    update_unit_info_label(punit_focus);
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_unload(struct unit *punit)
{
  if(get_transporter_capacity(punit) == 0) {
    append_output_window(_("Game: Only transporter units can be unloaded."));
    return;
  }

  request_unit_wait(punit);    /* RP: unfocus the ship */
  
  dsend_packet_unit_unload(&aconnection, punit->id);
}

/**************************************************************************
...
**************************************************************************/
void request_unit_airlift(struct unit *punit, struct city *pcity)
{
  dsend_packet_unit_airlift(&aconnection, punit->id,pcity->id);
}

/**************************************************************************
  Return-and-recover for a particular unit.  This sets the unit to GOTO
  the nearest city.
**************************************************************************/
void request_unit_return(struct unit *punit)
{
  struct pf_path *path;

  if (is_air_unit(punit) || is_heli_unit(punit)) {
    /* PF doesn't support air or helicopter units yet. */
    return;
  }

  if ((path = path_to_nearest_allied_city(punit))) {
    send_goto_path(punit, path);
    pf_destroy_path(path);
  }
}

/**************************************************************************
(RP:) un-sentry all my own sentried units on punit's tile
**************************************************************************/
void request_unit_wakeup(struct unit *punit)
{
  wakeup_sentried_units(punit->x,punit->y);
}

/**************************************************************************
  Request a diplomat to do a specific action.
  - action : The action to be requested.
  - dipl_id : The unit ID of the diplomatic unit.
  - target_id : The ID of the target unit or city.
  - value : For DIPLOMAT_STEAL or DIPLOMAT_SABOTAGE, the technology
            or building to aim for (spies only).
**************************************************************************/
void request_diplomat_action(enum diplomat_actions action, int dipl_id,
			     int target_id, int value)
{
  dsend_packet_unit_diplomat_action(&aconnection, dipl_id,action,target_id,value);
}

void wakeup_sentried_units(int x, int y)
{
  unit_list_iterate(map_get_tile(x,y)->units, punit) {
    if(punit->activity==ACTIVITY_SENTRY && game.player_idx==punit->owner) {
      request_new_unit_activity(punit, ACTIVITY_IDLE);
    }
  }
  unit_list_iterate_end;
}

/**************************************************************************
Player pressed 'b' or otherwise instructed unit to build or add to city.
If the unit can build a city, we popup the appropriate dialog.
Otherwise, we just send a packet to the server.
If this action is not appropriate, the server will respond
with an appropriate message.  (This is to avoid duplicating
all the server checks and messages here.)
**************************************************************************/
void request_unit_build_city(struct unit *punit)
{
  if (can_unit_build_city(punit)) {
    dsend_packet_city_name_suggestion_req(&aconnection, punit->id);
    /* the reply will trigger a dialog to name the new city */
  } else {
    char name[] = "";
    dsend_packet_unit_build_city(&aconnection, punit->id, name);
  }
}

/**************************************************************************
  This function is called whenever the player pressed an arrow key.

  We do NOT take into account that punit might be a caravan or a diplomat
  trying to move into a city, or a diplomat going into a tile with a unit;
  the server will catch those cases and send the client a package to pop up
  a dialog. (the server code has to be there anyway as goto's are entirely
  in the server)
**************************************************************************/
void request_move_unit_direction(struct unit *punit, int dir)
{
  int dest_x, dest_y;

  /* Catches attempts to move off map */
  if (!MAPSTEP(dest_x, dest_y, punit->x, punit->y, dir)) {
    return;
  }

  if (punit->moves_left > 0) {
    dsend_packet_unit_move(&aconnection, punit->id, dest_x,dest_y);
  } else {
    /* Initiate a "goto" with direction keys for exhausted units. */
    send_goto_unit(punit, dest_x, dest_y);
  }
}

/**************************************************************************
...
**************************************************************************/
void request_new_unit_activity(struct unit *punit, enum unit_activity act)
{
  dsend_packet_unit_change_activity(&aconnection, punit->id, act,
				    S_NO_SPECIAL);
}

/**************************************************************************
...
**************************************************************************/
void request_new_unit_activity_targeted(struct unit *punit,
					enum unit_activity act,
					enum tile_special_type tgt)
{
  dsend_packet_unit_change_activity(&aconnection, punit->id, act, tgt);
}

/**************************************************************************
...
**************************************************************************/
void request_unit_disband(struct unit *punit)
{
  dsend_packet_unit_disband(&aconnection, punit->id);
}

/**************************************************************************
...
**************************************************************************/
void request_unit_change_homecity(struct unit *punit)
{
  struct city *pcity=map_get_city(punit->x, punit->y);
  
  if (pcity) {
    dsend_packet_unit_change_homecity(&aconnection, punit->id, pcity->id);
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_upgrade(struct unit *punit)
{
  struct city *pcity=map_get_city(punit->x, punit->y);

  if (pcity) {
    dsend_packet_unit_upgrade(&aconnection, punit->id);
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_auto(struct unit *punit)
{
  if (can_unit_do_auto(punit)) {
    dsend_packet_unit_auto(&aconnection, punit->id);
  } else {
    append_output_window(_("Game: Only settler units and military units"
			   " in cities can be put in auto-mode."));
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_caravan_action(struct unit *punit, enum packet_type action)
{
  if (!map_get_city(punit->x, punit->y)) {
    return;
  }

  if (action == PACKET_UNIT_ESTABLISH_TRADE) {
    dsend_packet_unit_establish_trade(&aconnection, punit->id);
  } else if (action == PACKET_UNIT_HELP_BUILD_WONDER) {
    dsend_packet_unit_help_build_wonder(&aconnection, punit->id);
  } else {
    freelog(LOG_ERROR, "Bad action (%d) in request_unit_caravan_action",
	    action);
  }
}

/**************************************************************************
 Explode nuclear at a tile without enemy units
**************************************************************************/
void request_unit_nuke(struct unit *punit)
{
  if(!unit_flag(punit, F_NUCLEAR)) {
    append_output_window(_("Game: Only nuclear units can do this."));
    return;
  }
  if(punit->moves_left == 0)
    do_unit_nuke(punit);
  else {
    set_hover_state(punit, HOVER_NUKE);
    update_unit_info_label(punit);
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_paradrop(struct unit *punit)
{
  if(!unit_flag(punit, F_PARATROOPERS)) {
    append_output_window(_("Game: Only paratrooper units can do this."));
    return;
  }
  if(!can_unit_paradrop(punit))
    return;

  set_hover_state(punit, HOVER_PARADROP);
  update_unit_info_label(punit);
}

/**************************************************************************
...
**************************************************************************/
void request_unit_patrol(void)
{
  struct unit *punit = punit_focus;

  if (!punit)
    return;

  if (hover_state != HOVER_PATROL) {
    set_hover_state(punit, HOVER_PATROL);
    update_unit_info_label(punit);
    /* Not yet implemented for air units, including helicopters. */
    if (is_air_unit(punit) || is_heli_unit(punit)) {
      draw_goto_line = FALSE;
    } else {
      enter_goto_state(punit);
      create_line_at_mouse_pos();
    }
  } else {
    assert(goto_is_active());
    goto_add_waypoint();
  }
}

/****************************************************************
...
*****************************************************************/
void request_unit_sentry(struct unit *punit)
{
  if(punit->activity!=ACTIVITY_SENTRY &&
     can_unit_do_activity(punit, ACTIVITY_SENTRY))
    request_new_unit_activity(punit, ACTIVITY_SENTRY);
}

/****************************************************************
...
*****************************************************************/
void request_unit_fortify(struct unit *punit)
{
  if(punit->activity!=ACTIVITY_FORTIFYING &&
     can_unit_do_activity(punit, ACTIVITY_FORTIFYING))
    request_new_unit_activity(punit, ACTIVITY_FORTIFYING);
}

/**************************************************************************
...
**************************************************************************/
void request_unit_pillage(struct unit *punit)
{
  struct tile *ptile = map_get_tile(punit->x, punit->y);
  enum tile_special_type pspresent = get_tile_infrastructure_set(ptile);
  enum tile_special_type psworking =
      get_unit_tile_pillage_set(punit->x, punit->y);
  enum tile_special_type what =
      get_preferred_pillage(pspresent & (~psworking));
  enum tile_special_type would =
      what | map_get_infrastructure_prerequisite(what);

  if ((game.rgame.pillage_select) &&
      ((pspresent & (~(psworking | would))) != S_NO_SPECIAL)) {
    popup_pillage_dialog(punit, (pspresent & (~psworking)));
  } else {
    request_new_unit_activity_targeted(punit, ACTIVITY_PILLAGE, what);
  }
}

/**************************************************************************
 Toggle display of grid lines on the map
**************************************************************************/
void request_toggle_map_grid(void) 
{
  if (!can_client_change_view()) {
    return;
  }

  draw_map_grid^=1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of national borders on the map
**************************************************************************/
void request_toggle_map_borders(void) 
{
  if (!can_client_change_view()) {
    return;
  }

  draw_borders ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of city names
**************************************************************************/
void request_toggle_city_names(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_city_names ^= 1;
  update_map_canvas_visible();
}
 
 /**************************************************************************
 Toggle display of city growth (turns-to-grow)
**************************************************************************/
void request_toggle_city_growth(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_city_growth ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of city productions
**************************************************************************/
void request_toggle_city_productions(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_city_productions ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of terrain
**************************************************************************/
void request_toggle_terrain(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_terrain ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of coastline
**************************************************************************/
void request_toggle_coastline(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_coastline ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of roads and rails
**************************************************************************/
void request_toggle_roads_rails(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_roads_rails ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of irrigation
**************************************************************************/
void request_toggle_irrigation(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_irrigation ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of mines
**************************************************************************/
void request_toggle_mines(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_mines ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of fortress and airbase
**************************************************************************/
void request_toggle_fortress_airbase(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_fortress_airbase ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of specials
**************************************************************************/
void request_toggle_specials(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_specials ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of pollution
**************************************************************************/
void request_toggle_pollution(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_pollution ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of cities
**************************************************************************/
void request_toggle_cities(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_cities ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of units
**************************************************************************/
void request_toggle_units(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_units ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of focus unit
**************************************************************************/
void request_toggle_focus_unit(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_focus_unit ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of fog of war
**************************************************************************/
void request_toggle_fog_of_war(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_fog_of_war ^= 1;
  update_map_canvas_visible();
  refresh_overview_canvas();
}

/**************************************************************************
...
**************************************************************************/
void request_center_focus_unit(void)
{
  if (punit_focus) {
    center_tile_mapcanvas(punit_focus->x, punit_focus->y);
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_wait(struct unit *punit)
{
  punit->focus_status=FOCUS_WAIT;
  if (punit == punit_focus) {
    advance_unit_focus();
    /* set_unit_focus(punit_focus); */  /* done in advance_unit_focus */
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_move_done(void)
{
  if (punit_focus) {
    punit_focus->focus_status = FOCUS_DONE;
    advance_unit_focus();
    /* set_unit_focus(punit_focus); */  /* done in advance_unit_focus */
  }
}

/**************************************************************************
  Called to have the client move a unit from one location to another,
  updating the graphics if necessary.
**************************************************************************/
void do_move_unit(struct unit *punit, struct unit *target_unit, bool carried)
{
  int x, y;
  bool was_teleported;
  
  was_teleported = !is_tiles_adjacent(punit->x, punit->y,
				      target_unit->x, target_unit->y);
  x = punit->x;
  y = punit->y;

  if (!was_teleported && punit->activity != ACTIVITY_SENTRY && !carried) {
    audio_play_sound(unit_type(punit)->sound_move,
		     unit_type(punit)->sound_move_alt);
  }

  unit_list_unlink(&map_get_tile(x, y)->units, punit);

  if (!carried) {
    refresh_tile_mapcanvas(x, y, FALSE);
  }
  
  if (game.player_idx == punit->owner
      && auto_center_on_unit
      && !unit_has_orders(punit)
      && punit->activity != ACTIVITY_GOTO
      && punit->activity != ACTIVITY_SENTRY
      && !tile_visible_and_not_on_border_mapcanvas(target_unit->x,
                                                   target_unit->y)) {
    center_tile_mapcanvas(target_unit->x, target_unit->y);
  }

  if (!carried && !was_teleported) {
    int dx, dy;

    map_distance_vector(&dx, &dy, punit->x, punit->y,
                        target_unit->x, target_unit->y);
    if (smooth_move_units) {
      move_unit_map_canvas(punit, x, y, dx, dy);
    }
    refresh_tile_mapcanvas(x, y, FALSE);
  }
    
  punit->x = target_unit->x;
  punit->y = target_unit->y;

  unit_list_insert(&map_get_tile(punit->x, punit->y)->units, punit);

  square_iterate(punit->x, punit->y, 2, x, y) {
    bool refresh = FALSE;
    unit_list_iterate(map_get_tile(x, y)->units, pu) {
      if (unit_flag(pu, F_PARTIAL_INVIS)) {
	refresh = TRUE;
	goto out;
      }
    } unit_list_iterate_end;
  out:
    if (refresh) {
      refresh_tile_mapcanvas(x, y, FALSE);
    }
  } square_iterate_end;
  
  if (!carried && tile_get_known(punit->x, punit->y) == TILE_KNOWN) {
    refresh_tile_mapcanvas(punit->x, punit->y, FALSE);
  }

  if (punit_focus == punit) update_menus();
}

/**************************************************************************
 Handles everything when the user clicked a tile
**************************************************************************/
void do_map_click(int xtile, int ytile, enum quickselect_type qtype)
{
  struct city *pcity = map_get_city(xtile, ytile);
  struct tile *ptile = map_get_tile(xtile, ytile);
  struct unit *punit = player_find_unit_by_id(game.player_ptr, hover_unit);
  bool maybe_goto = FALSE;

  if (punit && hover_state != HOVER_NONE) {
    switch (hover_state) {
    case HOVER_NONE:
      die("well; shouldn't get here :)");
    case HOVER_GOTO:
      do_unit_goto(xtile, ytile);
      break;
    case HOVER_NUKE:
      if (3 * real_map_distance(punit->x, punit->y, xtile, ytile)
	  > punit->moves_left) {
        append_output_window(_("Game: Too far for this unit."));
      } else {
	do_unit_goto(xtile, ytile);
	/* note that this will be executed by the server after the goto */
	if (!pcity)
	  do_unit_nuke(punit);
      }
      break;
    case HOVER_PARADROP:
      do_unit_paradrop_to(punit, xtile, ytile);
      break;
    case HOVER_CONNECT:
      popup_unit_connect_dialog(punit, xtile, ytile);
      break;
    case HOVER_PATROL:
      do_unit_patrol_to(punit, xtile, ytile);
      break;	
    }
    set_hover_state(NULL, HOVER_NONE);
    update_unit_info_label(punit);
  }

  /* Bypass stack or city popup if quickselect is specified. */
  else if (qtype) {
    struct unit *qunit = quickselect(ptile, qtype);
    if (qunit) {
      set_unit_focus_and_select(qunit);
      maybe_goto = keyboardless_goto;
    }
  }
  /* Otherwise use popups. */
  else if (pcity && game.player_idx==pcity->owner) {
    popup_city_dialog(pcity, FALSE);
  }
  else if (unit_list_size(&ptile->units) == 0 && !pcity
           && punit_focus) {
    maybe_goto = keyboardless_goto;
  }
  else if (unit_list_size(&ptile->units) == 1
      && !unit_list_get(&ptile->units, 0)->occupy) {
    struct unit *punit=unit_list_get(&ptile->units, 0);
    if(game.player_idx==punit->owner) {
      if(can_unit_do_activity(punit, ACTIVITY_IDLE)) {
        maybe_goto = keyboardless_goto;
	set_unit_focus_and_select(punit);
      }
    } else if (pcity) {
      /* Don't hide the unit in the city. */
      popup_unit_select_dialog(ptile);
    }
  }
  else if(unit_list_size(&ptile->units) > 0) {
    /* The stack list is always popped up, even if it includes enemy units.
     * If the server doesn't want the player to know about them it shouldn't
     * tell him!  The previous behavior would only pop up the stack if you
     * owned a unit on the tile.  This gave cheating clients an advantage,
     * and also showed you allied units if (and only if) you had a unit on
     * the tile (inconsistent). */
    popup_unit_select_dialog(ptile);
  }

  /* See mapctrl_common.c */
  keyboardless_goto_start_x = maybe_goto ? xtile : -1;
  keyboardless_goto_start_y = maybe_goto ? ytile : -1;
  keyboardless_goto_button_down = maybe_goto;
  keyboardless_goto_active = FALSE;
}

/**************************************************************************
 Quickselecting a unit is normally done with <control> left, right click,
 or keypad / * for the current tile. Bypassing the stack popup is quite
 convenient, and can be tactically important in furious multiplayer games.
**************************************************************************/
static struct unit *quickselect(struct tile *ptile,
                          enum quickselect_type qtype)
{
  int listsize = unit_list_size(&ptile->units);
  struct unit *panytransporter = NULL,
              *panymovesea  = NULL, *panysea  = NULL,
              *panymoveland = NULL, *panyland = NULL,
              *panymoveunit = NULL, *panyunit = NULL;

  assert(qtype > SELECT_POPUP);

  if (listsize == 0) {
    return NULL;
  } else if (listsize == 1) {
    struct unit *punit = unit_list_get(&ptile->units, 0);
    return (game.player_idx == punit->owner) ? punit : NULL;
  }

  /*  Quickselect priorities. Units with moves left
   *  before exhausted. Focus unit is excluded.
   *
   *    SEA:  Transporter
   *          Sea unit
   *          Any unit
   *
   *    LAND: Military land unit
   *          Non-combatant
   *          Sea unit
   *          Any unit
   */

    unit_list_iterate(ptile->units, punit)  {
  if(game.player_idx != punit->owner || punit == punit_focus) {
    continue;
  }
  if (qtype == SELECT_SEA) {
    /* Transporter. */
    if (get_transporter_capacity(punit)) {
      if (punit->moves_left > 0) {
        return punit;
      } else if (!panytransporter) {
        panytransporter = punit;
      }
    }
    /* Any sea, pref. moves left. */
    else if (is_sailing_unit(punit)) {
      if (punit->moves_left > 0) {
        if (!panymovesea) {
          panymovesea = punit;
        }
      } else if (!panysea) {
          panysea = punit;
      }
    }
  } else if (qtype == SELECT_LAND) {
    if (is_ground_unit(punit))  {
      if (punit->moves_left > 0) {
        if (is_military_unit(punit)) {
          return punit;
        } else if (!panymoveland) {
            panymoveland = punit;
        }
      } else if (!panyland) {
        panyland = punit;
      }
    }
    else if (is_sailing_unit(punit)) {
      if (punit->moves_left > 0) {
        panymovesea = punit;
      } else {
        panysea = punit;
      }
    }
  }
  if (punit->moves_left > 0 && !panymoveunit) {
    panymoveunit = punit;
  }
  if (!panyunit) {
    panyunit = punit;
  }
    } unit_list_iterate_end;

  if (qtype == SELECT_SEA) {
    if (panytransporter) {
      return panytransporter;
    } else if (panymovesea) {
      return panymovesea;
    } else if (panysea) {
      return panysea;
    } else if (panymoveunit) {
      return panymoveunit;
    } else if (panyunit) {
      return panyunit;
    }
  }
  else if (qtype == SELECT_LAND) {
    if (panymoveland) {
      return panymoveland;
    } else if (panyland) {
      return panyland;
    } else if (panymovesea) {
      return panymovesea;
    } else if (panysea) {
      return panysea;
    } else if (panymoveunit) {
      return panymoveunit;
    } else if (panyunit) {
      return panyunit;
    }
  }
  return NULL;
}

/**************************************************************************
 Finish the goto mode and let the unit which is stored in hover_unit move
 to a given location.
**************************************************************************/
void do_unit_goto(int x, int y)
{
  struct unit *punit = player_find_unit_by_id(game.player_ptr, hover_unit);

  if (hover_unit == 0 || hover_state != HOVER_GOTO)
    return;

  if (punit) {
    if (!draw_goto_line) {
      send_goto_unit(punit, x, y);
    } else {
      int dest_x, dest_y;
      draw_line(x, y);
      get_line_dest(&dest_x, &dest_y);
      if (same_pos(dest_x, dest_y, x, y)) {
	send_goto_route(punit);
      } else {
	append_output_window(_("Game: Didn't find a route to the destination!"));
      }
    }
  }

  set_hover_state(NULL, HOVER_NONE);
}

/**************************************************************************
Explode nuclear at a tile without enemy units
**************************************************************************/
void do_unit_nuke(struct unit *punit)
{
  dsend_packet_unit_nuke(&aconnection, punit->id);
}

/**************************************************************************
Paradrop to a location
**************************************************************************/
void do_unit_paradrop_to(struct unit *punit, int x, int y)
{
  dsend_packet_unit_paradrop_to(&aconnection, punit->id, x, y);
}
 
/**************************************************************************
Paradrop to a location
**************************************************************************/
void do_unit_patrol_to(struct unit *punit, int x, int y)
{
  if (is_air_unit(punit)) {
    append_output_window(_("Game: Sorry, airunit patrol not yet implemented."));
    return;
  } else {
    int dest_x, dest_y;
    draw_line(x, y);
    get_line_dest(&dest_x, &dest_y);
    if (same_pos(dest_x, dest_y, x, y)) {
      send_patrol_route(punit);
    } else {
      append_output_window(_("Game: Didn't find a route to the destination!"));
    }
  }

  set_hover_state(NULL, HOVER_NONE);
}
 
/**************************************************************************
 The 'Escape' key.
**************************************************************************/
void key_cancel_action(void)
{
  bool popped = FALSE;

  cancel_tile_hiliting();

  if (hover_state == HOVER_GOTO || hover_state == HOVER_PATROL)
    if (draw_goto_line)
      popped = goto_pop_waypoint();

  if (hover_state != HOVER_NONE && !popped) {
    struct unit *punit = player_find_unit_by_id(game.player_ptr, hover_unit);

    set_hover_state(NULL, HOVER_NONE);
    update_unit_info_label(punit);

    keyboardless_goto_button_down = FALSE;
    keyboardless_goto_active = FALSE;
    keyboardless_goto_start_x =
    keyboardless_goto_start_y = -1;
  }
}

/**************************************************************************
  Center the mapview on the player's capital, or print a failure message.
**************************************************************************/
void key_center_capital(void)
{
  struct city *capital = find_palace(game.player_ptr);

  if (capital)  {
    /* Center on the tile, and pop up the crosshair overlay. */
    center_tile_mapcanvas(capital->x, capital->y);
    put_cross_overlay_tile(capital->x, capital->y);
  } else {
    append_output_window(_("Game: Oh my! You seem to have no capital!"));
  }
}

/**************************************************************************
...
**************************************************************************/
void key_end_turn(void)
{
  send_turn_done();
}

/**************************************************************************
  Recall the previous focus unit and focus on it.  See store_focus().
**************************************************************************/
void key_recall_previous_focus_unit(void)
{
  struct unit *punit = player_find_unit_by_id(game.player_ptr,
                                              previous_focus_id);
  if (punit) {
    set_unit_focus_and_select(punit);
  }
}

/**************************************************************************
  Move the focus unit in the given direction.  Here directions are
  defined according to the GUI, so that north is "up" in the interface.
**************************************************************************/
void key_unit_move(enum direction8 gui_dir)
{
  if (punit_focus) {
    enum direction8 map_dir = gui_to_map_dir(gui_dir);
    request_move_unit_direction(punit_focus, map_dir);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_build_city(void)
{
  if (punit_focus) {
    request_unit_build_city(punit_focus);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_build_wonder(void)
{
  if (punit_focus && unit_flag(punit_focus, F_HELP_WONDER)) {
    request_unit_caravan_action(punit_focus, PACKET_UNIT_HELP_BUILD_WONDER);
  }
}

/**************************************************************************
handle user pressing key for 'Connect' command
**************************************************************************/
void key_unit_connect(void)
{
  if (punit_focus) {
    request_unit_connect();
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_diplomat_actions(void)
{
  struct city *pcity;		/* need pcity->id */
  if (punit_focus
     && is_diplomat_unit(punit_focus)
     && (pcity = map_get_city(punit_focus->x, punit_focus->y))
     && !diplomat_dialog_is_open()    /* confusing otherwise? */
     && diplomat_can_do_action(punit_focus, DIPLOMAT_ANY_ACTION,
			       punit_focus->x, punit_focus->y))
     process_diplomat_arrival(punit_focus, pcity->id);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_done(void)
{
  if (punit_focus) {
    request_unit_move_done();
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_goto(void)
{
  if (punit_focus) {
    request_unit_goto();
  }
}

/**************************************************************************
Explode nuclear at a tile without enemy units
**************************************************************************/
void key_unit_nuke(void)
{
  if (punit_focus) {
    request_unit_nuke(punit_focus);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_paradrop(void)
{
  if (punit_focus && can_unit_paradrop(punit_focus)) {
    request_unit_paradrop(punit_focus);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_patrol(void)
{
  if (punit_focus) {
    request_unit_patrol();
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_traderoute(void)
{
  if (punit_focus && unit_flag(punit_focus, F_TRADE_ROUTE)) {
    request_unit_caravan_action(punit_focus, PACKET_UNIT_ESTABLISH_TRADE);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_unload(void)
{
  if (punit_focus) {
    request_unit_unload(punit_focus);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_wait(void)
{
  if (punit_focus) {
    request_unit_wait(punit_focus);
  }
}

/**************************************************************************
...
***************************************************************************/
void key_unit_wakeup_others(void)
{
  if (punit_focus) {
    request_unit_wakeup(punit_focus);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_airbase(void)
{
  if (punit_focus &&
      can_unit_do_activity(punit_focus, ACTIVITY_AIRBASE)) {
    request_new_unit_activity(punit_focus, ACTIVITY_AIRBASE);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_auto_attack(void)
{
  if (punit_focus && !unit_flag(punit_focus, F_SETTLERS) &&
      can_unit_do_auto(punit_focus)) {
    request_unit_auto(punit_focus);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_auto_explore(void)
{
  if (punit_focus &&
      can_unit_do_activity(punit_focus, ACTIVITY_EXPLORE)) {
    request_new_unit_activity(punit_focus, ACTIVITY_EXPLORE);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_auto_settle(void)
{
  if (punit_focus && unit_flag(punit_focus, F_SETTLERS) &&
      can_unit_do_auto(punit_focus)) {
    request_unit_auto(punit_focus);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_disband(void)
{
  if (punit_focus) {
    request_unit_disband(punit_focus);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_fallout(void)
{
  if (punit_focus &&
      can_unit_do_activity(punit_focus, ACTIVITY_FALLOUT)) {
    request_new_unit_activity(punit_focus, ACTIVITY_FALLOUT);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_fortify(void)
{
  if (punit_focus &&
      can_unit_do_activity(punit_focus, ACTIVITY_FORTIFYING)) {
    request_new_unit_activity(punit_focus, ACTIVITY_FORTIFYING);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_fortress(void)
{
  if (punit_focus &&
      can_unit_do_activity(punit_focus, ACTIVITY_FORTRESS)) {
    request_new_unit_activity(punit_focus, ACTIVITY_FORTRESS);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_homecity(void)
{
  if (punit_focus) {
    request_unit_change_homecity(punit_focus);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_irrigate(void)
{
  if (punit_focus &&
      can_unit_do_activity(punit_focus, ACTIVITY_IRRIGATE)) {
    request_new_unit_activity(punit_focus, ACTIVITY_IRRIGATE);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_mine(void)
{
  if (punit_focus &&
      can_unit_do_activity(punit_focus, ACTIVITY_MINE)) {
    request_new_unit_activity(punit_focus, ACTIVITY_MINE);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_pillage(void)
{
  if (punit_focus &&
      can_unit_do_activity(punit_focus, ACTIVITY_PILLAGE)) {
    request_unit_pillage(punit_focus);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_pollution(void)
{
  if (punit_focus &&
      can_unit_do_activity(punit_focus, ACTIVITY_POLLUTION)) {
    request_new_unit_activity(punit_focus, ACTIVITY_POLLUTION);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_road(void)
{
  if (punit_focus) {
    if(can_unit_do_activity(punit_focus, ACTIVITY_ROAD))
      request_new_unit_activity(punit_focus, ACTIVITY_ROAD);
    else if(can_unit_do_activity(punit_focus, ACTIVITY_RAILROAD))
      request_new_unit_activity(punit_focus, ACTIVITY_RAILROAD);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_sentry(void)
{
  if (punit_focus &&
      can_unit_do_activity(punit_focus, ACTIVITY_SENTRY)) {
    request_new_unit_activity(punit_focus, ACTIVITY_SENTRY);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_transform(void)
{
  if (punit_focus &&
      can_unit_do_activity(punit_focus, ACTIVITY_TRANSFORM)) {
    request_new_unit_activity(punit_focus, ACTIVITY_TRANSFORM);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_map_grid_toggle(void)
{
  request_toggle_map_grid();
}

/**************************************************************************
  Toggle map borders on the mapview on/off based on a keypress.
**************************************************************************/
void key_map_borders_toggle(void)
{
  request_toggle_map_borders();
}

/**************************************************************************
...
**************************************************************************/
void key_city_names_toggle(void)
{
  request_toggle_city_names();
}

/**************************************************************************
  Toggles the "show city growth turns" option by passing off the
  request to another function...
**************************************************************************/
void key_city_growth_toggle(void)
{
  request_toggle_city_growth();
}

/**************************************************************************
...
**************************************************************************/
void key_city_productions_toggle(void)
{
  request_toggle_city_productions();
}

/**************************************************************************
...
**************************************************************************/
void key_terrain_toggle(void)
{
  request_toggle_terrain();
}

/**************************************************************************
...
**************************************************************************/
void key_coastline_toggle(void)
{
  request_toggle_coastline();
}

/**************************************************************************
...
**************************************************************************/
void key_roads_rails_toggle(void)
{
  request_toggle_roads_rails();
}

/**************************************************************************
...
**************************************************************************/
void key_irrigation_toggle(void)
{
  request_toggle_irrigation();
}

/**************************************************************************
...
**************************************************************************/
void key_mines_toggle(void)
{
  request_toggle_mines();
}

/**************************************************************************
...
**************************************************************************/
void key_fortress_airbase_toggle(void)
{
  request_toggle_fortress_airbase();
}

/**************************************************************************
...
**************************************************************************/
void key_specials_toggle(void)
{
  request_toggle_specials();
}

/**************************************************************************
...
**************************************************************************/
void key_pollution_toggle(void)
{
  request_toggle_pollution();
}

/**************************************************************************
...
**************************************************************************/
void key_cities_toggle(void)
{
  request_toggle_cities();
}

/**************************************************************************
...
**************************************************************************/
void key_units_toggle(void)
{
  request_toggle_units();
}

/**************************************************************************
...
**************************************************************************/
void key_focus_unit_toggle(void)
{
  request_toggle_focus_unit();
}

/**************************************************************************
...
**************************************************************************/
void key_fog_of_war_toggle(void)
{
  request_toggle_fog_of_war();
}

/**************************************************************************
...
**************************************************************************/
void key_quickselect(enum quickselect_type qtype)
{
  struct unit *punit;

  if(punit_focus) {
    punit = quickselect(map_get_tile(punit_focus->x, punit_focus->y),
                        qtype);
    set_unit_focus_and_select(punit);
  }
}
