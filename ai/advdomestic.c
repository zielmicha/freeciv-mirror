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
#include <aitools.h>
#include <map.h>
#include <game.h>
#include <unit.h>
#include <citytools.h>

struct move_cost_map {
  unsigned char cost[MAP_MAX_WIDTH][MAP_MAX_HEIGHT];
  struct city *warcity; /* so we know what we're dealing with here */
};

extern struct move_cost_map warmap; /* useful for caravans, I assure you -- Syela */

/********************************************************************** 
... this function should assign a value to choice and want and type, where 
    want is a value between 1 and 100.
    if want is 0 this advisor doesn't want anything
    type = 1 means unit, type = 0 means building
***********************************************************************/

int ai_best_tile_value(struct city *pcity)
{
  int x, y, bx, by, food, best, cur;

/* food = (pcity->size *2 -get_food_tile(2,2, pcity)) + settler_eats(pcity); */
  food = 0; /* simply works better as far as I can tell */
  do {
    bx=0;
    by=0;
    best = 0;
    city_map_iterate(x, y) {
      if(can_place_worker_here(pcity, x, y)) {
        if ((cur = city_tile_value(pcity, x, y, food, 0)) > best) {
          bx=x;
          by=y;
          best = cur;
        }
      }
    }
  } while(0);
  if (bx || by)
     return(best);
  return 0;
}

int building_value(int max, struct city *pcity, int val)
{
  int i, j = 0;
  int elvis = pcity->ppl_elvis;
  int sad = pcity->ppl_unhappy[0]; /* yes, I'm sure about that! */
  int bored = pcity->ppl_content[4]; /* this I'm not sure of anymore */

  i = pcity->ppl_unhappy[3] - pcity->ppl_unhappy[2];
  sad += i; /* if units are making us unhappy, count that too. */
/*  printf("In %s, unh[0] = %d, unh[4] = %d, sad = %d\n",
       pcity->name, pcity->ppl_unhappy[0], pcity->ppl_unhappy[4], sad); */

  i = max;
  while (i && elvis) { i--; elvis--; j += val; }
  while (i && sad) { i--; sad--; j += 16; }
  i -= MIN(pcity->ppl_content[0], i); /* prevent the happy overdose */
  while (i && bored) { i--; bored--; j += 16; } /* 16 is debatable value */
/*  printf("%s: %d elvis %d sad %d bored %d size %d max %d val\n",
    pcity->name, pcity->ppl_elvis, pcity->ppl_unhappy[4],
    pcity->ppl_content[4], pcity->size, max, j);  */

/* should be better now.  Problem was that the AI had MICHELANGELO keeping
everyone content, and then built BACH anyway.  Now the AI should know that
it has a buffer before it suffers unhappiness and not build BACH -- Syela */

  return(j);
}

int ocean_workers(struct city *pcity)
{
  int x, y, i = 0;
  city_map_iterate(x, y) {
    if (map_get_tile(pcity->x+x-2, pcity->y+y-2)->terrain == T_OCEAN) {
      i++; /* this is a kluge; wasn't getting enough harbors because
often everyone was stuck farming grassland. */
      if (is_worker_here(pcity, x, y)) i++;
    }
  }
  return(i / 2);
}

int railroad_trade(struct city *pcity)
{
  int x, y, i = 0; 
  city_map_iterate(x, y) {
    if (is_worker_here(pcity, x, y)) {
      if (map_get_special(pcity->x+x-2, pcity->y+y-2) & S_RAILROAD) i++;
    }
  }  
  return(i); 
}

int farmland_food(struct city *pcity)
{
  int x, y, i = 0; 
  city_map_iterate(x, y) {
    if (is_worker_here(pcity, x, y)) {
      if (map_get_special(pcity->x+x-2, pcity->y+y-2) & S_IRRIGATION) i++;
    }
  }  
  return(i); 
}

