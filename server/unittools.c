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
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "city.h"
#include "events.h"
#include "fcintl.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "packets.h"
#include "player.h"
#include "rand.h"
#include "shared.h"
#include "unit.h"

#include "cityhand.h"
#include "citytools.h"
#include "cityturn.h"
#include "civserver.h"
#include "maphand.h"
#include "plrhand.h"
#include "unitfunc.h"
#include "unithand.h"

#include "aiunit.h"

#include "unittools.h"

/**************************************************************************
  unit can be moved if:
  1) the unit is idle or on goto
  2) the target location is on the map
  3) the target location is next to the unit
  4) ground unit can only move to ocean squares if there is a transporter
     with free capacity
  5) marines are the only units that can attack from a ocean square
  6) naval units can only be moved to ocean squares or city squares
  7) if there is no enemy units blocking (zoc) [or igzoc is true]
**************************************************************************/
int can_unit_move_to_tile(struct unit *punit, int x, int y, int igzoc)
{
  struct tile *ptile,*ptile2;
  int zoc;

  if(punit->activity!=ACTIVITY_IDLE && punit->activity!=ACTIVITY_GOTO && !punit->connecting)
    return 0;
  
  if(x<0 || x>=map.xsize || y<0 || y>=map.ysize)
    return 0;
  
  if(!is_tiles_adjacent(punit->x, punit->y, x, y))
    return 0;

  if(is_enemy_unit_tile(x,y,punit->owner))
    return 0;

  ptile=map_get_tile(x, y);
  ptile2=map_get_tile(punit->x, punit->y);
  if(is_ground_unit(punit)) {
    /* Check condition 4 */
    if(ptile->terrain==T_OCEAN &&
       !is_transporter_with_free_space(&game.players[punit->owner], x, y))
	return 0;

    /* Moving from ocean */
    if(ptile2->terrain==T_OCEAN) {
      /* Can't attack a city from ocean unless marines */
      if(!unit_flag(punit->type, F_MARINES)
	 && is_enemy_city_tile(x,y,punit->owner)) {
	char *units_str = get_units_with_flag_string(F_MARINES);
	if (units_str) {
	  notify_player_ex(&game.players[punit->owner], punit->x, punit->y,
			   E_NOEVENT, _("Game: Only %s can attack from sea."),
			   units_str);
	  free(units_str);
	} else {
	  notify_player_ex(&game.players[punit->owner], punit->x, punit->y,
			   E_NOEVENT, _("Game: Cannot attack from sea."));
	}
	return 0;
      }
    }
  } else if(is_sailing_unit(punit)) {
    if(ptile->terrain!=T_OCEAN && ptile->terrain!=T_UNKNOWN)
      if(!is_friendly_city_tile(x,y,punit->owner))
	return 0;
  } 
  zoc = igzoc || zoc_ok_move(punit, x, y);
  if (!zoc) 
    notify_player_ex(&game.players[punit->owner], punit->x, punit->y, E_NOEVENT,
		     _("Game: %s can only move into your own zone of control."),
		     unit_types[punit->type].name);
  return zoc;
}

/**************************************************************************
 is there an enemy city on this tile?
**************************************************************************/
int is_enemy_city_tile(int x, int y, int owner)
{
    struct city *pcity;

    pcity=map_get_city(x,y);

    if(pcity==NULL) return 0;

    return(pcity->owner != owner);
}

/**************************************************************************
 is there an enemy unit on this tile?
**************************************************************************/
int is_enemy_unit_tile(int x, int y, int owner)
{
    struct unit_list *punit_list;
    struct unit *punit;

    punit_list=&map_get_tile(x, y)->units;
    punit = unit_list_get(punit_list, 0);

    if(!punit) return 0;
    return(punit->owner != owner);
}

/**************************************************************************
 is there an friendly unit on this tile?
**************************************************************************/
int is_friendly_unit_tile(int x, int y, int owner)
{
    struct unit_list *punit_list;
    struct unit *punit;

    punit_list=&map_get_tile(x, y)->units;
    punit = unit_list_get(punit_list, 0);

    if(!punit) return 0;
    return(punit->owner == owner);
}

