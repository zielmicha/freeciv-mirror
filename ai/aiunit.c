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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "city.h"
#include "combat.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "rand.h"
#include "shared.h"
#include "timing.h"
#include "unit.h"

#include "barbarian.h"
#include "citytools.h"
#include "cityturn.h"
#include "diplomats.h"
#include "gotohand.h"
#include "maphand.h"
#include "settlers.h"
#include "unithand.h"
#include "unittools.h"

#include "advmilitary.h"
#include "aicity.h"
#include "aihand.h"
#include "aitools.h"

#include "aiunit.h"

static void ai_manage_diplomat(struct player *pplayer, struct unit *pdiplomat);
static void ai_manage_military(struct player *pplayer,struct unit *punit);
static void ai_manage_caravan(struct player *pplayer, struct unit *punit);
static void ai_manage_barbarian_leader(struct player *pplayer,
				       struct unit *leader);

static void ai_military_findjob(struct player *pplayer,struct unit *punit);
static void ai_military_gohome(struct player *pplayer,struct unit *punit);
static void ai_military_attack(struct player *pplayer,struct unit *punit);

static int unit_move_turns(struct unit *punit, int x, int y);
static bool unit_can_defend(Unit_Type_id type);

/*
 * Cached values. Updated by update_simple_ai_types.
 *
 * This a hack to enable emulation of old loops previously hardwired
 * as 
 *    for (i = U_WARRIORS; i <= U_BATTLESHIP; i++)
 *
 * (Could probably just adjust the loops themselves fairly simply,
 * but this is safer for regression testing.)
 *
 * Not dealing with planes yet.
 *
 * Terminated by U_LAST.
 */
Unit_Type_id simple_ai_types[U_LAST];

/**************************************************************************
  Similar to is_my_zoc(), but with some changes:
  - destination (x0,y0) need not be adjacent?
  - don't care about some directions?
  
  Note this function only makes sense for ground units.

  Fix to bizarre did-not-find bug.  Thanks, Katvrr -- Syela
**************************************************************************/
static bool could_be_my_zoc(struct unit *myunit, int x0, int y0)
{
  assert(is_ground_unit(myunit));
  
  if (same_pos(x0, y0, myunit->x, myunit->y))
    return FALSE; /* can't be my zoc */
  if (is_tiles_adjacent(x0, y0, myunit->x, myunit->y)
      && !is_non_allied_unit_tile(map_get_tile(x0, y0),
				  unit_owner(myunit)))
    return FALSE;

  adjc_iterate(x0, y0, ax, ay) {
    if (map_get_terrain(ax, ay) != T_OCEAN
	&& is_non_allied_unit_tile(map_get_tile(ax, ay),
				   unit_owner(myunit))) return FALSE;
  } adjc_iterate_end;
  
  return TRUE;
}

/**************************************************************************
  returns:
    0 if can't move
    1 if zoc_ok
   -1 if zoc could be ok?
**************************************************************************/
int could_unit_move_to_tile(struct unit *punit, int src_x, int src_y,
			    int dest_x, int dest_y)
{
  enum unit_move_result result;

  result =
      test_unit_move_to_tile(punit->type, unit_owner(punit),
			     punit->activity, punit->connecting,
			     punit->x, punit->y, dest_x, dest_y, FALSE);
  if (result == MR_OK)
    return 1;

  if (result == MR_ZOC) {
    if (could_be_my_zoc(punit, src_x, src_y))
      return -1;
  }
  return 0;
}

/********************************************************************** 
  This is a much simplified form of assess_defense (see advmilitary.c),
  but which doesn't use pcity->ai.wallvalue and just returns a boolean
  value.  This is for use with "foreign" cities, especially non-ai
  cities, where ai.wallvalue may be out of date or uninitialized --dwp
***********************************************************************/
static bool has_defense(struct city *pcity)
{
  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit) {
    if (is_military_unit(punit) && get_defense_power(punit) != 0 && punit->hp != 0) {
      return TRUE;
    }
  }
  unit_list_iterate_end;
  return FALSE;
}


/**********************************************************************
 Precondition: A warmap must already be generated for the punit.

 Returns the minimal amount of turns required to reach the given
 destination position. The actual turn at which the unit will
 reach the given point depends on the movement points it has remaining.

 For example: Unit has a move rate of 3, path cost is 5, unit has 2
 move points left.

 path1 costs: first tile = 3, second tile = 2

 turn 0: points=2, unit has to wait
 turn 1: points=3, unit can move, points=0, has to wait
 turn 2: points=3, unit can move, points=1

 path2 costs: first tile=2, second tile=3

 turn 0: points=2, unit can move, points=0, has to wait
 turn 1: points=3, unit can move, points=0

 In spite of the path costs being the same, these two units will arrive
 at different times. This function also does not take into account ZOC.
 
 Note: even if a unit has only fractional move points left, there is
 still a possibility it could cross the tile.
**************************************************************************/
static int unit_move_turns(struct unit *punit, int x, int y)
{
  int move_time;
  int move_rate = unit_type(punit)->move_rate;
 
  switch (unit_type(punit)->move_type) {
    case LAND_MOVING:
    
     /* FIXME: IGTER units should have their move rates multiplied by 
      * igter_speedup. Note: actually, igter units should never have their 
      * move rates multiplied. The correct behaviour is to have every tile 
      * they cross cost 1/3 of a movement point. ---RK */
   
      if (unit_flag(punit, F_IGTER)) {
        move_rate *= 3;
      }
      move_time = warmap.cost[x][y] / move_rate;
      break;
   
    case SEA_MOVING:
     
      if (player_owns_active_wonder(unit_owner(punit), B_LIGHTHOUSE)) {
          move_rate += SINGLE_MOVE;
      }
      
      if (player_owns_active_wonder(unit_owner(punit), B_MAGELLAN)) {
          move_rate += (improvement_variant(B_MAGELLAN) == 1) 
                         ?  SINGLE_MOVE : 2 * SINGLE_MOVE;
      }
        
      if (player_knows_techs_with_flag(unit_owner(punit), TF_BOAT_FAST)) {
          move_rate += SINGLE_MOVE;
      }
        
      move_time = warmap.seacost[x][y] / move_rate;
      break;
   
    case HELI_MOVING:
    case AIR_MOVING:
       move_time = real_map_distance(punit->x, punit->y, x, y) 
                     * SINGLE_MOVE / move_rate;
       break;
   
    default:
      assert(0);
      freelog(LOG_FATAL, "In ai/aiunit.c: function unit_move_turns");
      freelog(LOG_FATAL, "Illegal move type %d", unit_type(punit)->move_type);
      exit(EXIT_FAILURE);
      move_time = -1;
  }
  return move_time;
}

/**************************************************************************
  is there any hope of reaching this tile without violating ZOC? 
**************************************************************************/
static bool tile_is_accessible(struct unit *punit, int x, int y)
{
  if (unit_type_really_ignores_zoc(punit->type))
    return TRUE;
  if (is_my_zoc(unit_owner(punit), x, y))
    return TRUE;

  adjc_iterate(x, y, x1, y1) {
    if (map_get_terrain(x1, y1) != T_OCEAN
	&& is_my_zoc(unit_owner(punit), x1, y1))
      return TRUE;
  } adjc_iterate_end;

  return FALSE;
}
 
/**************************************************************************
Handle eXplore mode of a unit (explorers are always in eXplore mode for AI) -
explores unknown territory, finds huts.

Returns whether there is any more territory to be explored.
**************************************************************************/
bool ai_manage_explorer(struct unit *punit)
{
  struct player *pplayer = unit_owner(punit);
  /* The position of the unit; updated inside the function */
  int x = punit->x, y = punit->y;
  /* Continent the unit is on */
  int continent;
  /* Unit's speed */
  int move_rate = unit_move_rate(punit);
  /* Range of unit's vision */
  int range;

  /* Get the range */
  /* FIXME: The vision range should NOT take into account watchtower benefit.
   * Now it is done in a wrong way: the watchtower bonus is computed locally,
   * at the point where the unit is, and then it is applied at a faraway
   * location, as if there is a watchtower there too. --gb */

  if (unit_profits_of_watchtower(punit)
      && map_has_special(punit->x, punit->y, S_FORTRESS)) {
    range = get_watchtower_vision(punit);
  } else {
    range = unit_type(punit)->vision_range;
  }

  /* Idle unit */

  if (punit->activity != ACTIVITY_IDLE) {
    handle_unit_activity_request(punit, ACTIVITY_IDLE);
  }

  /* Localize the unit */
  
  if (is_ground_unit(punit)) {
    continent = map_get_continent(x, y);
  } else {
    continent = 0;
  }

  /*
   * PART 1: Look for huts
   * Non-Barbarian Ground units ONLY.
   */

  if (!is_barbarian(pplayer)
      && is_ground_unit(punit)) {
    /* Maximal acceptable _number_ of moves to the target */
    int maxmoves = pplayer->ai.control ? 2 * THRESHOLD : 3;
    /* Move _cost_ to the best target (=> lower is better) */
    int bestcost = maxmoves * SINGLE_MOVE + 1;
    /* Desired destination */
    int best_x = -1, best_y = -1;

    /* CPU-expensive but worth it -- Syela */
    generate_warmap(map_get_city(x, y), punit);
  
    /* We're iterating outward so that with two tiles with the same movecost
     * the nearest is used. */
    iterate_outward(x, y, maxmoves, x1, y1) {
      if (map_has_special(x1, y1, S_HUT)
          && warmap.cost[x1][y1] < bestcost
          && (!ai_handicap(pplayer, H_HUTS) || map_get_known(x1, y1, pplayer))
          && tile_is_accessible(punit, x1, y1)
          && ai_fuzzy(pplayer, TRUE)) {
        best_x = x1;
        best_y = y1;
        bestcost = warmap.cost[best_x][best_y];
      }
    } iterate_outward_end;
    
    if (bestcost <= maxmoves * SINGLE_MOVE) {
      /* Go there! */
      punit->goto_dest_x = best_x;
      punit->goto_dest_y = best_y;
      set_unit_activity(punit, ACTIVITY_GOTO);
      if (do_unit_goto(punit, GOTO_MOVE_ANY, FALSE) == GR_DIED) {
        /* We're dead. */
        return FALSE;
      }

      /* TODO: Take more advantage from the do_unit_goto() return value. */

      if (punit->moves_left > 0) {
        /* We can still move on... */

        if (punit->x == best_x && punit->y == best_y) {
          /* ...and got into desired place. */
          return ai_manage_explorer(punit);
  
        } else {
          /* Something went wrong. This should almost never happen. */
          if (punit->x != x || punit->y != y)
            generate_warmap(map_get_city(punit->x, punit->y), punit);
          
          x = punit->x;
          y = punit->y;
          /* Fallthrough to next part. */
        }

      } else {
        return TRUE;
      }
    }
  }

  /* 
   * PART 2: Move into unexplored territory
   * Move the unit as long as moving will unveil unknown territory
   */
  
  while (punit->moves_left > 0) {
    /* Best (highest) number of unknown tiles adjacent (in vision range) */
    int most_unknown = 0;
    /* Desired destination */
    int best_x = -1, best_y = -1;

    /* Evaluate all adjacent tiles. */
    
    square_iterate(x, y, 1, x1, y1) {
      /* Number of unknown tiles in vision range around this tile */
      int unknown = 0;
      
      square_iterate(x1, y1, range, x2, y2) {
        if (!map_get_known(x2, y2, pplayer))
          unknown++;
      } square_iterate_end;

      if (unknown > most_unknown) {
        if (unit_flag(punit, F_TRIREME)
            && trireme_loss_pct(pplayer, x1, y1) != 0)
          continue;
        
        if (map_get_continent(x1, y1) != continent)
          continue;
        
        if (!can_unit_move_to_tile(punit, x1, y1, FALSE))
          continue;

        /* We won't travel into cities, unless we are able to do so - diplomats
         * and caravans can. */
        /* FIXME/TODO: special flag for this? --pasky */
        /* FIXME: either comment or code is wrong here :-) --pasky */
        if (map_get_city(x1, y1) && (unit_flag(punit, F_DIPLOMAT) 
                                     || unit_flag(punit, F_TRADE_ROUTE)))
          continue;

        if (is_barbarian(pplayer) && map_has_special(x1, y1, S_HUT))
          continue;
          
        most_unknown = unknown;
        best_x = x1;
        best_y = y1;
      }
    } square_iterate_end;

    if (most_unknown > 0) {
      /* We can die because easy AI may stumble on huts and so disappear in the
       * wilderness - unit_id is used to check this */
      int unit_id = punit->id;
      
      /* Some tile have unexplored territory adjacent, let's move there. */
      
      if (!handle_unit_move_request(punit, best_x, best_y, FALSE, FALSE)) {
        /* This shouldn't happen, but occassionally it can. */
        break;
      }
      
      if (!player_find_unit_by_id(pplayer, unit_id)) {
        /* We're dead. */
        return FALSE;
      }
      
      x = punit->x;
      y = punit->y;
      
    } else {
      /* Everything is already explored. */
      break;
    }
  }

  if (punit->moves_left == 0) {
    /* We can't move on anymore. */
    return TRUE;
  }

  /* 
   * PART 3: Go towards unexplored territory
   * No adjacent squares help us to explore - really slow part follows.
   */

  {
    /* Best (highest) number of unknown tiles adjectent (in vision range) */
    int most_unknown = 0;
    /* Desired destination */
    int best_x = -1, best_y = -1;
  
    generate_warmap(map_get_city(x, y), punit);

    /* XXX: There's some duplicate code here, but it's several tiny pieces,
     * impossible to group together and not worth their own function
     * separately. --pasky */
    
    whole_map_iterate(x1, y1) {
      /* The actual map tile */
      struct tile *ptile = map_get_tile(x1, y1);
      
      if (ptile->continent == continent
          && !is_non_allied_unit_tile(ptile, pplayer)
          && !is_non_allied_city_tile(ptile, pplayer)
          && tile_is_accessible(punit, x1, y1)) {
        /* Number of unknown tiles in vision range around this tile */
        int unknown = 0;
        
        square_iterate(x1, y1, range, x2, y2) {
          if (!map_get_known(x2, y2, pplayer))
            unknown++;
        } square_iterate_end;
        
        if (unknown > 0) {
#define COSTWEIGHT 9
          /* How far it's worth moving away */
          int threshold = THRESHOLD * move_rate;
          
          if (is_sailing_unit(punit))
            unknown += COSTWEIGHT * (threshold - warmap.seacost[x1][y1]);
          else
            unknown += COSTWEIGHT * (threshold - warmap.cost[x1][y1]);
          
          /* FIXME? Why we don't do same tests like in part 2? --pasky */
          if (((unknown > most_unknown) ||
               (unknown == most_unknown && myrand(2) == 1))
              && !(is_barbarian(pplayer) && tile_has_special(ptile, S_HUT))) {
            best_x = x1;
            best_y = y1;
            most_unknown = unknown;
          }
#undef COSTWEIGHT
        }
      }
    } whole_map_iterate_end;

    if (most_unknown > 0) {
      /* Go there! */
      
      punit->goto_dest_x = best_x;
      punit->goto_dest_y = best_y;
      handle_unit_activity_request(punit, ACTIVITY_GOTO);
      do_unit_goto(punit, GOTO_MOVE_ANY, FALSE);

      /* FIXME? Why we don't test if the unit is still alive? */
      /* TODO: Take more advantage from the do_unit_goto() return value. */
      
      if (punit->moves_left > 0) {
        /* We can still move on... */

        if (punit->x == best_x && punit->y == best_y) {
          /* ...and got into desired place. */
          return ai_manage_explorer(punit);
          
        } else {
          /* Something went wrong. What to do but return? */
          handle_unit_activity_request(punit, ACTIVITY_IDLE);
          return TRUE;
        }
        
      } else {
        return TRUE;
      }
    }
    
    /* No candidates; fall-through. */
  }

  /* We have nothing to explore, so we can go idle. */
  
  freelog(LOG_DEBUG, "%s's %s at (%d,%d) failed to explore.",
          pplayer->name, unit_type(punit)->name, punit->x, punit->y);
  handle_unit_activity_request(punit, ACTIVITY_IDLE);
  
  /* 
   * PART 4: Go home
   * If we are AI controlled _military_ unit (so Explorers don't count, why?
   * --pasky), we will return to our homecity, maybe even to another continent.
   */
  
  if (pplayer->ai.control && is_military_unit(punit)) {
    /* Unit's homecity */
    struct city *pcity = find_city_by_id(punit->homecity);

    if (pcity) {
      if (map_get_continent(pcity->x, pcity->y) == continent) {
        ai_military_gohome(pplayer, punit);
      } else {
        /* Sea travel */
        if (find_boat(pplayer, &x, &y, 0) == 0) {
          punit->ai.ferryboat = -1;
        } else {
          punit->goto_dest_x = x;
          punit->goto_dest_y = y;
          do_unit_goto(punit, GOTO_MOVE_ANY, FALSE);
        }
      }
    }
  }

  return FALSE;
}

