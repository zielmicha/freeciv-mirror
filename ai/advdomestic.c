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
#include <string.h>

#include <aitools.h>
#include <map.h>
#include <game.h>
#include <unit.h>
#include <citytools.h>
#include <advmilitary.h>
#include <unitfunc.h>
#include <cityhand.h>
#include <aicity.h>
#include <aiunit.h>
#include <settlers.h>
#include <unittools.h>

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

  if (pcity->ppl_content[1] < pcity->ppl_content[0]) /* should fix the above */
    bored += pcity->ppl_content[0] - pcity->ppl_content[1];

  i = pcity->ppl_unhappy[3] - pcity->ppl_unhappy[2];
  sad += i; /* if units are making us unhappy, count that too. */
/*  printf("In %s, unh[0] = %d, unh[4] = %d, sad = %d\n",
       pcity->name, pcity->ppl_unhappy[0], pcity->ppl_unhappy[4], sad); */

  i = max;
  while (i && elvis) { i--; elvis--; j += val; }
  while (i && sad) { i--; sad--; j += 16; }
  if (city_unhappy(pcity)) j += 16 * (sad + bored); /* Desperately seeking Colosseum */
  while (i) { i--; j += 16; } /* 16 is debatable value */
/* using (i && bored) led to a lack of foresight, especially re: Chapel -- Syela */
/*  printf("%s: %d elvis %d sad %d bored %d size %d max %d val\n",
    pcity->name, pcity->ppl_elvis, pcity->ppl_unhappy[4],
    pcity->ppl_content[4], pcity->size, max, j);  */

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
  return(i>>1);
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

int pollution_cost(struct player *pplayer, struct city *pcity, enum improvement_type_id id)
{
  int p, mod = 0, poppul = 0, a, b, c, x, y, tmp = 0;
  p = 0;
  city_map_iterate(x, y) {
    if(get_worker_city(pcity, x, y)==C_TILE_WORKER) {
      p += get_shields_tile(x, y, pcity);
    }
  }
  if (city_got_building(pcity, B_FACTORY) || id == B_FACTORY) {
    if (city_got_building(pcity, B_MFG) || id == B_MFG) tmp = 100;
    else tmp = 50;
  }
  if (city_affected_by_wonder(pcity, B_HOOVER) || id == B_HOOVER ||
      city_got_building(pcity, B_POWER) || id == B_POWER ||
      city_got_building(pcity, B_HYDRO) || id == B_HYDRO ||
      city_got_building(pcity, B_NUCLEAR) || id == B_NUCLEAR) tmp *= 1.5;
  p = p * (tmp + 100) / 100;

  if (city_got_building(pcity, B_RECYCLING) || id == B_RECYCLING) p /= 3;
  else if (city_got_building(pcity, B_HYDRO) ||
           city_affected_by_wonder(pcity, B_HOOVER) ||
           city_got_building(pcity, B_NUCLEAR) ||
           id == B_HYDRO || id == B_HOOVER || id == B_NUCLEAR) p /= 2;

  if (!city_got_building(pcity, B_MASS) && id != B_MASS) {
    if (get_invention(pplayer, A_INDUSTRIALIZATION)==TECH_KNOWN)  mod=1;
    if (get_invention(pplayer, A_AUTOMOBILE)==TECH_KNOWN) mod=2;
    if (get_invention(pplayer, A_MASS)==TECH_KNOWN) mod=3;
    if (get_invention(pplayer, A_PLASTICS)==TECH_KNOWN) mod=4;
    poppul=(pcity->size*mod)/4;
    p += poppul;
  }
  p -= 20;
  if (p < 0) p = 0;
  b = (POLLUTION_WEIGHTING + pplayer->ai.warmth)<<6;
  if (pcity->pollution > 0) {
    a = amortize(b, 100 / pcity->pollution);
    c = ((a * b) / (MAX(1, b - a)))>>6;
/*printf("City: %s, Pollu: %d, cost: %d ", pcity->name, pcity->pollution, c);
printf("Id: %d, P: %d\n", id, p);*/
  } else c = 0;
  if (p) {
    a = amortize(b, 100 / p);
    c -= ((a * b) / (MAX(1, b - a)))>>6;
  } 
  return(c); /* benefit or cost of this building */
}
 