/**************************************************************************
 is there an friendly city on this tile?
**************************************************************************/
int is_friendly_city_tile(int x, int y, int owner)
{
    struct city *pcity;

    pcity=map_get_city(x,y);

    if(pcity==NULL) return 0;

    return(pcity->owner == owner);
}

/**************************************************************************
  Is this square controlled by the unit's owner?

  Here "is_my_zoc" means essentially a square which is
  *not* adjacent to an enemy unit on a land tile.
  (Or, currently, an enemy city even if empty.)

  Note this function only makes sense for ground units.
**************************************************************************/
int is_my_zoc(struct unit *myunit, int x0, int y0)
{
  int x,y;
  int ax,ay;
  int owner=myunit->owner;

  assert(is_ground_unit(myunit));
     
  for (x=x0-1;x<x0+2;x++) for (y=y0-1;y<y0+2;y++) {
    ax=map_adjust_x(x);
    ay=map_adjust_y(y);
    if ((map_get_terrain(ax,ay)!=T_OCEAN) && is_enemy_unit_tile(ax,ay,owner))
      return 0;
  }
  return 1;
}

/**************************************************************************
  Returns whether the unit is allowed (by ZOC) to move from (x1,y1)
  to (x2,y2) (assumed adjacent).
  You CAN move if:
  1. You have units there already
  2. Your unit isn't a ground unit
  3. Your unit ignores ZOC (diplomat, freight, etc.)
  4. You're moving from or to a city
  5. You're moving from an ocean square (from a boat)
  6. The spot you're moving from or to is in your ZOC
**************************************************************************/
int zoc_ok_move_gen(struct unit *punit, int x1, int y1, int x2, int y2)
{
  if (unit_really_ignores_zoc(punit))  /* !ground_unit or IGZOC */
    return 1;
  if (is_friendly_unit_tile(x2, y2, punit->owner))
    return 1;
  if (map_get_city(x1, y1) || map_get_city(x2, y2))
    return 1;
  if (map_get_terrain(x1,y1)==T_OCEAN)
    return 1;
  return (is_my_zoc(punit, x1, y1) || is_my_zoc(punit, x2, y2)); 
}

/**************************************************************************
  Convenience wrapper for zoc_ok_move_gen(), using the unit's (x,y)
  as the starting point.
**************************************************************************/
int zoc_ok_move(struct unit *punit, int x, int y)
{
  return zoc_ok_move_gen(punit, punit->x, punit->y, x, y);
}

/**************************************************************************
 calculate how expensive it is to bribe the unit
 depends on distance to the capital, and government form
 settlers are half price

 Plus, the damage to the unit reduces the price.

**************************************************************************/
int unit_bribe_cost(struct unit *punit)
{  
  struct government *g = get_gov_iplayer(punit->owner);
  int cost;
  struct city *capital;
  int dist;
  int default_hp = get_unit_type(punit->type)->hp;

  cost = (&game.players[punit->owner])->economic.gold+750;
  capital=find_palace(&game.players[punit->owner]);
  if (capital) {
    int tmp = map_distance(capital->x, capital->y, punit->x, punit->y);
    dist=MIN(32, tmp);
  }
  else
    dist=32;
  if (g->fixed_corruption_distance)
    dist = MIN(g->fixed_corruption_distance, dist);
  cost=(cost/(dist+2))*(get_unit_type(punit->type)->build_cost/10);
  /* FIXME: This is a weird one - should be replaced */
  if (unit_flag(punit->type, F_CITIES)) 
    cost/=2;

  /* Cost now contains the basic bribe cost.  We now reduce it by:

     cost = basecost/2 + basecost/2 * damage/hp
     
   */
  
  cost = (int)((float)cost/(float)2 + ((float)punit->hp/(float)default_hp) * ((float)cost/(float)2));
  
  return cost;
}

/**************************************************************************
 return number of diplomats on this square.  AJS 20000130
**************************************************************************/
int count_diplomats_on_tile(int x, int y)
{
  int count = 0;

  unit_list_iterate(map_get_tile(x, y)->units, punit)
    if (unit_flag(punit->type, F_DIPLOMAT))
      count++;
  unit_list_iterate_end;
  return count;
}