/*********************************************************************
  In the words of Syela: "Using funky fprime variable instead of f in
  the denom, so that def=1 units are penalized correctly."

  Translation (GB): build_cost_balanced is used in the denominator of
  the want equation (see, e.g.  find_something_to_kill) instead of
  just build_cost to make AI build more balanced units (with def > 1).
*********************************************************************/
int build_cost_balanced(Unit_Type_id type)
{
  struct unit_type *ptype = get_unit_type(type);

  return 2 * ptype->build_cost * ptype->attack_strength /
      (ptype->attack_strength + ptype->defense_strength);
}


/**************************************************************************
...
**************************************************************************/
static struct city *wonder_on_continent(struct player *pplayer, int cont)
{
  city_list_iterate(pplayer->cities, pcity) 
    if (!(pcity->is_building_unit) && is_wonder(pcity->currently_building) && map_get_continent(pcity->x, pcity->y) == cont)
      return pcity;
  city_list_iterate_end;
  return NULL;
}

/**************************************************************************
Returns whether we stayed in the (eventual) city on the square to defend it.
**************************************************************************/
static bool stay_and_defend_city(struct unit *punit)
{
  struct city *pcity = map_get_city(punit->x, punit->y);
  bool has_defense = FALSE;

  if (!pcity) return FALSE;
  if (pcity->id == punit->homecity) return FALSE;
  if (pcity->owner != punit->owner) return FALSE;

  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, pdef) {
    if (assess_defense_unit(pcity, punit, FALSE) >= 0
	&& pdef != punit
	&& pdef->homecity == pcity->id) {
      has_defense = TRUE;
    }
  } unit_list_iterate_end;
 
  /* Guess I better stay / you can live at home now */
  if (!has_defense) {
    struct packet_unit_request packet;
    punit->ai.ai_role = AIUNIT_DEFEND_HOME;

    punit->ai.charge = 0; /* Very important, or will not stay -- Syela */
    
    /* change homecity to this city */
    /* FIXME: it is stupid to change homecity if the unit has no homecity
       in advance or the new city does not have enough shields to support it */
    packet.unit_id = punit->id;
    packet.city_id = pcity->id;
    handle_unit_change_homecity(unit_owner(punit), &packet);
    return TRUE;
  }
  return FALSE;
}

/**************************************************************************
...
**************************************************************************/
int base_unit_belligerence_primitive(Unit_Type_id type, bool veteran,
				     int moves_left, int hp)
{
  return (base_get_attack_power(type, veteran, moves_left) * hp *
	  get_unit_type(type)->firepower / POWER_DIVIDER);
}

static int unit_belligerence_primitive(struct unit *punit)
{
  return (base_unit_belligerence_primitive(punit->type, punit->veteran,
					   punit->moves_left, punit->hp));
}

int unit_belligerence_basic(struct unit *punit)
{
  return (base_unit_belligerence_primitive(punit->type, punit->veteran,
					   SINGLE_MOVE, punit->hp));
}

int unit_belligerence(struct unit *punit)
{
  int v = unit_belligerence_basic(punit);
  return(v * v);
}

int unit_vulnerability_basic(struct unit *punit, struct unit *pdef)
{
  return (get_total_defense_power(punit, pdef) *
	  (punit->id != 0 ? pdef->hp : unit_type(pdef)->hp) *
	  unit_type(pdef)->firepower / POWER_DIVIDER);
}

int unit_vulnerability_virtual(struct unit *punit)
{
  int v = base_get_defense_power(punit) * punit->hp / POWER_DIVIDER;

  return v * v;
}

/**************************************************************************
  See get_virtual_defense_power for the arguments att_type, def_type,
  x, y, fortified and veteran. If use_alternative_hp is FALSE the
  (maximum) hps of the given unit type is used. If use_alternative_hp
  is TRUE the given alternative_hp value is used.
**************************************************************************/
int unit_vulnerability_virtual2(Unit_Type_id att_type, Unit_Type_id def_type,
				int x, int y, bool fortified, bool veteran,
				bool use_alternative_hp, int alternative_hp)
{
  int v = (get_virtual_defense_power(att_type, def_type, x, y, fortified,
				     veteran) *
	   (use_alternative_hp ? alternative_hp : unit_types[def_type].
	    hp) * unit_types[def_type].firepower / POWER_DIVIDER);
  return v * v;
}

int unit_vulnerability(struct unit *punit, struct unit *pdef)
{
  int v = unit_vulnerability_basic(punit, pdef);
  return (v * v);
}

static int stack_attack_value(int x, int y)
{
  int val = 0;
  struct tile *ptile = map_get_tile(x, y);
  unit_list_iterate(ptile->units, aunit)
    val += unit_belligerence_basic(aunit);
  unit_list_iterate_end;
  return(val);
}

/*************************************************************************
  Mark an invasion threat of punit in the surrounding cities. The
  given radius limites the area which is searched for cities. The
  center of the area is either the unit itself (dest == FALSE) or the
  destiniation of the current goto (dest == TRUE). The invasion threat
  is marked in pcity->ai.invasion via ORing the "which" argument (to
  tell attack from sea apart from ground unit attacks). Note that
  "which" should only have one bit set.
**************************************************************************/
static void invasion_funct(struct unit *punit, bool dest, int radius,
			   int which)
{
  int x, y;
  bool want_attack = (dest || punit->activity != ACTIVITY_GOTO);

  if (dest) {
    x = punit->goto_dest_x;
    y = punit->goto_dest_y;
  } else {
    x = punit->x;
    y = punit->y;
  }

  square_iterate(x, y, radius, x1, y1) {
    struct city *pcity = map_get_city(x1, y1);

    if (pcity && pcity->owner != punit->owner
	&& (pcity->ai.invasion & which) != which
	&& (want_attack || !has_defense(pcity))) {
      pcity->ai.invasion |= which;
    }
  } square_iterate_end;
}

/**************************************************************************
Compute how much we want to kill certain victim we've chosen, counted in
SHIELDs.

FIXME?: The equation is not accurate as the other values can vary for other
victims on same tile (we take values from best defender) - however I believe
it's accurate just enough now and lost speed isn't worth that. --pasky

Benefit is something like 'attractiveness' of the victim, how nice it would be
to destroy it. Larger value, worse loss for enemy.

Attack is the total possible attack power we can throw on the victim. Note that
it usually comes squared.

Loss is the possible loss when we would lose the unit we are attacking with (in
SHIELDs).

Vuln is vulnerability of our unit when attacking the enemy. Note that it
usually comes squared as well.

Victim count is number of victims stacked in the target tile. Note that we
shouldn't treat cities as a stack (despite the code using this function) - the
scaling is probably different. (extremely dodgy usage of it -- GB)
**************************************************************************/
int kill_desire(int benefit, int attack, int loss, int vuln, int victim_count)
{
  int desire;

  /*         attractiveness     danger */ 
  desire = ((benefit * attack - loss * vuln) * victim_count * SHIELD_WEIGHTING
            / (attack + vuln * victim_count));

  return desire;
}

/**************************************************************************
Military "want" estimates are amortized in this complicated way.
COMMENTME: Why not use simple amortize? -- GB
**************************************************************************/
int military_amortize(int value, int delay, int build_cost)
{
  int simply_amortized, fully_amortized;

  if (value <= 0) {
    return 0;
  }

  simply_amortized = amortize(value, delay);
  fully_amortized = ((value * simply_amortized) * 100
                     / (MAX(1, value - simply_amortized))
                     / (build_cost * MORT));

  return fully_amortized;
}

/**************************************************************************
 this is still pretty dopey but better than the original -- Syela
**************************************************************************/
static int reinforcements_value(struct unit *punit, int x, int y)
{
  int val = 0;
  square_iterate(x, y, 1, i, j) {
    struct tile *ptile = map_get_tile(i, j);
    unit_list_iterate(ptile->units, aunit) {
      if (aunit == punit || aunit->owner != punit->owner) continue;
      val += (unit_belligerence_basic(aunit));
    } unit_list_iterate_end;
  } square_iterate_end;

  return val;
}

static int city_reinforcements_cost_and_value(struct city *pcity, struct unit *punit)
{ 
  int val, val2 = 0, val3 = 0;
  struct tile *ptile;

  if (!pcity) return 0;

  square_iterate(pcity->x, pcity->y, 1, i, j) {
    ptile = map_get_tile(i, j);
    unit_list_iterate(ptile->units, aunit) {
      if (aunit == punit || aunit->owner != punit->owner)
	continue;
      val = (unit_belligerence_basic(aunit));
      if (val != 0) {
	val2 += val;
	val3 += unit_type(aunit)->build_cost;
      }
    }
    unit_list_iterate_end;
  } square_iterate_end;

  pcity->ai.a = val2;
  pcity->ai.f = val3;
  return(val2);
}

