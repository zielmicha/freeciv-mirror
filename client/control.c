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

#include <stdio.h>

#include "capability.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"

#include "chatline_g.h"
#include "citydlg_g.h"
#include "dialogs_g.h"
#include "mapctrl_g.h"
#include "mapview_g.h"
#include "menu_g.h"

#include "civclient.h"
#include "options.h"

#include "control.h"

extern struct connection aconnection;

/* unit_focus points to the current unit in focus */
static struct unit *punit_focus;

/* set high, if the player has selected goto */
/* actually, set to id of unit goto-ing (id is non-zero) */
int goto_state;

/* set high, if the player has selected nuke */
int nuke_state;

/* set high, if the player has selected paradropping */
int paradrop_state;

/* set high if the player has selected connect */
int connect_state;

/* units involved in current combat */
static struct unit *punit_attacking;
static struct unit *punit_defending;

/*************************************************************************/

static struct unit *find_best_focus_candidate(void);

/**************************************************************************
...
**************************************************************************/
void handle_advance_focus(struct packet_generic_integer *packet)
{
  struct unit *punit = find_unit_by_id(packet->value);
  if (punit && punit_focus == punit)
    advance_unit_focus();
}

/**************************************************************************
note: punit can be NULL
We make sure that the previous focus unit is refreshed, if necessary,
_after_ setting the new focus unit (otherwise if the previous unit is
in a city, the refresh code draws the previous unit instead of the city).
**************************************************************************/
void set_unit_focus(struct unit *punit)
{
  struct unit *punit_old_focus=punit_focus;

  punit_focus=punit;

  if(punit) {
    if(auto_center_on_unit && 
       !tile_visible_and_not_on_border_mapcanvas(punit->x, punit->y))
      center_tile_mapcanvas(punit->x, punit->y);

    punit->focus_status=FOCUS_AVAIL;
    refresh_tile_mapcanvas(punit->x, punit->y, 1);
    put_cross_overlay_tile(punit->x, punit->y);
  }
  
  /* avoid the old focus unit disappearing: */
  if (punit_old_focus!=NULL
      && (punit==NULL || !same_pos(punit_old_focus->x, punit_old_focus->y,
				   punit->x, punit->y))) {
    refresh_tile_mapcanvas(punit_old_focus->x, punit_old_focus->y, 1);
  }

  update_unit_info_label(punit);
  update_menus();
}

