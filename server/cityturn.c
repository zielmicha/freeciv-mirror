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
#include <stdlib.h>
#include <string.h>

#include "city.h"
#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "player.h"
#include "rand.h"
#include "shared.h"
#include "support.h"
#include "tech.h"
#include "unit.h"

#include "script.h"

#include "citytools.h"
#include "gamelog.h"
#include "maphand.h"
#include "plrhand.h"
#include "sanitycheck.h"
#include "settlers.h"
#include "spacerace.h"
#include "srv_main.h"
#include "techtools.h"
#include "unittools.h"
#include "unithand.h"

#include "cm.h"

#include "advdomestic.h"
#include "aicity.h"
#include "aidata.h"
#include "ailog.h"
#include "aitools.h"

#include "cityturn.h"

static void check_pollution(struct city *pcity);
static void city_populate(struct city *pcity);

static bool worklist_change_build_target(struct player *pplayer,
					 struct city *pcity);

static bool city_distribute_surplus_shields(struct player *pplayer,
					    struct city *pcity);
static bool city_build_building(struct player *pplayer, struct city *pcity);
static bool city_build_unit(struct player *pplayer, struct city *pcity);
static bool city_build_stuff(struct player *pplayer, struct city *pcity);
static Impr_type_id building_upgrades_to(struct city *pcity, Impr_type_id b);
static void upgrade_building_prod(struct city *pcity);
static struct unit_type *unit_upgrades_to(struct city *pcity,
					  struct unit_type *id);
static void upgrade_unit_prod(struct city *pcity);
static void pay_for_buildings(struct player *pplayer, struct city *pcity);

static bool disband_city(struct city *pcity);

static void define_orig_production_values(struct city *pcity);
static void update_city_activity(struct player *pplayer, struct city *pcity);
static void nullify_caravan_and_disband_plus(struct city *pcity);

/**************************************************************************
...
**************************************************************************/
void city_refresh(struct city *pcity)
{
   generic_city_refresh(pcity, TRUE, send_unit_info);
   /* AI would calculate this 1000 times otherwise; better to do it
      once -- Syela */
   pcity->ai.trade_want
     = TRADE_WEIGHTING - city_waste(pcity, O_TRADE, TRADE_WEIGHTING);
}

/**************************************************************************
...
called on government change or wonder completion or stuff like that -- Syela
**************************************************************************/
void global_city_refresh(struct player *pplayer)
{
  conn_list_do_buffer(pplayer->connections);
  city_list_iterate(pplayer->cities, pcity)
    city_refresh(pcity);
    send_city_info(pplayer, pcity);
  city_list_iterate_end;
  conn_list_do_unbuffer(pplayer->connections);
}

/**************************************************************************
...
**************************************************************************/
void remove_obsolete_buildings_city(struct city *pcity, bool refresh)
{
  struct player *pplayer = city_owner(pcity);
  bool sold = FALSE;

  built_impr_iterate(pcity, i) {
    if (can_city_sell_building(pcity, i) && improvement_obsolete(pplayer, i)) {
      do_sell_building(pplayer, pcity, i);
      notify_player(pplayer, pcity->tile, E_IMP_SOLD, 
		       _("%s is selling %s (obsolete) for %d."),
		       pcity->name, get_improvement_name(i), 
		       impr_sell_gold(i));
      sold = TRUE;
    }
  } built_impr_iterate_end;

  if (sold && refresh) {
    city_refresh(pcity);
    send_city_info(pplayer, pcity);
    send_player_info(pplayer, NULL); /* Send updated gold to all */
  }
}

/**************************************************************************
...
**************************************************************************/
void remove_obsolete_buildings(struct player *pplayer)
{
  city_list_iterate(pplayer->cities, pcity) {
    remove_obsolete_buildings_city(pcity, FALSE);
  } city_list_iterate_end;
}

/**************************************************************************
  Rearrange workers according to a cm_result struct.  The caller must make
  sure that the result is valid.
**************************************************************************/
void apply_cmresult_to_city(struct city *pcity, struct cm_result *cmr)
{
  /* The caller had better check this! */
  if (!cmr->found_a_valid) {
    freelog(LOG_ERROR, "apply_cmresult_to_city() called with non-valid "
            "cm_result");
    assert(0);
    return;
  }

  /* Now apply results */
  city_map_checked_iterate(pcity->tile, x, y, ptile) {
    if (pcity->city_map[x][y] == C_TILE_WORKER
        && !is_free_worked_tile(x, y)
        && !cmr->worker_positions_used[x][y]) {
      server_remove_worker_city(pcity, x, y);
    }
    if (pcity->city_map[x][y] != C_TILE_WORKER
        && !is_free_worked_tile(x, y)
        && cmr->worker_positions_used[x][y]) {
      server_set_worker_city(pcity, x, y);
    }
  } city_map_checked_iterate_end;
  specialist_type_iterate(sp) {
    pcity->specialists[sp] = cmr->specialists[sp];
  } specialist_type_iterate_end;
}