#ifdef UNUSED
static int reinforcements_cost(struct unit *punit, int x, int y)
{ /* I might rather have one function which does this and the above -- Syela */
  int val = 0;
  struct tile *ptile;

  square_iterate(x, y, 1, i, j) {
    ptile = map_get_tile(i, j);
    unit_list_iterate(ptile->units, aunit) {
      if (aunit == punit || aunit->owner != punit->owner)
	continue;
      if (unit_belligerence_basic(aunit))
	val += unit_type(aunit)->build_cost;
    }
    unit_list_iterate_end;
  }
  square_iterate_end;

  return(val);
}
#endif

/*************************************************************************
...
**************************************************************************/
static bool is_my_turn(struct unit *punit, struct unit *pdef)
{
  int val = unit_belligerence_primitive(punit), cur, d;
  struct tile *ptile;

  square_iterate(pdef->x, pdef->y, 1, i, j) {
    ptile = map_get_tile(i, j);
    unit_list_iterate(ptile->units, aunit) {
      if (aunit == punit || aunit->owner != punit->owner)
	continue;
      if (!can_unit_attack_unit_at_tile(aunit, pdef, pdef->x, pdef->y))
	continue;
      d = get_virtual_defense_power(aunit->type, pdef->type, pdef->x,
				    pdef->y, FALSE, FALSE);
      if (d == 0)
	return TRUE;		/* Thanks, Markus -- Syela */
      cur = unit_belligerence_primitive(aunit) *
	  get_virtual_defense_power(punit->type, pdef->type, pdef->x,
				    pdef->y, FALSE, FALSE) / d;
      if (cur > val && ai_fuzzy(unit_owner(punit), TRUE))
	return FALSE;
    }
    unit_list_iterate_end;
  }
  square_iterate_end;
  return TRUE;
}

/*************************************************************************
This function looks at tiles adjacent to the unit in order to find something
to kill or explore.  It prefers tiles in the following order:

Returns value of the victim which has been chosen:

99999    means empty (undefended) city
99998    means hut
2..99997 means value of enemy unit weaker than our unit
1        means barbarians wanting to pillage
0        means nothing found or error
-2*MORT*TRADE_WEIGHTING
         means nothing found and punit causing unhappiness

If value <= 0 is returned, (dest_x, dest_y) is set to actual punit's position.
**************************************************************************/
static int ai_military_findvictim(struct player *pplayer, struct unit *punit,
                                  int *dest_x, int *dest_y)
{
  /* Set the tile with our target as the best (with value new_best). */
#define SET_BEST(new_best) \
  do { best = (new_best); *dest_x = x1; *dest_y = y1; } while (FALSE)

  int bellig = unit_belligerence_primitive(punit);
  int x = punit->x, y = punit->y;
  int best = 0;
  int stack_size = unit_list_size(&(map_get_tile(punit->x, punit->y)->units));
  
  *dest_y = y;
  *dest_x = x;
 
  /* Ferryboats with passengers do not attack. */
  if (punit->ai.passenger > 0) {
    return 0;
  }
 
  if (punit->unhappiness > 0) {
    /* When we're causing unhappiness, we'll set best even lower, 
     * so that we will take even targets which we would ignore otherwise 
     * (in other words -- we're going to commit suicide). */
    best = - 2 * MORT * TRADE_WEIGHTING;
  }

  adjc_iterate(x, y, x1, y1) {
    struct unit *pdef = get_defender(punit, x1, y1);
    
    if (pdef) {
      struct unit *patt = get_attacker(punit, x1, y1);

      if (!can_unit_attack_tile(punit, x1, y1)) {
        continue;
      }

      /* If we are in the city, let's deeply consider defending it - however,
       * horsemen in city refused to attack phalanx just outside that was
       * bodyguarding catapult; thus, we get the best attacker on the tile (x1,
       * y1) as well, for the case when there are multiple different units on
       * the tile (x1, y1). Thus we force punit to attack a stack of units if
       * they're endangering punit seriously, even if they aren't that weak. */
      /* FIXME: The get_total_defense_power(pdef, punit) should probably use
       * patt rather than pdef. There also ought to be a better metric for
       * determining this. */
      if (map_get_city(x, y) && get_total_defense_power(pdef, punit) *
                                get_total_defense_power(punit, pdef) >=
                                get_total_attack_power(patt, punit) *
                                get_total_attack_power(punit, pdef)
          && stack_size < 2 && get_total_attack_power(patt, punit) > 0) {
        freelog(LOG_DEBUG, "%s defending %s from %s's %s",
                unit_type(punit)->name,
                map_get_city(x, y)->name,
                unit_owner(pdef)->name, unit_type(pdef)->name);
        
      } else {
        /* See description of kill_desire() about this variables. */
        int vuln = unit_vulnerability(punit, pdef);
        int attack = reinforcements_value(punit, pdef->x, pdef->y) + bellig;
        int benefit = unit_type(pdef)->build_cost;
        int loss = unit_type(punit)->build_cost;
        
        attack *= attack;
        
        /* If the victim is in the city, we increase the benefit and correct
         * it with our health because there may be more units in the city
         * stacked, and we won't destroy them all at once, so in the next
         * turn they may attack us. So we shouldn't send already injured
         * units to useless suicide. */
        if (map_get_city(x1, y1)) {
          benefit += unit_value(get_role_unit(F_CITIES, 0));
          benefit = (benefit * punit->hp) / unit_type(punit)->hp;
        }
        
        /* If we have non-zero belligerence... */
        if (attack > 0 && is_my_turn(punit, pdef)) {
          int desire;

          /* FIXME? Why we don't use stack_size as victim_count? --pasky */

          desire = kill_desire(benefit, attack, loss, vuln, 1);
          
          /* No need to amortize! We're doing it in one-turn horizon. */
          
          if (desire > best && ai_fuzzy(pplayer, TRUE)) {
            freelog(LOG_DEBUG, "Better than %d is %d (%s)",
                          best, desire, unit_type(pdef)->name);
            SET_BEST(desire);
            
          } else {
            freelog(LOG_DEBUG, "NOT better than %d is %d (%s)",
                    best, desire, unit_type(pdef)->name);
          }
        }
      }
      
    } else {
      struct city *pcity = map_get_city(x1, y1);
      
      /* No defender... */
     
      /* ...and free foreign city waiting for us. Who would resist! */
      /* TODO: When diplomacy is implemented, don't capture enemy cities so
       * enthusiastically. -- mike, pasky */
      if (pcity && pcity->owner != pplayer->player_no
          && is_non_allied_city_tile(map_get_tile(x1, y1), pplayer)
          && is_ground_unit(punit)
          && map_get_terrain(punit->x, punit->y) != T_OCEAN) {
        SET_BEST(99999);
        continue;
      }

      /* ...or tiny pleasant hut here! */
      if (map_has_special(x1, y1, S_HUT) && best < 99999
          && could_unit_move_to_tile(punit, punit->x, punit->y, x1, y1) != 0
          && !is_barbarian(unit_owner(punit))
          && punit->ai.ai_role != AIUNIT_ESCORT
          && punit->ai.charge == 0 /* Above line doesn't seem to work. :( */
          && punit->ai.ai_role != AIUNIT_DEFEND_HOME) {
        SET_BEST(99998);
        continue;
      }
      
      /* If we have nothing to do, we can at least pillage something, hmm? */
      if (is_land_barbarian(pplayer) && best == 0
          && get_tile_infrastructure_set(map_get_tile(x1, y1)) != S_NO_SPECIAL
          && could_unit_move_to_tile(punit, punit->x, punit->y, x1, y1) != 0) {
        SET_BEST(1);
        continue;
      }
    }
  } adjc_iterate_end;

  return best;
  
#undef SET_BEST
}

/*************************************************************************
...
**************************************************************************/
static void ai_military_bodyguard(struct player *pplayer, struct unit *punit)
{
  struct unit *aunit = player_find_unit_by_id(pplayer, punit->ai.charge);
  struct city *acity = find_city_by_id(punit->ai.charge);
  int x, y, id = punit->id, i;

  if (aunit && aunit->owner == punit->owner) { x = aunit->x; y = aunit->y; }
  else if (acity && acity->owner == punit->owner) { x = acity->x; y = acity->y; }
  else { /* should be impossible */
    punit->ai.charge = 0;
    return;
  }
  
  if (aunit) {
    freelog(LOG_DEBUG, "%s#%d@(%d,%d) -> %s#%d@(%d,%d) [body=%d]",
	    unit_type(punit)->name, punit->id, punit->x, punit->y,
	    unit_type(aunit)->name, aunit->id, aunit->x, aunit->y,
	    aunit->ai.bodyguard);
  }

  if (!same_pos(punit->x, punit->y, x, y)) {
    if (goto_is_sane(punit, x, y, TRUE)) {
      punit->goto_dest_x = x;
      punit->goto_dest_y = y;
      do_unit_goto(punit, GOTO_MOVE_ANY, FALSE);
    } else punit->ai.charge = 0; /* can't possibly get there to help */
  } else { /* I had these guys set to just fortify, which is so dumb. -- Syela */
    i = ai_military_findvictim(pplayer, punit, &x, &y);
    freelog(LOG_DEBUG,
	    "Stationary escort @(%d,%d) received %d best @(%d,%d)",
	    punit->x, punit->y, i, x, y);
    if (i >= 40 * SHIELD_WEIGHTING)
      handle_unit_move_request(punit, x, y, FALSE, FALSE);
/* otherwise don't bother, but free cities are free cities and must be snarfed. -- Syela */
  }
  if (aunit && unit_list_find(&map_get_tile(x, y)->units, id) && aunit->ai.bodyguard != 0)
    aunit->ai.bodyguard = id;
}

/*************************************************************************
...
**************************************************************************/
int find_beachhead(struct unit *punit, int dest_x, int dest_y, int *x, int *y)
{
  int ok, t, best = 0;

  adjc_iterate(dest_x, dest_y, x1, y1) {
    ok = 0;
    t = map_get_terrain(x1, y1);
    if (warmap.seacost[x1][y1] <= 6 * THRESHOLD && t != T_OCEAN) {
      /* accessible beachhead */
      adjc_iterate(x1, y1, x2, y2) {
	if (map_get_terrain(x2, y2) == T_OCEAN
	    && is_my_zoc(unit_owner(punit), x2, y2)) {
	  ok++;
	  goto OK;

	  /* FIXME: The above used to be "break; out of adjc_iterate",
	  but this is incorrect since the current adjc_iterate() is
	  implemented as two nested loops.  */
	}
      } adjc_iterate_end;
    OK:

      if (ok > 0) {
	/* accessible beachhead with zoc-ok water tile nearby */
        ok = get_tile_type(t)->defense_bonus;
	if (map_has_special(x1, y1, S_RIVER))
	  ok += (ok * terrain_control.river_defense_bonus) / 100;
        if (get_tile_type(t)->movement_cost * SINGLE_MOVE <
            unit_type(punit)->move_rate)
	  ok *= 8;
        ok += (6 * THRESHOLD - warmap.seacost[x1][y1]);
        if (ok > best) {
	  best = ok;
	  *x = x1;
	  *y = y1;
	}
      }
    }
  } adjc_iterate_end;

  return best;
}

/**************************************************************************
find_beachhead() works only when city is not further that 1 tile from
the sea. But Sea Raiders might want to attack cities inland.
So this finds the nearest land tile on the same continent as the city.
**************************************************************************/
static void find_city_beach( struct city *pc, struct unit *punit, int *x, int *y)
{
  int best_x = punit->x;
  int best_y = punit->y;
  int dist = 100;
  int search_dist = real_map_distance(pc->x, pc->y, punit->x, punit->y) - 1;
  
  square_iterate(punit->x, punit->y, search_dist, xx, yy) {
    if (map_get_continent(xx, yy) == map_get_continent(pc->x, pc->y)
        && real_map_distance(punit->x, punit->y, xx, yy) < dist) {

      dist = real_map_distance(punit->x, punit->y, xx, yy);
      best_x = xx;
      best_y = yy;
    }
  } square_iterate_end;

  *x = best_x;
  *y = best_y;
}

