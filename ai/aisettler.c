/********************************************************************** 
 Freeciv - Copyright (C) 2004 - The Freeciv Team
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
#include <string.h>

/* utility */
#include "mem.h"
#include "log.h"
#include "support.h" 
#include "timing.h"

/* common */
#include "city.h"
#include "game.h"
#include "government.h"
#include "map.h"
#include "movement.h"
#include "packets.h"
#include "player.h"

/* aicore */
#include "pf_tools.h"

/* server */
#include "citytools.h"
#include "maphand.h"
#include "srv_log.h"
#include "unithand.h"
#include "unittools.h"

/* server/advisors */
#include "advdata.h"
#include "advtools.h"
#include "autosettlers.h"
#include "infracache.h"

/* ai */
#include "aicity.h"
#include "aiferry.h"
#include "aiplayer.h"
#include "aitools.h"
#include "aiunit.h"
#include "citymap.h"

#include "aisettler.h"


/* COMMENTS */
/* 
   This code tries hard to do the right thing, including looking
   into the future (wrt to government), and also doing this in a
   modpack friendly manner. However, there are some pieces missing.

   A tighter integration into the city management code would 
   give more optimal city placements, since existing cities could
   move their workers around to give a new city better placement.
   Occasionally you will see cities being placed sub-optimally
   because the best city center tile is taken when another tile
   could have been worked instead by the city that took it.

   The code is not generic enough. It assumes smallpox too much,
   and should calculate with a future city of a bigger size.

   We need to stop the build code from running this code all the
   time and instead try to complete what it has started.
*/

/* A big WAG to save a big amount of CPU: */
#define RESULT_IS_ENOUGH 250

#define FERRY_TECH_WANT 500

#define GROWTH_PRIORITY 15

/* Perfection gives us an idea of how long to search for optimal
 * solutions, instead of going for quick and dirty solutions that
 * waste valuable time. Decrease for maps where good city placements
 * are hard to find. Lower means more perfection. */
#define PERFECTION 3

/* How much to deemphasise the potential for city growth for city
 * placements. Decrease this value if large cities are important
 * or planned. Increase if running strict smallpox. */
#define GROWTH_POTENTIAL_DEEMPHASIS 8

/* Percentage bonus to city locations near an ocean. */
#define NAVAL_EMPHASIS 20

/* Modifier for defense bonus that is applied to city location want. 
 * This is % of defense % to increase want by. */
#define DEFENSE_EMPHASIS 20

struct tile_data_cache {
  char food;    /* food output of the tile */
  char trade;   /* trade output of the tile */
  char shield;  /* shield output of the tile */

  int sum;      /* weighted sum of the tile output (used by AI) */
};

struct cityresult {
  struct tile *tile;
  int total;              /* total value of position */
  int result;             /* amortized and adjusted total value */
  int corruption, waste;
  bool overseas;          /* have to use boat to get there */
  bool virt_boat;         /* virtual boat was used in search, 
                           * so need to build one */
  struct tile *other_tile;/* coords to best other tile */
  int o_x, o_y;           /* city-relative coords for other tile */
  int city_center;        /* value of city center */
  int best_other;         /* value of best other tile */
  int remaining;          /* value of all other tiles */
  struct tile_data_cache citymap[CITY_MAP_MAX_SIZE][CITY_MAP_MAX_SIZE];
  int city_radius_sq;     /* current squared radius of the city */
  int reserved[CITY_MAP_MAX_SIZE][CITY_MAP_MAX_SIZE]; 
};

static struct cityresult *cityresult_new();
static void cityresult_destroy(struct cityresult *result);

static void cityresult_fill(struct player *pplayer,
                            struct cityresult *result);
static bool food_starvation(const struct cityresult *result);
static bool shield_starvation(const struct cityresult *result);
static int defense_bonus(const struct cityresult *result);
static int naval_bonus(const struct cityresult *result);
static void print_cityresult(struct player *pplayer,
                             const struct cityresult *cr);
struct cityresult * city_desirability(struct player *pplayer,
                                      struct unit *punit, struct tile *ptile);
static struct cityresult *settler_map_iterate(struct pf_parameter *parameter,
                                              struct unit *punit,
                                              int boat_cost);
static struct cityresult *find_best_city_placement(struct unit *punit,
                                                   bool look_for_boat,
                                                   bool use_virt_boat);
static bool ai_do_build_city(struct player *pplayer, struct unit *punit);