/**************************************************************************
  You need to call sync_cities so that the affected cities are synced with 
  the client.
**************************************************************************/
void auto_arrange_workers(struct city *pcity)
{
  struct cm_parameter cmp;
  struct cm_result cmr;

  /* See comment in freeze_workers(): we can't rearrange while
   * workers are frozen (i.e. multiple updates need to be done). */
  if (pcity->server.workers_frozen > 0) {
    pcity->server.needs_arrange = TRUE;
    return;
  }
  TIMING_LOG(AIT_CITIZEN_ARRANGE, TIMER_START);

  /* Freeze the workers and make sure all the tiles around the city
   * are up to date.  Then thaw, but hackishly make sure that thaw
   * doesn't call us recursively, which would waste time. */
  city_freeze_workers(pcity);
  pcity->server.needs_arrange = FALSE;

  map_city_radius_iterate(pcity->tile, ptile) {
    update_city_tile_status_map(pcity, ptile);
  } map_city_radius_iterate_end;

  pcity->server.needs_arrange = FALSE;
  city_thaw_workers(pcity);

  /* Now start actually rearranging. */
  sanity_check_city(pcity);
  cm_clear_cache(pcity);

  cm_init_parameter(&cmp);
  cmp.require_happy = FALSE;
  cmp.allow_disorder = FALSE;
  cmp.allow_specialists = TRUE;

  /* We used to look at pplayer->ai.xxx_priority to determine the values
   * to be used here.  However that doesn't work at all because those values
   * are on a different scale.  Later the ai may wish to adjust its
   * priorities - this should be done via a separate set of variables. */
  if (pcity->size > 1) {
    if (pcity->size <= game.info.notradesize) {
      cmp.factor[O_FOOD] = 15;
    } else {
      cmp.factor[O_FOOD] = 10;
    }
  } else {
    /* Growing to size 2 is the highest priority. */
    cmp.factor[O_FOOD] = 20;
  }
  cmp.factor[O_SHIELD] = 5;
  cmp.factor[O_TRADE] = 0; /* Trade only provides gold/science. */
  cmp.factor[O_GOLD] = 2;
  cmp.factor[O_LUXURY] = 0; /* Luxury only influences happiness. */
  cmp.factor[O_SCIENCE] = 2;
  cmp.happy_factor = 0;

  cmp.minimal_surplus[O_FOOD] = 1;
  cmp.minimal_surplus[O_SHIELD] = 1;
  cmp.minimal_surplus[O_TRADE] = 0;
  cmp.minimal_surplus[O_GOLD] = -FC_INFINITY;
  cmp.minimal_surplus[O_LUXURY] = 0;
  cmp.minimal_surplus[O_SCIENCE] = 0;

  cm_query_result(pcity, &cmp, &cmr);

  if (!cmr.found_a_valid) {
    /* Drop surpluses and try again. */
    cmp.minimal_surplus[O_FOOD] = 0;
    cmp.minimal_surplus[O_SHIELD] = 0;
    cmp.minimal_surplus[O_GOLD] = -FC_INFINITY;
    cm_query_result(pcity, &cmp, &cmr);
  }
  if (!cmr.found_a_valid) {
    /* Emergency management.  Get _some_ result.  This doesn't use
     * cm_init_emergency_parameter so we can keep the factors from
     * above. */
    output_type_iterate(o) {
      cmp.minimal_surplus[o] = MIN(cmp.minimal_surplus[o],
				   MIN(pcity->surplus[o], 0));
    } output_type_iterate_end;
    cmp.require_happy = FALSE;
    cmp.allow_disorder = city_owner(pcity)->ai.control ? FALSE : TRUE;
    cm_query_result(pcity, &cmp, &cmr);
  }
  if (!cmr.found_a_valid) {
    /* Should never happen. */
    CITY_LOG(LOG_DEBUG, pcity, "emergency management");
    cm_init_emergency_parameter(&cmp);
    cm_query_result(pcity, &cmp, &cmr);
  }
  assert(cmr.found_a_valid);

  apply_cmresult_to_city(pcity, &cmr);

  sanity_check_city(pcity);

  city_refresh(pcity);
  TIMING_LOG(AIT_CITIZEN_ARRANGE, TIMER_STOP);
}

/**************************************************************************
Notices about cities that should be sent to all players.
**************************************************************************/
void send_global_city_turn_notifications(struct conn_list *dest)
{
  if (!dest) {
    dest = game.all_connections;
  }

  players_iterate(pplayer) {
    city_list_iterate(pplayer->cities, pcity) {
      /* can_player_build_improvement() checks whether wonder is build
	 elsewhere (or destroyed) */
      if (!pcity->production.is_unit && is_great_wonder(pcity->production.value)
	  && (city_turns_to_build(pcity, pcity->production, TRUE) <= 1)
	  && can_player_build_improvement(city_owner(pcity), pcity->production.value)) {
	notify_conn_ex(dest, pcity->tile,
		       E_WONDER_WILL_BE_BUILT,
		       _("Notice: Wonder %s in %s will be finished"
			 " next turn."), 
		       get_improvement_name(pcity->production.value),
		       pcity->name);
      }
    } city_list_iterate_end;
  } players_iterate_end;
}

/**************************************************************************
  Send turn notifications for specified city to specified connections.
  Neither dest nor pcity may be NULL.
**************************************************************************/
void send_city_turn_notifications(struct conn_list *dest, struct city *pcity)
{
  int turns_growth, turns_granary;
  bool can_grow;
 
  if (pcity->surplus[O_FOOD] > 0) {
    turns_growth = (city_granary_size(pcity->size) - pcity->food_stock - 1)
		   / pcity->surplus[O_FOOD];

    if (get_city_bonus(pcity, EFT_GROWTH_FOOD) == 0
	&& get_current_construction_bonus(pcity, EFT_GROWTH_FOOD) > 0
	&& pcity->surplus[O_SHIELD] > 0) {
      /* From the check above, the surplus must always be positive. */
      turns_granary = (impr_build_shield_cost(pcity->production.value)
		       - pcity->shield_stock) / pcity->surplus[O_SHIELD];
      /* if growth and granary completion occur simultaneously, granary
	 preserves food.  -AJS */
      if (turns_growth < 5 && turns_granary < 5
	  && turns_growth < turns_granary) {
	notify_conn_ex(dest, pcity->tile,
			 E_CITY_GRAN_THROTTLE,
			 _("Suggest throttling growth in %s to use %s "
			   "(being built) more effectively."), pcity->name,
		       get_improvement_name(pcity->production.value));
      }
    }

    can_grow = city_can_grow_to(pcity, pcity->size + 1);

    if ((turns_growth <= 0) && !city_celebrating(pcity) && can_grow) {
      notify_conn_ex(dest, pcity->tile,
		       E_CITY_MAY_SOON_GROW,
		       _("%s may soon grow to size %i."),
		       pcity->name, pcity->size + 1);
    }
  } else {
    if (pcity->food_stock + pcity->surplus[O_FOOD] <= 0
	&& pcity->surplus[O_FOOD] < 0) {
      notify_conn_ex(dest, pcity->tile,
		     E_CITY_FAMINE_FEARED,
		     _("Warning: Famine feared in %s."),
		     pcity->name);
    }
  }
  
}

/**************************************************************************
...
**************************************************************************/
void update_city_activities(struct player *pplayer)
{
  int gold;
  gold=pplayer->economic.gold;
  pplayer->bulbs_last_turn = 0;
  city_list_iterate(pplayer->cities, pcity)
     update_city_activity(pplayer, pcity);
  city_list_iterate_end;
  pplayer->ai.prev_gold = gold;
  /* This test include the cost of the units because pay_for_units is called
   * in update_city_activity */
  if (gold - (gold - pplayer->economic.gold) * 3 < 0) {
    notify_player(pplayer, NULL, E_LOW_ON_FUNDS,
		     _("WARNING, we're LOW on FUNDS sire."));  
  }
    /* uncomment to unbalance the game, like in civ1 (CLG)
      if (pplayer->got_tech && pplayer->research->researched > 0)    
        pplayer->research->researched=0;
    */
}