/**************************************************************************
...
**************************************************************************/
static int ai_military_gothere(struct player *pplayer, struct unit *punit,
			       int dest_x, int dest_y)
{
  int id, x, y, boatid = 0, bx = 0, by = 0;
  struct unit *ferryboat;
  struct unit *def;
  struct city *dcity;
  struct tile *ptile;
  int d_type, d_val;

  id = punit->id; x = punit->x; y = punit->y;

  if (is_ground_unit(punit)) boatid = find_boat(pplayer, &bx, &by, 2);
  ferryboat = unit_list_find(&(map_get_tile(x, y)->units), boatid);

  if (!(dest_x == x && dest_y == y)) {

/* do we require protection? */
    d_val = stack_attack_value(dest_x, dest_y);
    if ((dcity = map_get_city(dest_x, dest_y))) {
      d_type = ai_choose_defender_versus(dcity, punit->type);
      d_val += 
	base_unit_belligerence_primitive(d_type, 
                                         do_make_unit_veteran(dcity, d_type), 
                                         SINGLE_MOVE, unit_types[d_type].hp);
    }
    d_val *= POWER_DIVIDER;
    d_val /= (unit_type(punit)->move_rate / SINGLE_MOVE);
    if (unit_flag(punit, F_IGTER)) d_val /= 1.5;
    freelog(LOG_DEBUG,
	    "%s@(%d,%d) looking for bodyguard, d_val=%d, my_val=%d",
	    unit_type(punit)->name, punit->x, punit->y, d_val,
	    (punit->hp * (punit->veteran ? 15 : 10)
	     * unit_type(punit)->defense_strength));
    ptile = map_get_tile(punit->x, punit->y);
    if (d_val >= punit->hp * (punit->veteran ? 15 : 10) *
                unit_type(punit)->defense_strength) {
/*      if (!unit_list_find(&pplayer->units, punit->ai.bodyguard))   Ugggggh */
      if (!unit_list_find(&ptile->units, punit->ai.bodyguard))
        punit->ai.bodyguard = -1;
    } else if (!unit_list_find(&ptile->units, punit->ai.bodyguard))
      punit->ai.bodyguard = 0;

    if( is_barbarian(pplayer) )  /* barbarians must have more currage */
      punit->ai.bodyguard = 0;
/* end protection subroutine */

    if (!goto_is_sane(punit, dest_x, dest_y, TRUE) ||
       (ferryboat && goto_is_sane(ferryboat, dest_x, dest_y, TRUE))) { /* Important!! */
      punit->ai.ferryboat = boatid;
      freelog(LOG_DEBUG, "%s: %d@(%d, %d): Looking for BOAT (id=%d).",
		    pplayer->name, punit->id, punit->x, punit->y, boatid);
      if (!same_pos(x, y, bx, by)) {
        punit->goto_dest_x = bx;
        punit->goto_dest_y = by;
	set_unit_activity(punit, ACTIVITY_GOTO);
        do_unit_goto(punit, GOTO_MOVE_ANY, FALSE);
        if (!player_find_unit_by_id(pplayer, id)) return(-1); /* died */
      }
      ptile = map_get_tile(punit->x, punit->y);
      ferryboat = unit_list_find(&ptile->units, punit->ai.ferryboat);
      if (ferryboat && (ferryboat->ai.passenger == 0 ||
          ferryboat->ai.passenger == punit->id)) {
	freelog(LOG_DEBUG, "We have FOUND BOAT, %d ABOARD %d@(%d,%d)->(%d, %d).",
		punit->id, ferryboat->id, punit->x, punit->y,
		dest_x, dest_y);
        set_unit_activity(punit, ACTIVITY_SENTRY); /* kinda cheating -- Syela */ 
        ferryboat->ai.passenger = punit->id;
/* the code that worked for settlers wasn't good for piles of cannons */
        if (find_beachhead(punit, dest_x, dest_y, &ferryboat->goto_dest_x,
               &ferryboat->goto_dest_y) != 0) {
/*  set_unit_activity(ferryboat, ACTIVITY_GOTO);   -- Extremely bad!! -- Syela */
          punit->goto_dest_x = dest_x;
          punit->goto_dest_y = dest_y;
          set_unit_activity(punit, ACTIVITY_SENTRY); /* anything but GOTO!! */
	  if (ground_unit_transporter_capacity(punit->x, punit->y, pplayer)
	      <= 0) {
	    freelog(LOG_DEBUG, "All aboard!");
	    /* perhaps this should only require two passengers */
            unit_list_iterate(ptile->units, mypass)
              if (mypass->ai.ferryboat == ferryboat->id) {
                set_unit_activity(mypass, ACTIVITY_SENTRY);
                def = unit_list_find(&ptile->units, mypass->ai.bodyguard);
                if (def) set_unit_activity(def, ACTIVITY_SENTRY);
              }
            unit_list_iterate_end; /* passengers are safely stowed away */
            do_unit_goto(ferryboat, GOTO_MOVE_ANY, FALSE);
	    if (!player_find_unit_by_id(pplayer, boatid)) return(-1); /* died */
            set_unit_activity(punit, ACTIVITY_IDLE);
          } /* else wait, we can GOTO later. */
        }
      } 
    }
    if (goto_is_sane(punit, dest_x, dest_y, TRUE) && punit->moves_left > 0 &&
       (!ferryboat || 
       (real_map_distance(punit->x, punit->y, dest_x, dest_y) < 3 &&
       (punit->ai.bodyguard == 0 || unit_list_find(&(map_get_tile(punit->x,
       punit->y)->units), punit->ai.bodyguard) || (dcity &&
       !has_defense(dcity)))))) {
/* if we are on a boat, disembark only if we are within two tiles of
our target, and either 1) we don't need a bodyguard, 2) we have a
bodyguard, or 3) we are going to an empty city.  Previously, cannons
would disembark before the cruisers arrived and die. -- Syela */

      punit->goto_dest_x = dest_x;
      punit->goto_dest_y = dest_y;

/* The following code block is supposed to stop units from running away from their
bodyguards, and not interfere with those that don't have bodyguards nearby -- Syela */
/* The case where the bodyguard has moves left and could meet us en route is not
handled properly.  There should be a way to do it with dir_ok but I'm tired now. -- Syela */
      if (punit->ai.bodyguard < 0) { 
	adjc_iterate(punit->x, punit->y, i, j) {
	  unit_list_iterate(map_get_tile(i, j)->units, aunit) {
	    if (aunit->ai.charge == punit->id) {
	      freelog(LOG_DEBUG,
		      "Bodyguard at (%d, %d) is adjacent to (%d, %d)",
		      i, j, punit->x, punit->y);
	      if (aunit->moves_left > 0) return(0);
	      else return handle_unit_move_request(punit, i, j, FALSE, FALSE) ? 1 : 0;
	    }
	  } unit_list_iterate_end;
        } adjc_iterate_end;
      } /* end if */
/* end 'short leash' subroutine */

      if (ferryboat) {
	freelog(LOG_DEBUG, "GOTHERE: %s#%d@(%d,%d)->(%d,%d)", 
		unit_type(punit)->name, punit->id,
		punit->x, punit->y, dest_x, dest_y);
      }
      set_unit_activity(punit, ACTIVITY_GOTO);
      do_unit_goto(punit, GOTO_MOVE_ANY, FALSE);
      /* liable to bump into someone that will kill us.  Should avoid? */
    } else {
      freelog(LOG_DEBUG, "%s#%d@(%d,%d) not moving -> (%d, %d)",
		    unit_type(punit)->name, punit->id,
		    punit->x, punit->y, dest_x, dest_y);
    }
  }
  if (player_find_unit_by_id(pplayer, id)) { /* didn't die */
    punit->ai.ai_role = AIUNIT_NONE; /* in case we need to change */
    if (x != punit->x || y != punit->y) return 1; /* moved */
    else return 0; /* didn't move, didn't die */
  }
  return(-1); /* died */
}

/*************************************************************************
...
**************************************************************************/
static bool unit_can_defend(Unit_Type_id type)
{
  if (unit_types[type].move_type != LAND_MOVING) return FALSE; /* temporary kluge */
  return (unit_has_role(type, L_DEFEND_GOOD));
}

#if 0
/* pre-rulesets method, which was too hard to generalize (because
   whether a unit is a "good" defender depends on how good other
   units are)
*/
int old_unit_can_defend(Unit_Type_id type)
{
  if (unit_types[type].move_type != LAND_MOVING) return FALSE; /* temporary kluge */
  if (unit_types[type].defense_strength * 
      (unit_types[type].hp > 10 ? 5 : 3) >=
      unit_types[type].attack_strength * 4 &&
      !unit_types[type].transport_capacity &&
      !unit_flag(type, F_NONMIL)) return TRUE;
  return FALSE;
}
#endif

/*************************************************************************
...
**************************************************************************/
int look_for_charge(struct player *pplayer, struct unit *punit, struct unit **aunit, struct city **acity)
{
  int d, def, val = 0, u;
  u = unit_vulnerability_virtual(punit);
  if (u == 0) return(0);
  unit_list_iterate(pplayer->units, buddy)
    if (buddy->ai.bodyguard == 0) continue;
    if (!goto_is_sane(punit, buddy->x, buddy->y, TRUE)) continue;
    if (unit_type(buddy)->move_rate > unit_type(punit)->move_rate) continue;
    if (unit_type(buddy)->move_type != unit_type(punit)->move_type) continue;
    d = unit_move_turns(punit, buddy->x, buddy->y);
    def = (u - unit_vulnerability_virtual(buddy))>>d;
    freelog(LOG_DEBUG, "(%d,%d)->(%d,%d), %d turns, def=%d",
	    punit->x, punit->y, buddy->x, buddy->y, d, def);
    unit_list_iterate(pplayer->units, body)
      if (body->ai.charge == buddy->id) def = 0;
    unit_list_iterate_end;
    if (def > val && ai_fuzzy(pplayer, TRUE)) { *aunit = buddy; val = def; }
  unit_list_iterate_end;
  city_list_iterate(pplayer->cities, mycity)
    if (!goto_is_sane(punit, mycity->x, mycity->y, TRUE)) continue;
    if (mycity->ai.urgency == 0) continue;
    d = unit_move_turns(punit, mycity->x, mycity->y);
    def = (mycity->ai.danger - assess_defense_quadratic(mycity))>>d;
    if (def > val && ai_fuzzy(pplayer, TRUE)) { *acity = mycity; val = def; }
  city_list_iterate_end;
  freelog(LOG_DEBUG, "%s@(%d,%d) looking for charge; %d/%d",
	  unit_type(punit)->name,
	  punit->x, punit->y, val, val * 100 / u);
  return((val * 100) / u);
}

/********************************************************************** 
...
***********************************************************************/
static void ai_military_findjob(struct player *pplayer,struct unit *punit)
{
  struct city *pcity = NULL, *acity = NULL;
  struct unit *aunit;
  int val, def;
  int q = 0;

/* tired of AI abandoning its cities! -- Syela */
  if (punit->homecity != 0 && (pcity = find_city_by_id(punit->homecity))) {
    if (pcity->ai.danger != 0) { /* otherwise we can attack */
      def = assess_defense(pcity);
      if (punit->x == pcity->x && punit->y == pcity->y) { /* I'm home! */
        val = assess_defense_unit(pcity, punit, FALSE); 
        def -= val; /* old bad kluge fixed 980803 -- Syela */
/* only the least defensive unit may leave home */
/* and only if this does not jeopardize the city */
/* def is the defense of the city without punit */
        if (unit_flag(punit, F_FIELDUNIT)) val = -1;
        unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, pdef)
          if (is_military_unit(pdef) && pdef != punit &&
              !unit_flag(pdef, F_FIELDUNIT)) {
            if (assess_defense_unit(pcity, pdef, FALSE) >= val) val = 0;
          }
        unit_list_iterate_end; /* was getting confused without the is_military part in */
        if (unit_vulnerability_virtual(punit) == 0) q = 0; /* thanks, JMT, Paul */
        else q = (pcity->ai.danger * 2 - def * unit_type(punit)->attack_strength /
             unit_type(punit)->defense_strength);
/* this was a WAG, but it works, so now it's just good code! -- Syela */
        if (val > 0 || q > 0) { /* Guess I better stay */
          ;
        } else q = 0;
      } /* end if home */
    } /* end if home is in danger */
  } /* end if we have a home */

  /* keep barbarians aggresive and primitive */
  if (is_barbarian(pplayer)) {
    if (can_unit_do_activity(punit, ACTIVITY_PILLAGE)
	&& is_land_barbarian(pplayer))
      punit->ai.ai_role = AIUNIT_PILLAGE;  /* land barbarians pillage */
    else
      punit->ai.ai_role = AIUNIT_ATTACK;
    return;
  }

  if (punit->ai.charge != 0) { /* I am a bodyguard */
    aunit = player_find_unit_by_id(pplayer, punit->ai.charge);
    acity = find_city_by_id(punit->ai.charge);

    if ((aunit && aunit->ai.bodyguard != 0 && unit_vulnerability_virtual(punit) >
         unit_vulnerability_virtual(aunit)) ||
         (acity && acity->owner == punit->owner && acity->ai.urgency != 0 &&
          acity->ai.danger > assess_defense_quadratic(acity))) {
      punit->ai.ai_role = AIUNIT_ESCORT;
      return;
    } else punit->ai.charge = 0;
  }

  /* ok, what if I'm somewhere new? - ugly, kludgy code by Syela */
  if (stay_and_defend_city(punit)) {
    return;
  }

  if (q > 0) {
    if (pcity->ai.urgency != 0) {
      punit->ai.ai_role = AIUNIT_DEFEND_HOME;
      return;
    }
  }

/* I'm not 100% sure this is the absolute best place for this... -- Syela */
  generate_warmap(map_get_city(punit->x, punit->y), punit);