/*****************************************************************************
  Allocated a city result.
*****************************************************************************/
static struct cityresult *cityresult_new()
{
  struct cityresult *result = fc_calloc(1, sizeof(*result));

  /* Set default values */
  /*
  result->tile = NULL;
  result->result = 0;
  result->total = 0;
  result->overseas = FALSE;
  result->virt_boat = FALSE;
  */

  result->city_radius_sq = game.info.init_city_radius_sq;

  return result;
}

/*****************************************************************************
  Destroy a city result.
*****************************************************************************/
static void cityresult_destroy(struct cityresult *result)
{
  if (result != NULL) {
    free(result);
  }
}

/**************************************************************************
  Fill cityresult struct with useful info about the city spot. It must 
  contain valid x, y coordinates and total should be zero.

  We assume whatever best government we are aiming for.

  We always return valid other_x and other_y if total > 0.
**************************************************************************/
static void cityresult_fill(struct player *pplayer,
                            struct cityresult *result)
{
  struct city *pcity = tile_city(result->tile);
  struct government *curr_govt = government_of_player(pplayer);
  struct player *saved_owner = NULL;
  struct tile *saved_claimer = NULL;
  bool virtual_city = FALSE;
  bool handicap = ai_handicap(pplayer, H_MAP);
  struct ai_data *ai = ai_data_get(pplayer);

  fc_assert_ret(ai != NULL);
  fc_assert_ret(ai->settler_map != NULL);

  pplayer->government = ai->goal.govt.gov;

  result->best_other = 0;
  result->other_tile = NULL;
  result->o_x = -1; /* as other_x but city-relative */
  result->o_y = -1;
  result->remaining = 0;
  result->overseas = FALSE;
  result->virt_boat = FALSE;
  result->result = -666;

  if (!pcity) {
    pcity = create_city_virtual(pplayer, result->tile, "Virtuaville");
    saved_owner = tile_owner(result->tile);
    saved_claimer = tile_claimer(result->tile);
    tile_set_owner(result->tile, pplayer, result->tile); /* temporarily */
    city_choose_build_default(pcity);  /* ?? */
    virtual_city = TRUE;
  }

  result->city_radius_sq = city_map_radius_sq_get(pcity);

  city_tile_iterate_index(result->city_radius_sq, result->tile, ptile,
                          cindex) {
    int i, j, tindex;

    city_tile_index_to_xy(&i, &j, cindex, result->city_radius_sq);
    tindex = tile_index(ptile);

    int reserved = citymap_read(ptile);
    bool city_center = (result->tile == ptile); /*is_city_center()*/

    if (reserved < 0
        || (handicap && !map_is_known(ptile, pplayer))
        || NULL != tile_worked(ptile)) {
      /* Tile is reserved or we can't see it */
      result->citymap[i][j].shield = 0;
      result->citymap[i][j].trade = 0;
      result->citymap[i][j].food = 0;
      result->citymap[i][j].sum = 0;
    } else if (ai->settler_map[tindex].sum <= 0 || city_center) {
      /* We cannot read city center from cache */

      /* Food */
      result->citymap[i][j].food
	= city_tile_output(pcity, ptile, FALSE, O_FOOD);

      /* Shields */
      result->citymap[i][j].shield
	= city_tile_output(pcity, ptile, FALSE, O_SHIELD);

      /* Trade */
      result->citymap[i][j].trade
	= city_tile_output(pcity, ptile, FALSE, O_TRADE);

      result->citymap[i][j].sum
         = result->citymap[i][j].food * ai->food_priority
           + result->citymap[i][j].trade * ai->science_priority
           + result->citymap[i][j].shield * ai->shield_priority;

      /* Balance perfection */
      result->citymap[i][j].sum *= PERFECTION / 2;
      if (result->citymap[i][j].food >= 2) {
        result->citymap[i][j].sum *= 2; /* we need this to grow */
      }

      if (!city_center && virtual_city) {
        /* real cities and any city center will give us spossibly
         * skewed results */
        ai->settler_map[tindex].sum = result->citymap[i][j].sum;
        ai->settler_map[tindex].trade = result->citymap[i][j].trade;
        ai->settler_map[tindex].shield = result->citymap[i][j].shield;
        ai->settler_map[tindex].food = result->citymap[i][j].food;
      }
    } else {
      result->citymap[i][j].sum = ai->settler_map[tindex].sum;
      result->citymap[i][j].shield = ai->settler_map[tindex].shield;
      result->citymap[i][j].trade = ai->settler_map[tindex].trade;
      result->citymap[i][j].food = ai->settler_map[tindex].food;
    }

    /* Save reservation status for debugging. */
    result->reserved[i][j] = reserved;

    /* Avoid crowdedness, except for city center. */
    if (result->citymap[i][j].sum > 0) {
      result->citymap[i][j].sum -= MIN(reserved * GROWTH_PRIORITY,
                                       result->citymap[i][j].sum - 1);
    }

    /* Calculate city center and best other than city center */
    if (city_center) {
      result->city_center = result->citymap[i][j].sum;
    } else if (result->citymap[i][j].sum > result->best_other) {
      /* First add other other to remaining */
      result->remaining += result->best_other
                           / GROWTH_POTENTIAL_DEEMPHASIS;
      /* Then make new best other */
      result->best_other = result->citymap[i][j].sum;
      result->other_tile = ptile;
      result->o_x = i;
      result->o_y = j;
    } else {
      /* Save total remaining calculation, divided by crowdedness
       * of the area and the emphasis placed on space for growth. */
      result->remaining += result->citymap[i][j].sum
                           / GROWTH_POTENTIAL_DEEMPHASIS;
    }
  } city_tile_iterate_index_end;