/**************************************************************************
  Reduce the city size.  Return TRUE if the city survives the population
  loss.
**************************************************************************/
bool city_reduce_size(struct city *pcity, int pop_loss)
{
  if (pop_loss == 0) {
    return TRUE;
  }

  if (pcity->size <= pop_loss) {
    remove_city(pcity);
    return FALSE;
  }
  pcity->size -= pop_loss;

  /* Cap the food stock at the new granary size. */
  if (pcity->food_stock > city_granary_size(pcity->size)) {
    pcity->food_stock = city_granary_size(pcity->size);
  }

  /* First try to kill off the specialists */
  while (pop_loss > 0 && city_specialists(pcity) > 0) {
    Specialist_type_id sp;

    for (sp = SP_COUNT - 1; sp >= 0; sp--) {
      if (pcity->specialists[sp] > 0) {
	pcity->specialists[sp]--;
	pop_loss--;
	break;
      }
    }
  }
  assert(pop_loss == 0 || city_specialists(pcity) == 0);

  /* we consumed all the pop_loss in specialists */
  if (pop_loss == 0) {
    city_refresh(pcity);
    send_city_info(city_owner(pcity), pcity);
  } else {
    /* Take it out on workers */
    city_map_iterate(x, y) {
      if (get_worker_city(pcity, x, y) == C_TILE_WORKER
          && !is_free_worked_tile(x, y) && pop_loss > 0) {
        server_remove_worker_city(pcity, x, y);
        pop_loss--;
      }
    } city_map_iterate_end;
    /* Then rearrange workers */
    assert(pop_loss == 0);
    auto_arrange_workers(pcity);
    sync_cities();
  }
  sanity_check_city(pcity);
  return TRUE;
}

/**************************************************************************
  Return the percentage of food that is lost in this city.

  Normally this value is 0% but this can be increased by EFT_GROWTH_FOOD
  effects.
**************************************************************************/
static int granary_savings(const struct city *pcity)
{
  int savings = get_city_bonus(pcity, EFT_GROWTH_FOOD);

  return CLIP(0, savings, 100);
}

/**************************************************************************
Note: We do not send info about the city to the clients as part of this function
**************************************************************************/
static void city_increase_size(struct city *pcity)
{
  struct player *powner = city_owner(pcity);
  bool have_square;
  int savings_pct = granary_savings(pcity), new_food;
  bool rapture_grow = city_rapture_grow(pcity); /* check before size increase! */

  if (!city_can_grow_to(pcity, pcity->size + 1)) { /* need improvement */
    if (get_current_construction_bonus(pcity, EFT_SIZE_ADJ) > 0
        || get_current_construction_bonus(pcity, EFT_SIZE_UNLIMIT) > 0) {
      notify_player(powner, pcity->tile, E_CITY_AQ_BUILDING,
		       _("%s needs %s (being built) "
			 "to grow any further."), pcity->name,
		       get_improvement_name(pcity->production.value));
    } else {
      notify_player(powner, pcity->tile, E_CITY_AQUEDUCT,
		       _("%s needs an improvement to grow any further."),
		       pcity->name);
    }
    /* Granary can only hold so much */
    new_food = (city_granary_size(pcity->size)
		* (100 * 100 - game.info.aqueductloss * (100 - savings_pct))
		/ (100 * 100));
    pcity->food_stock = MIN(pcity->food_stock, new_food);
    return;
  }

  pcity->size++;
  /* Do not empty food stock if city is growing by celebrating */
  if (rapture_grow) {
    new_food = city_granary_size(pcity->size);
  } else {
    new_food = city_granary_size(pcity->size) * savings_pct / 100;
  }
  pcity->food_stock = MIN(pcity->food_stock, new_food);

  /* If there is enough food, and the city is big enough,
   * make new citizens into scientists or taxmen -- Massimo */
  /* Ignore food if no square can be worked */
  have_square = FALSE;
  city_map_iterate(x, y) {
    if (can_place_worker_here(pcity, x, y)) {
      have_square = TRUE;
    }
  } city_map_iterate_end;
  if ((pcity->surplus[O_FOOD] >= 2 || !have_square)
      && is_city_option_set(pcity, CITYO_NEW_EINSTEIN)) {
    pcity->specialists[best_specialist(O_SCIENCE, pcity)]++;
  } else if ((pcity->surplus[O_FOOD] >= 2 || !have_square)
	     && is_city_option_set(pcity, CITYO_NEW_TAXMAN)) {
    pcity->specialists[best_specialist(O_GOLD, pcity)]++;
  } else {
    pcity->specialists[DEFAULT_SPECIALIST]++; /* or else city is !sane */
    auto_arrange_workers(pcity);
  }

  city_refresh(pcity);

  notify_player(powner, pcity->tile, E_CITY_GROWTH,
                   _("%s grows to size %d."), pcity->name, pcity->size);
  script_signal_emit("city_growth", 2,
		      API_TYPE_CITY, pcity, API_TYPE_INT, pcity->size);

  sanity_check_city(pcity);
  sync_cities();
}

/**************************************************************************
  Check whether the population can be increased or
  if the city is unable to support a 'settler'...
**************************************************************************/
static void city_populate(struct city *pcity)
{
  pcity->food_stock += pcity->surplus[O_FOOD];
  if (pcity->food_stock >= city_granary_size(pcity->size) 
     || city_rapture_grow(pcity)) {
    city_increase_size(pcity);
  } else if (pcity->food_stock < 0) {
    /* FIXME: should this depend on units with ability to build
     * cities or on units that require food in uppkeep?
     * I'll assume citybuilders (units that 'contain' 1 pop) -- sjolie
     * The above may make more logical sense, but in game terms
     * you want to disband a unit that is draining your food
     * reserves.  Hence, I'll assume food upkeep > 0 units. -- jjm
     */
    unit_list_iterate_safe(pcity->units_supported, punit) {
      if (unit_type(punit)->upkeep[O_FOOD] > 0 
          && !unit_flag(punit, F_UNDISBANDABLE)) {

	notify_player(city_owner(pcity), pcity->tile, E_UNIT_LOST,
			 _("Famine feared in %s, %s lost!"), 
			 pcity->name, unit_type(punit)->name);
 
        gamelog(GAMELOG_UNITLOSS, punit, NULL, "famine");
        wipe_unit(punit);

	pcity->food_stock = (city_granary_size(pcity->size)
			     * granary_savings(pcity)) / 100;
	return;
      }
    } unit_list_iterate_safe_end;
    if (pcity->size > 1) {
      notify_player(city_owner(pcity), pcity->tile, E_CITY_FAMINE,
		       _("Famine causes population loss in %s."),
		       pcity->name);
    } else {
      notify_player(city_owner(pcity), pcity->tile, E_CITY_FAMINE,
		       _("Famine destroys %s entirely."), pcity->name);
    }
    pcity->food_stock = (city_granary_size(pcity->size - 1)
			 * granary_savings(pcity)) / 100;
    city_reduce_size(pcity, 1);
  }
}

/**************************************************************************
...
**************************************************************************/
void advisor_choose_build(struct player *pplayer, struct city *pcity)
{
  struct ai_choice choice;

  /* See what AI has to say */
  ai_advisor_choose_building(pcity, &choice);
  if (choice.choice >= 0 && choice.choice < B_LAST) {
    struct city_production target = {.is_unit = FALSE,
				     .value = choice.choice};

    change_build_target(pplayer, pcity, target, E_IMP_AUTO);
    return;
  }

  /* Build the first thing we can think of (except a new palace). */
  impr_type_iterate(i) {
    if (can_build_improvement(pcity, i)
	&& !building_has_effect(i, EFT_CAPITAL_CITY)) {
      struct city_production target = {.is_unit = FALSE, .value = i};

      change_build_target(pplayer, pcity, target, E_IMP_AUTO);
      return;
    }
  } impr_type_iterate_end;
}

