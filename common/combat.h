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
#ifndef FC__COMBAT_H
#define FC__COMBAT_H

struct unit;

double win_chance(int as, int ahp, int afp, int ds, int dhp, int dfp);

void get_modified_firepower(struct unit *attacker, struct unit *defender,
			    int *att_fp, int *def_fp);
double unit_win_chance(struct unit *attacker, struct unit *defender);

int unit_ignores_citywalls(struct unit *punit);
int unit_really_ignores_citywalls(struct unit *punit);
int unit_behind_walls(struct unit *punit);
int unit_on_fortress(struct unit *punit);
int unit_behind_coastal(struct unit *punit);
int unit_behind_sam(struct unit *punit);
int unit_behind_sdi(struct unit *punit);
struct city *sdi_defense_close(int owner, int x, int y);

int get_attack_power(struct unit *punit);
int get_defense_power(struct unit *punit);
int get_total_defense_power(struct unit *attacker, struct unit *defender);
int get_simple_defense_power(int d_type, int x, int y);
int get_virtual_defense_power(int a_type, int d_type, int x, int y);
int get_total_attack_power(struct unit *attacker, struct unit *defender);

struct unit *get_defender(struct unit *aunit, int x, int y);
struct unit *get_attacker(struct unit *aunit, int x, int y);

#endif /* FC__COMBAT_H */