  if (virtual_city) {
    /* Baseline is a size one city (city center + best extra tile). */
    result->total = result->city_center + result->best_other;
  } else if (result->best_other != -1) {
    /* Baseline is best extra tile only. This is why making new cities 
     * is so darn good. */
    result->total = result->best_other;
  } else {
    /* There is no available tile in this city. All is worked. */
    result->total = 0;
    return;
  }

  if (virtual_city) {
    /* Corruption and waste of a size one city deducted. Notice that we
     * don't do this if 'fulltradesize' is changed, since then we'd
     * never make cities. */
#define CMMR CITY_MAP_MAX_RADIUS
    if (game.info.fulltradesize == 1) {
      result->corruption = ai->science_priority
        * city_waste(pcity, O_TRADE,
                     result->citymap[result->o_x][result->o_y].trade
                     + result->citymap[CMMR][CMMR].trade);
    } else {
      result->corruption = 0;
    }

    result->waste = ai->shield_priority
      * city_waste(pcity, O_SHIELD,
                   result->citymap[result->o_x][result->o_y].shield
                   + result->citymap[CMMR][CMMR].shield);
#undef CMMR
  } else {
    /* Deduct difference in corruption and waste for real cities. Note that it
     * is possible (with notradesize) that we _gain_ value here. */
    pcity->size++;
    result->corruption = ai->science_priority
      * (city_waste(pcity, O_TRADE,
		    result->citymap[result->o_x][result->o_y].trade)
	 - pcity->waste[O_TRADE]);
    result->waste = ai->shield_priority
      * (city_waste(pcity, O_SHIELD,
		    result->citymap[result->o_x][result->o_y].shield)
	 - pcity->waste[O_SHIELD]);
    pcity->size--;
  }
  result->total -= result->corruption;
  result->total -= result->waste;
  result->total = MAX(0, result->total);

  pplayer->government = curr_govt;
  if (virtual_city) {
    destroy_city_virtual(pcity);
    tile_set_owner(result->tile, saved_owner, saved_claimer);
  }

  fc_assert(result->city_center >= 0);
  fc_assert(result->remaining >= 0);
}

/**************************************************************************
  Check if a city on this location would starve.
**************************************************************************/
static bool food_starvation(const struct cityresult *result)
{
  /* Avoid starvation: We must have enough food to grow. */
  return (result->citymap[CITY_MAP_MAX_RADIUS][CITY_MAP_MAX_RADIUS].food
          + result->citymap[result->o_x][result->o_y].food < 3);
}

/**************************************************************************
  Check if a city on this location would lack shields.
**************************************************************************/
static bool shield_starvation(const struct cityresult *result)
{
  /* Avoid resource starvation. */
  return (result->citymap[CITY_MAP_MAX_RADIUS][CITY_MAP_MAX_RADIUS].shield
          + result->citymap[result->o_x][result->o_y].shield == 0);
}

/**************************************************************************
  Calculate defense bonus, which is a % of total results equal to a
  given % of the defense bonus %.
**************************************************************************/
static int defense_bonus(const struct cityresult *result)
{
  /* Defense modification (as tie breaker mostly) */
  int defense_bonus = 
    10 + tile_terrain(result->tile)->defense_bonus / 10;
  if (tile_has_special(result->tile, S_RIVER)) {
    defense_bonus +=
        (defense_bonus * terrain_control.river_defense_bonus) / 100;
  }

  return 100 / result->total * (100 / defense_bonus * DEFENSE_EMPHASIS);
}