/**************************************************************************
  Examine the worklist and change the build target.  Return 0 if no
  targets are available to change to.  Otherwise return non-zero.  Has
  the side-effect of removing from the worklist any no-longer-available
  targets as well as the target actually selected, if any.
**************************************************************************/
static bool worklist_change_build_target(struct player *pplayer,
					 struct city *pcity)
{
  bool success = FALSE;
  int i;

  if (worklist_is_empty(&pcity->worklist))
    /* Nothing in the worklist; bail now. */
    return FALSE;

  i = 0;
  while (TRUE) {
    struct city_production target;

    /* What's the next item in the worklist? */
    if (!worklist_peek_ith(&pcity->worklist, &target, i))
      /* Nothing more in the worklist.  Ah, well. */
      break;

    i++;

    /* Sanity checks */
    if (target.is_unit &&
	!can_build_unit(pcity, get_unit_type(target.value))) {
      struct unit_type *ptarget = get_unit_type(target.value);
      struct unit_type *new_target = unit_upgrades_to(pcity, ptarget);

      /* Maybe we can just upgrade the target to what the city /can/ build. */
      if (new_target == U_NOT_OBSOLETED) {
	/* Nope, we're stuck.  Dump this item from the worklist. */
	notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
			 _("%s can't build %s from the worklist; "
			   "tech not yet available.  Postponing..."),
			 pcity->name, ptarget->name);
	script_signal_emit("unit_cant_be_built", 3,
			   API_TYPE_UNIT_TYPE, ptarget,
			   API_TYPE_CITY, pcity,
			   API_TYPE_STRING, "need_tech");
	continue;
      } else if (!can_eventually_build_unit(pcity, new_target)) {
	/* If the city can never build this unit or its descendants,
	 * drop it. */
	notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
			 _("%s can't build %s from the worklist.  "
			   "Purging..."),
			 pcity->name,
			 /* Yes, warn about the targets that's actually
			    in the worklist, not its obsolete-closure
			    new_target. */
			 ptarget->name);
	script_signal_emit("unit_cant_be_built", 3,
			   API_TYPE_UNIT_TYPE, ptarget,
			   API_TYPE_CITY, pcity,
			   API_TYPE_STRING, "never");
	/* Purge this worklist item. */
	worklist_remove(&pcity->worklist, i-1);
	/* Reset i to index to the now-next element. */
	i--;
	
	continue;
      } else {
	/* Yep, we can go after new_target instead.  Joy! */
	notify_player(pplayer, pcity->tile, E_WORKLIST,
			 _("Production of %s is upgraded to %s in %s."),
			 ptarget->name, 
			 new_target->name,
			 pcity->name);
	ptarget = new_target;
	target.value = new_target->index;
      }
    } else if (!target.is_unit
	       && !can_build_improvement(pcity, target.value)) {
      Impr_type_id new_target = building_upgrades_to(pcity, target.value);
      struct impr_type *ptarget = get_improvement_type(target.value);

      /* If the city can never build this improvement, drop it. */
      if (!can_eventually_build_improvement(pcity, new_target)) {
	/* Nope, never in a million years. */
	notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
			 _("%s can't build %s from the worklist.  "
			   "Purging..."),
			 pcity->name,
			 get_impr_name_ex(pcity, ptarget->index));
	script_signal_emit("building_cant_be_built", 3,
			   API_TYPE_BUILDING_TYPE, ptarget,
			   API_TYPE_CITY, pcity,
			   API_TYPE_STRING, "never");

	/* Purge this worklist item. */
	worklist_remove(&pcity->worklist, i-1);
	/* Reset i to index to the now-next element. */
	i--;
	
	continue;
      }


      /* Maybe this improvement has been obsoleted by something that
	 we can build. */
      if (new_target == target.value) {
	struct impr_type *building = get_improvement_type(target.value);
	bool known = FALSE;

	/* Nope, no use.  *sigh*  */
	requirement_vector_iterate(&building->reqs, preq) {
	  if (!is_req_active(pplayer, pcity, NULL, NULL, NULL, NULL, NULL,
			     preq)) {
	    known = TRUE;
	    switch (preq->source.type) {
	    case REQ_TECH:
	      notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
			       _("%s can't build %s from the worklist; "
				 "tech %s not yet available.  Postponing..."),
			       pcity->name,
			       get_impr_name_ex(pcity, building->index),
			       get_tech_name(pplayer,
					     preq->source.value.tech));
	      script_signal_emit("building_cant_be_built", 3,
				 API_TYPE_BUILDING_TYPE, building,
				 API_TYPE_CITY, pcity,
				 API_TYPE_STRING, "need_tech");
	      break;
	    case REQ_BUILDING:
	      notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
			       _("%s can't build %s from the worklist; "
				 "need to have %s first.  Postponing..."),
			       pcity->name,
			       get_impr_name_ex(pcity, building->index),
			       get_impr_name_ex(pcity,
						preq->source.value.building));
	      script_signal_emit("building_cant_be_built", 3,
				 API_TYPE_BUILDING_TYPE, building,
				 API_TYPE_CITY, pcity,
				 API_TYPE_STRING, "need_building");
	      break;
	    case REQ_GOV:
	      notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
			       _("%s can't build %s from the worklist; "
				 "it needs %s government.  Postponing..."),
			       pcity->name,
			       get_impr_name_ex(pcity, building->index),
			       get_government_name(preq->source.value.gov));
	      script_signal_emit("building_cant_be_built", 3,
				 API_TYPE_BUILDING_TYPE, building,
				 API_TYPE_CITY, pcity,
				 API_TYPE_STRING, "need_government");
	      break;
	    case REQ_SPECIAL:
	      notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
			       _("%s can't build %s from the worklist; "
				 "%s special is required.  Postponing..."),
			       pcity->name,
			       get_impr_name_ex(pcity, building->index),
			       get_special_name(preq->source.value.special));
	      script_signal_emit("building_cant_be_built", 3,
				 API_TYPE_BUILDING_TYPE, building,
				 API_TYPE_CITY, pcity,
				 API_TYPE_STRING, "need_special");
	      break;
	    case REQ_TERRAIN:
	      notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
			       _("%s can't build %s from the worklist; "
				 "%s terrain is required.  Postponing..."),
			       pcity->name,
			       get_impr_name_ex(pcity, building->index),
			       get_name(preq->source.value.terrain));
	      script_signal_emit("building_cant_be_built", 3,
				 API_TYPE_BUILDING_TYPE, building,
				 API_TYPE_CITY, pcity,
				 API_TYPE_STRING, "need_terrain");
	      break;
	    case REQ_NATION:
	      /* FIXME: we should skip rather than postpone, since we'll
	       * never be able to meet this req... */
	      notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
			       _("%s can't build %s from the worklist; "
				 "only %s may build this.  Postponing..."),
			       pcity->name,
			       get_impr_name_ex(pcity, building->index),
			       get_nation_name(preq->source.value.nation));
	      script_signal_emit("building_cant_be_built", 3,
				 API_TYPE_BUILDING_TYPE, building,
				 API_TYPE_CITY, pcity,
				 API_TYPE_STRING, "need_nation");
	      break;
	    case REQ_UNITTYPE:
	    case REQ_UNITFLAG:
	    case REQ_OUTPUTTYPE:
	    case REQ_SPECIALIST:
	      /* Will only happen with a bogus ruleset. */
	      break;
	    case REQ_MINSIZE:
	      notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
			       _("%s can't build %s from the worklist; "
				 "city must be of size %d.  Postponing..."),
			       pcity->name,
			       get_impr_name_ex(pcity, building->index),
			       preq->source.value.minsize);
	      script_signal_emit("building_cant_be_built", 3,
				 API_TYPE_BUILDING_TYPE, building,
				 API_TYPE_CITY, pcity,
				 API_TYPE_STRING, "need_minsize");
	      break;
	    case REQ_NONE:
	    case REQ_LAST:
	      assert(0);
	      break;
	    }
	    break;
	  }
	} requirement_vector_iterate_end;
	if (!known) {
	  /* This shouldn't happen...
	     FIXME: make can_build_improvement() return a reason enum. */
	  notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
			   _("%s can't build %s from the worklist; "
			     "Reason unknown!  Postponing..."),
			   pcity->name,
			   get_impr_name_ex(pcity, building->index));
	}
	continue;
      } else {
	/* Hey, we can upgrade the improvement!  */
	notify_player(pplayer, pcity->tile, E_WORKLIST,
			 _("Production of %s is upgraded to %s in %s."),
			 get_impr_name_ex(pcity, target.value), 
			 get_impr_name_ex(pcity, new_target),
			 pcity->name);
	target.value = new_target;
      }
    }

    /* All okay.  Switch targets. */
    change_build_target(pplayer, pcity, target, E_WORKLIST);

    success = TRUE;
    break;
  }

  if (success) {
    /* i is the index immediately _after_ the item we're changing to.
       Remove the (i-1)th item from the worklist. */
    worklist_remove(&pcity->worklist, i-1);
  }

  if (worklist_is_empty(&pcity->worklist)) {
    /* There *was* something in the worklist, but it's empty now.  Bug the
       player about it. */
    notify_player(pplayer, pcity->tile, E_WORKLIST,
		     _("%s's worklist is now empty."),
		     pcity->name);
  }

  return success;
}

