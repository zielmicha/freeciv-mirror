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
#ifndef FC__GAME_H
#define FC__GAME_H

#include "shared.h"
#include "player.h"

enum server_states { 
  PRE_GAME_STATE, 
  SELECT_RACES_STATE, 
  RUN_GAME_STATE,
  GAME_OVER_STATE
};

enum client_states { 
  CLIENT_BOOT_STATE,
  CLIENT_PRE_GAME_STATE,
  CLIENT_SELECT_RACE_STATE,
  CLIENT_WAITING_FOR_GAME_START_STATE,
  CLIENT_GAME_RUNNING_STATE
};

struct unit;
struct city;

struct civ_game {
  int version;
  int civstyle;
  int gold;
  int settlers, explorer;
  int tech;
  int skill_level;
  int timeout;
  int end_year;
  int year;
  int techlevel;
  int diplcost, freecost, conquercost;
  int diplchance;
  int cityfactor;
  int civilwarsize;
  int min_players, max_players, nplayers;
  int aifill;
  int unhappysize;
  char *startmessage;
  int player_idx;
  struct player *player_ptr;
  struct player players[MAX_NUM_PLAYERS];   
  int global_advances[A_LAST];             /* a counter */
  int global_wonders[B_LAST];              /* contains city id's */
         /* global_wonders[] may also be (-1), or the id of a city
	    which no longer exists, if the wonder has been destroyed */
  int globalwarming;                       /* counter of how disturbed 
					      mother nature is */
  int rail_food, rail_trade, rail_prod;
  int farmfood;
  int heating;
  int warminglevel;
  char save_name[MAX_LEN_NAME];
  int save_nturns;
  int scenario;
  int foodbox;
  int aqueductloss;
  int techpenalty;
  int razechance;
  int scorelog;
  int randseed;
  int aqueduct_size;
  int sewer_size;
  int spacerace;
  struct {
    char techs[MAX_LEN_NAME];
    char units[MAX_LEN_NAME];
    char buildings[MAX_LEN_NAME];
    char terrain[MAX_LEN_NAME];
  } ruleset;
  int firepower_factor;		/* See README.rulesets */
  struct {
    int get_bonus_tech;		/* eg Philosophy */
    int boat_fast;		/* eg Nuclear Power */
    int cathedral_plus;		/* eg Theology */
    int cathedral_minus;	/* eg Communism */
    int colosseum_plus;		/* eg Electricity */
    int nav;			/* AI convenience: tech_req for first
				   non-trireme ferryboat */
  } rtech;
};

struct lvldat {
  int advspeed;
};

void game_init(void);
void game_next_year(void);

int civ_population(struct player *pplayer);
struct unit *game_find_unit_by_id(int unit_id);
struct city *game_find_city_by_name(char *name);

void game_remove_player(int plrno);
void game_remove_all_players(void);
void game_renumber_players(int plrno);

void game_remove_unit(int unit_id);
void game_remove_city(struct city *pcity);
int research_time(struct player *pplayer);
int total_player_citizens(struct player *pplayer);
int civ_score(struct player *pplayer);
void initialize_globals(void);

struct player *get_player(int player_id);

extern struct civ_game game;

#define GAME_DEFAULT_RANDSEED        0
#define GAME_MIN_RANDSEED            0
#define GAME_MAX_RANDSEED            (MAX_UINT32 >> 1)

#define GAME_DEFAULT_GOLD        50
#define GAME_MIN_GOLD            0
#define GAME_MAX_GOLD            5000

#define GAME_DEFAULT_SETTLERS    2
#define GAME_MIN_SETTLERS        1
#define GAME_MAX_SETTLERS        10

#define GAME_DEFAULT_EXPLORER    1
#define GAME_MIN_EXPLORER        0
#define GAME_MAX_EXPLORER        10

#define GAME_DEFAULT_TECHLEVEL   3
#define GAME_MIN_TECHLEVEL       0
#define GAME_MAX_TECHLEVEL       50

#define GAME_DEFAULT_UNHAPPYSIZE 4
#define GAME_MIN_UNHAPPYSIZE 1
#define GAME_MAX_UNHAPPYSIZE 6