/**************************************************************************
  Add bonus for coast.
**************************************************************************/
static int naval_bonus(const struct cityresult *result)
{
  bool ocean_adjacent = is_ocean_near_tile(result->tile);

  /* Adjust for ocean adjacency, which is nice */
  if (ocean_adjacent) {
    return (result->total * NAVAL_EMPHASIS) / 100;
  } else {
    return 0;
  }
}

/**************************************************************************
  For debugging, print the city result table.
**************************************************************************/
static void print_cityresult(struct player *pplayer,
                             const struct cityresult *cr)
{
  int *city_map_data;

  city_map_data = fc_calloc(city_map_tiles(cr->city_radius_sq),
                            sizeof(*city_map_data));

  /* print reservations */
  city_map_iterate(cr->city_radius_sq, index, x, y) {
    city_map_data[index] = cr->reserved[x][y];
  } city_map_iterate_end;
  log_test("cityresult for (x,y,radius_sq) = (%d, %d, %d) - Reservations:",
           cr->tile->x, cr->tile->y, cr->city_radius_sq);
  citylog_map_data(LOG_TEST, cr->city_radius_sq, city_map_data);

  /* print food */
  city_map_iterate(cr->city_radius_sq, index, x, y) {
    city_map_data[index] = cr->citymap[x][y].food;
  } city_map_iterate_end;
  log_test("cityresult for (x,y,radius_sq) = (%d, %d, %d) - Food:",
           cr->tile->x, cr->tile->y, cr->city_radius_sq);
  citylog_map_data(LOG_TEST, cr->city_radius_sq, city_map_data);

  /* print shield */
  city_map_iterate(cr->city_radius_sq, index, x, y) {
    city_map_data[index] = cr->citymap[x][y].shield;
  } city_map_iterate_end;
  log_test("cityresult for (x,y,radius_sq) = (%d, %d, %d) - Shield:",
           cr->tile->x, cr->tile->y, cr->city_radius_sq);
  citylog_map_data(LOG_TEST, cr->city_radius_sq, city_map_data);

  /* print trade */
  city_map_iterate(cr->city_radius_sq, index, x, y) {
    city_map_data[index] = cr->citymap[x][y].trade;
  } city_map_iterate_end;
  log_test("cityresult for (x,y,radius_sq) = (%d, %d, %d) - Trade:",
           cr->tile->x, cr->tile->y, cr->city_radius_sq);
  citylog_map_data(LOG_TEST, cr->city_radius_sq, city_map_data);

  FC_FREE(city_map_data);

  log_test("city center (%d, %d) %d + best other (abs: %d, %d)"
           " (rel: %d,%d) %d", cr->tile->x, cr->tile->y, cr->city_center,
           cr->other_tile->x, cr->other_tile->y, cr->o_x, cr->o_y,
           cr->best_other);
  log_test("- corr %d - waste %d + remaining %d"
           " + defense bonus %d + naval bonus %d", cr->corruption,
           cr->waste, cr->remaining, defense_bonus(cr),
           naval_bonus(cr));
  log_test("= %d (%d)", cr->total, cr->result);

  if (food_starvation(cr)) {
    log_test(" ** FOOD STARVATION **");
  }
  if (shield_starvation(cr)) {
    log_test(" ** RESOURCE STARVATION **");
  }
}

/*****************************************************************************
  Calculates the desire for founding a new city at 'ptile'. The citymap
  ensures that we do not build cities too close to each other. Returns NULL
  if no place was found.
*****************************************************************************/
struct cityresult *city_desirability(struct player *pplayer,
                                     struct unit *punit, struct tile *ptile)
{
  struct city *pcity = tile_city(ptile);
  struct ai_data *ai = ai_data_get(pplayer);
  struct cityresult *cr = NULL;

  fc_assert_ret_val(punit, NULL);
  fc_assert_ret_val(pplayer, NULL);
  fc_assert_ret_val(ai, NULL);

  if (!city_can_be_built_here(ptile, punit)
      || (ai_handicap(pplayer, H_MAP)
          && !map_is_known(ptile, pplayer))) {
    return NULL;
  }

  /* Check if another settler has taken a spot within mindist */
  square_iterate(ptile, game.info.min_dist_bw_cities-1, tile1) {
    if (citymap_is_reserved(tile1)) {
      return NULL;
    }
  } square_iterate_end;

  if (enemies_at(punit, ptile)) {
    return NULL;
  }