/* I need this in order to call unit_move_turns, here and in look_for_charge */

  if (q > 0) {
    q *= 100;
    q /= unit_vulnerability_virtual(punit);
    q >>= unit_move_turns(punit, pcity->x, pcity->y);
  }

  val = 0; acity = NULL; aunit = NULL;
  if (unit_can_defend(punit->type)) {

/* This is a defending unit that doesn't need to stay put. 
It needs to defend something, but not necessarily where it's at.
Therefore, it will consider becoming a bodyguard. -- Syela */

    val = look_for_charge(pplayer, punit, &aunit, &acity);

  }
  if (q > val && ai_fuzzy(pplayer, TRUE)) {
    punit->ai.ai_role = AIUNIT_DEFEND_HOME;
    return;
  }
  /* this is bad; riflemen might rather attack if val is low -- Syela */
  if (acity) {
    punit->ai.ai_role = AIUNIT_ESCORT;
    punit->ai.charge = acity->id;
    freelog(LOG_DEBUG, "%s@(%d, %d) going to defend %s@(%d, %d)",
		  unit_type(punit)->name, punit->x, punit->y,
		  acity->name, acity->x, acity->y);
  } else if (aunit) {
    punit->ai.ai_role = AIUNIT_ESCORT;
    punit->ai.charge = aunit->id;
    freelog(LOG_DEBUG, "%s@(%d, %d) going to defend %s@(%d, %d)",
		  unit_type(punit)->name, punit->x, punit->y,
		  unit_type(aunit)->name, aunit->x, aunit->y);
  } else if (unit_attack_desirability(punit->type) != 0 ||
      (pcity && !same_pos(pcity->x, pcity->y, punit->x, punit->y)))
     punit->ai.ai_role = AIUNIT_ATTACK;
  else punit->ai.ai_role = AIUNIT_DEFEND_HOME; /* for default */
}

static void ai_military_gohome(struct player *pplayer,struct unit *punit)
{
  struct city *pcity;
  int dest_x, dest_y;
  if (punit->homecity != 0){
    pcity=find_city_by_id(punit->homecity);
    freelog(LOG_DEBUG, "GOHOME (%d)(%d,%d)C(%d,%d)",
		 punit->id,punit->x,punit->y,pcity->x,pcity->y); 
    if ((punit->x == pcity->x)&&(punit->y == pcity->y)) {
      freelog(LOG_DEBUG, "INHOUSE. GOTO AI_NONE(%d)", punit->id);
      /* aggro defense goes here -- Syela */
      ai_military_findvictim(pplayer, punit, &dest_x, &dest_y);
      punit->ai.ai_role=AIUNIT_NONE;
      handle_unit_move_request(punit, dest_x, dest_y, FALSE, FALSE);
                                       /* might bash someone */
    } else {
      freelog(LOG_DEBUG, "GOHOME(%d,%d)",
		   punit->goto_dest_x, punit->goto_dest_y);
      punit->goto_dest_x=pcity->x;
      punit->goto_dest_y=pcity->y;
      set_unit_activity(punit, ACTIVITY_GOTO);
      do_unit_goto(punit, GOTO_MOVE_ANY, FALSE);
    }
  } else {
    handle_unit_activity_request(punit, ACTIVITY_FORTIFYING);
  }
}

/*************************************************************************
...
**************************************************************************/
int find_something_to_kill(struct player *pplayer, struct unit *punit, int *x, int *y)
{
  int a=0, b, c, d, e, m, n, v, f, b0, ab, g;
#ifdef DEBUG
  int aa = 0, bb = 0, cc = 0, dd = 0, bestb0 = 0;
#endif
  int con = map_get_continent(punit->x, punit->y);
  struct unit *pdef;
  int best = 0, maxd, boatid = 0, needferry;
  int harborcity = 0, bx = 0, by = 0;
  int fprime;
  bool handicap;
  struct unit *ferryboat = 0;
  bool sanity;
  int boatspeed;
  bool unhap = FALSE;
  struct city *pcity;
  int xx, yy; /* for beachheads */
  int bk = 0; /* this is a kluge, because if we don't set x and y with !punit->id,
p_a_w isn't called, and we end up not wanting ironclads and therefore never
learning steam engine, even though ironclads would be very useful. -- Syela */

/* this is horrible, but I need to do something like this somewhere. -- Syela */
  players_iterate(aplayer) {
    if (aplayer == pplayer) continue;
    /* AI will try to conquer only enemy cities. -- Nb */
    city_list_iterate(aplayer->cities, acity)
      city_reinforcements_cost_and_value(acity, punit);
      acity->ai.invasion = 0;
    city_list_iterate_end;
  } players_iterate_end;

  unit_list_iterate(pplayer->units, aunit)
    if (aunit == punit) continue;
    if (aunit->activity == ACTIVITY_GOTO &&
       (pcity = map_get_city(aunit->goto_dest_x, aunit->goto_dest_y))) {
      if (unit_type(aunit)->attack_strength >
        unit_type(aunit)->transport_capacity) { /* attacker */
        a = unit_belligerence_basic(aunit);
        pcity->ai.a += a;
        pcity->ai.f += unit_type(aunit)->build_cost;
      }
    }
/* dealing with invasion stuff */
    if (unit_type(aunit)->attack_strength >
      unit_type(aunit)->transport_capacity) { /* attacker */
      if (aunit->activity == ACTIVITY_GOTO) invasion_funct(aunit, TRUE, 0,
         ((is_ground_unit(aunit) || is_heli_unit(aunit)) ? 1 : 2));
      invasion_funct(aunit, FALSE, unit_type(aunit)->move_rate / SINGLE_MOVE,
         ((is_ground_unit(aunit) || is_heli_unit(aunit)) ? 1 : 2));
    } else if (aunit->ai.passenger != 0 &&
              !same_pos(aunit->x, aunit->y, punit->x, punit->y)) {
      if (aunit->activity == ACTIVITY_GOTO) invasion_funct(aunit, TRUE, 1, 1);
      invasion_funct(aunit, FALSE, 2, 1);
    }
  unit_list_iterate_end;
/* end horrible initialization subroutine */
        
  pcity = map_get_city(punit->x, punit->y);

  if (pcity && (punit->id == 0 || pcity->id == punit->homecity))
    unhap = ai_assess_military_unhappiness(pcity, get_gov_pplayer(pplayer));

  *x = punit->x; *y = punit->y;
  ab = unit_belligerence_basic(punit);
  if (ab == 0) return(0); /* don't want to deal with SIGFPE's -- Syela */
  m = unit_type(punit)->move_rate;
  if (unit_flag(punit, F_IGTER)) m *= SINGLE_MOVE;
  maxd = MIN(6, m) * THRESHOLD + 1;
  f = unit_type(punit)->build_cost;
  fprime = build_cost_balanced(punit->type);

  generate_warmap(map_get_city(*x, *y), punit);
                             /* most flexible but costs milliseconds */
  freelog(LOG_DEBUG, "%s's %s at (%d, %d) has belligerence %d.",
		pplayer->name, unit_type(punit)->name,
		punit->x, punit->y, a);

  if (is_ground_unit(punit)) boatid = find_boat(pplayer, &bx, &by, 2);
  if (boatid != 0) ferryboat = player_find_unit_by_id(pplayer, boatid);
  if (ferryboat) really_generate_warmap(map_get_city(ferryboat->x, ferryboat->y),
                       ferryboat, SEA_MOVING);

  if (ferryboat) boatspeed = (unit_flag(ferryboat, F_TRIREME) ? 6 : 12);
  else boatspeed = (get_invention(pplayer, game.rtech.nav) != TECH_KNOWN ? 6 : 12);

  if (is_ground_unit(punit) && punit->id == 0 &&
      is_terrain_near_tile(punit->x, punit->y, T_OCEAN)) harborcity++;

  handicap=ai_handicap(pplayer, H_TARGETS);
  players_iterate(aplayer) {
    if (aplayer != pplayer) { /* enemy */
      city_list_iterate(aplayer->cities, acity)
        if (handicap && !map_get_known(acity->x, acity->y, pplayer)) continue;
        sanity = (goto_is_sane(punit, acity->x, acity->y, TRUE) &&
                 warmap.cost[acity->x][acity->y] < maxd); /* for Tangier->Malaga */
        if (ai_fuzzy(pplayer, TRUE) && ((is_ground_unit(punit) &&
          ((sanity) || 
          ((ferryboat || harborcity != 0) &&
                      warmap.seacost[acity->x][acity->y] <= 6 * THRESHOLD))) ||
          (is_sailing_unit(punit) && warmap.seacost[acity->x][acity->y] < maxd))) {
          if ((pdef = get_defender(punit, acity->x, acity->y))) {
            d = unit_vulnerability(punit, pdef);
            b = unit_type(pdef)->build_cost + 40;
          } else { d = 0; b = 40; }
/* attempting to make empty cities less enticing. -- Syela */
          if (is_ground_unit(punit)) {
            if (!sanity) {
              c = (warmap.seacost[acity->x][acity->y]) / boatspeed; /* kluge */
              if (boatspeed < 9 && c > 2) c = 999; /* tired of Kaput! -- Syela */
              if (ferryboat) c += (warmap.cost[bx][by] + m - 1) / m + 1;
              else c += 1;
              if (unit_flag(punit, F_MARINES)) c -= 1;
            } else c = (warmap.cost[acity->x][acity->y] + m - 1) / m;
          } else if (is_sailing_unit(punit))
             c = (warmap.seacost[acity->x][acity->y] + m - 1) / m;
          else c = (real_map_distance(punit->x, punit->y, acity->x, acity->y)
		    * SINGLE_MOVE) / m;
          if (c > 1) {
            n = ai_choose_defender_versus(acity, punit->type);
	    v = unit_vulnerability_virtual2(punit->type, n, acity->x,
					    acity->y, FALSE,
					    do_make_unit_veteran(acity, n),
					    FALSE, 0);
            if (v >= d) { d = v; b = unit_types[n].build_cost + 40; }
          } /* let's hope this works! */
          if (!is_ground_unit(punit) && !is_heli_unit(punit) &&
              !TEST_BIT(acity->ai.invasion, 0)) b -= 40; /* boats can't conquer cities */
          if (punit->id == 0
	      && !unit_really_ignores_citywalls(punit)
	      && c > (player_knows_improvement_tech(aplayer, B_CITY) ? 2 : 4)
	      && !city_got_citywalls(acity))
	    d *= 9;

          a = (ab + acity->ai.a) * (ab + acity->ai.a);
/* Avoiding handling upkeep aggregation this way -- Syela */

          g = unit_list_size(&(map_get_tile(acity->x, acity->y)->units)) + 1;
/* AI was not sending enough reinforcements to totally wipe out a city
and conquer it in one turn.  This variable enables total carnage. -- Syela */

          if (!is_ground_unit(punit) && !is_heli_unit(punit) && !pdef) b0 = 0;
/* !d instead of !pdef was horrible bug that led to yoyo-ing AI warships -- Syela */
          else if (c > THRESHOLD) b0 = 0;
          else if ((is_ground_unit(punit) || is_heli_unit(punit)) &&
                   acity->ai.invasion == 2) b0 = f * SHIELD_WEIGHTING;
          else {
            int a_squared = acity->ai.a * acity->ai.a;
            
            b0 = kill_desire(b, a, (f + acity->ai.f), d, g);
            if (b * a_squared > acity->ai.f * d) {
              /* If there're enough units to do the job, we don't need this
               * one. */
              /* FIXME: The problem with ai.f is that bigger it is, less is our
               * desire to go help other units.  Now suppose we need five
               * cavalries to take over a city, we have four (which is not
               * enough), then we will be severely discouraged to build the
               * fifth one.  Where is logic in this??!?! --GB */
              b0 -= kill_desire(b, a_squared, acity->ai.f, d, g);
            }
          }
          b0 -= c * (unhap ? SHIELD_WEIGHTING + 2 * TRADE_WEIGHTING : SHIELD_WEIGHTING);
          /* FIXME: build_cost of ferry */
          needferry = 
            (!sanity && boatid == 0 && is_ground_unit(punit) ? 40 : 0);
          e = military_amortize(b0, MAX(1, c), fprime + needferry);
          /* BEGIN STEAM-ENGINES-ARE-OUR-FRIENDS KLUGE */
          if (b0 <= 0 && punit->id == 0 && best == 0) {
            int bk_e = military_amortize(b * SHIELD_WEIGHTING, 
                                         MAX(1, c), fprime + needferry);
            if (bk_e > bk) {
              if (punit->id != 0 && is_ground_unit(punit) 
                  && !unit_flag(punit, F_MARINES) 
                  && map_get_continent(acity->x, acity->y) != con) {
                if (find_beachhead(punit, acity->x, acity->y, &xx, &yy) != 0) {
                  *x = acity->x;
                  *y = acity->y;
                  bk = bk_e;
                } /* else no beachhead */
              } else {
                *x = acity->x;
                *y = acity->y;
                bk = bk_e;
              }
            }
          }
          /* END STEAM-ENGINES KLUGE */

	  if (punit->id != 0 && ferryboat && is_ground_unit(punit)) {
	    freelog(LOG_DEBUG, "%s@(%d, %d) -> %s@(%d, %d) -> %s@(%d, %d)"
		    " (sanity=%d, c=%d, e=%d, best=%d)",
		    unit_type(punit)->name, punit->x, punit->y,
		    unit_type(ferryboat)->name, bx, by,
		    acity->name, acity->x, acity->y, sanity, c, e, best);
	  }

          if (e > best && ai_fuzzy(pplayer, TRUE)) {
            if (punit->id != 0 && is_ground_unit(punit) &&
                !unit_flag(punit, F_MARINES) &&
                map_get_continent(acity->x, acity->y) != con) {
/* a non-virtual ground unit is trying to attack something on another continent */
/* need a beachhead which is adjacent to the city and an available ocean tile */
              if (find_beachhead(punit, acity->x, acity->y, &xx, &yy) != 0) { /* LaLALala */
#ifdef DEBUG
                aa = a; bb = b; cc = c; dd = d; bestb0 = b0;
#endif
                best = e;
                *x = acity->x;
                *y = acity->y;
/* the ferryboat needs to target the beachhead, but the unit needs to target
the city itself.  This is a little weird, but it's the best we can do. -- Syela */
              } /* else do nothing, since we have no beachhead */
            } else {
#ifdef DEBUG
              aa = a; bb = b; cc = c; dd = d; bestb0 = b0;
#endif
              best = e;
              *x = acity->x;
              *y = acity->y;
            } /* end need-beachhead else */
          }
        }
      city_list_iterate_end;

      a = unit_belligerence(punit);
/* I'm not sure the following code is good but it seems to be adequate. -- Syela */
/* I am deliberately not adding ferryboat code to the unit_list_iterate. -- Syela */
      unit_list_iterate(aplayer->units, aunit)
        if (map_get_city(aunit->x, aunit->y)) continue; /* already dealt with it */
        if (handicap && !map_get_known(aunit->x, aunit->y, pplayer)) continue;
        if ((unit_flag(aunit, F_HELP_WONDER) || unit_flag(aunit, F_TRADE_ROUTE))
             && punit->id == 0) 
          continue; /* kluge */
        if (ai_fuzzy(pplayer, TRUE) &&
	    (aunit == get_defender(punit, aunit->x, aunit->y) &&
           ((is_ground_unit(punit) &&
                map_get_continent(aunit->x, aunit->y) == con &&
                warmap.cost[aunit->x][aunit->y] < maxd) ||
            (is_sailing_unit(punit) &&
                goto_is_sane(punit, aunit->x, aunit->y, TRUE) && /* Thanks, Damon */
                warmap.seacost[aunit->x][aunit->y] < maxd)))) {
          d = unit_vulnerability(punit, aunit);

          b = unit_type(aunit)->build_cost;
          if (is_ground_unit(punit)) n = warmap.cost[aunit->x][aunit->y];
          else if (is_sailing_unit(punit)) n = warmap.seacost[aunit->x][aunit->y];
          else n = real_map_distance(punit->x, punit->y, aunit->x, aunit->y) * 3;
          if (n > m) { /* if n <= m, it can't run away -- Syela */
            n *= unit_type(aunit)->move_rate;
            if (unit_flag(aunit, F_IGTER)) n *= 3;
          }
          c = (n + m - 1) / m;
          if (!is_ground_unit(punit) && d == 0) b0 = 0;
          else if (c > THRESHOLD) b0 = 0;
          else b0 = ((b * a - f * d) * SHIELD_WEIGHTING / (a + d)) - 
            c * (unhap ? SHIELD_WEIGHTING + 2 * TRADE_WEIGHTING : SHIELD_WEIGHTING);
          e = military_amortize(b0, MAX(1, c), fprime);
          if (e > best && ai_fuzzy(pplayer, TRUE)) {
#ifdef DEBUG
            aa = a; bb = b; cc = c; dd = d; bestb0 = b0;
#endif
            best = e;
            *x = aunit->x;
            *y = aunit->y;
          }
        }
      unit_list_iterate_end;
    } /* end if enemy */
  } players_iterate_end;

#ifdef DEBUG
  if (best && map_get_city(*x, *y) && !punit->id) {
    freelog(LOG_DEBUG,
	    "%s's %s#%d at (%d, %d) targeting (%d, %d) [desire = %d/%d]",
	    pplayer->name, unit_type(punit)->name, punit->id,
	    punit->x, punit->y, *x, *y, best, bestb0);
    freelog(LOG_DEBUG, "A = %d, B = %d, C = %d, D = %d, F = %d, E = %d",
	    aa, bb, cc, dd, f, best);
  }
  if (punit->id && (pcity = map_get_city(*x, *y)) &&
      pcity->owner != punit->owner && pcity->ai.invasion == 2 &&
      (is_ground_unit(punit) || is_heli_unit(punit))) {
    freelog(LOG_DEBUG, "%s's %s#%d@(%d,%d) invading %s@(%d,%d)",
	    pplayer->name, unit_name(punit->type), punit->id,
	    punit->x, punit->y, pcity->name, *x, *y);
  }
  if (!best && bk) {
    freelog(LOG_DEBUG, "%s's %s@(%d,%d) faking target %s@(%d,%d)",
	    pplayer->name, unit_name(punit->type), punit->x, punit->y, 
	    map_get_city(*x, *y)->name, *x, *y);
  }
#endif
  return(best);
}