/**************************************************************************
  Assuming we just finished building something, find something new to
  build.  The policy is: use the worklist if we can; if not, try not
  changing; if we must change, get desparate and use the AI advisor.
**************************************************************************/
static void choose_build_target(struct player *pplayer,
				struct city *pcity)
{
  /* Pick the next thing off the worklist. */
  if (worklist_change_build_target(pplayer, pcity)) {
    return;
  }

  /* Try building the same thing again.  Repeat building doesn't require a
   * call to change_build_target, so just return. */
  if (pcity->production.is_unit) {
    /* We can build a unit again unless it's unique. */
    if (!unit_type_flag(get_unit_type(pcity->production.value), F_UNIQUE)) {
      return;
    }
  } else if (can_build_improvement(pcity, pcity->production.value)) {
    /* We can build space and coinage again, and possibly others. */
    return;
  }

  /* Find *something* to do! */
  freelog(LOG_DEBUG, "Trying advisor_choose_build.");
  advisor_choose_build(pplayer, pcity);
  freelog(LOG_DEBUG, "Advisor_choose_build didn't kill us.");
}

/**************************************************************************
  Follow the list of replaced_by buildings until we hit something that
  we can build.  Returns -1 if we can't upgrade at all (including if the
  original building is unbuildable).
**************************************************************************/
static Impr_type_id building_upgrades_to(struct city *pcity, Impr_type_id id)
{
  Impr_type_id check = id, latest_ok = id;

  if (!can_build_improvement_direct(pcity, check)) {
    return -1;
  }
  while (improvement_exists(check
			    = get_improvement_type(check)->replaced_by)) {
    if (can_build_improvement_direct(pcity, check)) {
      latest_ok = check;
    }
  }
  if (latest_ok == id) {
    return -1; /* Can't upgrade */
  }

  return latest_ok;
}

/**************************************************************************
  Try to upgrade production in pcity.
**************************************************************************/
static void upgrade_building_prod(struct city *pcity)
{
  struct player *pplayer = city_owner(pcity);
  Impr_type_id upgrades_to = building_upgrades_to(pcity,
						  pcity->production.value);

  if (can_build_improvement(pcity, upgrades_to)) {
    notify_player(pplayer, pcity->tile, E_UNIT_UPGRADED,
		     _("Production of %s is upgraded to %s in %s."),
		     get_improvement_type(pcity->production.value)->name,
		     get_improvement_type(upgrades_to)->name,
		     pcity->name);
    pcity->production.value = upgrades_to;
  }
}

/**************************************************************************
  Follow the list of obsoleted_by units until we hit something that
  we can build.  Return id if we can't upgrade at all.  NB:  returning
  id doesn't guarantee that pcity really _can_ build id; just that
  pcity can't build whatever _obsoletes_ id.

  FIXME: this function is a duplicate of can_upgrade_unittype.
**************************************************************************/
static struct unit_type *unit_upgrades_to(struct city *pcity,
					  struct unit_type *punittype)
{
  struct unit_type *check = punittype, *latest_ok = punittype;

  if (!can_build_unit_direct(pcity, check)) {
    return U_NOT_OBSOLETED;
  }
  while ((check = check->obsoleted_by) != U_NOT_OBSOLETED) {
    if (can_build_unit_direct(pcity, check)) {
      latest_ok = check;
    }
  }
  if (latest_ok == punittype) {
    return U_NOT_OBSOLETED; /* Can't upgrade */
  }

  return latest_ok;
}

/**************************************************************************
  Try to upgrade production in pcity.
**************************************************************************/
static void upgrade_unit_prod(struct city *pcity)
{
  struct player *pplayer = city_owner(pcity);
  struct unit_type *id = get_unit_type(pcity->production.value);
  struct unit_type *id2 = unit_upgrades_to(pcity, id);

  if (id2 && can_build_unit_direct(pcity, id2)) {
    pcity->production.value = id2->index;
    notify_player(pplayer, pcity->tile, E_UNIT_UPGRADED, 
		  _("Production of %s is upgraded to %s in %s."),
		  id->name, id2->name, 
		  pcity->name);
  }
}

/**************************************************************************
  Disband units if we don't have enough shields to support them.  Returns
  FALSE if the _city_ is disbanded as a result.
**************************************************************************/
static bool city_distribute_surplus_shields(struct player *pplayer,
					    struct city *pcity)
{
  struct government *g = get_gov_pplayer(pplayer);

