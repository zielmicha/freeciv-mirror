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
#ifndef FC__ADVDOMESTIC_H
#define FC__ADVDOMESTIC_H

#include "fc_types.h"

void domestic_advisor_choose_build(struct player *pplayer, struct city *pcity,
				   struct adv_choice *choice);

void dai_wonder_city_distance(struct player *pplayer, 
                              struct adv_data *adv);

#endif  /* FC__ADVDOMESTIC_H */