  if (pcity && (pcity->size + unit_pop_value(punit)
		> game.info.add_to_size_limit)) {
    /* Can't exceed population limit. */
    return NULL;
  }

  if (!pcity && citymap_is_reserved(ptile)) {
    return NULL; /* reserved, go away */
  }

  /* If (x, y) is an existing city, consider immigration */
  if (pcity && city_owner(pcity) == pplayer) {
    return NULL;
  }

  cr = cityresult_new();
  cr->tile = ptile;
  cr->total = 0;

  cityresult_fill(pplayer, cr); /* Burn CPU, burn! */
  if (cr->total == 0) {
    /* Failed to find a good spot */
    cityresult_destroy(cr);
    return NULL;
  }

  /*** Alright: Now consider building a new city ***/

  if (food_starvation(cr) || shield_starvation(cr)) {
    cityresult_destroy(cr);
    return NULL;
  }

  cr->total += defense_bonus(cr);
  cr->total += naval_bonus(cr);

  /* Add remaining points, which is our potential */
  cr->total += cr->remaining;

  fc_assert_ret_val(cr->total >= 0, NULL); /* Does not frees cr! */

  return cr;
}

/**************************************************************************
  Find nearest and best city placement in a PF iteration according to 
  "parameter".  The value in "boat_cost" is both the penalty to pay for 
  using a boat and an indicator (boat_cost!=0) if a boat was used at all. 
  The result is returned in "best".

  Return value is a 'struct cityresult' if found something better than what
  was originally in "best" was found; else NULL.

  TODO: Transparently check if we should add ourselves to an existing city.
**************************************************************************/
static struct cityresult *settler_map_iterate(struct pf_parameter *parameter,
                                              struct unit *punit,
                                              int boat_cost)
{
  struct cityresult *cr = NULL, *best = NULL;
  int best_turn = 0; /* Which turn we found the best fit */
  struct player *pplayer = unit_owner(punit);
  struct pf_map *pfm;

  pfm = pf_map_new(parameter);
  pf_map_move_costs_iterate(pfm, ptile, move_cost, FALSE) {
    int turns;

    if (is_ocean_tile(ptile)) {
      continue; /* This can happen if there is a ferry near shore. */
    }
    if (boat_cost == 0
        && tile_continent(ptile) != tile_continent(punit->tile)) {
      /* We have an accidential land bridge. Ignore it. It will in all
       * likelihood go away next turn, or even in a few nanoseconds. */
      continue;
    }
    if (BORDERS_DISABLED != game.info.borders) {
      struct player *powner = tile_owner(ptile);
      if (NULL != powner
       && powner != pplayer
       && pplayers_in_peace(powner, pplayer)) {
        /* Land theft does not make for good neighbours. */
        continue;
      }
    }

    /* Calculate worth */
    cr = city_desirability(pplayer, punit, ptile);

    /* Check if actually found something */
    if (!cr) {
      continue;
    }

    /* This algorithm punishes long treks */
    turns = move_cost / parameter->move_rate;
    cr->result = amortize(cr->total, PERFECTION * turns);

    /* Reduce want by settler cost. Easier than amortize, but still
     * weeds out very small wants. ie we create a threshold here. */
    /* We also penalise here for using a boat (either virtual or real)
     * it's crude but what isn't? */
    cr->result -= unit_build_shield_cost(punit) + boat_cost;

    /* Find best spot */
    if ((!best && cr->result > 0)
        || (best && cr->result > best->result)) {
      /* Destroy the old 'best' value. */
      cityresult_destroy(best);
      /* save the new 'best' value. */
      best = cr;
      cr = NULL;
      best_turn = turns;

      log_debug("settler map search (search): (%d,%d) %d", best->tile->x,
                best->tile->y, best->result);
    } else {
      /* Destroy the unused result. */
      cityresult_destroy(cr);
      cr = NULL;
    }

    /* Can we terminate early? We have a 'good enough' spot, and
     * we don't block the establishment of a better city just one
     * further step away. */
    if (best && best->result > RESULT_IS_ENOUGH
        && turns > parameter->move_rate /* sic -- yeah what an explanation! */
        && best_turn < turns /*+ game.info.min_dist_bw_cities*/) {
      break;
    }
  } pf_map_positions_iterate_end;

  pf_map_destroy(pfm);

  if (best) {
    log_debug("settler map search (final): (%d,%d) %d", best->tile->x,
              best->tile->y, best->result);
  } else {
    log_debug("settler map search (final): no result");
  }

  return best;
}