  if (pcity->surplus[O_SHIELD] < 0) {
    unit_list_iterate_safe(pcity->units_supported, punit) {
      if (utype_upkeep_cost(unit_type(punit), pplayer, g, O_SHIELD) > 0
	  && pcity->surplus[O_SHIELD] < 0
          && !unit_flag(punit, F_UNDISBANDABLE)) {
	notify_player(pplayer, pcity->tile, E_UNIT_LOST,
			 _("%s can't upkeep %s, unit disbanded."),
			 pcity->name, unit_type(punit)->name);
        handle_unit_disband(pplayer, punit->id);
	/* pcity->surplus[O_SHIELD] is automatically updated. */
      }
    } unit_list_iterate_safe_end;
  }

  if (pcity->surplus[O_SHIELD] < 0) {
    /* Special case: F_UNDISBANDABLE. This nasty unit won't go so easily.
     * It'd rather make the citizens pay in blood for their failure to upkeep
     * it! If we make it here all normal units are already disbanded, so only
     * undisbandable ones remain. */
    unit_list_iterate_safe(pcity->units_supported, punit) {
      int upkeep = utype_upkeep_cost(unit_type(punit), pplayer, g, O_SHIELD);

      if (upkeep > 0 && pcity->surplus[O_SHIELD] < 0) {
	assert(unit_flag(punit, F_UNDISBANDABLE));
	notify_player(pplayer, pcity->tile, E_UNIT_LOST,
			 _("Citizens in %s perish for their failure to "
			 "upkeep %s!"), pcity->name, unit_type(punit)->name);
	if (!city_reduce_size(pcity, 1)) {
	  return FALSE;
	}

	/* No upkeep for the unit this turn. */
	pcity->surplus[O_SHIELD] += upkeep;
      }
    } unit_list_iterate_safe_end;
  }

  /* Now we confirm changes made last turn. */
  pcity->shield_stock += pcity->surplus[O_SHIELD];
  pcity->before_change_shields = pcity->shield_stock;
  pcity->last_turns_shield_surplus = pcity->surplus[O_SHIELD];

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
static bool city_build_building(struct player *pplayer, struct city *pcity)
{
  bool space_part;
  int mod;
  Impr_type_id id = pcity->production.value;
  struct impr_type *building = get_improvement_type(id);

  if (get_current_construction_bonus(pcity, EFT_PROD_TO_GOLD) > 0) {
    assert(pcity->surplus[O_SHIELD] >= 0);
    /* pcity->before_change_shields already contains the surplus from
     * this turn. */
    pplayer->economic.gold += pcity->before_change_shields;
    pcity->before_change_shields = 0;
    pcity->shield_stock = 0;
    choose_build_target(pplayer, pcity);
  }
  upgrade_building_prod(pcity);
  if (!can_build_improvement(pcity, id)) {
    notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
		     _("%s is building %s, which "
		       "is no longer available."),
		     pcity->name, get_impr_name_ex(pcity, id));
    script_signal_emit("building_cant_be_built", 3,
		       API_TYPE_BUILDING_TYPE, building,
		       API_TYPE_CITY, pcity,
		       API_TYPE_STRING, "unavailable");
    return TRUE;
  }
  if (pcity->shield_stock >= impr_build_shield_cost(id)) {
    if (is_small_wonder(id)) {
      city_list_iterate(pplayer->cities, wcity) {
	if (city_got_building(wcity, id)) {
	  city_remove_improvement(wcity, id);
	  break;
	}
      } city_list_iterate_end;
    }

    space_part = TRUE;
    if (get_current_construction_bonus(pcity, EFT_SS_STRUCTURAL) > 0) {
      pplayer->spaceship.structurals++;
    } else if (get_current_construction_bonus(pcity, EFT_SS_COMPONENT) > 0) {
      pplayer->spaceship.components++;
    } else if (get_current_construction_bonus(pcity, EFT_SS_MODULE) > 0) {
      pplayer->spaceship.modules++;
    } else {
      space_part = FALSE;
      city_add_improvement(pcity, id);
    }
    pcity->before_change_shields -= impr_build_shield_cost(id);
    pcity->shield_stock -= impr_build_shield_cost(id);
    pcity->turn_last_built = game.info.turn;
    /* to eliminate micromanagement */
    if (is_great_wonder(id)) {
      game.info.great_wonders[id] = pcity->id;

      notify_player(NULL, pcity->tile, E_WONDER_BUILD,
		       _("The %s have finished building %s in %s."),
		       get_nation_name_plural(pplayer->nation),
		       get_impr_name_ex(pcity, id),
		       pcity->name);

    } else if (is_small_wonder(id)) {
      pplayer->small_wonders[id] = pcity->id;
    }

    /* TODO: if wonders become just-another-building, remove this */
    if (is_great_wonder(id)) {
      gamelog(GAMELOG_WONDER, pcity);
    } else {
      gamelog(GAMELOG_BUILD, pcity);
    }

    notify_player(pplayer, pcity->tile, E_IMP_BUILD,
		     _("%s has finished building %s."), pcity->name,
		     get_improvement_name(id));
    script_signal_emit("building_built", 2,
		       API_TYPE_BUILDING_TYPE, get_improvement_type(id),
		       API_TYPE_CITY, pcity);


    if ((mod = get_current_construction_bonus(pcity, EFT_GIVE_IMM_TECH))) {
      int i;

      notify_player(pplayer, NULL, E_TECH_GAIN,
		    PL_("%s boosts research; you gain %d immediate advance.",
			"%s boosts research; you gain %d immediate advances.",
			mod),
		    get_improvement_name(id), mod);

      for (i = 0; i < mod; i++) {
	Tech_type_id tech = give_immediate_free_tech(pplayer);

	notify_embassies(pplayer, NULL, NULL, E_TECH_GAIN,
	    _("The %s have acquired %s from %s."),
	    get_nation_name_plural(pplayer->nation),
	    get_tech_name(pplayer, tech),
	    get_improvement_name(id));
      }
    }
    if (space_part && pplayer->spaceship.state == SSHIP_NONE) {
      notify_player(NULL, pcity->tile, E_SPACESHIP,
		       _("The %s have started "
			 "building a spaceship!"),
		       get_nation_name_plural(pplayer->nation));
      pplayer->spaceship.state = SSHIP_STARTED;
    }
    if (space_part) {
      send_spaceship_info(pplayer, NULL);
    } else {
      city_refresh(pcity);
    }

    /* Move to the next thing in the worklist */
    choose_build_target(pplayer, pcity);
  }

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
static bool city_build_unit(struct player *pplayer, struct city *pcity)
{
  struct unit_type *utype;

  upgrade_unit_prod(pcity);
  utype = get_unit_type(pcity->production.value);

  /* We must make a special case for barbarians here, because they are
     so dumb. Really. They don't know the prerequisite techs for units
     they build!! - Per */
  if (!can_build_unit_direct(pcity, utype)
      && !is_barbarian(pplayer)) {
    notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
        _("%s is building %s, which is no longer available."),
        pcity->name, unit_name(utype));
    script_signal_emit("unit_cant_be_built", 3,
		       API_TYPE_UNIT_TYPE, utype,
		       API_TYPE_CITY, pcity,
		       API_TYPE_STRING, "unavailable");
    freelog(LOG_VERBOSE, _("%s's %s tried build %s, which is not available"),
            pplayer->name, pcity->name, unit_name(utype));
    return TRUE;
  }
  if (pcity->shield_stock >= unit_build_shield_cost(utype)) {
    int pop_cost = unit_pop_value(utype);
    struct unit *punit;

    /* Should we disband the city? -- Massimo */
    if (pcity->size == pop_cost
	&& is_city_option_set(pcity, CITYO_DISBAND)) {
      return !disband_city(pcity);
    }

    if (pcity->size <= pop_cost) {
      notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
		       _("%s can't build %s yet."),
		       pcity->name, unit_name(utype));
      script_signal_emit("unit_cant_be_built", 3,
			 API_TYPE_UNIT_TYPE, utype,
			 API_TYPE_CITY, pcity,
			 API_TYPE_STRING, "pop_cost");
      return TRUE;
    }