/**************************************************************************
  returns how many hp's a unit will gain on this square
  depends on whether or not it's inside city or fortress.
  barracks will regen landunits completely
  airports will regen airunits  completely
  ports    will regen navalunits completely
  fortify will add a little extra.
***************************************************************************/
int hp_gain_coord(struct unit *punit)
{
  int hp;
  struct city *pcity;
  if (unit_on_fortress(punit))
    hp=get_unit_type(punit->type)->hp/4;
  else
    hp=0;
  if((pcity=map_get_city(punit->x,punit->y))) {
    if ((city_got_barracks(pcity) &&
	 (is_ground_unit(punit) || improvement_variant(B_BARRACKS)==1)) ||
	(city_got_building(pcity, B_AIRPORT) && is_air_unit(punit)) || 
	(city_got_building(pcity, B_AIRPORT) && is_heli_unit(punit)) || 
	(city_got_building(pcity, B_PORT) && is_sailing_unit(punit))) {
      hp=get_unit_type(punit->type)->hp;
    }
    else
      hp=get_unit_type(punit->type)->hp/3;
  }
  else if (!is_heli_unit(punit))
    hp++;

  if(punit->activity==ACTIVITY_FORTIFY)
    hp++;
  
  return hp;
}

/**************************************************************************
  used to find the best defensive unit on a square
**************************************************************************/
int rate_unit_d(struct unit *punit, struct unit *against)
{
  int val;

  if(!punit) return 0;
  val = get_total_defense_power(against,punit);

/*  return val*100+punit->hp;       This is unnecessarily crude. -- Syela */
  val *= punit->hp;
  val *= val;
  val /= get_unit_type(punit->type)->build_cost;
  return val; /* designed to minimize expected losses */
}

/**************************************************************************
get best defending unit which is NOT owned by pplayer
**************************************************************************/
struct unit *get_defender(struct player *pplayer, struct unit *aunit, 
			  int x, int y)
{
  struct unit *bestdef = 0;
  int unit_d, bestvalue=-1, ct=0;

  unit_list_iterate(map_get_tile(x, y)->units, punit) {
    if (pplayer->player_no==punit->owner)
      return 0;
    ct++;
    if (unit_can_defend_here(punit)) {
      unit_d = rate_unit_d(punit, aunit);
      if (unit_d > bestvalue) {
	bestvalue = unit_d;
	bestdef = punit;
      }
    }
  }
  unit_list_iterate_end;
  if(ct&&!bestdef) freelog(LOG_NORMAL, "Get_def bugged at (%d,%d)", x, y);
  return bestdef;
}

/**************************************************************************
  used to find the best offensive unit on a square
**************************************************************************/
int rate_unit_a(struct unit *punit, struct unit *against)
{
  int val;

  if(!punit) return 0;
  val = unit_types[punit->type].attack_strength *
        (punit->veteran ? 15 : 10);

  val *= punit->hp;
  val *= val;
  val /= get_unit_type(punit->type)->build_cost;
  return val; /* designed to minimize expected losses */
}

struct unit *get_attacker(struct player *pplayer, struct unit *aunit, 
			  int x, int y)
{ /* get unit at (x, y) that wants to kill aunit */
  struct unit *bestatt = 0;
  int bestvalue=-1, unit_a;
  unit_list_iterate(map_get_tile(x, y)->units, punit) {
    if (pplayer->player_no==punit->owner)
      return 0;
    unit_a=rate_unit_a(punit, aunit);
    if(unit_a>bestvalue) {
      bestvalue=unit_a;
      bestatt=punit;
    }
  }
  unit_list_iterate_end;
  return bestatt;
}

/**************************************************************************
 returns the attack power, modified by moves left, and veteran status.
**************************************************************************/
int get_attack_power(struct unit *punit)
{
  int power;
  power=get_unit_type(punit->type)->attack_strength*10;
  if (punit->veteran)
    power*=1.5;
  if (unit_flag(punit->type, F_IGTIRED)) return power;
  if (punit->moves_left==1)
    return power/3;
  if (punit->moves_left==2)
    return (power*2)/3;
  return power;
}

