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
#ifndef FC__IMPROVEMENT_H
#define FC__IMPROVEMENT_H

/* City Improvements, including Wonders.  (Alternatively "Buildings".) */

#include "shared.h"		/* MAX_LEN_NAME */
#include "tech.h"		/* Tech_Type_id */
#include "terrain.h"		/* enum tile_terrain_type etc */
#include "unittype.h"		/* Unit_Class_id, Unit_Type_id */

struct player;


/* Improvement types.
 * improvement types are loaded from the buildings.ruleset file.) */
typedef int Impr_Type_id;

/* Improvement status (for cities' lists of improvements)
 * An enum or bitfield would be neater here, but we use a typedef for
 * a) less memory usage and b) compatibility with old behaviour */
typedef unsigned char Impr_Status;
#define I_NONE       0   /* Improvement not built */
#define I_ACTIVE     1   /* Improvement built, and having its effect */
#define I_OBSOLETE   2   /* Built, but obsoleted by a tech */
#define I_REDUNDANT  3   /* Built, but replaced by wonder/other building */


/* FIXME: Remove this define when there is per-file need for this enum. */
#define OLD_IMPR_TYPE_ENUM

/* FIXME: Remove this enum and the ifdef/endif when gen-impr implemented. */
#ifdef OLD_IMPR_TYPE_ENUM
enum improvement_type_id {
  B_AIRPORT=0, B_AQUEDUCT, B_BANK, B_BARRACKS, B_BARRACKS2, B_BARRACKS3, 
  B_CATHEDRAL, B_CITY, B_COASTAL, B_COLOSSEUM, B_COURTHOUSE,  B_FACTORY, 
  B_GRANARY, B_HARBOUR, B_HYDRO, B_LIBRARY, B_MARKETPLACE, B_MASS, B_MFG, 
  B_NUCLEAR, B_OFFSHORE, B_PALACE, B_POLICE, B_PORT, B_POWER,
  B_RECYCLING, B_RESEARCH, B_SAM, B_SDI, B_SEWER, B_SOLAR, B_SCOMP, 
  B_SMODULE, B_SSTRUCTURAL, B_STOCK, B_SUPERHIGHWAYS, B_SUPERMARKET, B_TEMPLE,
  B_UNIVERSITY,  
  
  B_APOLLO, B_ASMITHS, B_COLLOSSUS, B_COPERNICUS, B_CURE, B_DARWIN, B_EIFFEL,
  B_GREAT, B_WALL, B_HANGING, B_HOOVER, B_ISAAC, B_BACH, B_RICHARDS, 
  B_LEONARDO, B_LIGHTHOUSE, B_MAGELLAN, B_MANHATTEN, B_MARCO, B_MICHELANGELO, 
  B_ORACLE, B_PYRAMIDS, B_SETI, B_SHAKESPEARE, B_LIBERTY, B_SUNTZU, 
  B_UNITED, B_WOMENS,
  B_CAPITAL, B_LAST_ENUM
};
#endif

/* B_LAST is a value which is guaranteed to be larger than all
 * actual Impr_Type_id values.  It is used as a flag value;
 * it can also be used for fixed allocations to ensure ability
 * to hold full number of improvement types.  */
#define B_LAST MAX_NUM_ITEMS

/* Range of effects (used in equiv_range and effect.range fields)
 * These must correspond to effect_range_names[] in improvement.c. */
enum effect_range {
  EFR_NONE,
  EFR_BUILDING,
  EFR_CITY,
  EFR_ISLAND,
  EFR_PLAYER,
  EFR_WORLD,
  EFR_LAST	/* keep this last */
};

/* Type of effects. (Used in effect.type field)
 * These must correspond to effect_type_names[] in improvement.c. */
