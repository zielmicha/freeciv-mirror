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
#ifndef FC__NATION_H
#define FC__NATION_H

#include "shared.h"		/* MAX_LEN_NAME */
#include "terrain.h"		/* T_COUNT */

#define MAX_NUM_TECH_GOALS 10
#define MAX_NUM_NATIONS  63
#define MAX_NUM_LEADERS  16

typedef int Nation_Type_id;

struct Sprite;			/* opaque; client-gui specific */
struct player;

enum advisor_type {ADV_ISLAND, ADV_MILITARY, ADV_TRADE, ADV_SCIENCE, ADV_FOREIGN, 
                   ADV_ATTITUDE, ADV_DOMESTIC, ADV_LAST};

/*
 * The city_name structure holds information about a default choice for
 * the city name.  The "name" field is, of course, just the name for
 * the city.  The "river" and "terrain" fields are entries recording
 * whether the terrain is present near the city - we give higher priority
 * to cities which have matching terrain.  In the case of a river we only
 * care if the city is _on_ the river, for other terrain features we give
 * the bonus if the city is close to the terrain.  Both of these entries
 * may hold a value of 0 (no preference), 1 (city likes the terrain), or -1
 * (city doesn't like the terrain).
 *
 * This is controlled through the nation's ruleset like this:
 *   cities = "Washington (ocean, river, swamp)", "New York (!mountains)"
 */
typedef int ternary;
struct city_name {
  char* name;
  ternary river;
  ternary terrain[T_COUNT];	
};

struct nation_type {
  char name[MAX_LEN_NAME];
  char name_plural[MAX_LEN_NAME];
  char flag_graphic_str[MAX_LEN_NAME];
  char flag_graphic_alt[MAX_LEN_NAME];
  int  leader_count;
  char *leader_name[MAX_NUM_LEADERS];
  bool  leader_is_male[MAX_NUM_LEADERS];
  int city_style;
  struct city_name *city_names;		/* The default city names. */
  struct Sprite *flag_sprite;

  /* untranslated copies: */
  char name_orig[MAX_LEN_NAME];
  char name_plural_orig[MAX_LEN_NAME];

  /* 
   * Advances given to this nation at game start.
   */
  int init_techs[MAX_NUM_TECH_LIST];

  /* AI hints */
  int attack;               /* c 0 = optimize for food, 2 =  optimize for prod  */
                            /* c0 = large amount of buildings, 2 = units */
  /* attack has been un-implemented for the time being. -- Syela */
  int expand;               /* c0 = transform first ,  2 = build cities first */
  /* expand has been un-implemented, probably permanently. -- Syela */
  int civilized;            /* c 0 = don't use nukes,  2 = use nukes, lots of pollution */
  /* civilized was never implemented, but will be eventually. -- Syela */
  int advisors[ADV_LAST];   /* never implemented either. -- Syela */

  /* Following basically disabled -- Syela */
  /* Note the client doesn't use/have these. */
  struct {
    int tech[MAX_NUM_TECH_GOALS];               /* tech goals     */
    int wonder;                                 /* primary Wonder */
    int government;
  } goals;
};

Nation_Type_id find_nation_by_name(const char *name);
const char *get_nation_name(Nation_Type_id nation);
const char *get_nation_name_plural(Nation_Type_id nation);
char **get_nation_leader_names(Nation_Type_id nation, int *dim);
bool get_nation_leader_sex(Nation_Type_id nation, const char *name);
struct nation_type *get_nation_by_plr(struct player *plr);
struct nation_type *get_nation_by_idx(Nation_Type_id nation);
bool check_nation_leader_name(Nation_Type_id nation, const char *name);
void nations_alloc(int num);
void nations_free(void);
void nation_free(Nation_Type_id nation);
void nation_city_names_free(struct city_name *city_names);
int get_nation_city_style(Nation_Type_id nation);

#endif  /* FC__NATION_H */