    assert(pop_cost == 0 || pcity->size >= pop_cost);

    /* don't update turn_last_built if we returned above */
    pcity->turn_last_built = game.info.turn;

    punit = create_unit(pplayer, pcity->tile, utype,
			do_make_unit_veteran(pcity, utype),
			pcity->id, 0);

    /* After we created the unit remove the citizen. This will also
       rearrange the worker to take into account the extra resources
       (food) needed. */
    if (pop_cost > 0) {
      city_reduce_size(pcity, pop_cost);
    }

    /* to eliminate micromanagement, we only subtract the unit's
       cost */
    pcity->before_change_shields -= unit_build_shield_cost(utype);
    pcity->shield_stock -= unit_build_shield_cost(utype);

    notify_player(pplayer, pcity->tile, E_UNIT_BUILT,
		     /* TRANS: <city> is finished building <unit/building>. */
		     _("%s is finished building %s."),
		     pcity->name,
		     get_unit_type(pcity->production.value)->name);

    script_signal_emit("unit_built",
		       2, API_TYPE_UNIT, punit, API_TYPE_CITY, pcity);
    gamelog(GAMELOG_BUILD, pcity);

    /* Done building this unit; time to move on to the next. */
    choose_build_target(pplayer, pcity);
  }
  return TRUE;
}

/**************************************************************************
return 0 if the city is removed
return 1 otherwise
**************************************************************************/
static bool city_build_stuff(struct player *pplayer, struct city *pcity)
{
  if (!city_distribute_surplus_shields(pplayer, pcity)) {
    return FALSE;
  }

  nullify_caravan_and_disband_plus(pcity);
  define_orig_production_values(pcity);

  if (!pcity->production.is_unit) {
    return city_build_building(pplayer, pcity);
  } else {
    return city_build_unit(pplayer, pcity);
  }
}

/**************************************************************************
...
**************************************************************************/
static void pay_for_buildings(struct player *pplayer, struct city *pcity)
{
  built_impr_iterate(pcity, i) {
    if (can_city_sell_building(pcity, i)
	&& pplayer->government != game.government_when_anarchy) {
      if (pplayer->economic.gold - improvement_upkeep(pcity, i) < 0) {
	notify_player(pplayer, pcity->tile, E_IMP_AUCTIONED,
			 _("Can't afford to maintain %s in %s, "
			   "building sold!"),
			 get_improvement_name(i), pcity->name);
	do_sell_building(pplayer, pcity, i);
	city_refresh(pcity);
      } else
	pplayer->economic.gold -= improvement_upkeep(pcity, i);
    }
  } built_impr_iterate_end;
}

/**************************************************************************
 Add some Pollution if we have waste
**************************************************************************/
static void check_pollution(struct city *pcity)
{
  int k=100;
  if (pcity->pollution != 0 && myrand(100) <= pcity->pollution) {
    while (k > 0) {
      /* place pollution somewhere in city radius */
      int cx = myrand(CITY_MAP_SIZE);
      int cy = myrand(CITY_MAP_SIZE);
      struct tile *ptile;

      /* if is a corner tile or not a real map position */
      if (!is_valid_city_coords(cx, cy)
	  || !(ptile = city_map_to_map(pcity, cx, cy))) {
	continue;
      }

      if (!terrain_has_flag(tile_get_terrain(ptile), TER_NO_POLLUTION)
	  && !tile_has_special(ptile, S_POLLUTION)) {
	tile_set_special(ptile, S_POLLUTION);
	update_tile_knowledge(ptile);
	notify_player(city_owner(pcity), pcity->tile,
			 E_POLLUTION, _("Pollution near %s."),
			 pcity->name);
	return;
      }
      k--;
    }
    freelog(LOG_DEBUG, "pollution not placed: city: %s", pcity->name);
  }
}

/**************************************************************************
  Returns the cost to incite a city. This depends on the size of the city,
  the number of happy, unhappy and angry citizens, whether it is
  celebrating, how close it is to the capital, how many units it has and
  upkeeps, presence of courthouse and its buildings and wonders.
**************************************************************************/
int city_incite_cost(struct player *pplayer, struct city *pcity)
{
  struct government *g = get_gov_pcity(pcity);
  struct city *capital;
  int dist, size, cost;

  if (get_city_bonus(pcity, EFT_NO_INCITE) > 0) {
    return INCITE_IMPOSSIBLE_COST;
  }

  /* Gold factor */
  cost = city_owner(pcity)->economic.gold + game.info.base_incite_cost;

  unit_list_iterate(pcity->tile->units, punit) {
    cost += (unit_build_shield_cost(punit->type)
	     * game.info.incite_unit_factor);
  } unit_list_iterate_end;

  /* Buildings */
  built_impr_iterate(pcity, i) {
    cost += impr_build_shield_cost(i) * game.info.incite_improvement_factor;
  } built_impr_iterate_end;

  /* Stability bonuses */
  if (g != game.government_when_anarchy) {
    if (!city_unhappy(pcity)) {
      cost *= 2;
    }
    if (city_celebrating(pcity)) {
      cost *= 2;
    }
  }

  /* City is empty */
  if (unit_list_size(pcity->tile->units) == 0) {
    cost /= 2;
  }

  /* Buy back is cheap, conquered cities are also cheap */
  if (pcity->owner != pcity->original) {
    if (pplayer == pcity->original) {
      cost /= 2;            /* buy back: 50% price reduction */
    } else {
      cost = cost * 2 / 3;  /* buy conquered: 33% price reduction */
    }
  }

  /* Distance from capital */
  capital = find_palace(city_owner(pcity));
  if (capital) {
    int tmp = map_distance(capital->tile, pcity->tile);
    dist = MIN(32, tmp);
  } else {
    /* No capital? Take max penalty! */
    dist = 32;
  }

  if (g->waste[O_TRADE].fixed_distance != 0) {
    dist = MIN(g->waste[O_TRADE].fixed_distance, dist);
  }

  size = MAX(1, pcity->size
                + pcity->ppl_happy[4]
                - pcity->ppl_unhappy[4]
                - pcity->ppl_angry[4] * 3);
  cost *= size;
  cost *= game.info.incite_total_factor;
  cost = cost / (dist + 3);
  
  cost += (cost * get_city_bonus(pcity, EFT_INCITE_COST_PCT)) / 100;
  cost /= 100;

  return cost;
}