enum effect_type {
  EFT_ADV_PARASITE,
  EFT_AIRLIFT,
  EFT_ANY_GOVERNMENT,
  EFT_BARB_ATTACK,
  EFT_BARB_DEFEND,
  EFT_CAPITAL_CITY,
  EFT_CAPITAL_EXISTS,
  EFT_ENABLE_NUKE,
  EFT_ENABLE_SPACE,
  EFT_ENEMY_PEACEFUL,
  EFT_FOOD_ADD_TILE,
  EFT_FOOD_BONUS,
  EFT_FOOD_INC_TILE,
  EFT_FOOD_PER_TILE,
  EFT_GIVE_IMM_ADV,
  EFT_GROWTH_FOOD,
  EFT_HAVE_EMBASSIES,
  EFT_IMPROVE_REP,
  EFT_LUXURY_BONUS,
  EFT_LUXURY_PCT,
  EFT_MAKE_CONTENT,
  EFT_MAKE_CONTENT_MIL,
  EFT_MAKE_CONTENT_PCT,
  EFT_MAKE_HAPPY,
  EFT_MAY_DECLARE_WAR,
  EFT_NO_ANARCHY,
  EFT_NO_SINK_DEEP,
  EFT_NUKE_PROOF,
  EFT_POLLU_ADJ,
  EFT_POLLU_ADJ_POP,
  EFT_POLLU_ADJ_PROD,
  EFT_POLLU_SET,
  EFT_POLLU_SET_POP,
  EFT_POLLU_SET_PROD,
  EFT_PROD_ADD_TILE,
  EFT_PROD_BONUS,
  EFT_PROD_INC_TILE,
  EFT_PROD_PER_TILE,
  EFT_PROD_TO_GOLD,
  EFT_REDUCE_CORRUPT,
  EFT_REDUCE_WASTE,
  EFT_REVEAL_CITIES,
  EFT_REVEAL_MAP,
  EFT_REVOLT_DIST,
  EFT_SCIENCE_BONUS,
  EFT_SCIENCE_PCT,
  EFT_SIZE_UNLIMIT,
  EFT_SLOW_NUKE_WINTER,
  EFT_SLOW_GLOBAL_WARM,
  EFT_SPACE_PART,
  EFT_SPY_RESISTANT,
  EFT_TAX_BONUS,
  EFT_TAX_PCT,
  EFT_TRADE_ADD_TILE,
  EFT_TRADE_BONUS,
  EFT_TRADE_INC_TILE,
  EFT_TRADE_PER_TILE,
  EFT_TRADE_ROUTE_PCT,
  EFT_UNIT_DEFEND,
  EFT_UNIT_MOVE,
  EFT_UNIT_NO_LOSE_POP,
  EFT_UNIT_RECOVER,
  EFT_UNIT_REPAIR,
  EFT_UNIT_VET_COMBAT,
  EFT_UNIT_VETERAN,
  EFT_UPGRADE_UNITS,
  EFT_UPGRADE_ONE_STEP,
  EFT_UPGRADE_ONE_LEAP,
  EFT_UPGRADE_ALL_STEP,
  EFT_UPGRADE_ALL_LEAP,
  EFT_UPKEEP_FREE,
  EFT_LAST	/* keep this last */
};

/* An effect conferred by an improvement. */
struct impr_effect {
  enum effect_type type;
  enum effect_range range;
  int amount;
  int survives;			   /* 1 = effect survives wonder destruction */
  Impr_Type_id cond_bldg;	   /* B_LAST = unconditional */
  int cond_gov;			   /* game.government_count = unconditional */
  Tech_Type_id cond_adv;	   /* A_NONE = unconditional; A_LAST = never */
  enum effect_type cond_eff;	   /* EFT_LAST = unconditional */
  Unit_Class_id aff_unit;	   /* UCL_LAST = all */
  enum tile_terrain_type aff_terr; /* T_UNKNOWN = all; T_LAST = none */
  enum tile_special_type aff_spec; /* S_* bit mask of specials affected */
};

/* Maximum number of effects per improvement 
 * (this should not be more than the number of bits in the Eff_Status type) */
#define MAX_EFFECTS 16