void ai_eval_buildings(struct city *pcity)
{
  int i, gov, tech, val, a, t, food, j, k, hunger, bar, grana;
  int tax, prod, sci, values[B_LAST];
  int est_food = pcity->food_surplus + 2 * pcity->ppl_scientist + 2 * pcity->ppl_taxman; 
  struct player *pplayer = city_owner(pcity);
  int needpower;
  
  a = get_race(city_owner(pcity))->attack;
  t = pcity->ai.trade_want; /* trade_weighting */
  sci = (pcity->trade_prod * pplayer->economic.science + 50) / 100;
  tax = pcity->trade_prod - sci;
#ifdef STUPID
  sci *= t;
  tax *= t;
#else
/* better might be a longterm weighted average, this is a quick-n-dirty fix -- Syela */
  sci = ((sci + pcity->trade_prod) * t)>>1;
  tax = ((tax + pcity->trade_prod) * t)>>1;
#endif
  if (pplayer->research.researching == A_NONE) sci = 0; /* don't need libraries!! */
  prod = pcity->shield_prod * 100 / city_shield_bonus(pcity) * SHIELD_WEIGHTING;
  needpower = (city_got_building(pcity, B_MFG) ? 2 :
              (city_got_building(pcity, B_FACTORY) ? 1 : 0));
  val = ai_best_tile_value(pcity);
  food = food_weighting(MAX(2,pcity->size));
  grana = food_weighting(MAX(3,pcity->size + 1));
/* because the benefit doesn't come immediately, and to stop stupidity */
/* the AI used to really love granaries for nascent cities, which is OK */
/* as long as they aren't rated above Settlers and Laws is above Pottery -- Syela*/
  i = (pcity->size * 2) + settler_eats(pcity);
  i -= pcity->food_prod; /* amazingly left out for a week! -- Syela */
  if (i > 0 && !pcity->ppl_scientist && !pcity->ppl_taxman) hunger = i + 1;
  else hunger = 1;

  gov = get_government(pcity->owner);
  tech = (pplayer->research.researching != A_NONE);

  for (i=0;i<B_LAST;i++) {
    values[i]=0;
  } /* rewrite by Syela - old values seemed very random */

  if (could_build_improvement(pcity, B_AQUEDUCT)) {
    if (city_happy(pcity) && pcity->size > 7 && est_food > 0)
      values[B_AQUEDUCT] = (((((city_got_building(pcity, B_GRANARY) ||
         city_affected_by_wonder(pcity, B_PYRAMIDS)) ? 3 : 2) *
         pcity->size * game.foodbox)>>1) - pcity->food_stock) * food;
    else values[B_AQUEDUCT] = food * est_food * 8 * game.foodbox /
           MAX(1, ((9 - MIN(8, pcity->size)) * MAX(8, pcity->size) *
           game.foodbox - pcity->food_stock));
  }


  if (could_build_improvement(pcity, B_BANK))
    values[B_BANK] = tax>>1;
  
  j = 0; k = 0;
  city_list_iterate(pplayer->cities, acity)
    if (acity->is_building_unit) {
      if (!unit_flag(acity->currently_building, F_NONMIL) &&
          unit_types[acity->currently_building].move_type == LAND_MOVING)
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

  if (!built_elsewhere(pcity, B_SUNTZU)) { /* yes, I want can, not could! */
    if (can_build_improvement(pcity, B_BARRACKS))
      values[B_BARRACKS] = bar;
    if (can_build_improvement(pcity, B_BARRACKS2))
      values[B_BARRACKS2] = bar;
    if (can_build_improvement(pcity, B_BARRACKS3))
      values[B_BARRACKS3] = bar;
  }

  if (could_build_improvement(pcity, B_CAPITAL))
    values[B_CAPITAL] = TRADE_WEIGHTING * 999 / MORT; /* trust me */
/* rationale: If cost is N and benefit is N gold per MORT turns, want is
TRADE_WEIGHTING * 100 / MORT.  This is comparable, thus the same weight -- Syela */

  if (could_build_improvement(pcity, B_CATHEDRAL) && !built_elsewhere(pcity, B_MICHELANGELO))
    values[B_CATHEDRAL] = building_value(get_cathedral_power(pcity), pcity, val);
  else if (get_invention(pplayer, A_THEOLOGY) != TECH_KNOWN)
    values[B_CATHEDRAL] = building_value(4, pcity, val) - building_value(3, pcity, val);

/* old wall code depended on danger, was a CPU hog and didn't really work anyway */
/* it was so stupid, AI wouldn't start building walls until it was in danger */
/* and it would have no chance to finish them before it was too late */

  if (could_build_improvement(pcity, B_CITY))
/* && !built_elsewhere(pcity, B_WALL))      was counterproductive -- Syela */
    values[B_CITY] = 40; /* WAG */

  if (could_build_improvement(pcity, B_COASTAL))
    values[B_COASTAL] = 40; /* WAG */

  if (could_build_improvement(pcity, B_SAM))
    values[B_SAM] = 50; /* WAG */

  if (could_build_improvement(pcity, B_COLOSSEUM))
    values[B_COLOSSEUM] = building_value(get_colosseum_power(pcity), pcity, val);
  else if (get_invention(pplayer, A_ELECTRICITY) != TECH_KNOWN)
    values[B_COLOSSEUM] = building_value(4, pcity, val) - building_value(3, pcity, val);
  
  if (could_build_improvement(pcity, B_COURTHOUSE)) {
    values[B_COURTHOUSE] = (pcity->corruption * t)>>1;
    if (gov == G_DEMOCRACY) values[B_COLOSSEUM] += building_value(1, pcity, val);
  }
  
  if (could_build_improvement(pcity, B_FACTORY))
    values[B_FACTORY] = (prod>>1) + pollution_cost(pplayer, pcity, B_FACTORY);
  
  if (could_build_improvement(pcity, B_GRANARY) && !built_elsewhere(pcity, B_PYRAMIDS))
    values[B_GRANARY] = grana * pcity->food_surplus;
  
  if (could_build_improvement(pcity, B_HARBOUR))
    values[B_HARBOUR] = food * ocean_workers(pcity) * hunger;

  if (could_build_improvement(pcity, B_HYDRO) && !built_elsewhere(pcity, B_HOOVER))
    values[B_HYDRO] = ((needpower * prod)>>2) + pollution_cost(pplayer, pcity, B_HYDRO);

  if (could_build_improvement(pcity, B_LIBRARY))
    values[B_LIBRARY] = sci>>1;

  if (could_build_improvement(pcity, B_MARKETPLACE))
    values[B_MARKETPLACE] = tax>>1;

  if (could_build_improvement(pcity, B_MFG))
    values[B_MFG] = ((city_got_building(pcity, B_HYDRO) ||
                     city_got_building(pcity, B_NUCLEAR) ||
                     city_got_building(pcity, B_POWER) ||
                     city_affected_by_wonder(pcity, B_HOOVER)) ?
                     (prod * 3)>>2 : prod>>1) +
                     pollution_cost(pplayer, pcity, B_MFG);

  if (could_build_improvement(pcity, B_NUCLEAR))
    values[B_NUCLEAR] = ((needpower * prod)>>2) + pollution_cost(pplayer, pcity, B_NUCLEAR);

  if (could_build_improvement(pcity, B_OFFSHORE)) /* ignoring pollu.  FIX? */
    values[B_OFFSHORE] = ocean_workers(pcity) * SHIELD_WEIGHTING;

  if (could_build_improvement(pcity, B_POWER))
    values[B_POWER] = ((needpower * prod)>>2) + pollution_cost(pplayer, pcity, B_POWER);

  if (could_build_improvement(pcity, B_RESEARCH) && !built_elsewhere(pcity, B_SETI))
    values[B_RESEARCH] = sci>>1;

  if (could_build_improvement(pcity, B_SDI))
    values[B_SDI] = 50; /* WAG */
  
  if (could_build_improvement(pcity, B_SEWER)) {
    if (city_happy(pcity) && pcity->size > 11 && est_food > 0)
      values[B_SEWER] = (((((city_got_building(pcity, B_GRANARY) ||
         city_affected_by_wonder(pcity, B_PYRAMIDS)) ? 3 : 2) *
         pcity->size * game.foodbox)>>1) - pcity->food_stock) * food;
    else values[B_SEWER] = food * est_food * 12 * game.foodbox /
          MAX(1, ((13 - MIN(12, pcity->size)) * MAX(12, pcity->size) *
          game.foodbox - pcity->food_stock));
  }

  if (could_build_improvement(pcity, B_STOCK))
    values[B_STOCK] = tax>>1;

  if (could_build_improvement(pcity, B_SUPERHIGHWAYS))
    values[B_SUPERHIGHWAYS] = railroad_trade(pcity) * t;

  if (could_build_improvement(pcity, B_SUPERMARKET))
    values[B_SUPERMARKET] = farmland_food(pcity) * food * hunger;

  if (could_build_improvement(pcity, B_TEMPLE))
    values[B_TEMPLE] = building_value(get_temple_power(pcity), pcity, val);

  if (could_build_improvement(pcity, B_UNIVERSITY))
    values[B_UNIVERSITY] = sci>>1;

  if (could_build_improvement(pcity, B_MASS))
    values[B_MASS] = pollution_cost(pplayer, pcity, B_MASS);

  if (could_build_improvement(pcity, B_RECYCLING))
    values[B_RECYCLING] = pollution_cost(pplayer, pcity, B_RECYCLING);

/* ignored: AIRPORT, PALACE, and POLICE. -- Syela*/
/* military advisor will deal with CITY and PORT */

  for (i = 0; i < B_LAST; i++) {
    if (is_wonder(i) && could_build_improvement(pcity, i) && !wonder_obsolete(i)) {
      if (i == B_ASMITHS)
        for (j = 0; j < B_LAST; j++)
          if (city_got_building(pcity, j) && improvement_upkeep(pcity, j) == 1)
            values[i] += t;
      if (i == B_COLLOSSUS)
        values[i] = (pcity->size + 1) * t; /* probably underestimates the value */
      if (i == B_COPERNICUS)
        values[i] = sci>>1;
      if (i == B_CURE)
        values[i] = building_value(1, pcity, val);
      if (i == B_DARWIN) /* this is a one-time boost, not constant */
        values[i] = ((research_time(pplayer) * 2 + game.techlevel) * t -
                    pplayer->research.researched * t) / MORT;
      if (i == B_GREAT) /* basically (100 - freecost)% of a free tech per turn */
        values[i] = (research_time(pplayer) * (100 - game.freecost)) * t *
                    (game.nplayers - 2) / (game.nplayers * 100); /* guessing */

      if (i == B_WALL && !city_got_citywalls(pcity))
/* allowing B_CITY when B_WALL exists, don't like B_WALL when B_CITY exists. */
        values[B_WALL] = 1; /* WAG */
 /* was 40, which led to the AI building WALL, not being able to build CITY,
someone learning Metallurgy, and the AI collapsing.  I hate the WALL. -- Syela */

      if (i == B_HANGING) /* will add the global effect to this. */
        values[i] = building_value(3, pcity, val) -
                    building_value(1, pcity, val);
      if (i == B_HOOVER && !city_got_building(pcity, B_HYDRO) &&
                           !city_got_building(pcity, B_NUCLEAR))
        values[i] = (city_got_building(pcity, B_POWER) ? 0 :
               (needpower * prod)>>2) + pollution_cost(pplayer, pcity, B_HOOVER);
      if (i == B_ISAAC)
        values[i] = sci;
      if (i == B_LEONARDO) {
        unit_list_iterate(pcity->units_supported, punit)
          j = can_upgrade_unittype(pplayer, punit->type);
          if (j >= 0) values[i] = MAX(values[i], 8 * unit_upgrade_price(pplayer,
               punit->type, j)); /* this is probably wrong -- Syela */
        unit_list_iterate_end;
      }
      if (i == B_BACH)
        values[i] = building_value(2, pcity, val);
      if (i == B_RICHARDS) /* ignoring pollu, I don't think it matters here -- Syela */
        values[i] = (pcity->size + 1) * SHIELD_WEIGHTING;
      if (i == B_MICHELANGELO && !city_got_building(pcity, B_CATHEDRAL))
        values[i] = building_value(get_cathedral_power(pcity), pcity, val);
      if (i == B_ORACLE) {
        if (city_got_building(pcity, B_TEMPLE)) {
          if (get_invention(pplayer, A_MYSTICISM) == TECH_KNOWN)
            values[i] = building_value(2, pcity, val);
          else {
            values[i] = building_value(4, pcity, val) - building_value(1, pcity, val);
            values[i] += building_value(2, pcity, val) - building_value(1, pcity, val);
/* The += has nothing to do with oracle, just the tech_Want of mysticism! */
          }
        } else {
          if (get_invention(pplayer, A_MYSTICISM) != TECH_KNOWN) {
            values[i] = building_value(2, pcity, val) - building_value(1, pcity, val);
            values[i] += building_value(2, pcity, val) - building_value(1, pcity, val);
          }
        }
      }
          
      if (i == B_PYRAMIDS && !city_got_building(pcity, B_GRANARY))
        values[i] = food * pcity->food_surplus; /* different tech req's */
      if (i == B_SETI && !city_got_building(pcity, B_RESEARCH))
        values[i] = sci>>1;
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
    if (!is_wonder(i)) { /* trying to buy fewer improvements */
      values[i] *= SHIELD_WEIGHTING;
      values[i] /= MORT;
    }
    j = improvement_value(i);
/* handle H_PROD here? -- Syela */
    pcity->ai.building_want[i] = values[i] / j;
  }

  return;
}  

void domestic_advisor_choose_build(struct player *pplayer, struct city *pcity,
				   struct ai_choice *choice)
{
  int set, con, i, want;
  struct ai_choice cur;
  int est_food = pcity->food_surplus + 2 * pcity->ppl_scientist + 2 * pcity->ppl_taxman; 
  int vans = 0;
/* had to add the scientist guess here too -- Syela */

  choice->choice = 0;
  choice->want   = 0;
  choice->type   = 0;
  set = city_get_settlers(pcity);
  con = map_get_continent(pcity->x, pcity->y); 

  if (est_food > (get_government(pcity->owner) >= G_REPUBLIC ? 2 : 1)) {
/* allowing multiple settlers per city now.  I think this is correct. -- Syela */
/* settlers are an option */
/* settler_want calculated in settlers.c, called from ai_manage_city */
    want = pcity->ai.settler_want;

    if (want > 0) {
/*      printf("%s (%d, %d) desires settlers with passion %d\n", pcity->x, pcity->y, pcity->name, want);*/
      if (can_build_unit(pcity, U_ENGINEERS)) choice->choice = U_ENGINEERS;
      else {
        choice->choice = U_SETTLERS;
        pplayer->ai.tech_want[A_EXPLOSIVES] += want;
      }
      choice->want = want;
      choice->type = 1;
    } else if (want < 0) { /* need boats to colonize! */
      choice->want = 0 - want;
      choice->type = 1;
      choice->choice = U_SETTLERS; /* default */
      ai_choose_ferryboat(pplayer, pcity, choice);
    }
  }

  unit_list_iterate(pplayer->units, punit)
    if (unit_flag(punit->type, F_CARAVAN) &&
        map_get_continent(punit->x, punit->y) == con) vans++;
  unit_list_iterate_end;
  city_list_iterate(pplayer->cities, acity)
    if (acity->is_building_unit && acity->shield_stock >= 50 &&
        unit_flag(acity->currently_building, F_CARAVAN) &&
        map_get_continent(acity->x, acity->y) == con) vans++;
  city_list_iterate_end;

  city_list_iterate(pplayer->cities, acity)
    if (!acity->is_building_unit && is_wonder(acity->currently_building)
        && map_get_continent(acity->x, acity->y) == con /* VERY important! */
        && acity != pcity && build_points_left(acity) > 50 * vans) {
      want = pcity->ai.building_want[acity->currently_building];
/* distance to wonder city was established after manage_bu and before this */
/* if we just started building a wonder during a_c_c_b, the started_building */
/* notify comes equipped with an update.  It calls generate_warmap, but this */
/* is a lot less warmap generation than there would be otherwise. -- Syela */
      i = pcity->ai.distance_to_wonder_city * 8 / (can_build_unit(pcity, U_FREIGHT) ? 6 : 3);
      want -= i;
/* value of 8 is a total guess and could be wrong, but it's better than 0 -- Syela */
      if (can_build_unit_direct(pcity, U_CARAVAN)) {
        if (want > choice->want) {
          if (can_build_unit(pcity, U_FREIGHT)) choice->choice = U_FREIGHT;
          else {
            choice->choice = U_CARAVAN;
            pplayer->ai.tech_want[A_CORPORATION] += i>>1;
          }
          choice->want = want;
          choice->type = 1;
        }
      } else pplayer->ai.tech_want[A_TRADE] += want;
    }
  city_list_iterate_end;

  ai_advisor_choose_building(pcity, &cur);
  if (cur.want > choice->want) {
    choice->choice = cur.choice;
    choice->want = cur.want;
/*    if (choice->want > 100) choice->want = 100; */
/* want > 100 means BUY RIGHT NOW */
    choice->type = 0;
  }
/* allowing buy of peaceful units after much testing -- Syela */

  if (!choice->want) { /* oh dear, better think of something! */
    if (can_build_unit(pcity, U_CARAVAN)) {
      choice->want = 1;
      choice->type = 1;
      choice->choice = U_CARAVAN;
    }
    else if (can_build_unit(pcity, U_DIPLOMAT)) {
      choice->want = 1; /* someday, real diplomat code will be here! */
      choice->type = 1;
      choice->choice = U_DIPLOMAT;
    }
    else if (can_build_unit(pcity, U_SPY)) {
      choice->want = 1; /* someday, real diplomat code will be here! */
      choice->type = 1;
      choice->choice = U_SPY;
    }
  }
  if (choice->want >= 200) choice->want = 199; /* otherwise we buy caravans in
city X when we should be saving money to buy defenses for city Y. -- Syela */

  return;
}