/**************************************************************************
 Called every turn, at beginning of turn, for every city.
**************************************************************************/
static void define_orig_production_values(struct city *pcity)
{
  /* Remember what this city is building last turn, so that on the next turn
   * the player can switch production to something else and then change it
   * back without penalty.  This has to be updated _before_ production for
   * this turn is calculated, so that the penalty will apply if the player
   * changes production away from what has just been completed.  This makes
   * sense if you consider what this value means: all the shields in the
   * city have been dedicated toward the project that was chosen last turn,
   * so the player shouldn't be penalized if the governor has to pick
   * something different.  See city_change_production_penalty(). */
  pcity->changed_from.value = pcity->production.value;
  pcity->changed_from.is_unit = pcity->production.is_unit;

  freelog(LOG_DEBUG,
	  "In %s, building %s.  Beg of Turn shields = %d",
	  pcity->name,
	  pcity->changed_from.is_unit
	  ? get_unit_type(pcity->changed_from.value)->name
	  : get_improvement_name(pcity->changed_from.value),
	  pcity->before_change_shields);
}

/**************************************************************************
...
**************************************************************************/
static void nullify_caravan_and_disband_plus(struct city *pcity)
{
  pcity->disbanded_shields=0;
  pcity->caravan_shields=0;
}

/**************************************************************************
...
**************************************************************************/
void nullify_prechange_production(struct city *pcity)
{
  nullify_caravan_and_disband_plus(pcity);
  pcity->before_change_shields=0;
}

/**************************************************************************
 Called every turn, at end of turn, for every city.
**************************************************************************/
static void update_city_activity(struct player *pplayer, struct city *pcity)
{
  struct government *g = get_gov_pcity(pcity);

  city_refresh(pcity);

  /* reporting of celebrations rewritten, copying the treatment of disorder below,
     with the added rapture rounds count.  991219 -- Jing */
  if (city_build_stuff(pplayer, pcity)) {
    if (city_celebrating(pcity)) {
      pcity->rapture++;
      if (pcity->rapture == 1)
	notify_player(pplayer, pcity->tile, E_CITY_LOVE,
			 _("We Love The %s Day celebrated in %s."), 
			 get_ruler_title(pplayer->government, pplayer->is_male,
					 pplayer->nation),
			 pcity->name);
    }
    else {
      if (pcity->rapture != 0)
	notify_player(pplayer, pcity->tile, E_CITY_NORMAL,
			 _("We Love The %s Day canceled in %s."),
			 get_ruler_title(pplayer->government, pplayer->is_male,
					 pplayer->nation),
			 pcity->name);
      pcity->rapture=0;
    }
    pcity->was_happy=city_happy(pcity);

    /* City population updated here, after the rapture stuff above. --Jing */
    {
      int id=pcity->id;
      city_populate(pcity);
      if(!player_find_city_by_id(pplayer, id))
	return;
    }

    pcity->is_updated=TRUE;

    pcity->did_sell=FALSE;
    pcity->did_buy = FALSE;
    pcity->airlift = (get_city_bonus(pcity, EFT_AIRLIFT) > 0);
    update_tech(pplayer, pcity->prod[O_SCIENCE]);
    pplayer->economic.gold+=pcity->prod[O_GOLD];
    pay_for_units(pplayer, pcity);
    pay_for_buildings(pplayer, pcity);

    if(city_unhappy(pcity)) { 
      pcity->anarchy++;
      if (pcity->anarchy == 1) 
        notify_player(pplayer, pcity->tile, E_CITY_DISORDER,
	  	         _("Civil disorder in %s."), pcity->name);
      else
        notify_player(pplayer, pcity->tile, E_CITY_DISORDER,
		         _("CIVIL DISORDER CONTINUES in %s."),
			 pcity->name);
    }
    else {
      if (pcity->anarchy != 0)
        notify_player(pplayer, pcity->tile, E_CITY_NORMAL,
	  	         _("Order restored in %s."), pcity->name);
      pcity->anarchy=0;
    }
    check_pollution(pcity);

    send_city_info(NULL, pcity);
    if (pcity->anarchy>2 
        && get_player_bonus(pplayer, EFT_REVOLUTION_WHEN_UNHAPPY) > 0) {
      notify_player(pplayer, pcity->tile, E_ANARCHY,
		       _("The people have overthrown your %s, "
			 "your country is in turmoil."),
		       get_government_name(g));
      handle_player_change_government(pplayer, g->index);
    }
    sanity_check_city(pcity);
  }
}

/**************************************************************************
 Disband a city into the built unit, supported by the closest city.
**************************************************************************/
static bool disband_city(struct city *pcity)
{
  struct player *pplayer = city_owner(pcity);
  struct tile *ptile = pcity->tile;
  struct city *rcity=NULL;
  struct unit_type *utype = get_unit_type(pcity->production.value);

  /* find closest city other than pcity */
  rcity = find_closest_owned_city(pplayer, ptile, FALSE, pcity);

  if (!rcity) {
    /* What should we do when we try to disband our only city? */
    notify_player(pplayer, ptile, E_CITY_CANTBUILD,
		     _("%s can't build %s yet, "
		     "and we can't disband our only city."),
		     pcity->name, unit_name(utype));
    script_signal_emit("unit_cant_be_built", 3,
		       API_TYPE_UNIT_TYPE, utype,
		       API_TYPE_CITY, pcity,
		       API_TYPE_STRING, "pop_cost");
    return FALSE;
  }

  (void) create_unit(pplayer, ptile, utype,
		     do_make_unit_veteran(pcity, utype),
		     pcity->id, 0);

  /* Shift all the units supported by pcity (including the new unit)
   * to rcity.  transfer_city_units does not make sure no units are
   * left floating without a transport, but since all units are
   * transferred this is not a problem. */
  transfer_city_units(pplayer, pplayer, pcity->units_supported, rcity, 
                      pcity, -1, TRUE);

  notify_player(pplayer, ptile, E_UNIT_BUILT,
		   /* TRANS: Settler production leads to disbanded city. */
		   _("%s is disbanded into %s."), 
		   pcity->name,
		   get_unit_type(pcity->production.value)->name);
  gamelog(GAMELOG_DISBANDCITY, pcity);

  remove_city(pcity);
  return TRUE;
}