void ai_eval_buildings(struct city *pcity)
{
  int i, gov, tech, val, a, t, food, j, k, hunger, def, danger, bar;
  int tax, prod, sci, values[B_LAST];
  struct player *plr = city_owner(pcity);
  int shield_weighting[3] = { 11, 13, 15 };
  int food_weighting[3] = { 15, 14, 13 };
  
  a = get_race(city_owner(pcity))->attack;
  t = pcity->ai.trade_want; /* trade_weighting */
  sci = (pcity->trade_prod * plr->economic.science + 50) / 100;
  tax = pcity->trade_prod - sci;
  sci *= t;
  tax *= t;
  prod = pcity->shield_prod * 100 / city_shield_bonus(pcity) * shield_weighting[a];
  val = ai_best_tile_value(pcity);
  food = food_weighting[a] * 4 / pcity->size;  
  i = (pcity->size *2 - get_food_tile(2,2, pcity)) + settler_eats(pcity);
  i -= pcity->food_prod; /* amazingly left out for a week! -- Syela */
  if (i > 0 && !pcity->ppl_scientist) hunger = i + 1; else hunger = 1;

  gov = get_government(pcity->owner);
  tech = (plr->research.researching != A_NONE);

  for (i=0;i<B_LAST;i++) {
    values[i]=0;
  } /* rewrite by Syela - old values seemed very random */

/* the can_build_improvement part of this might be a waste of CPU cycles -- Syela */

  if (can_build_improvement(pcity, B_AQUEDUCT)) {
    values[B_AQUEDUCT] = food * (pcity->food_surplus + 2 * pcity->ppl_scientist) /
                         (9 - MIN(8, pcity->size)); /* guessing about food if we did farm */
    values[B_AQUEDUCT] *= 2; /* guessing about value of loving the president */
    if (city_happy(pcity)) values[B_AQUEDUCT] = (pcity->size * 
              (city_got_building(pcity,B_GRANARY) ? 3 : 2) *
              game.foodbox / 2 - pcity->food_stock) * food / (9 - pcity->size);
  }


  if (can_build_improvement(pcity, B_BANK))
    values[B_BANK] = tax / 2;
  
  j = 0; k = 0;
  city_list_iterate(plr->cities, acity)
    if (acity->is_building_unit) {
      if (!unit_flag(acity->currently_building, F_NONMIL))
        j += prod;
      else if (unit_flag(acity->currently_building, F_CARAVAN) &&
        built_elsewhere(acity, B_SUNTZU)) j += prod; /* this also stops flip-flops */
    } else if (acity->currently_building == B_BARRACKS || /* this stops flip-flops */
             acity->currently_building == B_BARRACKS2 ||
             acity->currently_building == B_BARRACKS3 ||
             acity->currently_building == B_SUNTZU) j += prod;
    k++;
  city_list_iterate_end;
  if (!k) printf("Gonna crash, 0 k, looking at %s (ai_eval_buildings)\n", pcity->name);
  /* rationale: barracks effectively double prod while building military units */
  /* if half our production is military, effective gain is 1/2 city prod */
  bar = j / k;

  if (!built_elsewhere(pcity, B_SUNTZU)) {
    if (can_build_improvement(pcity, B_BARRACKS))
      values[B_BARRACKS] = bar;
    if (can_build_improvement(pcity, B_BARRACKS2))
      values[B_BARRACKS2] = bar;
    if (can_build_improvement(pcity, B_BARRACKS3))
      values[B_BARRACKS3] = bar;
  }

  if (can_build_improvement(pcity, B_CATHEDRAL) && !built_elsewhere(pcity, B_MICHELANGELO))
    values[B_CATHEDRAL] = building_value(get_cathedral_power(pcity), pcity, val);

  def = assess_defense(pcity); /* not in the if so B_WALL can check them */
/* old wall code depended on danger, was a CPU hog and didn't really work anyway */
/* it was so stupid, AI wouldn't start building walls until it was in danger */
/* and it would have no chance to finish them before it was too late */

  if (can_build_improvement(pcity, B_CITY) && !built_elsewhere(pcity, B_WALL))
    values[B_CITY] = 40; /* WAG */

  if (can_build_improvement(pcity, B_COLOSSEUM))
    values[B_COLOSSEUM] = building_value(get_colosseum_power(pcity), pcity, val);
  
  if (can_build_improvement(pcity, B_COURTHOUSE)) {
    values[B_COURTHOUSE] = pcity->corruption * t / 2;
    if (gov == G_DEMOCRACY) values[B_COLOSSEUM] += building_value(1, pcity, val);
  }
  
  if (can_build_improvement(pcity, B_FACTORY))
    values[B_FACTORY] = prod / 2;
  
  if (can_build_improvement(pcity, B_GRANARY) && !built_elsewhere(pcity, B_PYRAMIDS))
    values[B_GRANARY] = food * pcity->food_surplus;
  
  if (can_build_improvement(pcity, B_HARBOUR))
    values[B_HARBOUR] = food * ocean_workers(pcity) * hunger;

  if (can_build_improvement(pcity, B_HYDRO) && !built_elsewhere(pcity, B_HOOVER))
    values[B_HYDRO] = prod / 2;

  if (can_build_improvement(pcity, B_LIBRARY))
    values[B_LIBRARY] = sci / 2;

  if (can_build_improvement(pcity, B_MARKETPLACE))
    values[B_MARKETPLACE] = tax / 2;

  if (can_build_improvement(pcity, B_MFG))
    values[B_MFG] = prod / 2;

  if (can_build_improvement(pcity, B_NUCLEAR))
    values[B_NUCLEAR] = prod / 2;

  if (can_build_improvement(pcity, B_OFFSHORE))
    values[B_OFFSHORE] = ocean_workers(pcity) * shield_weighting[a];

  if (can_build_improvement(pcity, B_POWER))
    values[B_POWER] =  prod / 2;

  if (can_build_improvement(pcity, B_RESEARCH) && !built_elsewhere(pcity, B_SETI))
    values[B_RESEARCH] = sci / 2;
  
  if (can_build_improvement(pcity, B_SEWER)) {
    values[B_SEWER] = food * (pcity->food_surplus + 2 * pcity->ppl_scientist) /
                      (13 - MIN(12, pcity->size)); /* guessing about food if we did farm */
    values[B_SEWER] *= 3; /* guessing about value of loving the president */
    if (city_happy(pcity)) values[B_SEWER] = (pcity->size *
              (city_got_building(pcity,B_GRANARY) ? 3 : 2) * 
              game.foodbox / 2 - pcity->food_stock) * food / (13 - pcity->size); 
  }

  if (can_build_improvement(pcity, B_STOCK))
    values[B_STOCK] = tax / 2;

  if (can_build_improvement(pcity, B_SUPERHIGHWAYS))
    values[B_SUPERHIGHWAYS] = railroad_trade(pcity) * t;

  if (can_build_improvement(pcity, B_SUPERMARKET))
    values[B_SUPERMARKET] = farmland_food(pcity) * t * hunger;

  if (can_build_improvement(pcity, B_TEMPLE))
    values[B_TEMPLE] = building_value(get_temple_power(pcity), pcity, val);

  if (can_build_improvement(pcity, B_UNIVERSITY))
    values[B_UNIVERSITY] = sci / 2;

/* ignored: AIRPORT, BARRACKS, COASTAL, MASS, PALACE, POLICE, PORT, */
/* RECYCLING, SAM, SDI, and any effects of pollution. -- Syela */
/* military advisor will deal with CITY */

  for (i = 0; i < B_LAST; i++) {
    if (is_wonder(i) && can_build_improvement(pcity, i) && !wonder_obsolete(i)) {
      if (i == B_ASMITHS)
        for (j = 0; j < B_LAST; j++)
          if (city_got_building(pcity, j) && improvement_upkeep(pcity, j) == 1)
            values[i] += t;
      if (i == B_COLLOSSUS)
        values[i] = (pcity->size + 1) * t; /* probably underestimates the value */
      if (i == B_COPERNICUS)
        values[i] = sci / 2;
      if (i == B_CURE)
        values[i] = building_value(1, pcity, val);
      if (i == B_DARWIN) /* this is a one-time boost, not constant */
        values[i] = (research_time(plr) * 2 + game.techlevel) * t -
                    400 * shield_weighting[a]; /* rough estimate at best */
      if (i == B_GREAT) /* basically (100 - techcost)% of a free tech per turn */
        values[i] = (research_time(plr) * (100 - game.techcost)) * t / 100 *
                    (game.nplayers - 2) / (game.nplayers); /* guessing */

      if (i == B_WALL)
        values[B_WALL] = 40; /* WAG */

      if (i == B_HANGING) /* will add the global effect to this. */
        values[i] = building_value(3, pcity, val) -
                    building_value(1, pcity, val);
      if (i == B_HOOVER && !city_got_building(pcity, B_HYDRO))
        values[i] = prod / 2; /* has to be this way or city will flip-flop */
      if (i == B_ISAAC)
        values[i] = sci;
      if (i == B_LEONARDO) {
        unit_list_iterate(pcity->units_supported, punit)
          j = can_upgrade_unittype(plr, punit->type);
          if (j >= 0) values[i] = MAX(values[i], 8 * unit_upgrade_price(plr,
               punit->type, j)); /* this is probably wrong -- Syela */
        unit_list_iterate_end;
      }
      if (i == B_BACH)
        values[i] = building_value(2, pcity, val);
      if (i == B_RICHARDS)
        values[i] = (pcity->size + 1) * shield_weighting[a];
      if (i == B_MICHELANGELO && !city_got_building(pcity, B_CATHEDRAL))
        values[i] = building_value(get_cathedral_power(pcity), pcity, val);
      if (i == B_ORACLE)
        if (city_got_building(pcity, B_TEMPLE))
          values[i] = building_value(get_temple_power(pcity), pcity, val);
      if (i == B_PYRAMIDS && !city_got_building(pcity, B_GRANARY))
        values[i] = food * pcity->food_surplus; /* different tech req's */
      if (i == B_SETI && !city_got_building(pcity, B_RESEARCH))
        values[i] = sci / 2;
      if (i == B_SHAKESPEARE)
        values[i] = building_value(pcity->size, pcity, val);
      if (i == B_SUNTZU)
        values[i] = bar;
      if (i == B_WOMENS) {
        unit_list_iterate(pcity->units_supported, punit)
          if (punit->unhappiness) values[i] += t * 2;
        unit_list_iterate_end;
      }
/* ignoring APOLLO, LIGHTHOUSE, MAGELLAN, MANHATTEN, STATUE, UNITED */
    }
  }

  for (i=0;i<B_LAST;i++) {
/*    if (values[i]) printf("%s wants %s with desire %d.\n", pcity->name,
        get_improvement_name(i), values[i]); */
    if (!is_wonder(i)) values[i] -= improvement_upkeep(pcity, i) * t;
    values[i] *= 100;
    j = improvement_value(i);
/*    if (pcity->currently_building == i && !pcity->is_building_unit) {
      j -= pcity->shield_stock;
      if (j < 1) j == 1;
    } commented out for now -- Syela */
    pcity->ai.building_want[i] = values[i] / j;
  }
  pcity->ai.building_want[B_DARWIN] -= 100;

  return;
}  