static bool find_nearest_friendly_port(struct unit *punit)
{
  struct player *pplayer = unit_owner(punit);
  int best = 6 * THRESHOLD + 1, cur;
  generate_warmap(map_get_city(punit->x, punit->y), punit);
  city_list_iterate(pplayer->cities, pcity)
    cur = warmap.seacost[pcity->x][pcity->y];
    if (city_got_building(pcity, B_PORT)) cur /= 3;
    if (cur < best) {
      punit->goto_dest_x = pcity->x;
      punit->goto_dest_y = pcity->y;
      best = cur;
    }
  city_list_iterate_end;
  if (best > 6 * THRESHOLD) return FALSE;
  freelog(LOG_DEBUG, "Friendly port nearest to (%d,%d) is %s@(%d,%d) [%d]",
		punit->x, punit->y,
		map_get_city(punit->goto_dest_x, punit->goto_dest_y)->name,
		punit->goto_dest_x, punit->goto_dest_y, best);
  return TRUE;
}

/*************************************************************************
This seems to do the attack. First find victim on the neighbouring tiles.
If no enemies nearby find_something_to_kill() anywhere else. If there is
nothing to kill, sailing units go home, others explore.
**************************************************************************/
static void ai_military_attack(struct player *pplayer,struct unit *punit)
{
  int dest_x, dest_y; 
  int id;
  bool flag;
  int went, ct = 10;

  if (punit->activity!=ACTIVITY_GOTO) {
    id = punit->id;
    do {
      flag = FALSE;
      ai_military_findvictim(pplayer, punit, &dest_x, &dest_y);  
      if (dest_x == punit->x && dest_y == punit->y) {
/* no one to bash here.  Will try to move onward */
        find_something_to_kill(pplayer, punit, &dest_x, &dest_y);
        if (same_pos(punit->x, punit->y, dest_x, dest_y)) {
/* nothing to kill.  Adjacency is something for us to kill later. */
          if (is_sailing_unit(punit)) {
            if (find_nearest_friendly_port(punit))
	      do_unit_goto(punit, GOTO_MOVE_ANY, FALSE);
          } else {
            ai_manage_explorer(punit); /* nothing else to do */
            /* you can still have some moves left here, but barbarians should
               not sit helplessly, but advance towards nearest known enemy city */
	    punit = find_unit_by_id(id);   /* unit might die while exploring */
            if( punit && punit->moves_left > 0 && is_barbarian(pplayer) ) {
              struct city *pc;
              int fx, fy;
              freelog(LOG_DEBUG,"Barbarians looking for target");
              if( (pc = dist_nearest_city(pplayer, punit->x, punit->y, FALSE, TRUE)) ) {
                if( map_get_terrain(punit->x,punit->y) != T_OCEAN ) {
                  freelog(LOG_DEBUG,"Marching to city");
                  ai_military_gothere(pplayer, punit, pc->x, pc->y);
                }
                else {
                  /* sometimes find_beachhead is not enough */
		  if (find_beachhead(punit, pc->x, pc->y, &fx, &fy) == 0)
                    find_city_beach(pc, punit, &fx, &fy);           
                  freelog(LOG_DEBUG,"Sailing to city");
                  ai_military_gothere(pplayer, punit, fx, fy);
                }
              }
            }
          }
          return; /* Jane, stop this crazy thing! */
        } else if (!is_tiles_adjacent(punit->x, punit->y, dest_x, dest_y)) {
/* if what we want to kill is adjacent, and findvictim didn't want it, WAIT! */
          if ((went = ai_military_gothere(pplayer, punit, dest_x, dest_y)) != 0) {
	    if (went > 0) {
	      flag = punit->moves_left > 0;
	    } else {
	      punit = NULL;
	    }
          } /* else we're having ZOC hell and need to break out of the loop */
        } /* else nothing to kill */
      } else { /* goto does NOT work for fast units */
	freelog(LOG_DEBUG, "%s's %s at (%d, %d) bashing (%d, %d)",
		      pplayer->name, unit_type(punit)->name,
		      punit->x, punit->y, dest_x, dest_y); 
        handle_unit_move_request(punit, dest_x, dest_y, FALSE, FALSE);
        punit = find_unit_by_id(id);
        if (punit) flag = punit->moves_left > 0; else flag = FALSE;
      }
      if (punit)
         if (stay_and_defend_city(punit)) return;
      ct--; /* infinite loops from railroads must be stopped */
    } while (flag && ct > 0); /* want units to attack multiple times */
  } /* end if */
}

/*************************************************************************
...
**************************************************************************/
static void ai_manage_caravan(struct player *pplayer, struct unit *punit)
{
  struct city *pcity;
  struct packet_unit_request req;
  int tradeval, best_city = -1, best=0;

  if (punit->activity != ACTIVITY_IDLE)
    return;
  if (punit->ai.ai_role == AIUNIT_NONE) {
    if ((pcity = wonder_on_continent(pplayer, 
                                     map_get_continent(punit->x, punit->y))) 
        && unit_flag(punit, F_HELP_WONDER)
        && build_points_left(pcity) > (pcity->shield_surplus * 2)) {
      if (!same_pos(pcity->x, pcity->y, punit->x, punit->y)) {
        if (punit->moves_left == 0) 
          return;
        auto_settler_do_goto(pplayer, punit, pcity->x, pcity->y);
        handle_unit_activity_request(punit, ACTIVITY_IDLE);
      } else {
        /*
         * We really don't want to just drop all caravans in immediately.
         * Instead, we want to aggregate enough caravans to build instantly.
         * -AJS, 990704
         */
        req.unit_id = punit->id;
        req.city_id = pcity->id;
        handle_unit_help_build_wonder(pplayer, &req);
      }
    } else {
       /* A caravan without a home?  Kinda strange, but it might happen.  */
       pcity=player_find_city_by_id(pplayer, punit->homecity);
       city_list_iterate(pplayer->cities,pdest) {
         if (pcity && can_establish_trade_route(pcity, pdest)
             && map_get_continent(pcity->x, pcity->y) 
                                == map_get_continent(pdest->x, pdest->y)) {
           tradeval=trade_between_cities(pcity, pdest);
           if (tradeval != 0) {
             if (best < tradeval) {
               best=tradeval;
               best_city=pdest->id;
             }
           }
         }
       } city_list_iterate_end;

       pcity = player_find_city_by_id(pplayer, best_city);

       if (pcity) {
         if (!same_pos(pcity->x, pcity->y, punit->x, punit->y)) {
           if (punit->moves_left == 0) return;
           auto_settler_do_goto(pplayer, punit, pcity->x, pcity->y);
         } else {
           req.unit_id = punit->id;
           req.city_id = pcity->id;
           handle_unit_establish_trade(pplayer, &req);
        }
      }
    }
  }
}

/**************************************************************************
This seems to manage the ferryboat. When it carries units on their way
to invade something, it goes there. If it carries other units, it returns home.
When empty, it tries to find some units to carry or goes home or explores.
Military units handled by ai_manage_military()
**************************************************************************/