/**************************************************************************
  Find nearest and best city placement or (TODO) a city to immigrate to.

  Option look_for_boat forces us to find a (real) boat before cosidering
  going overseas.  Option use_virt_boat allows to use virtual boat but only
  if punit is in a coastal city right now (should only be used by 
  virtual units).  I guess it won't hurt to remove this condition, PF 
  will just give no positions.
  If (!look_for_boat && !use_virt_boat), will not consider placements
  overseas.

  Returns the better cityresult or NULL if no result was found.
**************************************************************************/
static struct cityresult *find_best_city_placement(struct unit *punit,
                                                   bool look_for_boat,
                                                   bool use_virt_boat)
{
  struct pf_parameter parameter;
  struct player *pplayer = unit_owner(punit);
  struct unit *ferry = NULL;
  struct unit_class *ferry_class = NULL;
  struct cityresult *cr1 = NULL, *cr2 = NULL;

  fc_assert_ret_val(pplayer->ai_controlled, NULL);
  /* Only virtual units may use virtual boats: */
  fc_assert_ret_val(0 == punit->id || !use_virt_boat, NULL);

  /* Phase 1: Consider building cities on our continent */

  pft_fill_unit_parameter(&parameter, punit);
  cr1 = settler_map_iterate(&parameter, punit, 0);

  if (cr1 && cr1->result > RESULT_IS_ENOUGH) {
    /* skip further searches */
    return cr1;
  }

  /* Phase 2: Consider travelling to another continent */

  if (look_for_boat) {
    int ferry_id = aiferry_find_boat(punit, 1, NULL);

    ferry = game_unit_by_number(ferry_id);
  }

  if (ferry || (use_virt_boat && is_ocean_near_tile(punit->tile)
                && tile_city(punit->tile))) {
    if (!ferry) {
      /* No boat?  Get a virtual one! */
      struct unit_type *boattype
        = best_role_unit_for_player(pplayer, L_FERRYBOAT);

      if (boattype == NULL) {
        /* Sea travel not possible yet. Bump tech want for ferries. */
        boattype = get_role_unit(L_FERRYBOAT, 0);

        if (NULL != boattype
         && A_NEVER != boattype->require_advance) {
          pplayer->ai_common.tech_want[advance_index(boattype->require_advance)]
            += FERRY_TECH_WANT;
          TECH_LOG(LOG_DEBUG, pplayer, boattype->require_advance,
                   "+ %d for %s to ferry settler",
                   FERRY_TECH_WANT,
                   utype_rule_name(boattype));
        }
        /* return the result from the search on our current continent */
        return cr1;
      }
      ferry = create_unit_virtual(pplayer, NULL, boattype, 0);
      ferry->tile = punit->tile;
    }

    ferry_class = unit_class(ferry);

    fc_assert(ferry_class->ai.sea_move != MOVE_NONE);
    pft_fill_unit_overlap_param(&parameter, ferry);
    parameter.get_TB = no_fights_or_unknown;

    /* FIXME: Maybe penalty for using an existing boat is too high?
     * We shouldn't make the penalty for building a new boat too high though.
     * Building a new boat is like a war against a weaker enemy -- 
     * good for the economy. (c) Bush family */
    cr2 = settler_map_iterate(&parameter, punit,
                              unit_build_shield_cost(ferry));
    if (cr2) {
      cr2->overseas = TRUE;
      cr2->virt_boat = (ferry->id == 0);
    }

    if (ferry->id == 0) {
      destroy_unit_virtual(ferry);
    }

    /* If we use a virtual boat, we must have permission and be emigrating: */
    /* FIXME: These assert do not frees cr2! */
    fc_assert_ret_val(!cr2 || (!cr2->virt_boat || use_virt_boat), NULL);
    fc_assert_ret_val(!cr2 || (!cr2->virt_boat || cr2->overseas), NULL);
  }

  if (!cr1) {
    /* No want for a new city on our current continent; return the result for
     * traveling by boat. */
    return cr2;
  } else if (!cr2) {
    /* No want for an overseas city; return the result for a city on our
     * current continent. */
    return cr1;
  }

  /* We want an overseas city and a city on the current continent - select the
   * best! */
  if (cr1->result > cr2->result) {
    cityresult_destroy(cr2);
    return cr1;
  } else {
    cityresult_destroy(cr1);
    return cr2;
  }
}