/* Type of improvement. (Read from buildings.ruleset file.) */
struct impr_type {
  char name[MAX_LEN_NAME];
  char name_orig[MAX_LEN_NAME];		/* untranslated */
  char graphic_str[MAX_LEN_NAME];	/* city icon of improv. */
  char graphic_alt[MAX_LEN_NAME];	/* city icon of improv. */
  Tech_Type_id tech_req;		/* A_LAST = never; A_NONE = always */
  Impr_Type_id bldg_req;		/* B_LAST = none required */
  enum tile_terrain_type *terr_gate;	/* list; T_LAST terminated */
  enum tile_special_type *spec_gate;	/* list; S_NO_SPECIAL terminated */
  enum effect_range equiv_range;
  Impr_Type_id *equiv_dupl;		/* list; B_LAST terminated */
  Impr_Type_id *equiv_repl;		/* list; B_LAST terminated */
  Tech_Type_id obsolete_by;		/* A_LAST = never obsolete */
  bool is_wonder;
  int build_cost;
  int upkeep;
  int sabotage;
  struct impr_effect *effect;		/* list; .type==EFT_LAST terminated */
  int variant;			/* FIXME: remove when gen-impr obsoletes */
  struct Sprite *sprite;		/* icon of the improvement */
  char *helptext;
  char soundtag[MAX_LEN_NAME];
  char soundtag_alt[MAX_LEN_NAME];
};


extern struct impr_type improvement_types[B_LAST];

/* Range of equivalence (used in equiv_range fields)
 * These must correspond to impr_range_names[] in improvement.c. */
enum impr_range {
  IR_NONE,
  IR_CITY,
  IR_ISLAND,
  IR_PLAYER,
  IR_WORLD,
  IR_LAST      /* keep this last */
};

/* impr range id/string converters */
enum impr_range impr_range_from_str(const char *str);
const char *impr_range_name(enum impr_range id);

/* improvement effect functions */
enum effect_range effect_range_from_str(const char *str);
const char *effect_range_name(enum effect_range id);
enum effect_type effect_type_from_str(const char *str);
const char *effect_type_name(enum effect_type id);

/* improvement functions */
void improvements_free(void);
struct impr_type *get_improvement_type(Impr_Type_id id);
bool improvement_exists(Impr_Type_id id);
int improvement_value(Impr_Type_id id);
bool is_wonder(Impr_Type_id id);
const char *get_improvement_name(Impr_Type_id id);

/* FIXME: remove improvement_variant() when gen-impr obsoletes */
int improvement_variant(Impr_Type_id id);  

bool improvement_obsolete(struct player *pplayer, Impr_Type_id id);
bool improvement_redundant(struct player *pplayer,struct city *pcity,
                          Impr_Type_id id, bool want_to_build);
bool wonder_obsolete(Impr_Type_id id);
bool is_wonder_useful(Impr_Type_id id);
Impr_Type_id find_improvement_by_name(const char *s);
void improvement_status_init(Impr_Status * improvements, size_t elements);

/* player related improvement and unit functions */
bool could_player_eventually_build_improvement(struct player *p, 
                                               Impr_Type_id id);
bool can_player_build_improvement(struct player *p, Impr_Type_id id);

/* city related improvement functions */
void mark_improvement(struct city *pcity,Impr_Type_id id,Impr_Status status);
void allot_island_improvs(void);
void improvements_update_obsolete(void);
void improvements_update_redundant(struct player *pplayer, struct city *pcity,
                                   int cont, enum impr_range range);

/* Iterates over all improvements. Creates a new variable names m_i
 * with type Impr_Type_id which holds the id of the current improvement. */
#define impr_type_iterate(m_i)                                                \
{                                                                             \
  Impr_Type_id m_i;                                                           \
  for (m_i = 0; m_i < game.num_impr_types; m_i++) {

#define impr_type_iterate_end                                                 \
  }                                                                           \
}

#endif  /* FC__IMPROVEMENT_H */