#define GAME_DEFAULT_END_YEAR    2000
#define GAME_MIN_END_YEAR        GAME_START_YEAR
#define GAME_MAX_END_YEAR        5000

#define GAME_DEFAULT_MIN_PLAYERS     1
#define GAME_MIN_MIN_PLAYERS         1
#define GAME_MAX_MIN_PLAYERS         MAX_NUM_PLAYERS

#define GAME_DEFAULT_MAX_PLAYERS     14
#define GAME_MIN_MAX_PLAYERS         1
#define GAME_MAX_MAX_PLAYERS         MAX_NUM_PLAYERS

#define GAME_DEFAULT_AIFILL          0
#define GAME_MIN_AIFILL              0
#define GAME_MAX_AIFILL              GAME_MAX_MAX_PLAYERS

#define GAME_DEFAULT_RESEARCHLEVEL   10
#define GAME_MIN_RESEARCHLEVEL       4
#define GAME_MAX_RESEARCHLEVEL       100

#define GAME_DEFAULT_DIPLCOST        0
#define GAME_MIN_DIPLCOST            0
#define GAME_MAX_DIPLCOST            100

#define GAME_DEFAULT_DIPLCHANCE      3
#define GAME_MIN_DIPLCHANCE          1
#define GAME_MAX_DIPLCHANCE          10

#define GAME_DEFAULT_FREECOST        0
#define GAME_MIN_FREECOST            0
#define GAME_MAX_FREECOST            100

#define GAME_DEFAULT_CONQUERCOST     0
#define GAME_MIN_CONQUERCOST         0
#define GAME_MAX_CONQUERCOST         100

#define GAME_DEFAULT_CITYFACTOR      14
#define GAME_MIN_CITYFACTOR          6
#define GAME_MAX_CITYFACTOR          100

#define GAME_DEFAULT_CIVILWARSIZE    10
#define GAME_MIN_CIVILWARSIZE        6
#define GAME_MAX_CIVILWARSIZE        1000

#define GAME_DEFAULT_RAILFOOD        0
#define GAME_MIN_RAILFOOD            0
#define GAME_MAX_RAILFOOD            100

#define GAME_DEFAULT_RAILTRADE       0
#define GAME_MIN_RAILTRADE           0
#define GAME_MAX_RAILTRADE           100

#define GAME_DEFAULT_RAILPROD        50
#define GAME_MIN_RAILPROD            0
#define GAME_MAX_RAILPROD            100

#define GAME_DEFAULT_FARMFOOD        50
#define GAME_MIN_FARMFOOD            0
#define GAME_MAX_FARMFOOD            100

#define GAME_DEFAULT_FOODBOX         10
#define GAME_MIN_FOODBOX             5
#define GAME_MAX_FOODBOX             30

#define GAME_DEFAULT_AQUEDUCTLOSS    0
#define GAME_MIN_AQUEDUCTLOSS        0
#define GAME_MAX_AQUEDUCTLOSS        100

#define GAME_DEFAULT_TECHPENALTY     100
#define GAME_MIN_TECHPENALTY         0
#define GAME_MAX_TECHPENALTY         100

#define GAME_DEFAULT_RAZECHANCE      20
#define GAME_MIN_RAZECHANCE          0
#define GAME_MAX_RAZECHANCE          100

#define GAME_DEFAULT_CIVSTYLE        2
#define GAME_MIN_CIVSTYLE            1
#define GAME_MAX_CIVSTYLE            2

#define GAME_DEFAULT_SCORELOG        0
#define GAME_MIN_SCORELOG            0
#define GAME_MAX_SCORELOG            1

#define GAME_DEFAULT_SPACERACE       1
#define GAME_MIN_SPACERACE           0
#define GAME_MAX_SPACERACE           1

#define GAME_DEFAULT_TIMEOUT          0
#define GAME_MIN_TIMEOUT              0
#define GAME_MAX_TIMEOUT            999

#define GAME_DEFAULT_RULESET         "default"

#define GAME_DEFAULT_SKILL_LEVEL 3      /* easy */
#define GAME_OLD_DEFAULT_SKILL_LEVEL 5  /* normal; for old save games */

#define GAME_START_YEAR -4000

#endif  /* FC__GAME_H */