/**************************************************************************
  Initialize ai settler engine.
**************************************************************************/
void ai_auto_settler_init(struct player *pplayer)
{
  struct ai_data *ai = ai_data_get(pplayer);

  fc_assert_ret(ai != NULL);
  fc_assert_ret(ai->settler_map == NULL);

  ai->settler_map = fc_malloc(MAP_INDEX_SIZE * sizeof(*ai->settler_map));
  memset(ai->settler_map, -1, MAP_INDEX_SIZE * sizeof(*ai->settler_map));
}

/**************************************************************************
  Auto settler that can also build cities.
**************************************************************************/
void ai_auto_settler_run(struct player *pplayer, struct unit *punit,
                         struct settlermap *state)
{
  int best_impr = 0;            /* best terrain improvement we can do */
  enum unit_activity best_act;
  struct tile *best_tile = NULL;
  struct pf_path *path = NULL;

  /* time it will take worker to complete its given task */
  int completion_time = 0;

  CHECK_UNIT(punit);

  /*** If we are on a city mission: Go where we should ***/

BUILD_CITY:

  if (punit->server.adv->role == AIUNIT_BUILD_CITY) {
    struct tile *ptile = punit->goto_tile;
    int sanity = punit->id;

    /* Check that the mission is still possible.  If the tile has become
     * unavailable, call it off. */
    if (!city_can_be_built_here(ptile, punit)) {
      ai_unit_new_role(punit, AIUNIT_NONE, NULL);
      set_unit_activity(punit, ACTIVITY_IDLE);
      send_unit_info(NULL, punit);
      return; /* avoid recursion at all cost */
    } else {
     /* Go there */
      if ((!ai_gothere(pplayer, punit, ptile)
           && NULL == game_unit_by_number(sanity))
          || punit->moves_left <= 0) {
        return;
      }
      if (same_pos(punit->tile, ptile)) {
        if (!ai_do_build_city(pplayer, punit)) {
          UNIT_LOG(LOG_DEBUG, punit, "could not make city on %s",
                   tile_get_info_text(punit->tile, 0));
          ai_unit_new_role(punit, AIUNIT_NONE, NULL);
          /* Only known way to end in here is that hut turned in to a city
           * when settler entered tile. So this is not going to lead in any
           * serious recursion. */
          ai_auto_settler_run(pplayer, punit, state);

          return;
       } else {
          return; /* We came, we saw, we built... */
        }
      } else {
        UNIT_LOG(LOG_DEBUG, punit, "could not go to target");
        /* ai_unit_new_role(punit, AIUNIT_NONE, NULL); */
        return;
      }
    }
  }

  /*** Try find some work ***/

  if (unit_has_type_flag(punit, F_SETTLERS)) {
    TIMING_LOG(AIT_WORKERS, TIMER_START);
    best_impr = settler_evaluate_improvements(punit, &best_act, &best_tile, 
                                              &path, state);
    UNIT_LOG(LOG_DEBUG, punit, "impr want %d", best_impr);
    if (path) {
      completion_time = pf_path_last_position(path)->turn;
    }
    TIMING_LOG(AIT_WORKERS, TIMER_STOP);
  }

  if (unit_has_type_flag(punit, F_CITIES)) {
    struct cityresult *result;

    /* may use a boat: */
    TIMING_LOG(AIT_SETTLERS, TIMER_START);
    result = find_best_city_placement(punit, TRUE, FALSE);
    TIMING_LOG(AIT_SETTLERS, TIMER_STOP);
    if (result && result->result > best_impr) {
      UNIT_LOG(LOG_DEBUG, punit, "city want %d", result->result);
      if (tile_city(result->tile)) {
        UNIT_LOG(LOG_DEBUG, punit, "immigrates to %s (%d, %d)", 
                 city_name(tile_city(result->tile)),
                 TILE_XY(result->tile));
      } else {
        UNIT_LOG(LOG_DEBUG, punit, "makes city at (%d, %d)", 
                 TILE_XY(result->tile));
        if (punit->server.debug) {
          print_cityresult(pplayer, result);
        }
      }
      /* Go make a city! */
      ai_unit_new_role(punit, AIUNIT_BUILD_CITY, result->tile);
      if (result->other_tile) {
        /* Reserve best other tile (if there is one). It is the tile where the
         * first citicen of the city is working. */
        citymap_reserve_tile(result->other_tile, punit->id);
      }
      punit->goto_tile = result->tile; /* TMP */

      cityresult_destroy(result);

      /*** Go back to and found a city ***/
      pf_path_destroy(path);
      path = NULL;
      goto BUILD_CITY;
    } else if (best_impr > 0) {
      UNIT_LOG(LOG_DEBUG, punit, "improves terrain instead of founding");
      /* Terrain improvements follows the old model, and is recalculated
       * each turn. */
      ai_unit_new_role(punit, AIUNIT_AUTO_SETTLER, best_tile);
    } else {
      UNIT_LOG(LOG_DEBUG, punit, "cannot find work");
      ai_unit_new_role(punit, AIUNIT_NONE, NULL);
      goto CLEANUP;
    }
  } else {
    /* We are a worker or engineer */
    ai_unit_new_role(punit, AIUNIT_AUTO_SETTLER, best_tile);
  }

