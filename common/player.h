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
#ifndef __PLAYER_H
#define __PLAYER_H

#include "tech.h"
#include "unit.h"
#include "city.h"
#include "spaceship.h"

struct tile;

#define PLAYER_DEFAULT_TAX_RATE 50
#define PLAYER_DEFAULT_SCIENCE_RATE 50
#define PLAYER_DEFAULT_LUXURY_RATE 0
#define TECH_GOALS 10
enum government_type { 
  G_ANARCHY, G_DESPOTISM, G_MONARCHY, G_COMMUNISM, G_REPUBLIC, G_DEMOCRACY,
  G_LAST
};

enum race_type {
  R_ROMAN, R_BABYLONIAN, R_GERMAN, R_EGYPTIAN, R_AMERICAN, R_GREEK, R_INDIAN, 
  R_RUSSIAN, R_ZULU, R_FRENCH, R_AZTEC, R_CHINESE, R_ENGLISH, R_MONGOL, R_LAST
};

enum advisor_type {ADV_ISLAND, ADV_MILITARY, ADV_TRADE, ADV_SCIENCE, ADV_FOREIGN, ADV_ATTITUDE, ADV_DOMESTIC, ADV_LAST};

enum handicap_type {
  H_NONE=0, /* no handicaps */
  H_RIGIDPROD=1, /* can't switch to/from building_unit without penalty */
  H_MAP=2, /* only knows map_get_known tiles */
  H_TECH=4, /* doesn't know what enemies have researched */
  H_CITYBUILDINGS=8, /* doesn't know what buildings are in enemy cities */
  H_CITYUNITS=16, /* doesn't know what units are in enemy cities */
  H_STACKS=32, /* doesn't know what units are in stacks */
  H_VETERAN=64, /* doesn't know veteran status of enemy units */
  H_SUB=128, /* doesn't know where subs may be lurking */
/* below this point are milder handicaps that I can actually implement -- Syela */
  H_RATES=256, /* can't set its rates beyond government limits */
  H_TARGETS=512, /* can't target anything it doesn't know exists */
  H_HUTS=1024 /* doesn't know which unseen tiles have huts on them */
/* anything else I have forgotten?  Let me know. -- Syela */
};

struct player_race {
  char name[MAX_LENGTH_NAME];
  char name_plural[MAX_LENGTH_NAME];
  int attack;     /* c 0 = optimize for food, 2 =  optimize for prod  */
                  /* c0 = large amount of buildings, 2 = units */
/* attack has been un-implemented for the time being. -- Syela */
  int expand;    /* c0 = transform first ,  2 = build cities first */
/* expand has been un-implemented, probably permanently. -- Syela */
  int civilized; /* c 0 = don't use nukes,  2 = use nukes, lots of pollution */
/* civilized was never implemented, but will be eventually. -- Syela */
  int advisors[ADV_LAST]; /* never implemented either. -- Syela */
  struct { /* basically disabled -- Syela */
    int tech[TECH_GOALS]; /* Tech goals */
    int wonder;   /* primary Wonder (maybe primary opponent, if other builds it) */
    int government; /* wanted government form */
  } goals;
};

struct player_economic {
  int gold;
  int tax;
  int science;
  int luxury;
};

struct player_research {
  int researched;     /* # bulbs reseached */
  int researchpoints; /* # bulbs to complete */
  int researching;    /* invention being researched in */
  unsigned char inventions[A_LAST];
};

struct player_score {
  int happy;
  int content;
  int unhappy;
  int taxmen;
  int scientists;
  int elvis;
  int wonders;
  int techs;
  int techout;
  int landmass;
  int cities;
  int units;
  int pollution;
  int literacy;
  int bnp;
  int mfg;
  int spaceship;
};

struct player_ai {
  int control;
  int tech_goal;
  int prev_gold;
  int maxbuycost;
  int tech_want[A_LAST];
  int tech_turns[A_LAST]; /* saves zillions of calculations! */
  int handicap;			/* sum of enum handicap_type */
  int skill_level;		/* 0-10 value for save/load/display */
  int fuzzy;			/* chance in 1000 to mis-decide */
  int expand;			/* percentage factor to value new cities */
  int warmth; /* threat of global warming */
};

/**************************************************************************
  command access levels for client-side use; at present, they are
  only used for server commands typed at the client chatline
 **************************************************************************/
enum cmdlevel_id {    /* access levels for users to issue commands        */
  ALLOW_NONE = 0,     /* user may issue no commands at all                */
  ALLOW_INFO,         /* user may issue informational commands            */
  ALLOW_CTRL,         /* user may issue commands that affect game & users */
  ALLOW_HACK,         /* user may issue *all* commands - dangerous!       */

  ALLOW_NUM,          /* the number of levels                             */
  ALLOW_UNRECOGNIZED  /* used as a failure return code                    */
};
/*  the set command is a special case:                                    */
/*    - ALLOW_CTRL is required for SSET_TO_CLIENT options                 */
/*    - ALLOW_HACK is required for SSET_TO_SERVER options                 */

/***************************************************************************
  On the distinction between races, players, and users,
  see freeciv_hackers_guide.txt
***************************************************************************/

struct player {
  int player_no;
  char name[MAX_LENGTH_NAME];
  enum government_type government;
  enum race_type race;
  int turn_done;
  int nturns_idle;
  int is_alive;
  int got_tech;
  int revolution;
  int capital; /* bool used to give player capital in first city. */
  int embassy;
  struct unit_list units;
  struct city_list cities;
  struct player_score score;
  struct player_economic economic;
  struct player_research research;
  struct player_spaceship spaceship;
  int future_tech;
  struct player_ai ai;
  int is_connected;
  struct connection *conn;
  char addr[MAX_LENGTH_ADDRESS];
};

void player_init(struct player *plr);
struct player *find_player_by_name(char *name);
void player_set_unit_focus_status(struct player *pplayer);
int player_has_embassy(struct player *pplayer, struct player *pplayer2);

int can_change_to_government(struct player *pplayer, 
			    enum government_type);
int get_government_max_rate(enum government_type type);
int get_government_civil_war_prob(enum government_type type);
char *get_government_name(enum government_type type);
char *get_ruler_title(enum government_type type);
char *get_race_name(enum race_type race);
char *get_race_name_plural(enum race_type race);
struct player_race *get_race(struct player *plr);
int player_can_see_unit(struct player *pplayer, struct unit *punit);
struct unit *player_find_visible_unit(struct player *pplayer, struct tile *);
int player_owns_city(struct player *pplayer, struct city *pcity);

struct city *player_find_city_by_id(struct player *pplayer, int city_id);
int player_owns_active_wonder(struct player *pplayer,
			      enum improvement_type_id id);
int player_owns_active_govchange_wonder(struct player *pplayer);
int player_knows_improvement_tech(struct player *pplayer,
				   enum improvement_type_id id);

struct city *find_palace(struct player *pplayer);

int ai_handicap(struct player *pplayer, enum handicap_type htype);
int ai_fuzzy(struct player *pplayer, int normal_decision);

extern struct player_race races[];
extern char *default_race_leader_names[];

char *cmdlevel_name(enum cmdlevel_id lvl);
enum cmdlevel_id cmdlevel_named (char *token);

#endif