void domestic_advisor_choose_build(struct player *pplayer, struct city *pcity,
				   struct ai_choice *choice)
{
  int expand_factor[3] = {1,3,8};
  int expand_cities [3] = {3, 5, 7};
  int set, con, work, cit, i, want;
  struct ai_choice cur;

  choice->choice = 0;
  choice->want   = 0;
  choice->type   = 0;
  set = city_get_settlers(pcity);
  con = map_get_continent(pcity->x, pcity->y); 
  cit = get_cities_on_island(pplayer, con);
  work = pplayer->ai.island_data[con].workremain; /* usually = cit */
  if (!set && pcity->food_surplus >= settler_eats(pcity)) { /* settlers are an option */
    set = get_settlers_on_island(pplayer, con);
    work -= set;
    work -= expand_factor[get_race(pplayer)->expand];
    if (work < 0) work = 0;
/* if we have 5 cities, 1 settler, and expand_factor = 3, work = 1 */
    want = work * 20;
    cit -= expand_cities[get_race(pplayer)->expand];
    if (cit < 0) want = 101; /* if we need to expand, just expand */
/* if we NEED a military unit, that takes precedence anyway */
/* was not getting enough settlers after expand_cities reached, so ... */
/* if 7 cities, expand = 2, cit will = 0. */
    else want += 100 - (pcity->size + cit) * 10;
    if (want > 0) {
/*      printf("%s desires settlers with passion %d\n", pcity->name, want); */
      if (can_build_unit(pcity, U_ENGINEERS)) choice->choice = U_ENGINEERS;
      else choice->choice = U_SETTLERS;
      choice->want = want;
      choice->type = 1;
    }
  }
  if (can_build_unit(pcity, U_CARAVAN)) {
    city_list_iterate(pplayer->cities, acity)
      if (!acity->is_building_unit && is_wonder(acity->currently_building)
          && acity != pcity) { /* can't believe I forgot this! */
        want = pcity->ai.building_want[acity->currently_building];
        want -= warmap.cost[acity->x][acity->y] * 8 / 3;
/* value of 8 is a total guess and could be wrong, but it's better than 0 -- Syela */
        if (want > choice->want) {
          if (can_build_unit(pcity, U_FREIGHT)) choice->choice = U_FREIGHT;
          else choice->choice = U_CARAVAN;
          choice->want = want;
          choice->type = 1;
        }
      }
    city_list_iterate_end;
  } /* kludgy caravan code by Syela -- really in a rush right now */
  ai_advisor_choose_building(pcity, &cur);
  if (cur.want > choice->want) {
    choice->choice = cur.choice;
    choice->want = cur.want;
/*    if (choice->want > 100) choice->want = 100; */
/* want > 100 means BUY RIGHT NOW */
    choice->type = 0;
  }
/* allowing buy of peaceful units after much testing -- Syela */
  return;
}






