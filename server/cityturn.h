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

#ifndef FC__CITYTURN_H
#define FC__CITYTURN_H

#include "shared.h"		/* bool type */

struct city;
struct player;
struct unit;
struct conn_list;

#define sanity_check_city(x) real_sanity_check_city(x, __FILE__, __LINE__)
void real_sanity_check_city(struct city *pcity, const char *file, int line);

void city_refresh(struct city *pcity);          /* call if city has changed */
void global_city_refresh(struct player *pplayer); /* tax/govt changed */

void auto_arrange_workers(struct city *pcity); /* will arrange the workers */

bool city_reduce_size(struct city *pcity, int pop_loss);
void send_global_city_turn_notifications(struct conn_list *dest);
void send_city_turn_notifications(struct conn_list *dest, struct city *pcity);
void begin_cities_turn(struct player *pplayer);
void update_city_activities(struct player *pplayer);
int city_incite_cost(struct player *pplayer, struct city *pcity);
void remove_obsolete_buildings_city(struct city *pcity, bool refresh);
void remove_obsolete_buildings(struct player *pplayer);

void advisor_choose_build(struct player *pplayer, struct city *pcity);

void nullify_prechange_production(struct city *pcity);
#endif  /* FC__CITYTURN_H */