/**************************************************************************
  returns the defense power, modified by terrain and veteran status
**************************************************************************/
int get_defense_power(struct unit *punit)
{
  int power;
  int terra;
  int db;

  if (!punit || punit->type<0 || punit->type>=U_LAST
      || punit->type>=game.num_unit_types)
    abort();
  power=get_unit_type(punit->type)->defense_strength*10;
  if (punit->veteran)
    power*=1.5;
  
  terra=map_get_terrain(punit->x, punit->y);
  db = get_tile_type(terra)->defense_bonus;
  if (map_get_special(punit->x, punit->y) & S_RIVER)
    db += (db * terrain_control.river_defense_bonus) / 100;
  power=(power*db)/10;

  return power;
}

/**************************************************************************
  Takes into account unit move_type as well as IGZOC
**************************************************************************/
int unit_really_ignores_zoc(struct unit *punit)
{
  return (!is_ground_unit(punit)) || (unit_flag(punit->type, F_IGZOC));
}

/**************************************************************************
  a wrapper that returns whether or not a unit ignores citywalls
**************************************************************************/
int unit_ignores_citywalls(struct unit *punit)
{
  return (unit_flag(punit->type, F_IGWALL));
}

/**************************************************************************
  Takes into account unit move_type as well, and Walls variant.
**************************************************************************/
int unit_really_ignores_citywalls(struct unit *punit)
{
  return unit_ignores_citywalls(punit)
    || is_air_unit(punit)
    || (is_sailing_unit(punit) && !(improvement_variant(B_CITY)==1));
}

/**************************************************************************
 a wrapper function that returns whether or not the unit is on a citysquare
 with citywalls
**************************************************************************/
int unit_behind_walls(struct unit *punit)
{
  struct city *pcity;
  
  if((pcity=map_get_city(punit->x, punit->y)))
    return city_got_citywalls(pcity);
  
  return 0;
}

/**************************************************************************
 a wrapper function returns 1 if the unit is on a square with fortress
**************************************************************************/
int unit_on_fortress(struct unit *punit)
{
  return (map_get_special(punit->x, punit->y)&S_FORTRESS);
}

/**************************************************************************
 a wrapper function returns 1 if the unit is on a square with coastal defense
**************************************************************************/
int unit_behind_coastal(struct unit *punit)
{
  struct city *pcity;
  if((pcity=map_get_city(punit->x, punit->y)))
    return city_got_building(pcity, B_COASTAL);
  return 0;
}

/**************************************************************************
 a wrapper function returns 1 if the unit is on a square with sam site
**************************************************************************/
int unit_behind_sam(struct unit *punit)
{
  struct city *pcity;
  if((pcity=map_get_city(punit->x, punit->y)))
    return city_got_building(pcity, B_SAM);
  return 0;
}

/**************************************************************************
 a wrapper function returns 1 if the unit is on a square with sdi defense
**************************************************************************/
int unit_behind_sdi(struct unit *punit)
{
  struct city *pcity;
  if((pcity=map_get_city(punit->x, punit->y)))
    return city_got_building(pcity, B_SDI);
  return 0;
}

/**************************************************************************
  a wrapper function returns 1 if there is a sdi-defense close to the square
**************************************************************************/
struct city *sdi_defense_close(int owner, int x, int y)
{
  struct city *pcity;
  int lx, ly;
  for (lx=x-2;lx<x+3;lx++)
    for (ly=y-2;ly<y+3;ly++) {
      pcity=map_get_city(lx,ly);
      if (pcity && (pcity->owner!=owner) && city_got_building(pcity, B_SDI))
	return pcity;
    }
  return NULL;
}

/**************************************************************************
  returns a unit type with a given role, use -1 if you don't want a tech 
  role. Always try tech role and only if not available, return role unit.
**************************************************************************/
int find_a_unit_type(int role, int role_tech)
{
  int which[U_LAST];
  int i, num=0, iunit;

  if( role_tech == -1 ) {
    for(i=0; i<num_role_units(role); i++) {
      which[num++] = get_role_unit(role, i);
    }
    return which[myrand(num)];
  }

  for(i=0; i<num_role_units(role_tech); i++) {
    iunit = get_role_unit(role_tech, i);
    if (game.global_advances[get_unit_type(iunit)->tech_requirement] >= 2) {
      which[num++] = iunit;
    }
  }
  if(num==0) {
    for(i=0; i<num_role_units(role); i++) {
      which[num++] = get_role_unit(role, i);
    }
  }
  return which[myrand(num)];
}