static void ai_manage_ferryboat(struct player *pplayer, struct unit *punit)
{ /* It's about 12 feet square and has a capacity of almost 1000 pounds.
     It is well constructed of teak, and looks seaworthy. */
  struct city *pcity;
  struct unit *bodyguard;
  int id = punit->id;
  int best = 4 * unit_type(punit)->move_rate, x = punit->x, y = punit->y;
  int n = 0, p = 0;

  if (!unit_list_find(&map_get_tile(punit->x, punit->y)->units, punit->ai.passenger))
    punit->ai.passenger = 0;

  unit_list_iterate(map_get_tile(punit->x, punit->y)->units, aunit)
    if (aunit->ai.ferryboat == punit->id) {
      if (punit->ai.passenger == 0) punit->ai.passenger = aunit->id; /* oops */
      p++;
      bodyguard = unit_list_find(&map_get_tile(punit->x, punit->y)->units, aunit->ai.bodyguard);
      pcity = map_get_city(aunit->goto_dest_x, aunit->goto_dest_y);
      if (aunit->ai.bodyguard == 0 || bodyguard ||
         (pcity && pcity->ai.invasion >= 2)) {
	if (pcity) {
	  freelog(LOG_DEBUG, "Ferrying to %s to %s, invasion = %d, body = %d",
		  unit_name(aunit->type), pcity->name,
		  pcity->ai.invasion, aunit->ai.bodyguard);
	}
        n++;
        set_unit_activity(aunit, ACTIVITY_SENTRY);
        if (bodyguard) set_unit_activity(bodyguard, ACTIVITY_SENTRY);
      }
    }
  unit_list_iterate_end;
  if (p != 0) {
    freelog(LOG_DEBUG, "%s#%d@(%d,%d), p=%d, n=%d",
		  unit_name(punit->type), punit->id, punit->x, punit->y, p, n);
    if (punit->moves_left > 0 && n != 0)
      do_unit_goto(punit, GOTO_MOVE_ANY, FALSE);
    else if (n == 0 && !map_get_city(punit->x, punit->y)) { /* rest in a city, for unhap */
      x = punit->goto_dest_x; y = punit->goto_dest_y;
      if (find_nearest_friendly_port(punit))
	do_unit_goto(punit, GOTO_MOVE_ANY, FALSE);
      if (!player_find_unit_by_id(pplayer, id)) return; /* oops! */
      punit->goto_dest_x = x; punit->goto_dest_y = y;
      send_unit_info(pplayer, punit); /* to get the crosshairs right -- Syela */
    } else {
      freelog(LOG_DEBUG, "Ferryboat %d@(%d,%d) stalling.",
		    punit->id, punit->x, punit->y);
      if(is_barbarian(pplayer)) /* just in case */
        ai_manage_explorer(punit);
    }
    return;
  }

  /* check if barbarian boat is empty and so not needed - the crew has landed */
  if( is_barbarian(pplayer) && unit_list_size(&map_get_tile(punit->x, punit->y)->units)<2 ) {
    wipe_unit(punit);
    return;
  }

/* ok, not carrying anyone, even the ferryman */
  punit->ai.passenger = 0;
  freelog(LOG_DEBUG, "Ferryboat %d@(%d, %d) is lonely.",
		punit->id, punit->x, punit->y);
  set_unit_activity(punit, ACTIVITY_IDLE);
  punit->goto_dest_x = punit->x;
  punit->goto_dest_y = punit->y;

  if (unit_type(punit)->attack_strength >
      unit_type(punit)->transport_capacity) {
    if (punit->moves_left > 0) ai_manage_military(pplayer, punit);
    return;
  } /* AI used to build frigates to attack and then use them as ferries -- Syela */

  generate_warmap(map_get_city(punit->x, punit->y), punit);
  p = 0; /* yes, I know it's already zero. -- Syela */
  unit_list_iterate(pplayer->units, aunit)
/*    if (aunit->ai.ferryboat == punit->id && warmap.seacost[aunit->x][aunit->y] < best) {*/
    if (aunit->ai.ferryboat != 0 && warmap.seacost[aunit->x][aunit->y] < best &&
	ground_unit_transporter_capacity(aunit->x, aunit->y, pplayer) <= 0
	&& ai_fuzzy(pplayer, TRUE)) {
      freelog(LOG_DEBUG, "Found a friend %d@(%d, %d)",
		    aunit->id, aunit->x, aunit->y);
      x = aunit->x;
      y = aunit->y;
      best = warmap.seacost[x][y];
    }
    if (is_sailing_unit(aunit) && map_get_terrain(aunit->x, aunit->y) == T_OCEAN) p++;
  unit_list_iterate_end;
  if (best < 4 * unit_type(punit)->move_rate) {
    punit->goto_dest_x = x;
    punit->goto_dest_y = y;
    set_unit_activity(punit, ACTIVITY_GOTO);
    do_unit_goto(punit, GOTO_MOVE_ANY, FALSE);
    if ((punit = player_find_unit_by_id(pplayer, id)))
      set_unit_activity(punit, ACTIVITY_IDLE);
    return;
  }

/* do cool stuff here */

  if (punit->moves_left == 0) return;  
  pcity = find_city_by_id(punit->homecity);
  if (pcity) {
    if (!ai_handicap(pplayer, H_TARGETS) ||
        unit_move_turns(punit, pcity->x, pcity->y) < p) {
      freelog(LOG_DEBUG, "No friends.  Going home.");
      punit->goto_dest_x = pcity->x;
      punit->goto_dest_y = pcity->y;
      set_unit_activity(punit, ACTIVITY_GOTO);
      do_unit_goto(punit, GOTO_MOVE_ANY, FALSE);
      return;
    }
  }
  if (map_get_terrain(punit->x, punit->y) == T_OCEAN) /* thanks, Tony */
    ai_manage_explorer(punit);
  return;
}

/**************************************************************************
decides what to do with a military unit.
**************************************************************************/
static void ai_manage_military(struct player *pplayer, struct unit *punit)
{
  int id;

  id = punit->id;

  if (punit->activity != ACTIVITY_IDLE)
    handle_unit_activity_request(punit, ACTIVITY_IDLE);

  punit->ai.ai_role = AIUNIT_NONE;
  /* was getting a bad bug where a settlers caused a defender to leave home */
  /* and then all other supported units went on DEFEND_HOME/goto */
  ai_military_findjob(pplayer, punit);

#ifdef DEBUG
  {
    struct city *pcity;
    if (unit_flag(punit, F_FIELDUNIT)
	&& (pcity = map_get_city(punit->x, punit->y))) {
      freelog(LOG_DEBUG, "%s in %s is going to %d", unit_name(punit->type),
	      pcity->name, punit->ai.ai_role);
    }
  }
#endif
  
  switch (punit->ai.ai_role) {
  case AIUNIT_AUTO_SETTLER:
    punit->ai.ai_role = AIUNIT_NONE; 
    break;
  case AIUNIT_BUILD_CITY:
    punit->ai.ai_role = AIUNIT_NONE; 
    break;
  case AIUNIT_DEFEND_HOME:
    ai_military_gohome(pplayer, punit);
    break;
  case AIUNIT_ATTACK:
    ai_military_attack(pplayer, punit);
    break;
  case AIUNIT_FORTIFY:
    ai_military_gohome(pplayer, punit);
    break;
  case AIUNIT_RUNAWAY: 
    break;
  case AIUNIT_ESCORT: 
    ai_military_bodyguard(pplayer, punit);
    break;
  case AIUNIT_PILLAGE:
    handle_unit_activity_request(punit, ACTIVITY_PILLAGE);
    return; /* when you pillage, you have moves left, avoid later fortify */
  case AIUNIT_EXPLORE:
    ai_manage_explorer(punit);
    break;
  default:
    abort();
  }

  if ((punit = find_unit_by_id(id))) {
    if (punit->activity != ACTIVITY_IDLE &&
        punit->activity != ACTIVITY_GOTO)
      handle_unit_activity_request(punit, ACTIVITY_IDLE); 

    if (punit->moves_left > 0) {
      if (unit_list_find(&(map_get_tile(punit->x, punit->y)->units),
          punit->ai.ferryboat))
        handle_unit_activity_request(punit, ACTIVITY_SENTRY);
      else 
        handle_unit_activity_request(punit, ACTIVITY_FORTIFYING);
    } /* better than doing nothing */
  }
}

/**************************************************************************
  Barbarian units may disband spontaneously if their age is more then 5,
  they are not in cities, and they are far from any enemy units. It is to 
  remove barbarians that do not engage into any activity for a long time.
**************************************************************************/
static bool unit_can_be_retired(struct unit *punit)
{
  if (punit->fuel > 0) {	/* fuel abused for barbarian life span */
    punit->fuel--;
    return FALSE;
  }

  if (is_allied_city_tile
      (map_get_tile(punit->x, punit->y), unit_owner(punit))) return FALSE;

  /* check if there is enemy nearby */
  square_iterate(punit->x, punit->y, 3, x, y) {
    if (is_enemy_city_tile(map_get_tile(x, y), unit_owner(punit)) ||
	is_enemy_unit_tile(map_get_tile(x, y), unit_owner(punit)))
      return FALSE;
  }
  square_iterate_end;

  return TRUE;
}

/**************************************************************************
 manage one unit
 Careful: punit may have been destroyed upon return from this routine!

 Gregor: This function is a very limited approach because if a unit has
 several flags the first one in order of appearance in this function
 will be used.
**************************************************************************/
static void ai_manage_unit(struct player *pplayer, struct unit *punit)
{
  /* retire useless barbarian units here, before calling the management
     function */
  if( is_barbarian(pplayer) ) {
    /* Todo: should be configurable */
    if( unit_can_be_retired(punit) && myrand(100) > 90 ) {
      wipe_unit(punit);
      return;
    }
    if( !is_military_unit(punit)
	&& !unit_has_role(punit->type, L_BARBARIAN_LEADER)) {
      freelog(LOG_VERBOSE, "Barbarians picked up non-military unit.");
      return;
    }
  }

  if ((unit_flag(punit, F_DIPLOMAT))
      || (unit_flag(punit, F_SPY))) {
    ai_manage_diplomat(pplayer, punit);
    /* the right test if the unit is in a city and 
       there is no other diplomat it musn't move.
       This unit is a bodyguard against enemy diplomats.
       Right now I don't know how to use bodyguards! (17/12/98) (--NB)
    */
    return;
  } else if (unit_flag(punit, F_SETTLERS)
	     ||unit_flag(punit, F_CITIES)) {
    if (punit->moves_left == 0) return; /* can't do anything with no moves */
    ai_manage_settler(pplayer, punit);
    return;
  } else if (unit_flag(punit, F_TRADE_ROUTE)
             || unit_flag(punit, F_HELP_WONDER)) {
    ai_manage_caravan(pplayer, punit);
    return;
  } else if (unit_has_role(punit->type, L_BARBARIAN_LEADER)) {
    ai_manage_barbarian_leader(pplayer, punit);
    return;
  } else if (get_transporter_capacity(punit) > 0) {
    ai_manage_ferryboat(pplayer, punit);
    return;
  } else if (is_military_unit(punit)) {
    if (punit->moves_left == 0) return; /* can't do anything with no moves */
    ai_manage_military(pplayer,punit); 
    return;
  } else {
    if (punit->moves_left == 0) return; /* can't do anything with no moves */
    ai_manage_explorer(punit); /* what else could this be? -- Syela */
    return;
  }
  /* should never get here */
  assert(0);
}

/**************************************************************************
 do all the gritty nitty chess like analysis here... (argh)
**************************************************************************/
void ai_manage_units(struct player *pplayer) 
{
  static struct timer *t = NULL;      /* alloc once, never free */
  static int *unitids = NULL;         /* realloc'd, never freed */
  static int max_units = 0;

  int count, index;
  struct unit *punit;
  char *type_name;

  t = renew_timer_start(t, TIMER_CPU, TIMER_DEBUG);

  freelog(LOG_DEBUG, "Managing units for %s", pplayer->name);

  count = genlist_size(&(pplayer->units.list));
  if (count > 0) {
    if (max_units < count) {
      max_units = count;
      unitids = fc_realloc(unitids, max_units * sizeof(int));
    }
    index = 0;
    unit_list_iterate(pplayer->units, punit) {
      unitids[index++] = punit->id;
    }
    unit_list_iterate_end;

    for (index = 0; index < count; index++) {
      punit = player_find_unit_by_id(pplayer, unitids[index]);
      if (!punit) {
	freelog(LOG_DEBUG, "Can't manage %s's dead unit %d", pplayer->name,
		unitids[index]);
	continue;
      }
      type_name = unit_type(punit)->name;

      freelog(LOG_DEBUG, "Managing %s's %s %d@(%d,%d)", pplayer->name,
	      type_name, unitids[index], punit->x, punit->y);

      ai_manage_unit(pplayer, punit);
      /* Note punit might be gone!! */

      freelog(LOG_DEBUG, "Finished managing %s's %s %d", pplayer->name,
	      type_name, unitids[index]);
    }
  }

  freelog(LOG_DEBUG, "Managed %s's units successfully.", pplayer->name);

  if (timer_in_use(t)) {
    freelog(LOG_VERBOSE, "%s's units consumed %g milliseconds.",
	    pplayer->name, 1000.0*read_timer_seconds(t));
  }
}

/**************************************************************************
 Assign tech wants for techs to get better units with given role/flag.
 Returns the best we can build so far, or U_LAST if none.  (dwp)
**************************************************************************/
Unit_Type_id ai_wants_role_unit(struct player *pplayer, struct city *pcity,
                                int role, int want)
{
  Unit_Type_id iunit;
  Tech_Type_id itech;
  int i, n;

  n = num_role_units(role);
  for (i=n-1; i>=0; i--) {
    iunit = get_role_unit(role, i);
    if (can_build_unit(pcity, iunit)) {
      return iunit;
    } else {
      /* careful; might be unable to build for non-tech reason... */
      itech = get_unit_type(iunit)->tech_requirement;
      if (get_invention(pplayer, itech) != TECH_KNOWN) {
	pplayer->ai.tech_want[itech] += want;
      }
    }
  }
  return U_LAST;
}