/**************************************************************************
note: punit can be NULL
Here we don't bother making sure the old focus unit is
refreshed, as this is only used in special cases where
thats not necessary.  (I think...) --dwp
**************************************************************************/
void set_unit_focus_no_center(struct unit *punit)
{
  punit_focus=punit;

  if(punit) {
    refresh_tile_mapcanvas(punit->x, punit->y, 1);
    punit->focus_status=FOCUS_AVAIL;
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
  if(punit_focus==NULL
     || (punit_focus->activity!=ACTIVITY_IDLE
	 && punit_focus->activity!=ACTIVITY_GOTO)
     || punit_focus->moves_left==0 
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
...
**************************************************************************/
void advance_unit_focus(void)
{
  struct unit *punit_old_focus=punit_focus;

  punit_focus=find_best_focus_candidate();

  goto_state=0;
  nuke_state=0;
  paradrop_state=0;
  connect_state=0;

  if(!punit_focus) {
    unit_list_iterate(game.player_ptr->units, punit) {
      if(punit->focus_status==FOCUS_WAIT)
	punit->focus_status=FOCUS_AVAIL;
    }
    unit_list_iterate_end;
    punit_focus=find_best_focus_candidate();
    if (punit_focus == punit_old_focus) {
      /* we don't want to same unit as before if there are any others */
      punit_focus=find_best_focus_candidate();
      if(!punit_focus) {
	/* but if that is the only choice, take it: */
	punit_focus=find_best_focus_candidate();
      }
    }
  }

  /* We have to do this ourselves, and not rely on set_unit_focus(),
   * because above we change punit_focus directly.
   */
  if(punit_old_focus!=NULL && punit_old_focus!=punit_focus)
    refresh_tile_mapcanvas(punit_old_focus->x, punit_old_focus->y, 1);

  set_unit_focus(punit_focus);

  /* Handle auto-turn-done mode:  If a unit was in focus (did move),
   * but now none are (no more to move), then fake a Turn Done keypress.
   */
  if(auto_turn_done && punit_old_focus!=NULL && punit_focus==NULL)
    key_end_turn();
}

/**************************************************************************
Find the nearest available unit for focus, excluding the current unit
in focus (if any).  If the current focus unit is the only possible
unit, or if there is no possible unit, returns NULL.
**************************************************************************/
static struct unit *find_best_focus_candidate(void)
{
  struct unit *best_candidate;
  int best_dist=99999;
  int x,y;

  if(punit_focus)  {
    x=punit_focus->x; y=punit_focus->y;
  } else {
    get_center_tile_mapcanvas(&x,&y);
  }
    
  best_candidate=NULL;
  unit_list_iterate(game.player_ptr->units, punit) {
    if(punit!=punit_focus) {
      if(punit->focus_status==FOCUS_AVAIL && punit->activity==ACTIVITY_IDLE &&
	 punit->moves_left && !punit->ai.control) {
        int d;
	d=sq_map_distance(punit->x, punit->y, x, y);
	if(d<best_dist) {
	  best_candidate=punit;
	  best_dist=d;
	}
      }
    }
  }
  unit_list_iterate_end;
  return best_candidate;
}

/**************************************************************************
Return a pointer to a visible unit, if there is one.
**************************************************************************/
struct unit *find_visible_unit(struct tile *ptile)
{
  if(unit_list_size(&ptile->units)==0) return NULL;

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

  /* If there is a transporter in the stack we will show that on top */
  unit_list_iterate(ptile->units, punit)
    if (get_transporter_capacity(punit) &&
	player_can_see_unit(game.player_ptr, punit))
      return punit;
  unit_list_iterate_end;

  /* Else just return the first unit we can see */
  unit_list_iterate(ptile->units, punit)
    if(player_can_see_unit(game.player_ptr, punit)) return punit;
  unit_list_iterate_end;

  return NULL;
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
...
**************************************************************************/
void blink_active_unit(void)
{
  static int is_shown;
  struct unit *punit;
  
  if((punit=get_unit_in_focus())) {
    struct tile *ptile;
    ptile=map_get_tile(punit->x, punit->y);

    if(is_shown) {
      struct unit_list units;
      units=ptile->units;
      unit_list_init(&ptile->units);
      refresh_tile_mapcanvas(punit->x, punit->y, 1);
      ptile->units=units;
    } else {
      refresh_tile_mapcanvas(punit->x, punit->y, 1);
    }
    
    is_shown=!is_shown;
  }
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
  static int is_init_arrival_queue = 0;
  int *p_id;

  /* arrival_queue is a list of individually malloc-ed ints with
     punit.id values, for units which have arrived. */

  if (!is_init_arrival_queue) {
    genlist_init(&arrival_queue);
    is_init_arrival_queue = 1;
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
  
  while (genlist_size(&arrival_queue)) {
    int id;
    
    p_id = genlist_get(&arrival_queue, 0);
    genlist_unlink(&arrival_queue, p_id);
    id = *p_id;
    free(p_id);
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
void process_diplomat_arrival(struct unit *punit, struct city *pcity)
{
  static struct genlist arrival_queue;
  static int is_init_arrival_queue = 0;
  int *p_ids;

  /* arrival_queue is a list of individually malloc-ed int[2]s with
     punit.id and pcity.id values, for units which have arrived. */

  if (!is_init_arrival_queue) {
    genlist_init(&arrival_queue);
    is_init_arrival_queue = 1;
  }

  if (punit && pcity) {
    p_ids = fc_malloc(2*sizeof(int));
    p_ids[0] = punit->id;
    p_ids[1] = pcity->id;
    genlist_insert(&arrival_queue, p_ids, -1);
  }

  /* There can only be one dialog at a time: */
  if (diplomat_dialog_is_open()) {
    return;
  }

  while (genlist_size(&arrival_queue)) {
    int unit_id, city_id;

    p_ids = genlist_get(&arrival_queue, 0);
    genlist_unlink(&arrival_queue, p_ids);
    unit_id = p_ids[0];
    city_id = p_ids[1];
    free(p_ids);
    punit = player_find_unit_by_id(game.player_ptr, unit_id);
    pcity = find_city_by_id(city_id);

    if(punit && pcity && unit_flag(punit->type, F_DIPLOMAT) &&
       is_diplomat_action_available(punit, DIPLOMAT_ANY_ACTION,
				    pcity->x, pcity->y) &&
       diplomat_can_do_action(punit, DIPLOMAT_ANY_ACTION,
			      pcity->x, pcity->y)) {
      popup_diplomat_dialog(punit, pcity->x, pcity->y);
      return;
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_goto(void)
{
  struct unit *punit=get_unit_in_focus();
     
  if(punit) {
    goto_state=punit->id;
    update_unit_info_label(punit);
  }
}

/**************************************************************************
prompt player for entering destination point for unit connect
(e.g. connecting with roads)
**************************************************************************/
void request_unit_connect(void)
{
  struct unit *punit=get_unit_in_focus();
     
  if(punit && can_unit_do_connect (punit, ACTIVITY_IDLE)) {
    goto_state=punit->id;
    connect_state=1;
    update_unit_info_label(punit);
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_unload(struct unit *punit)
{
  struct packet_unit_request req;

  if(!get_transporter_capacity(punit)) {
    append_output_window(_("Game: You can only unload transporter units."));
    return;
  }

  request_unit_wait(punit);    /* RP: unfocus the ship */
  
  req.unit_id=punit->id;
  req.name[0]='\0';
  send_packet_unit_request(&aconnection, &req, PACKET_UNIT_UNLOAD);
}

/**************************************************************************
(RP:) un-sentry all my own sentried units on punit's tile
**************************************************************************/
void request_unit_wakeup(struct unit *punit)
{
  wakeup_sentried_units(punit->x,punit->y);
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
  if(can_unit_build_city(punit)) {
    struct packet_generic_integer req;
    req.value = punit->id;
    send_packet_generic_integer(&aconnection,
				PACKET_CITY_NAME_SUGGEST_REQ, &req);
    /* the reply will trigger a dialog to name the new city */
  }
  else {
    struct packet_unit_request req;
    req.unit_id=punit->id;
    req.name[0]='\0';
    send_packet_unit_request(&aconnection, &req, PACKET_UNIT_BUILD_CITY);
    return;
  }
}

/**************************************************************************
...

  No need to do caravan-specific stuff here any more: server does trade
  routes to enemy cities automatically, and for friendly cities we wait
  for the unit to enter the city.  --dwp

  No need to do diplomat-specific stuff here any more: server notifies
  client whenever diplomat attempts to enter enemy city.  --jjm

**************************************************************************/
void request_move_unit_direction(struct unit *punit, int dx, int dy)
{
  int dest_x, dest_y;
  struct unit req_unit;

  dest_x=map_adjust_x(punit->x+dx);
  dest_y=punit->y+dy;   /* not adjusting on purpose*/   /* why? --dwp */

  req_unit=*punit;
  req_unit.x=dest_x;
  req_unit.y=dest_y;
  send_move_unit(&req_unit);
}

/**************************************************************************
...
**************************************************************************/
void request_new_unit_activity(struct unit *punit, enum unit_activity act)
{
  struct unit req_unit;
  req_unit=*punit;
  req_unit.activity=act;
  req_unit.activity_target=0;
  send_unit_info(&req_unit);
}

/**************************************************************************
...
**************************************************************************/
void request_new_unit_activity_targeted(struct unit *punit, enum unit_activity act,
					int tgt)
{
  struct unit req_unit;
  req_unit=*punit;
  req_unit.activity=act;
  req_unit.activity_target=tgt;
  send_unit_info(&req_unit);
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
  struct tile * ptile;
  int pspresent;
  int psworking;
  int what;
  int would;

  ptile = map_get_tile(punit->x, punit->y);
  pspresent = get_tile_infrastructure_set(ptile);
  psworking = get_unit_tile_pillage_set(punit->x, punit->y);
  what = get_preferred_pillage(pspresent & (~psworking));
  would = what | map_get_infrastructure_prerequisite (what);

  if ((game.civstyle==2) &&
      ((pspresent & (~(psworking | would))) != S_NO_SPECIAL)) {
    popup_pillage_dialog(punit, pspresent & (~psworking));
  } else {
    request_new_unit_activity_targeted(punit, ACTIVITY_PILLAGE, what);
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_disband(struct unit *punit)
{
  struct packet_unit_request req;
  req.unit_id=punit->id;
  req.name[0]='\0';
  send_packet_unit_request(&aconnection, &req, PACKET_UNIT_DISBAND);
}

/**************************************************************************
...
**************************************************************************/
void request_unit_change_homecity(struct unit *punit)
{
  struct city *pcity;
  
  if((pcity=map_get_city(punit->x, punit->y))) {
    struct packet_unit_request req;
    req.unit_id=punit->id;
    req.city_id=pcity->id;
    req.name[0]='\0';
    send_packet_unit_request(&aconnection, &req, PACKET_UNIT_CHANGE_HOMECITY);
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_upgrade(struct unit *punit)
{
  struct city *pcity;

  if((pcity=map_get_city(punit->x, punit->y)))  {
    struct packet_unit_request req;
    req.unit_id=punit->id;
    req.city_id=pcity->id;
    req.name[0]='\0';
    send_packet_unit_request(&aconnection, &req, PACKET_UNIT_UPGRADE);
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_auto(struct unit *punit)
{
  if (can_unit_do_auto(punit)) {
    struct packet_unit_request req;
    req.unit_id=punit->id;
    req.name[0]='\0';
    send_packet_unit_request(&aconnection, &req, PACKET_UNIT_AUTO);
  } else {
    append_output_window(_("Game: Only settlers, and military units"
			 " in cities, can be put in auto-mode."));
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_caravan_action(struct unit *punit, enum packet_type action)
{
  struct packet_unit_request req;
  struct city *pcity = map_get_city(punit->x, punit->y);

  if (!pcity) return;
  if (!(action==PACKET_UNIT_ESTABLISH_TRADE
	||(action==PACKET_UNIT_HELP_BUILD_WONDER))) {
    freelog(LOG_NORMAL, "Bad action (%d) in request_unit_caravan_action",
	    action);
    return;
  }
  
  req.unit_id = punit->id;
  req.city_id = pcity->id;
  req.name[0]='\0';
  send_packet_unit_request(&aconnection, &req, action);
}

/**************************************************************************
 Explode nuclear at a tile without enemy units
**************************************************************************/
void request_unit_nuke(struct unit *punit)
{
  if(!unit_flag(punit->type, F_NUCLEAR)) {
    append_output_window(_("Game: Only nuclear units can do this."));
    return;
  }
  if(!(punit->moves_left))
    do_unit_nuke(punit);
  else {
    nuke_state=1;
    goto_state=punit->id;
    update_unit_info_label(punit);
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_paradrop(struct unit *punit)
{
  if(!unit_flag(punit->type, F_PARATROOPERS)) {
    append_output_window(_("Game: Only paratrooper units can do this."));
    return;
  }
  if(!can_unit_paradropped(punit))
    return;

  paradrop_state=1;
  goto_state=punit->id;
  update_unit_info_label(punit);
}

/**************************************************************************
 Toggle display of grid lines on the map
**************************************************************************/
void request_toggle_map_grid(void) 
{
  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE) return;

  draw_map_grid^=1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of city names
**************************************************************************/
void request_toggle_city_names(void)
{
  if (get_client_state() != CLIENT_GAME_RUNNING_STATE)
    return;

  draw_city_names ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of city productions
**************************************************************************/
void request_toggle_city_productions(void)
{
  if (get_client_state() != CLIENT_GAME_RUNNING_STATE)
    return;

  draw_city_productions ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
...
**************************************************************************/
void do_move_unit(struct unit *punit, struct packet_unit_info *pinfo)
{
  int x, y, was_carried, was_teleported;
     
  was_carried=(pinfo->movesleft==punit->moves_left && 
	       (map_get_terrain(punit->x, punit->y)==T_OCEAN ||
		map_get_terrain(pinfo->x, pinfo->y)==T_OCEAN));
  
  was_teleported=!is_tiles_adjacent(punit->x, punit->y, pinfo->x, pinfo->y);
  x=punit->x;
  y=punit->y;
  punit->x=-1;  /* focus hack - if we're moving the unit in focus, it wont
		 * be redrawn on top of the city */

  unit_list_unlink(&map_get_tile(x, y)->units, punit);

  if(!was_carried)
    refresh_tile_mapcanvas(x, y, was_teleported);
  
  if(game.player_idx==punit->owner && punit->activity!=ACTIVITY_GOTO && 
     auto_center_on_unit &&
     !tile_visible_and_not_on_border_mapcanvas(pinfo->x, pinfo->y))
    center_tile_mapcanvas(pinfo->x, pinfo->y);

  if(!was_carried && !was_teleported) {
    int dx=pinfo->x - x;
    if(dx>1) dx=-1;
    else if(dx<-1)
      dx=1;
    if(smooth_move_units)
      move_unit_map_canvas(punit, x, y, dx, pinfo->y - punit->y);
    refresh_tile_mapcanvas(x, y, 1);
  }
    
  punit->x=pinfo->x;
  punit->y=pinfo->y;
  punit->fuel=pinfo->fuel;
  punit->hp=pinfo->hp;
  unit_list_insert(&map_get_tile(punit->x, punit->y)->units, punit);

  for(y=punit->y-2; y<punit->y+3; ++y) { 
    if(y<0 || y>=map.ysize)
      continue;
    for(x=punit->x-2; x<punit->x+3; ++x) { 
      unit_list_iterate(map_get_tile(x, y)->units, pu)
	if(unit_flag(pu->type, F_SUBMARINE)) {
	  refresh_tile_mapcanvas(map_adjust_x(pu->x), y, 1);
	}
      unit_list_iterate_end
    }
  }
  
  if(!was_carried && tile_is_known(punit->x,punit->y) == TILE_KNOWN)
    refresh_tile_mapcanvas(punit->x, punit->y, 1);

  if(get_unit_in_focus()==punit) update_menus();
}

/**************************************************************************
 Finish the goto mode and let the unit which is stored in goto_state move
 to a given location.
 returns 1 if goto mode was activated before calling this function
 otherwise 0 (then this function does nothing)
**************************************************************************/
int do_goto(int xtile, int ytile)
{
  if(goto_state) {
    struct unit *punit;

    if((punit=player_find_unit_by_id(game.player_ptr, goto_state))) {
      struct packet_unit_request req;
      req.unit_id=punit->id;
      req.name[0]='\0';
      req.x=xtile;
      req.y=ytile;
      send_packet_unit_request(&aconnection, &req, PACKET_UNIT_GOTO_TILE);
    }

    goto_state=0;
    nuke_state=0;
    paradrop_state=0;
    connect_state=0;

    return 1;
  }
  return 0;
}

/**************************************************************************
 Handles everything when the user clicked a tile
**************************************************************************/
void do_map_click(int xtile, int ytile)
{
  struct city *pcity;
  struct tile *ptile;

  ptile=map_get_tile(xtile, ytile);
  pcity=map_get_city(xtile, ytile);

  if(goto_state) { 
    struct unit *punit;

    if((punit=player_find_unit_by_id(game.player_ptr, goto_state))) {
      struct packet_unit_request req;

      if(paradrop_state) {
        do_unit_paradrop_to(punit, xtile, ytile);
        goto_state=0;
        nuke_state=0;
        paradrop_state=0;
	connect_state=0;
        return;
      }

      if(connect_state) {
        popup_unit_connect_dialog(punit, xtile, ytile);
        goto_state=0;
        nuke_state=0;
        paradrop_state=0;
        connect_state=0;
        return;
      }

      if(nuke_state &&
	 3*real_map_distance(punit->x,punit->y,xtile,ytile) > punit->moves_left) {
        append_output_window(_("Game: Too far for this unit."));
        goto_state=0;
        nuke_state=0;
        paradrop_state=0;
        connect_state=0;
        update_unit_info_label(punit);
        return;
      }

      req.unit_id=punit->id;
      req.name[0]='\0';
      req.x=xtile;
      req.y=ytile;
      send_packet_unit_request(&aconnection, &req, PACKET_UNIT_GOTO_TILE);
      if(nuke_state && (!pcity))
	do_unit_nuke(punit);
    }

    goto_state=0;
    nuke_state=0;
    paradrop_state=0;
    connect_state=0;
    return;
  }
  
  if(pcity && game.player_idx==pcity->owner) {
    popup_city_dialog(pcity, 0);
    return;
  }
  
  if(unit_list_size(&ptile->units)==1) {
    struct unit *punit=unit_list_get(&ptile->units, 0);
    if(game.player_idx==punit->owner) {
      if(can_unit_do_activity(punit, ACTIVITY_IDLE)) {
        /* struct unit *old_focus=get_unit_in_focus(); */
        request_new_unit_activity(punit, ACTIVITY_IDLE);
        /* this is now done in set_unit_focus: --dwp */
        /* if(old_focus)
             refresh_tile_mapcanvas(old_focus->x, old_focus->y, 1); */
        set_unit_focus(punit);
      }
    }
  }
  else if(unit_list_size(&ptile->units)>=2) {
    if(unit_list_get(&ptile->units, 0)->owner==game.player_idx)
      popup_unit_select_dialog(ptile);
  }
}

/**************************************************************************
Explode nuclear at a tile without enemy units
**************************************************************************/
void do_unit_nuke(struct unit *punit)
{
  struct packet_unit_request req;
 
  req.unit_id=punit->id;
  req.name[0]='\0';
  send_packet_unit_request(&aconnection, &req, PACKET_UNIT_NUKE);
}

/**************************************************************************
Paradrop to a location
**************************************************************************/
void do_unit_paradrop_to(struct unit *punit, int x, int y)
{
  struct packet_unit_request req;

  req.unit_id=punit->id;
  req.x = x;
  req.y = y;
  req.name[0]='\0';
  send_packet_unit_request(&aconnection, &req, PACKET_UNIT_PARADROP_TO);
}
 
/**************************************************************************
...
**************************************************************************/
void request_unit_wait(struct unit *punit)
{
  punit->focus_status=FOCUS_WAIT;
  if(punit==get_unit_in_focus()) {
    advance_unit_focus();
    /* set_unit_focus(punit_focus); */  /* done in advance_unit_focus */
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_move_done(void)
{
  if(get_unit_in_focus()) {
    get_unit_in_focus()->focus_status=FOCUS_DONE;
    advance_unit_focus();
    /* set_unit_focus(punit_focus); */  /* done in advance_unit_focus */
  }
}

/**************************************************************************
...
**************************************************************************/
void request_center_focus_unit(void)
{
  struct unit *punit;
  
  if((punit=get_unit_in_focus()))
    center_tile_mapcanvas(punit->x, punit->y);
}

/**************************************************************************
...
**************************************************************************/
void key_cancel_action(void)
{
  if(goto_state) {
    struct unit *punit;

    punit=player_find_unit_by_id(game.player_ptr, goto_state);

    goto_state=0;
    nuke_state=0;
    paradrop_state=0;
    connect_state=0;

    update_unit_info_label(punit);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_city_names_toggle(void)
{
  request_toggle_city_names();
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
void key_end_turn(void)
{
  if(!game.player_ptr->turn_done) {
    struct packet_generic_message gen_packet;
    gen_packet.message[0]='\0';
    send_packet_generic_message(&aconnection, PACKET_TURN_DONE, &gen_packet);
    set_turn_done_button_state(FALSE);
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
...
**************************************************************************/
void key_move_north(void)
{
  if(get_unit_in_focus())
    request_move_unit_direction(punit_focus, 0, -1);
}

/**************************************************************************
...
**************************************************************************/
void key_move_north_east(void)
{
  if(get_unit_in_focus())
    request_move_unit_direction(punit_focus, 1, -1);
}

/**************************************************************************
...
**************************************************************************/
void key_move_east(void)
{
  if(get_unit_in_focus())
    request_move_unit_direction(punit_focus, 1, 0);
}

/**************************************************************************
...
**************************************************************************/
void key_move_south_east(void)
{
  if(get_unit_in_focus())
     request_move_unit_direction(punit_focus, 1, 1);
}

/**************************************************************************
...
**************************************************************************/
void key_move_south(void)
{
  if(get_unit_in_focus())
     request_move_unit_direction(punit_focus, 0, 1);
}

/**************************************************************************
...
**************************************************************************/
void key_move_south_west(void)
{
  if(get_unit_in_focus())
     request_move_unit_direction(punit_focus, -1, 1);
}

/**************************************************************************
...
**************************************************************************/
void key_move_west(void)
{
  if(get_unit_in_focus())
    request_move_unit_direction(punit_focus, -1, 0);
}

/**************************************************************************
...
**************************************************************************/
void key_move_north_west(void)
{
  if(get_unit_in_focus())
     request_move_unit_direction(punit_focus, -1, -1);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_airbase(void)
{
  if(get_unit_in_focus())
    if(can_unit_do_activity(punit_focus, ACTIVITY_AIRBASE))
      request_new_unit_activity(punit_focus, ACTIVITY_AIRBASE);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_auto_attack(void)
{
  if(get_unit_in_focus())
    if(!unit_flag(punit_focus->type, F_SETTLERS) &&
       can_unit_do_auto(punit_focus))
      request_unit_auto(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_auto_explore(void)
{
  if(get_unit_in_focus())
    if(can_unit_do_activity(punit_focus, ACTIVITY_EXPLORE))
      request_new_unit_activity(punit_focus, ACTIVITY_EXPLORE);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_auto_settle(void)
{
  if(get_unit_in_focus())
    if(unit_flag(punit_focus->type, F_SETTLERS) &&
       can_unit_do_auto(punit_focus))
      request_unit_auto(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_build_city(void)
{
  if(get_unit_in_focus())
    request_unit_build_city(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_build_wonder(void)
{
  if(get_unit_in_focus())
    if(unit_flag(punit_focus->type, F_CARAVAN))
      request_unit_caravan_action(punit_focus, PACKET_UNIT_HELP_BUILD_WONDER);
}

/**************************************************************************
handle user pressing key for 'Connect' command
**************************************************************************/
void key_unit_connect(void)
{
  request_unit_connect();
}

/**************************************************************************
...
**************************************************************************/
void key_unit_disband(void)
{
  if(get_unit_in_focus())
    request_unit_disband(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_done(void)
{
  request_unit_move_done();
}

/**************************************************************************
...
**************************************************************************/
void key_unit_fortify(void)
{
  if(get_unit_in_focus())
    if(can_unit_do_activity(punit_focus, ACTIVITY_FORTIFYING))
      request_new_unit_activity(punit_focus, ACTIVITY_FORTIFYING);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_fortress(void)
{
  if(get_unit_in_focus())
    if(can_unit_do_activity(punit_focus, ACTIVITY_FORTRESS))
      request_new_unit_activity(punit_focus, ACTIVITY_FORTRESS);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_goto(void)
{
  request_unit_goto();
}

/**************************************************************************
...
**************************************************************************/
void key_unit_homecity(void)
{
  if(get_unit_in_focus())
    request_unit_change_homecity(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_irrigate(void)
{
  if(get_unit_in_focus())
    if(can_unit_do_activity(punit_focus, ACTIVITY_IRRIGATE))
      request_new_unit_activity(punit_focus, ACTIVITY_IRRIGATE);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_mine(void)
{
  if(get_unit_in_focus())
    if(can_unit_do_activity(punit_focus, ACTIVITY_MINE))
      request_new_unit_activity(punit_focus, ACTIVITY_MINE);
}

/**************************************************************************
Explode nuclear at a tile without enemy units
**************************************************************************/
void key_unit_nuke(void)
{
  if(get_unit_in_focus())
    request_unit_nuke(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_paradrop(void)
{
  if(get_unit_in_focus())
    if(can_unit_paradropped(punit_focus))
      request_unit_paradrop(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_pillage(void)
{
  if(get_unit_in_focus())
    if(can_unit_do_activity(punit_focus, ACTIVITY_PILLAGE))
      request_unit_pillage(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_pollution(void)
{
  if(get_unit_in_focus())
    if(can_unit_do_activity(punit_focus, ACTIVITY_POLLUTION))
      request_new_unit_activity(punit_focus, ACTIVITY_POLLUTION);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_road(void)
{
  if(get_unit_in_focus()) {
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
  if(get_unit_in_focus())
    if(can_unit_do_activity(punit_focus, ACTIVITY_SENTRY))
      request_new_unit_activity(punit_focus, ACTIVITY_SENTRY);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_traderoute(void)
{
  if(get_unit_in_focus())
    if(unit_flag(punit_focus->type, F_CARAVAN))
      request_unit_caravan_action(punit_focus, PACKET_UNIT_ESTABLISH_TRADE);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_transform(void)
{
  if(get_unit_in_focus())
    if(can_unit_do_activity(punit_focus, ACTIVITY_TRANSFORM))
      request_new_unit_activity(punit_focus, ACTIVITY_TRANSFORM);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_unload(void)
{
  if(get_unit_in_focus())
    request_unit_unload(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_wait(void)
{
  if(get_unit_in_focus())
    request_unit_wait(punit_focus);
}

/**************************************************************************
...
***************************************************************************/
void key_unit_wakeup_others(void)
{
  if(get_unit_in_focus())
    request_unit_wakeup(punit_focus);
}