  auto_settler_setup_work(pplayer, punit, state, 0, path,
                          best_tile, best_act,
                          completion_time);

CLEANUP:

  if (NULL != path) {
    pf_path_destroy(path);
  }
}

/**************************************************************************
  Deinitialize ai settler engine.
**************************************************************************/
void ai_auto_settler_free(struct player *pplayer)
{
  struct ai_data *ai = ai_data_get(pplayer);

  fc_assert_ret(ai != NULL);

  if (ai->settler_map) {
    free(ai->settler_map);
  }
  ai->settler_map = NULL;
}

/**************************************************************************
  Build a city and initialize AI infrastructure cache.
**************************************************************************/
static bool ai_do_build_city(struct player *pplayer, struct unit *punit)
{
  struct tile *ptile = punit->tile;
  struct city *pcity;

  fc_assert_ret_val(pplayer == unit_owner(punit), FALSE);
  unit_activity_handling(punit, ACTIVITY_IDLE);

  /* Free city reservations */
  ai_unit_new_role(punit, AIUNIT_NONE, NULL);

  pcity = tile_city(ptile);
  if (pcity) {
    /* This can happen for instance when there was hut at this tile
     * and it turned in to a city when settler entered tile. */
    log_debug("%s: There is already a city at (%d, %d)!",
              player_name(pplayer), TILE_XY(ptile));
    return FALSE;
  }
  handle_unit_build_city(pplayer, punit->id,
                         city_name_suggestion(pplayer, ptile));
  pcity = tile_city(ptile);
  if (!pcity) {
    log_error("%s: Failed to build city at (%d, %d)",
              player_name(pplayer), TILE_XY(ptile));
    return FALSE;
  }

  /* We have to rebuild at least the cache for this city.  This event is
   * rare enough we might as well build the whole thing.  Who knows what
   * else might be cached in the future? */
  fc_assert_ret_val(pplayer == city_owner(pcity), FALSE);
  initialize_infrastructure_cache(pplayer);

  /* Init ai.choice. Handling ferryboats might use it. */
  init_choice(&def_ai_city_data(pcity)->choice);

  return TRUE;
}

/**************************************************************************
  Return want for city settler. Note that we rely here on the fact that
  ai_settler_init() has been run while doing autosettlers.
**************************************************************************/
void contemplate_new_city(struct city *pcity)
{
  struct unit *virtualunit;
  struct tile *pcenter = city_tile(pcity);
  struct player *pplayer = city_owner(pcity);
  struct unit_type *unit_type = best_role_unit(pcity, F_CITIES); 

  if (unit_type == NULL) {
    log_debug("No F_CITIES role unit available");
    return;
  }

  /* Create a localized "virtual" unit to do operations with. */
  virtualunit = create_unit_virtual(pplayer, pcity, unit_type, 0);
  virtualunit->tile = pcenter;

  fc_assert_ret(pplayer->ai_controlled);

  if (pplayer->ai_controlled) {
    struct cityresult *result;
    bool is_coastal = is_ocean_near_tile(pcenter);
    struct ai_city *city_data = def_ai_city_data(pcity);

    result = find_best_city_placement(virtualunit, is_coastal, is_coastal);

    if (result) {
      fc_assert_ret(0 <= result->result); /* 'result' is not freed! */

      CITY_LOG(LOG_DEBUG, pcity, "want(%d) to establish city at"
               " (%d, %d) and will %s to get there", result->result, 
               TILE_XY(result->tile), 
               (result->virt_boat ? "build a boat" : 
                (result->overseas ? "use a boat" : "walk")));

      city_data->founder_want = (result->virt_boat ? 
                                 -result->result : result->result);
      city_data->founder_boat = result->overseas;

      cityresult_destroy(result);
    } else {
      CITY_LOG(LOG_DEBUG, pcity, "want no city");
    }
  }
  destroy_unit_virtual(virtualunit);
}