/**************************************************************************
  unit can't attack if :
 1) it don't have any attack power
 2) it's not a fighter and the defender is a flying unit (except city/airbase)
 3) if it's not a marine (and ground unit) and it attacks from ocean
 4) a ground unit can't attack a ship on an ocean square (except marines)
**************************************************************************/
int can_unit_attack_unit_at_tile(struct unit *punit, struct unit *pdefender,
				 int dest_x, int dest_y)
{
  int fromtile=map_get_terrain(punit->x, punit->y);
  int totile=map_get_terrain(dest_x, dest_y);
 
  if(!is_military_unit(punit))
    return 0;

  /* only fighters can attack planes, except for city or airbase attacks */
  if (!unit_flag(punit->type, F_FIGHTER) && is_air_unit(pdefender) &&
      !(map_get_city(dest_x, dest_y) || map_get_special(dest_x, dest_y)&S_AIRBASE)) {
    return 0;
  }
  /* can't attack with ground unit from ocean, except for marines */
  if(fromtile==T_OCEAN && is_ground_unit(punit) && !unit_flag(punit->type, F_MARINES)) {
    return 0;
  }

  if(totile==T_OCEAN && is_ground_unit(punit)) {
    return 0;
  }

  if (unit_flag(punit->type, F_SUBMARINE) && totile!=T_OCEAN)  {
    return 0;
  }
  
  /* Shore bombardement */
  if (fromtile==T_OCEAN && is_sailing_unit(punit) && totile!=T_OCEAN)
    return (get_attack_power(punit)>0);
  return 1;
}


int can_unit_attack_tile(struct unit *punit, int dest_x, int dest_y)
{
  struct unit *pdefender;
  pdefender=get_defender(&game.players[punit->owner], punit, dest_x, dest_y);
  return(can_unit_attack_unit_at_tile(punit, pdefender, dest_x, dest_y));
}

/**************************************************************************
  calculate the remaining build points 
**************************************************************************/
int build_points_left(struct city *pcity)
{
 return (improvement_value(pcity->currently_building) - pcity->shield_stock);
}

/**************************************************************************
  return if it's possible to place a partisan on this square 
  possible if:
  1) square isn't a city square
  2) there is no units on this square
  3) it's not an ocean square 
**************************************************************************/
int can_place_partisan(int x, int y) 
{
  struct tile *ptile;
  ptile = map_get_tile(x, y);
  return (!map_get_city(x, y) && !unit_list_size(&ptile->units) && map_get_terrain(x,y) != T_OCEAN && map_get_terrain(x, y) < T_UNKNOWN); 
}

int enemies_at(struct unit *punit, int x, int y)
{
  int i, j, a = 0, d, db;
  struct player *pplayer = get_player(punit->owner);

  if (is_friendly_city_tile(x, y, punit->owner)) return 0;

  db = get_tile_type(map_get_terrain(x, y))->defense_bonus;
  if (map_get_special(x, y) & S_RIVER)
    db += (db * terrain_control.river_defense_bonus) / 100;
  d = unit_vulnerability_virtual(punit) * db;
  for (j = y - 1; j <= y + 1; j++) {
    if (j < 0 || j >= map.ysize) continue;
    for (i = x - 1; i <= x + 1; i++) {
      if (same_pos(i, j, x, y)) continue; /* after some contemplation */
      if (!pplayer->ai.control && !map_get_known_and_seen(x, y, pplayer)) continue;
      if (is_enemy_city_tile(i, j, punit->owner)) return 1;
      unit_list_iterate(map_get_tile(i, j)->units, enemy)
        if (enemy->owner != punit->owner &&
            can_unit_attack_unit_at_tile(enemy, punit, x, y)) {
          a += unit_belligerence_basic(enemy);
          if ((a * a * 10) >= d) return 1;
        }
      unit_list_iterate_end;
    }
  }
  return 0; /* as good a quick'n'dirty should be -- Syela */
}



/**************************************************************************
  Return first unit on square that is not owned by player.

                          - Kris Bubendorfer 
**************************************************************************/

struct unit *is_enemy_unit_on_tile(int x, int y, int owner)
{
  unit_list_iterate(map_get_tile(x, y)->units, punit) 
    if (owner!=punit->owner) 
      return punit;
  unit_list_iterate_end;
  
  return 0;
}

