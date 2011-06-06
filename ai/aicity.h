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
#ifndef FC__AICITY_H
#define FC__AICITY_H

/* common */
#include "fc_types.h"

struct adv_data;

enum choice_type {
  CT_NONE = 0,
  CT_BUILDING = 1,
  CT_CIVILIAN,
  CT_ATTACKER,
  CT_DEFENDER,
  CT_LAST
};

struct ai_choice {
  enum choice_type type;
  universals_u value; /* what the advisor wants */
  int want;              /* how much it wants it (0-100) */
  bool need_boat;        /* unit being built wants a boat */
};

struct ai_activity_cache; /* defined and only used within aicity.c */

/* Who's coming to kill us, for attack co-ordination */
struct ai_invasion {
  int attack;         /* Units capable of attacking city */
  int occupy;         /* Units capable of occupying city */
};

struct ai_city {
  int building_turn;            /* only recalculate every Nth turn */
  int building_wait;            /* for weighting values */
#define BUILDING_WAIT_MINIMUM (1)

  struct ai_choice choice;      /* to spend gold in the right place only */

  struct ai_invasion invasion;
  int attack, bcost; /* This is also for invasion - total power and value of
                      * all units coming to kill us. */

  unsigned int danger;          /* danger to be compared to assess_defense */
  unsigned int grave_danger;    /* danger, should show positive feedback */
  unsigned int urgency;         /* how close the danger is; if zero,
                                   bodyguards can leave */
  int wallvalue;                /* how much it helps for defenders to be
                                   ground units */

  int distance_to_wonder_city;  /* wondercity will set this for us,
                                   avoiding paradox */

  bool celebrate;               /* try to celebrate in this city */
  bool diplomat_threat;         /* enemy diplomat or spy is near the city */
  bool has_diplomat;            /* this city has diplomat or spy defender */

  /* so we can contemplate with warmap fresh and decide later */
  /* These values are for builder (F_SETTLERS) and founder (F_CITIES) units.
   * Negative values indicate that the city needs a boat first;
   * -value is the degree of want in that case. */
  bool founder_boat;            /* city founder will need a boat */
  int founder_turn;             /* only recalculate every Nth turn */
  int founder_want;
  int settler_want;
};

void ai_manage_cities(struct player *pplayer);

void ai_city_alloc(struct city *pcity);
void ai_city_free(struct city *pcity);

struct section_file;
void ai_city_save(struct section_file *file, const struct city *pcity,
                  const char *citystr);
void ai_city_load(const struct section_file *file, struct city *pcity,
                  const char *citystr);

void want_techs_for_improvement_effect(struct player *pplayer,
                                       const struct city *pcity,
                                       const struct impr_type *pimprove,
                                       struct tech_vector *needed_techs,
                                       int building_want);

void dont_want_tech_obsoleting_impr(struct player *pplayer,
                                    const struct city *pcity,
                                    const struct impr_type *pimprove,
                                    int building_want);

#endif  /* FC__AICITY_H */