/**************************************************************************
 As ai_wants_role_unit, but also set choice->choice if we can build something.
**************************************************************************/
void ai_choose_role_unit(struct player *pplayer, struct city *pcity,
			 struct ai_choice *choice, int role, int want)
{
  Unit_Type_id iunit = ai_wants_role_unit(pplayer, pcity, role, want);
  if (iunit != U_LAST)
    choice->choice = iunit;
}

/**************************************************************************
 Whether unit_type test is on the "upgrade path" of unit_type base,
 even if we can't upgrade now.
**************************************************************************/
bool is_on_unit_upgrade_path(Unit_Type_id test, Unit_Type_id base)
{
#if 0
  /* This is a hack for regression testing; I believe the new version
   * is actually better (at least as it is used in aicity.c)  --dwp
   */
  return (base==U_WARRIORS && test==U_PHALANX)
    || (base==U_PHALANX && test==U_MUSKETEERS);
#else
  /* This is the real function: */
  do {
    base = unit_types[base].obsoleted_by;
    if (base == test) {
      return TRUE;
    }
  } while (base != -1);
  return FALSE;
#endif
}

/*
 * If we are the only diplomat in a city, defend against enemy actions.
 * The passive defense is set by game.diplchance.  The active defense is
 * to bribe units which end their move nearby.
 * Our next trick is to look for enemy units and cities on our continent.
 * If we find a city, we look first to establish an embassy.  The
 * information gained this way is assumed to be more useful than anything
 * else we could do.  Making this come true is for future code.  -AJS
 */
static void ai_manage_diplomat(struct player *pplayer, struct unit *pdiplomat)
{
  bool handicap, has_emb;
  int continent, dist, rmd, oic, did;
  struct packet_unit_request req;
  struct packet_diplomat_action dact;
  struct city *pcity, *ctarget;
  struct tile *ptile;
  struct unit *ptres;

  if (pdiplomat->activity != ACTIVITY_IDLE)
    handle_unit_activity_request(pdiplomat, ACTIVITY_IDLE);

  pcity = map_get_city(pdiplomat->x, pdiplomat->y);

  if (pcity && count_diplomats_on_tile(pdiplomat->x, pdiplomat->y) == 1) {
    /* We're the only diplomat in a city.  Defend it. */
    if (pdiplomat->homecity != pcity->id) {
      /* this may be superfluous, but I like it.  -AJS */
      req.unit_id=pdiplomat->id;
      req.city_id=pcity->id;
      req.name[0]='\0';
      handle_unit_change_homecity(pplayer, &req);
    }
  } else {
    if (pcity) {
      /*
       * More than one diplomat in a city: may try to bribe trespassers.
       * We may want this to be a city_map_iterate, or a warmap distance
       * check, but this will suffice for now.  -AJS
       */
      adjc_iterate(pcity->x, pcity->y, x, y) {
	if (diplomat_can_do_action(pdiplomat, DIPLOMAT_BRIBE, x, y)) {
	  /* A lone trespasser! Seize him! -AJS */
	  ptile = map_get_tile(x, y);
	  ptres = unit_list_get(&ptile->units, 0);
	  ptres->bribe_cost = unit_bribe_cost(ptres);
	  if (ptres->bribe_cost <
	      (pplayer->economic.gold - pplayer->ai.est_upkeep)) {
	    dact.diplomat_id = pdiplomat->id;
	    dact.target_id = ptres->id;
	    dact.action_type = DIPLOMAT_BRIBE;
	    handle_diplomat_action(pplayer, &dact);
	    return;
	  }
	}
      }
      adjc_iterate_end;
    }
    /*
     *  We're wandering in the desert, or there is more than one diplomat
     *  here.  Go elsewhere.
     *  First, we look for an embassy, to steal a tech, or for a city to
     *  subvert.  Then we look for a city of our own without a defending
     *  diplomat.  This may not prove to work so very well, but it's
     *  possible otherwise that all diplomats we ever produce are home
     *  guard, and that's kind of silly.  -AJS, 20000130
     */
    ctarget = NULL;
    dist=MAX(map.xsize, map.ysize);
    continent=map_get_continent(pdiplomat->x, pdiplomat->y);
    handicap = ai_handicap(pplayer, H_TARGETS);
    players_iterate(aplayer) {
      if (aplayer == pplayer) continue;
      /* sneaky way of avoiding foul diplomat capture  -AJS */
      has_emb=player_has_embassy(pplayer, aplayer) || pdiplomat->foul;
      city_list_iterate(aplayer->cities, acity)
	if (handicap && !map_get_known(acity->x, acity->y, pplayer)) continue;
	if (continent != map_get_continent(acity->x, acity->y)) continue;
	city_incite_cost(acity);
	/* figure our incite cost */
	oic = acity->incite_revolt_cost;
	if (pplayer->player_no == acity->original) oic = oic / 2;
	rmd=real_map_distance(pdiplomat->x, pdiplomat->y, acity->x, acity->y);
	if (!ctarget || (dist > rmd)) {
	  if (!has_emb || acity->steal == 0 || (oic < 
               pplayer->economic.gold - pplayer->ai.est_upkeep)) {
	    /* We have the closest enemy city so far on the same continent */
	    ctarget = acity;
	    dist = rmd;
	  }
	}
      city_list_iterate_end;
    } players_iterate_end;

    if (!ctarget) {
      /* No enemy cities are useful.  Check our own. -AJS */
      city_list_iterate(pplayer->cities, acy)
	if (continent != map_get_continent(acy->x, acy->y)) continue;
	if (count_diplomats_on_tile(acy->x, acy->y) == 0) {
	  ctarget=acy;
	  dist=real_map_distance(pdiplomat->x, pdiplomat->y, acy->x, acy->y);
	  /* keep dist's integrity, and we can use it later. -AJS */
	}
      city_list_iterate_end;
    }
    if (ctarget) {
      /* Otherwise, we just kinda sit here.  -AJS */
      did = -1;
      if ((dist == 1) && (pplayer->player_no != ctarget->owner)) {
	dact.diplomat_id=pdiplomat->id;
	dact.target_id=ctarget->id;
	if (!pdiplomat->foul && diplomat_can_do_action(pdiplomat,
	     DIPLOMAT_EMBASSY, ctarget->x, ctarget->y)) {
	    did=pdiplomat->id;
	    dact.action_type=DIPLOMAT_EMBASSY;
	    handle_diplomat_action(pplayer, &dact);
	} else if (ctarget->steal == 0 && diplomat_can_do_action(pdiplomat,
		    DIPLOMAT_STEAL, ctarget->x, ctarget->y)) {
	    did=pdiplomat->id;
	    dact.action_type=DIPLOMAT_STEAL;
	    handle_diplomat_action(pplayer, &dact);
	} else if (diplomat_can_do_action(pdiplomat, DIPLOMAT_INCITE,
		   ctarget->x, ctarget->y)) {
	    did=pdiplomat->id;
	    dact.action_type=DIPLOMAT_INCITE;
	    handle_diplomat_action(pplayer, &dact);
	}
      }
      if ((did < 0) || find_unit_by_id(did)) {
	pdiplomat->goto_dest_x=ctarget->x;
	pdiplomat->goto_dest_y=ctarget->y;
	set_unit_activity(pdiplomat, ACTIVITY_GOTO);
	do_unit_goto(pdiplomat, GOTO_MOVE_ANY, FALSE);
      }
    }
  }
  return;
}

/*************************************************************************
Barbarian leader tries to stack with other barbarian units, and if it's
not possible it runs away. When on coast, it may disappear with 33% chance.
**************************************************************************/
static void ai_manage_barbarian_leader(struct player *pplayer, struct unit *leader)
{
  int con = map_get_continent(leader->x, leader->y);
  int safest = 0, safest_x = leader->x, safest_y = leader->y;
  struct unit *closest_unit = NULL;
  int dist, mindist = 10000;

  if (leader->moves_left == 0 || 
      (map_get_terrain(leader->x, leader->y) != T_OCEAN &&
       unit_list_size(&(map_get_tile(leader->x, leader->y)->units)) > 1) ) {
      handle_unit_activity_request(leader, ACTIVITY_SENTRY);
      return;
  }
  /* the following takes much CPU time and could be avoided */
  generate_warmap(map_get_city(leader->x, leader->y), leader);

  /* duck under own units */
  unit_list_iterate(pplayer->units, aunit) {
    if (unit_has_role(aunit->type, L_BARBARIAN_LEADER)
	|| !is_ground_unit(aunit)
	|| map_get_continent(aunit->x, aunit->y) != con)
      continue;

    if (warmap.cost[aunit->x][aunit->y] < mindist) {
      mindist = warmap.cost[aunit->x][aunit->y];
      closest_unit = aunit;
    }
  } unit_list_iterate_end;

  if (closest_unit
      && !same_pos(closest_unit->x, closest_unit->y, leader->x, leader->y)
      && (map_get_continent(leader->x, leader->y)
          == map_get_continent(closest_unit->x, closest_unit->y))) {
    auto_settler_do_goto(pplayer, leader, closest_unit->x, closest_unit->y);
    handle_unit_activity_request(leader, ACTIVITY_IDLE);
    return; /* sticks better to own units with this -- jk */
  }

  freelog(LOG_DEBUG, "Barbarian leader needs to flee");
  mindist = 1000000;
  closest_unit = NULL;

  players_iterate(other_player) {
    unit_list_iterate(other_player->units, aunit) {
      if (is_military_unit(aunit)
	  && is_ground_unit(aunit)
	  && map_get_continent(aunit->x, aunit->y) == con) {
	/* questionable assumption: aunit needs as many moves to reach us as we
	   need to reach it */
	dist = warmap.cost[aunit->x][aunit->y] - unit_move_rate(aunit);
	if (dist < mindist) {
	  freelog(LOG_DEBUG, "Barbarian leader: closest enemy is %s at %d, %d, dist %d",
                  unit_name(aunit->type), aunit->x, aunit->y, dist);
	  mindist = dist;
	  closest_unit = aunit;
	}
      }
    } unit_list_iterate_end;
  } players_iterate_end;

  /* Disappearance - 33% chance on coast, when older than barbarian life span */
  if (is_at_coast(leader->x, leader->y) && leader->fuel == 0) {
    if(myrand(3) == 0) {
      freelog(LOG_DEBUG, "Barbarian leader disappeared at %d %d", leader->x, leader->y);
      wipe_unit(leader);
      return;
    }
  }

  if (!closest_unit) {
    handle_unit_activity_request(leader, ACTIVITY_IDLE);
    freelog(LOG_DEBUG, "Barbarian leader: No enemy.");
    return;
  }

  generate_warmap(map_get_city(closest_unit->x, closest_unit->y), closest_unit);

  do {
    int last_x, last_y;
    freelog(LOG_DEBUG, "Barbarian leader: moves left: %d\n",
	    leader->moves_left);

    square_iterate(leader->x, leader->y, 1, x, y) {
      if (warmap.cost[x][y] > safest
	  && can_unit_move_to_tile(leader, x, y, FALSE)) {
	safest = warmap.cost[x][y];
	freelog(LOG_DEBUG,
		"Barbarian leader: safest is %d, %d, safeness %d", x, y,
		safest);
	safest_x = x;
	safest_y = y;
      }
    } 
    square_iterate_end;

    if (same_pos(leader->x, leader->y, safest_x, safest_y)) {
      freelog(LOG_DEBUG, "Barbarian leader reached the safest position.");
      handle_unit_activity_request(leader, ACTIVITY_IDLE);
      return;
    }

    freelog(LOG_DEBUG, "Fleeing to %d, %d.", safest_x, safest_y);
    last_x = leader->x;
    last_y = leader->y;
    auto_settler_do_goto(pplayer, leader, safest_x, safest_y);
    if (leader->x == last_x && leader->y == last_y) {
      /* Deep inside the goto handling code, in 
	 server/unithand.c::handle_unite_move_request(), the server
	 may decide that a unit is better off not moving this turn,
	 because the unit doesn't have quite enough movement points
	 remaining.  Unfortunately for us, this favor that the server
	 code does may lead to an endless loop here in the barbarian
	 leader code:  the BL will try to flee to a new location, execute 
	 the goto, find that it's still at its present (unsafe) location,
	 and repeat.  To break this loop, we test for the condition
	 where the goto doesn't do anything, and break if this is
	 the case. */
      break;
    }
  } while (leader->moves_left > 0);
}

/*************************************************************************
  Updates the global array simple_ai_types.
**************************************************************************/
void update_simple_ai_types(void)
{
  int i = 0;

  unit_type_iterate(id) {
    if (unit_type_exists(id) && !unit_type_flag(id, F_NONMIL)
	&& !unit_type_flag(id, F_MISSILE)
	&& !unit_type_flag(id, F_NO_LAND_ATTACK)
	&& get_unit_type(id)->transport_capacity < 8) {
      simple_ai_types[i] = id;
      i++;
    }
  } unit_type_iterate_end;

  simple_ai_types[i] = U_LAST;
}