/**************************************************************************
Teleport punit to city at cost specified.  Returns success.
(If specified cost is <0, then teleportation costs all movement.)
                         - Kris Bubendorfer
**************************************************************************/

int teleport_unit_to_city(struct unit *punit, struct city *pcity, int mov_cost)
{
  int src_x = punit->x,src_y = punit->y;
  if(pcity->owner == punit->owner){
    teleport_unit_sight_points(punit->x,punit->y,pcity->x,pcity->y,punit);
    unit_list_unlink(&map_get_tile(punit->x, punit->y)->units, punit);
    punit->x = pcity->x;
    punit->y = pcity->y;
    if((mov_cost < 0) || (punit->moves_left < mov_cost))
      punit->moves_left = 0;
    else
      punit->moves_left -= mov_cost;
    
    unit_list_insert(&map_get_tile(pcity->x, pcity->y)->units, punit);
    send_unit_info_to_onlookers(0, punit, src_x,src_y);

    return 1;
  }
  return 0;
}

/**************************************************************************
  Resolve unit stack

  When in civil war (or an alliance breaks) there will potentially be units 
  from both sides coexisting on the same squares.  This routine resolves 
  this by teleporting the units in multiowner stacks to the closest city.

  That is, if a unit is closer to it's city than the coexistent enemy unit
  then the enemy unit is teleported home.

                         - Kris Bubendorfer

  If verbose is true, the unit owner gets messages about where each
  units goes.  --dwp
**************************************************************************/

void resolve_unit_stack(int x, int y, int verbose)
{
  struct unit *punit = unit_list_get(&map_get_tile(x, y)->units, 0);
  struct unit *cunit = is_enemy_unit_on_tile(x, y, punit->owner);
  
  while(punit && cunit){
    struct city *pcity = find_closest_owned_city(get_player(punit->owner), x, y);
    struct city *ccity = find_closest_owned_city(get_player(cunit->owner), x, y);
    
    if(map_distance(x, y, pcity->x, pcity->y) 
       < map_distance(x, y, ccity->x, ccity->y)){
      teleport_unit_to_city(cunit, ccity, 0);
      freelog(LOG_VERBOSE, "Teleported %s's %s from (%d, %d) to %s",
	      get_player(cunit->owner)->name, unit_name(cunit->type),
	      x, y,ccity->name);
      if (verbose) {
	notify_player(get_player(cunit->owner),
		      _("Game: Teleported your %s from (%d, %d) to %s."),
		      unit_name(cunit->type), x, y,ccity->name);
      }
    }else{
      teleport_unit_to_city(punit, pcity, 0);
      freelog(LOG_VERBOSE, "Teleported %s's %s from (%d, %d) to %s",
	      get_player(punit->owner)->name, unit_name(punit->type),
	      x, y, pcity->name);
      if (verbose) {
	notify_player(get_player(punit->owner),
		      _("Game: Teleported your %s from (%d, %d) to %s."),
		      unit_name(punit->type), x, y, pcity->name);
      }
    }
    
    punit = unit_list_get(&map_get_tile(x, y)->units, 0);
    cunit = is_enemy_unit_on_tile(x, y, punit->owner);
  }

  /* There is only one players units left on this square.  If there is not 
     enough transporter capacity left , send surplus to the closest friendly 
     city. */

  if(!punit && cunit)
    punit = cunit;
  
  if(punit && map_get_terrain(punit->x, punit->y)==T_OCEAN && 
     !is_enough_transporter_space(get_player(punit->owner), x, y)){    
    unit_list_iterate(map_get_tile(x, y)->units, vunit){
      struct city *vcity = find_closest_owned_city(get_player(vunit->owner), x, y);
      if(is_ground_unit(vunit)){
	teleport_unit_to_city(vunit, vcity, 0);	  
	freelog(LOG_VERBOSE, "Teleported  %s's %s to %s"
		" as there is no transport space on square (%d, %d)",
		get_player(vunit->owner)->name, unit_name(vunit->type),
		vcity->name, x, y);
	if (verbose) {
	  notify_player(get_player(vunit->owner),
			_("Game: Teleported your %s to %s as there is"
			  " no transport space on square (%d, %d)."),
			unit_name(vunit->type), vcity->name, x, y);
	}
      }
    }
    unit_list_iterate_end;
  }
}
