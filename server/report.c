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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>

#include "fciconv.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "rand.h"
#include "support.h"

#include "events.h"
#include "game.h"
#include "government.h"
#include "packets.h"
#include "player.h"
#include "specialist.h"
#include "unitlist.h"
#include "version.h"

#include "citytools.h"
#include "report.h"
#include "score.h"
#include "srv_main.h"

/* data needed for logging civ score */
struct plrdata_slot {
  char *name;
};

struct logging_civ_score {
  FILE *fp;
  int last_turn;
  struct plrdata_slot *plrdata;
};

static struct logging_civ_score *score_log = NULL;

static void plrdata_slot_init(struct plrdata_slot *plrdata,
                              const char *name);
static void plrdata_slot_replace(struct plrdata_slot *plrdata,
                                 const char *name);
static void plrdata_slot_free(struct plrdata_slot *plrdata);

static void page_conn_etype(struct conn_list *dest, const char *caption,
			    const char *headline, const char *lines,
			    enum event_type event);
enum historian_type {
        HISTORIAN_RICHEST=0, 
        HISTORIAN_ADVANCED=1,
        HISTORIAN_MILITARY=2,
        HISTORIAN_HAPPIEST=3,
        HISTORIAN_LARGEST=4};

#define HISTORIAN_FIRST		HISTORIAN_RICHEST
#define HISTORIAN_LAST 		HISTORIAN_LARGEST

static const char *historian_message[]={
    /* TRANS: year <name> reports ... */
    N_("%s %s reports on the RICHEST Civilizations in the World."),
    /* TRANS: year <name> reports ... */
    N_("%s %s reports on the most ADVANCED Civilizations in the World."),
    /* TRANS: year <name> reports ... */
    N_("%s %s reports on the most MILITARIZED Civilizations in the World."),
    /* TRANS: year <name> reports ... */
    N_("%s %s reports on the HAPPIEST Civilizations in the World."),
    /* TRANS: year <name> reports ... */
    N_("%s %s reports on the LARGEST Civilizations in the World.")
};

static const char *historian_name[]={
    /* TRANS: [year] <name> [reports ...] */
    N_("Herodotus"),
    /* TRANS: [year] <name> [reports ...] */
    N_("Thucydides"),
    /* TRANS: [year] <name> [reports ...] */
    N_("Pliny the Elder"),
    /* TRANS: [year] <name> [reports ...] */
    N_("Livy"),
    /* TRANS: [year] <name> [reports ...] */
    N_("Toynbee"),
    /* TRANS: [year] <name> [reports ...] */
    N_("Gibbon"),
    /* TRANS: [year] <name> [reports ...] */
    N_("Ssu-ma Ch'ien"),
    /* TRANS: [year] <name> [reports ...] */
    N_("Pan Ku")
};

static const char scorelog_magic[] = "#FREECIV SCORELOG2 ";

struct player_score_entry {
  struct player *player;
  int value;
};

struct city_score_entry {
  struct city *city;
  int value;
};

static int get_population(struct player *pplayer);
static int get_landarea(struct player *pplayer);
static int get_settledarea(struct player *pplayer);
static int get_research(struct player *pplayer);
static int get_literacy(struct player *pplayer);
static int get_production(struct player *pplayer);
static int get_economics(struct player *pplayer);
static int get_pollution(struct player *pplayer);
static int get_mil_service(struct player *pplayer);

static const char *area_to_text(int value);
static const char *percent_to_text(int value);
static const char *production_to_text(int value);
static const char *economics_to_text(int value);
static const char *science_to_text(int value);
static const char *mil_service_to_text(int value);
static const char *pollution_to_text(int value);

#define GOOD_PLAYER(p) ((p)->is_alive && !is_barbarian(p))

/*
 * Describes a row.
 */
static struct dem_row {
  const char key;
  const char *name;
  int (*get_value) (struct player *);
  const char *(*to_text) (int);
  bool greater_values_are_better;
} rowtable[] = {
  {'N', N_("Population"),       get_population,  population_to_text,  TRUE },
  {'A', N_("Land Area"),        get_landarea,    area_to_text,        TRUE },
  {'S', N_("Settled Area"),     get_settledarea, area_to_text,        TRUE },
  {'R', N_("Research Speed"),   get_research,    science_to_text,     TRUE },
  {'L', N_("Literacy"),         get_literacy,    percent_to_text,     TRUE },
  {'P', N_("Production"),       get_production,  production_to_text,  TRUE },
  {'E', N_("Economics"),        get_economics,   economics_to_text,   TRUE },
  {'M', N_("Military Service"), get_mil_service, mil_service_to_text, FALSE },
  {'O', N_("Pollution"),        get_pollution,   pollution_to_text,   FALSE }
};

/* Demographics columns. */
enum dem_flag {
  DEM_COL_QUANTITY,
  DEM_COL_RANK,
  DEM_COL_BEST,
  DEM_COL_LAST
};
BV_DEFINE(bv_cols, DEM_COL_LAST);
static struct dem_col {
  char key;
} coltable[] = {{'q'}, {'r'}, {'b'}}; /* Corresponds to dem_flag enum */

/* prime number of entries makes for better scaling */
static const char *ranking[] = {
  /* TRANS: <#>: The <ranking> Poles */
  N_("%2d: The Supreme %s"),
  /* TRANS: <#>: The <ranking> Poles */
  N_("%2d: The Magnificent %s"),
  /* TRANS: <#>: The <ranking> Poles */
  N_("%2d: The Great %s"),
  /* TRANS: <#>: The <ranking> Poles */
  N_("%2d: The Glorious %s"),
  /* TRANS: <#>: The <ranking> Poles */
  N_("%2d: The Excellent %s"),
  /* TRANS: <#>: The <ranking> Poles */
  N_("%2d: The Eminent %s"),
  /* TRANS: <#>: The <ranking> Poles */
  N_("%2d: The Distinguished %s"),
  /* TRANS: <#>: The <ranking> Poles */
  N_("%2d: The Average %s"),
  /* TRANS: <#>: The <ranking> Poles */
  N_("%2d: The Mediocre %s"),
  /* TRANS: <#>: The <ranking> Poles */
  N_("%2d: The Ordinary %s"),
  /* TRANS: <#>: The <ranking> Poles */
  N_("%2d: The Pathetic %s"),
  /* TRANS: <#>: The <ranking> Poles */
  N_("%2d: The Useless %s"),
  /* TRANS: <#>: The <ranking> Poles */
  N_("%2d: The Valueless %s"),
  /* TRANS: <#>: The <ranking> Poles */
  N_("%2d: The Worthless %s"),
  /* TRANS: <#>: The <ranking> Poles */
  N_("%2d: The Wretched %s"),
};

/**************************************************************************
...
**************************************************************************/
static int secompare(const void *a, const void *b)
{
  return (((const struct player_score_entry *)b)->value -
	  ((const struct player_score_entry *)a)->value);
}

/**************************************************************************
...
**************************************************************************/
static void historian_generic(enum historian_type which_news)
{
  int i, j = 0, rank = 0;
  char buffer[4096];
  char title[1024];
  struct player_score_entry size[player_count()];

  players_iterate(pplayer) {
    if (GOOD_PLAYER(pplayer)) {
      switch(which_news) {
      case HISTORIAN_RICHEST:
	size[j].value = pplayer->economic.gold;
	break;
      case HISTORIAN_ADVANCED:
	size[j].value
	  = pplayer->score.techs + get_player_research(pplayer)->future_tech;
	break;
      case HISTORIAN_MILITARY:
	size[j].value = pplayer->score.units;
	break;
      case HISTORIAN_HAPPIEST: 
	size[j].value =
	    (((pplayer->score.happy - pplayer->score.unhappy) * 1000) /
	     (1 + total_player_citizens(pplayer)));
	break;
      case HISTORIAN_LARGEST:
	size[j].value = total_player_citizens(pplayer);
	break;
      }
      size[j].player = pplayer;
      j++;
    } /* else the player is dead or barbarian or observer */
  } players_iterate_end;

  qsort(size, j, sizeof(size[0]), secompare);
  buffer[0] = '\0';
  for (i = 0; i < j; i++) {
    if (i > 0 && size[i].value < size[i - 1].value) {
      /* since i < j, only top entry reigns Supreme */
      rank = ((i * ARRAY_SIZE(ranking)) / j) + 1;
    }
    if (rank >= ARRAY_SIZE(ranking)) {
      /* clamp to final entry */
      rank = ARRAY_SIZE(ranking) - 1;
    }
    cat_snprintf(buffer, sizeof(buffer),
		 _(ranking[rank]),
		 i + 1,
		 nation_plural_for_player(size[i].player));
    fc_strlcat(buffer, "\n", sizeof(buffer));
  }
  fc_snprintf(title, sizeof(title), _(historian_message[which_news]),
              textyear(game.info.year),
              _(historian_name[fc_rand(ARRAY_SIZE(historian_name))]));
  page_conn_etype(game.est_connections, _("Historian Publishes!"),
		  title, buffer, E_BROADCAST_REPORT);
}

/**************************************************************************
 Returns the number of wonders the given city has.
**************************************************************************/
static int nr_wonders(struct city *pcity)
{
  int result = 0;

  city_built_iterate(pcity, i) {
    if (is_great_wonder(i)) {
      result++;
    }
  } city_built_iterate_end;

  return result;
}

/**************************************************************************
  Send report listing the "best" 5 cities in the world.
**************************************************************************/
void report_top_five_cities(struct conn_list *dest)
{
  const int NUM_BEST_CITIES = 5;
  /* a wonder equals WONDER_FACTOR citizen */
  const int WONDER_FACTOR = 5;
  struct city_score_entry size[NUM_BEST_CITIES];
  int i;
  char buffer[4096];

  for (i = 0; i < NUM_BEST_CITIES; i++) {
    size[i].value = 0;
    size[i].city = NULL;
  }

  players_iterate(pplayer) {
    city_list_iterate(pplayer->cities, pcity) {
      int value_of_pcity = pcity->size + nr_wonders(pcity) * WONDER_FACTOR;

      if (value_of_pcity > size[NUM_BEST_CITIES - 1].value) {
	size[NUM_BEST_CITIES - 1].value = value_of_pcity;
	size[NUM_BEST_CITIES - 1].city = pcity;
	qsort(size, NUM_BEST_CITIES, sizeof(size[0]), secompare);
      }
    } city_list_iterate_end;
  } players_iterate_end;

  buffer[0] = '\0';
  for (i = 0; i < NUM_BEST_CITIES; i++) {
    int wonders;

    if (!size[i].city) {
	/* 
	 * pcity may be NULL if there are less then NUM_BEST_CITIES in
	 * the whole game.
	 */
      break;
    }

    cat_snprintf(buffer, sizeof(buffer),
		 _("%2d: The %s City of %s of size %d, "), i + 1,
		 nation_adjective_for_player(city_owner(size[i].city)),
		 city_name(size[i].city),
		 size[i].city->size);

    wonders = nr_wonders(size[i].city);
    if (wonders == 0) {
      cat_snprintf(buffer, sizeof(buffer), _("with no wonders\n"));
    } else {
      cat_snprintf(buffer, sizeof(buffer),
		   PL_("with %d wonder\n", "with %d wonders\n", wonders),
		   wonders);}
  }
  page_conn(dest, _("Traveler's Report:"),
	    _("The Five Greatest Cities in the World!"), buffer);
}

/**************************************************************************
  Send report listing all built and destroyed wonders, and wonders
  currently being built.
**************************************************************************/
void report_wonders_of_the_world(struct conn_list *dest)
{
  char buffer[4096];

  buffer[0] = '\0';

  improvement_iterate(i) {
    if (is_great_wonder(i)) {
      struct city *pcity = find_city_from_great_wonder(i);

      if (pcity) {
	cat_snprintf(buffer, sizeof(buffer), _("%s in %s (%s)\n"),
		     city_improvement_name_translation(pcity, i),
		     city_name(pcity),
		     nation_adjective_for_player(city_owner(pcity)));
      } else if (great_wonder_is_destroyed(i)) {
	cat_snprintf(buffer, sizeof(buffer), _("%s has been DESTROYED\n"),
		     improvement_name_translation(i));
      }
    }
  } improvement_iterate_end;

  improvement_iterate(i) {
    if (is_great_wonder(i)) {
      players_iterate(pplayer) {
	city_list_iterate(pplayer->cities, pcity) {
	  if (VUT_IMPROVEMENT == pcity->production.kind
	   && pcity->production.value.building == i) {
	    cat_snprintf(buffer, sizeof(buffer),
			 _("(building %s in %s (%s))\n"),
			 improvement_name_translation(i),
			 city_name(pcity),
			 nation_adjective_for_player(pplayer));
	  }
	} city_list_iterate_end;
      } players_iterate_end;
    }
  } improvement_iterate_end;

  page_conn(dest, _("Traveler's Report:"),
	    _("Wonders of the World"), buffer);
}

/**************************************************************************
 Helper functions which return the value for the given player.
**************************************************************************/
static int get_population(struct player *pplayer)
{
  return pplayer->score.population;
}

static int get_pop(struct player *pplayer)
{
  return total_player_citizens(pplayer);
}

static int get_landarea(struct player *pplayer)
{
    return pplayer->score.landarea;
}

static int get_settledarea(struct player *pplayer)
{
  return pplayer->score.settledarea;
}

static int get_research(struct player *pplayer)
{
  return pplayer->score.techout;
}

static int get_literacy(struct player *pplayer)
{
  int pop = civ_population(pplayer);

  if (pop <= 0) {
    return 0;
  } else if (pop >= 10000) {
    return pplayer->score.literacy / (pop / 100);
  } else {
    return (pplayer->score.literacy * 100) / pop;
  }
}

static int get_production(struct player *pplayer)
{
  return pplayer->score.mfg;
}

static int get_economics(struct player *pplayer)
{
  return pplayer->score.bnp;
}

static int get_pollution(struct player *pplayer)
{
  return pplayer->score.pollution;
}

static int get_mil_service(struct player *pplayer)
{
  return (pplayer->score.units * 5000) / (10 + civ_population(pplayer));
}

static int get_cities(struct player *pplayer)
{
  return pplayer->score.cities;
}

static int get_techs(struct player *pplayer)
{
  return pplayer->score.techs;
}

static int get_munits(struct player *pplayer)
{
  int result = 0;

  /* count up military units */
  unit_list_iterate(pplayer->units, punit) {
    if (is_military_unit(punit)) {
      result++;
    }
  } unit_list_iterate_end;

  return result;
}

static int get_settlers(struct player *pplayer)
{
  int result = 0;

  /* count up settlers */
  unit_list_iterate(pplayer->units, punit) {
    if (unit_has_type_flag(punit, F_CITIES)) {
      result++;
    }
  } unit_list_iterate_end;

  return result;
}

static int get_wonders(struct player *pplayer)
{
  return pplayer->score.wonders;
}

static int get_techout(struct player *pplayer)
{
  return pplayer->score.techout;
}

static int get_literacy2(struct player *pplayer)
{
  return pplayer->score.literacy;
}

static int get_spaceship(struct player *pplayer)
{
  return pplayer->score.spaceship;
}

static int get_gold(struct player *pplayer)
{
  return pplayer->economic.gold;
}

static int get_taxrate(struct player *pplayer)
{
  return pplayer->economic.tax;
}

static int get_scirate(struct player *pplayer)
{
  return pplayer->economic.science;
}

static int get_luxrate(struct player *pplayer)
{
  return pplayer->economic.luxury;
}

static int get_riots(struct player *pplayer)
{
  int result = 0;

  city_list_iterate(pplayer->cities, pcity) {
    if (pcity->anarchy > 0) {
      result++;
    }
  } city_list_iterate_end;

  return result;
}

static int get_happypop(struct player *pplayer)
{
  return pplayer->score.happy;
}

static int get_contentpop(struct player *pplayer)
{
  return pplayer->score.content;
}

static int get_unhappypop(struct player *pplayer)
{
  return pplayer->score.unhappy;
}

static int get_specialists(struct player *pplayer)
{
  int count = 0;

  specialist_type_iterate(sp) {
    count += pplayer->score.specialists[sp];
  } specialist_type_iterate_end;

  return count;
}

static int get_gov(struct player *pplayer)
{
  return government_number(government_of_player(pplayer));
}

static int get_corruption(struct player *pplayer)
{
  int result = 0;

  city_list_iterate(pplayer->cities, pcity) {
    result += pcity->waste[O_TRADE];
  } city_list_iterate_end;

  return result;
}

static int get_total_score(struct player *pplayer)
{
  return pplayer->score.game;
}

/**************************************************************************
...
**************************************************************************/
static const char *value_units(int val, const char *uni)
{
  static char buf[64];

  if (fc_snprintf(buf, sizeof(buf), "%s%s", int_to_text(val), uni) == -1) {
    log_error("String truncated in value_units()!");
  }

  return buf;
}

/**************************************************************************
  Helper functions which transform the given value to a string
  depending on the unit.
**************************************************************************/
static const char *area_to_text(int value)
{
  /* TRANS: abbreviation of "square miles" */
  return value_units(value, PL_(" sq. mi.", " sq. mi.", value));
}

static const char *percent_to_text(int value)
{
  return value_units(value, "%");
}

static const char *production_to_text(int value)
{
  int clip = MAX(0, value);
  /* TRANS: "M tons" = million tons, so always plural */
  return value_units(clip, PL_(" M tons", " M tons", clip));
}

static const char *economics_to_text(int value)
{
  /* TRANS: "M goods" = million goods, so always plural */
  return value_units(value, PL_(" M goods", " M goods", value));
}

static const char *science_to_text(int value)
{
  return value_units(value, PL_(" bulb", " bulbs", value));
}

static const char *mil_service_to_text(int value)
{
  return value_units(value, PL_(" month", " months", value));
}

static const char *pollution_to_text(int value)
{
  return value_units(value, PL_(" ton", " tons", value));
}

/**************************************************************************
...
**************************************************************************/
static const char *number_to_ordinal_string(int num)
{
  static char buf[16];
  char fmt[] = "(%d%s)";

  fc_assert_action(num > 0, num = (num % 10) + 10);

  if ((num % 10) == 1 && num != 11) {
    fc_snprintf(buf, sizeof(buf), fmt, num, _("st"));
  } else if ((num % 10) == 2 && num != 12) {
    fc_snprintf(buf, sizeof(buf), fmt, num, _("nd"));
  } else if ((num % 10) == 3 && num != 13) {
    fc_snprintf(buf, sizeof(buf), fmt, num, _("rd"));
  } else {
    fc_snprintf(buf, sizeof(buf), fmt, num, _("th"));
  }

  return buf;
}

/**************************************************************************
...
**************************************************************************/
static void dem_line_item(char *outptr, size_t out_size,
			  struct player *pplayer, struct dem_row *prow,
			  bv_cols selcols)
{
  if (BV_ISSET(selcols, DEM_COL_QUANTITY)) {
    const char *text = prow->to_text(prow->get_value(pplayer));

    cat_snprintf(outptr, out_size, " %s", text);
    cat_snprintf(outptr, out_size, "%*s",
                 18 - (int) get_internal_string_length(text), "");
  }

  if (BV_ISSET(selcols, DEM_COL_RANK)) {
    int basis = prow->get_value(pplayer);
    int place = 1;

    players_iterate(other) {
      if (GOOD_PLAYER(other)
	  && ((prow->greater_values_are_better
	       && prow->get_value(other) > basis)
	      || (!prow->greater_values_are_better
	          && prow->get_value(other) < basis))) {
	place++;
      }
    } players_iterate_end;

    cat_snprintf(outptr, out_size, " %6s", number_to_ordinal_string(place));
  }

  if (BV_ISSET(selcols, DEM_COL_BEST)) {
    struct player *best_player = pplayer;
    int best_value = prow->get_value(pplayer);

    players_iterate(other) {
      if (GOOD_PLAYER(other)) {
	int value = prow->get_value(other);

	if ((prow->greater_values_are_better && value > best_value)
	    || (!prow->greater_values_are_better && value < best_value)) {
	  best_player = other;
	  best_value = value;
	}
      }
    } players_iterate_end;

    if(player_has_embassy(pplayer, best_player) && (pplayer != best_player)) {
      cat_snprintf(outptr, out_size, "   %s: %s",
		   nation_plural_for_player(best_player),
		   prow->to_text(prow->get_value(best_player)));
    }
  }
}

/*************************************************************************
  Verify that a given demography string is valid.  See
  game.demography. If the string is not valid the index of the _first_
  invalid character is return as 'error'.

  Other settings callback functions are in settings.c, but this one uses
  static values from this file so it's done separately.
*************************************************************************/
bool is_valid_demography(const char *demography, int *error)
{
  int len = strlen(demography), i;

  /* We check each character individually to see if it's valid.  This
   * does not check for duplicate entries. */
  for (i = 0; i < len; i++) {
    bool found = FALSE;
    int j;

    /* See if the character is a valid column label. */
    for (j = 0; j < DEM_COL_LAST; j++) {
      if (demography[i] == coltable[j].key) {
	found = TRUE;
	break;
      }
    }

    if (found) {
      continue;
    }

    /* See if the character is a valid row label. */
    for (j = 0; j < ARRAY_SIZE(rowtable); j++) {
      if (demography[i] == rowtable[j].key) {
	found = TRUE;
	break;
      }
    }

    if (!found) {
      if (error != NULL) {
        (*error) = i;
      }
      /* The character is invalid. */
      return FALSE;
    }
  }

  /* Looks like all characters were valid. */
  return TRUE;
}

/*************************************************************************
  Send demographics report; what gets reported depends on value of
  demographics server option.  
*************************************************************************/
void report_demographics(struct connection *pconn)
{
  char civbuf[1024];
  char buffer[4096];
  unsigned int i;
  bool anyrows;
  bv_cols selcols;
  int numcols = 0;
  struct player *pplayer = pconn->playing;

  BV_CLR_ALL(selcols);
  fc_assert_ret(ARRAY_SIZE(coltable) == DEM_COL_LAST);
  for (i = 0; i < DEM_COL_LAST; i++) {
    if (strchr(game.server.demography, coltable[i].key)) {
      BV_SET(selcols, i);
      numcols++;
    }
  }

  anyrows = FALSE;
  for (i = 0; i < ARRAY_SIZE(rowtable); i++) {
    if (strchr(game.server.demography, rowtable[i].key)) {
      anyrows = TRUE;
      break;
    }
  }

  if (!pplayer || !pplayer->is_alive || !anyrows || numcols == 0) {
    page_conn(pconn->self, _("Demographics Report:"),
	      _("Sorry, the Demographics report is unavailable."), "");
    return;
  }

  /* TRANS: <nation adjective> <government name> (<year>).
   * E.g. "Polish Despotism (200 AD)". */
  fc_snprintf(civbuf, sizeof(civbuf), _("%s %s (%s)"),
              nation_adjective_for_player(pplayer),
              government_name_for_player(pplayer),
              textyear(game.info.year));

  buffer[0] = '\0';
  for (i = 0; i < ARRAY_SIZE(rowtable); i++) {
    if (strchr(game.server.demography, rowtable[i].key)) {
      const char *name = _(rowtable[i].name);

      cat_snprintf(buffer, sizeof(buffer), "%s", name);
      cat_snprintf(buffer, sizeof(buffer), "%*s",
                   18 - (int) get_internal_string_length(name), "");
      dem_line_item(buffer, sizeof(buffer), pplayer, &rowtable[i], selcols);
      sz_strlcat(buffer, "\n");
    }
  }

  page_conn(pconn->self, _("Demographics Report:"), civbuf, buffer);
}

/**************************************************************************
  ...
**************************************************************************/
static void plrdata_slot_init(struct plrdata_slot *plrdata,
                              const char *name)
{
  fc_assert_ret(plrdata->name == NULL);

  plrdata->name = fc_calloc(MAX_LEN_NAME, sizeof(plrdata->name));
  plrdata_slot_replace(plrdata, name);
}

/**************************************************************************
  ...
**************************************************************************/
static void plrdata_slot_replace(struct plrdata_slot *plrdata,
                                 const char *name)
{
  fc_assert_ret(plrdata->name != NULL);

  fc_strlcpy(plrdata->name, name, MAX_LEN_NAME);
}

/**************************************************************************
  ...
**************************************************************************/
static void plrdata_slot_free(struct plrdata_slot *plrdata)
{
  if (plrdata->name != NULL) {
    free(plrdata->name);
    plrdata->name = NULL;
  }
}

/**************************************************************************
  Reads the whole file denoted by fp. Sets last_turn and id to the
  values contained in the file. Returns the player_names indexed by
  player_no at the end of the log file.

  Returns TRUE iff the file had read successfully.
**************************************************************************/
static bool scan_score_log(char *id)
{
  int line_nr, turn, plr_no, spaces;
  struct plrdata_slot *plrdata;
  char plr_name[MAX_LEN_NAME], line[80], *ptr;

  fc_assert_ret_val(score_log != NULL, FALSE);
  fc_assert_ret_val(score_log->fp != NULL, FALSE);

  score_log->last_turn = -1;
  id[0] = '\0';

  for (line_nr = 1;; line_nr++) {
    if (!fgets(line, sizeof(line), score_log->fp)) {
      if (feof(score_log->fp) != 0) {
        break;
      }
      log_error("[%s:-] Can't read scorelog file header!",
                game.server.scorefile);
      return FALSE;
    }

    ptr = strchr(line, '\n');
    if (!ptr) {
      log_error("[%s:%d] Line too long!", game.server.scorefile, line_nr);
      return FALSE;
    }
    *ptr = '\0';

    if (line_nr == 1) {
      if (strncmp(line, scorelog_magic, strlen(scorelog_magic)) != 0) {
        log_error("[%s:%d] Bad file magic!", game.server.scorefile, line_nr);
        return FALSE;
      }
    }

    if (strncmp(line, "id ", strlen("id ")) == 0) {
      if (strlen(id) > 0) {
        log_error("[%s:%d] Multiple ID entries!", game.server.scorefile,
                  line_nr);
        return FALSE;
      }
      fc_strlcpy(id, line + strlen("id "), MAX_LEN_GAME_IDENTIFIER);
      if (strcmp(id, server.game_identifier) != 0) {
        log_error("[%s:%d] IDs don't match! game='%s' scorelog='%s'",
                  game.server.scorefile, line_nr, server.game_identifier,
                  id);
        return FALSE;
      }
    }

    if (strncmp(line, "turn ", strlen("turn ")) == 0) {
      if (sscanf(line + strlen("turn "), "%d", &turn) != 1) {
        log_error("[%s:%d] Bad line (turn)!", game.server.scorefile,
                  line_nr);
        return FALSE;
      }

      fc_assert_ret_val(turn > score_log->last_turn, FALSE);
      score_log->last_turn = turn;
    }

    if (strncmp(line, "addplayer ", strlen("addplayer ")) == 0) {
      if (3 != sscanf(line + strlen("addplayer "), "%d %d %s",
                      &turn, &plr_no, plr_name)) {
        log_error("[%s:%d] Bad line (addplayer)!",
                  game.server.scorefile, line_nr);
        return FALSE;
      }

      /* Now get the complete player name if there are several parts. */
      ptr = line + strlen("addplayer ");
      spaces = 0;
      while (*ptr != '\0' && spaces < 2) {
        if (*ptr == ' ') {
          spaces++;
        }
        ptr++;
      }
      fc_snprintf(plr_name, sizeof(plr_name), "%s", ptr);
      log_debug("add player '%s' (from line %d: '%s')", plr_name, line_nr,
                line);

      if (0 > plr_no || plr_no >= player_slot_count()) {
        log_error("[%s:%d] Invalid player number: %d!",
                  game.server.scorefile, line_nr, plr_no);
        return FALSE;
      }

      plrdata = score_log->plrdata + plr_no;
      if (plrdata->name != NULL) {
        log_error("[%s:%d] Two names for one player (id %d)!",
                  game.server.scorefile, line_nr, plr_no);
        return FALSE;
      }

      plrdata_slot_init(plrdata, plr_name);
    }

    if (strncmp(line, "delplayer ", strlen("delplayer ")) == 0) {
      if (2 != sscanf(line + strlen("delplayer "), "%d %d",
                      &turn, &plr_no)) {
        log_error("[%s:%d] Bad line (delplayer)!",
                  game.server.scorefile, line_nr);
        return FALSE;
      }

      if (!(plr_no >= 0 && plr_no < player_slot_count())) {
        log_error("[%s:%d] Invalid player number: %d!",
                  game.server.scorefile, line_nr, plr_no);
        return FALSE;
      }

      plrdata = score_log->plrdata + plr_no;
      if (plrdata->name == NULL) {
        log_error("[%s:%d] Trying to remove undefined player (id %d)!",
                  game.server.scorefile, line_nr, plr_no);
        return FALSE;
      }

      plrdata_slot_free(plrdata);
    }
  }

  if (score_log->last_turn == -1) {
    log_error("[%s:-] Scorelog contains no turn!", game.server.scorefile);
    return FALSE;
  }

  if (strlen(id) == 0) {
    log_error("[%s:-] Scorelog contains no ID!", game.server.scorefile);
    return FALSE;
  }

  if (score_log->last_turn + 1 != game.info.turn) {
    log_error("[%s:-] Scorelog doesn't match savegame!",
              game.server.scorefile);
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
  ...
**************************************************************************/
void log_civ_score_init(void)
{
  fc_assert_ret(score_log == NULL);

  score_log = fc_calloc(1, sizeof(*score_log));
  score_log->fp = NULL;
  score_log->last_turn = -1;
  score_log->plrdata = fc_calloc(player_slot_count(),
                                 sizeof(*score_log->plrdata));
  player_slots_iterate(pslot) {
    struct plrdata_slot *plrdata = score_log->plrdata
                                   + player_slot_index(pslot);
    plrdata->name = NULL;
  } player_slots_iterate_end;
}

/**************************************************************************
  ...
**************************************************************************/
void log_civ_score_free(void)
{
  if (!score_log) {
    /* nothing to do */
    return;
  }

  if (score_log->fp) {
    fclose(score_log->fp);
    score_log->fp = NULL;
  }

  if (score_log->plrdata) {
    player_slots_iterate(pslot) {
      struct plrdata_slot *plrdata = score_log->plrdata
                                     + player_slot_index(pslot);
      if (plrdata->name != NULL) {
        free(plrdata->name);
      }
    } player_slots_iterate_end;
    free(score_log->plrdata);
  }

  free(score_log);
  score_log = NULL;
}

/**************************************************************************
  Create a log file of the civilizations so you can see what was happening.
**************************************************************************/
void log_civ_score_now(void)
{
  enum { SL_CREATE, SL_APPEND, SL_UNSPEC } oper = SL_UNSPEC;
  char id[MAX_LEN_GAME_IDENTIFIER];
  int i = 0;

  /* Add new tags only at end of this list. Maintaining the order of
   * old tags is critical. */
  static const struct {
    char *name;
    int (*get_value) (struct player *);
  } score_tags[] = {
    {"pop",             get_pop},
    {"bnp",             get_economics},
    {"mfg",             get_production},
    {"cities",          get_cities},
    {"techs",           get_techs},
    {"munits",          get_munits},
    {"settlers",        get_settlers},  /* "original" tags end here */

    {"wonders",         get_wonders},
    {"techout",         get_techout},
    {"landarea",        get_landarea},
    {"settledarea",     get_settledarea},
    {"pollution",       get_pollution},
    {"literacy",        get_literacy2},
    {"spaceship",       get_spaceship}, /* new 1.8.2 tags end here */

    {"gold",            get_gold},
    {"taxrate",         get_taxrate},
    {"scirate",         get_scirate},
    {"luxrate",         get_luxrate},
    {"riots",           get_riots},
    {"happypop",        get_happypop},
    {"contentpop",      get_contentpop},
    {"unhappypop",      get_unhappypop},
    {"specialists",     get_specialists},
    {"gov",             get_gov},
    {"corruption",      get_corruption}, /* new 1.11.5 tags end here */

    {"score",           get_total_score} /* New 2.1.10 tag */
  };

  if (!game.server.scorelog) {
    return;
  }

  if (!score_log) {
    return;
  }

  if (!score_log->fp) {
    if (game.info.year == GAME_START_YEAR) {
      oper = SL_CREATE;
    } else {
      score_log->fp = fc_fopen(game.server.scorefile, "r");
      if (!score_log->fp) {
        oper = SL_CREATE;
      } else {
        if (!scan_score_log(id)) {
          goto log_civ_score_disable;
        }
        oper = SL_APPEND;

        fclose(score_log->fp);
        score_log->fp = NULL;
      }
    }

    switch (oper) {
    case SL_CREATE:
      score_log->fp = fc_fopen(game.server.scorefile, "w");
      if (!score_log->fp) {
        log_error("Can't open scorelog file '%s' for creation!",
                  game.server.scorefile);
        goto log_civ_score_disable;
      }
      fprintf(score_log->fp, "%s%s\n", scorelog_magic, VERSION_STRING);
      fprintf(score_log->fp,
              "\n"
              "# For a specification of the format of this see doc/README.scorelog or \n"
              "# <http://svn.gna.org/viewcvs/freeciv/trunk/doc/README.scorelog?view=auto>.\n"
              "\n");

      fprintf(score_log->fp, "id %s\n", server.game_identifier);
      for (i = 0; i < ARRAY_SIZE(score_tags); i++) {
        fprintf(score_log->fp, "tag %d %s\n", i, score_tags[i].name);
      }
      break;
    case SL_APPEND:
      score_log->fp = fc_fopen(game.server.scorefile, "a");
      if (!score_log->fp) {
        log_error("Can't open scorelog file '%s' for appending!",
                  game.server.scorefile);
        goto log_civ_score_disable;
      }
      break;
    default:
      log_error("[%s] bad operation %d", __FUNCTION__, (int) oper);
      goto log_civ_score_disable;
    }
  }

  if (game.info.turn > score_log->last_turn) {
    fprintf(score_log->fp, "turn %d %d %s\n", game.info.turn, game.info.year,
            textyear(game.info.year));
    score_log->last_turn = game.info.turn;
  }

  player_slots_iterate(pslot) {
    struct plrdata_slot *plrdata = score_log->plrdata
                                   + player_slot_index(pslot);
    if (plrdata->name != NULL
        && player_slot_is_used(pslot)
        && !GOOD_PLAYER(player_slot_get_player(pslot))) {
      fprintf(score_log->fp, "delplayer %d %d\n", game.info.turn - 1, i);
      plrdata_slot_free(plrdata);
    }
  } player_slots_iterate_end;

  players_iterate(pplayer) {
    struct plrdata_slot *plrdata = score_log->plrdata + player_index(pplayer);
    if (plrdata->name == NULL && GOOD_PLAYER(pplayer)) {
      fprintf(score_log->fp, "addplayer %d %d %s\n", game.info.turn,
              player_number(pplayer), player_name(pplayer));
      plrdata_slot_init(plrdata, player_name(pplayer));
    }
  } players_iterate_end;

  players_iterate(pplayer) {
    struct plrdata_slot *plrdata = score_log->plrdata + player_index(pplayer);

    if (GOOD_PLAYER(pplayer)
        && strcmp(plrdata->name, player_name(pplayer)) != 0) {
      log_debug("player names does not match '%s' != '%s'", plrdata->name,
                player_name(pplayer));
      fprintf(score_log->fp, "delplayer %d %d\n", game.info.turn - 1,
              player_number(pplayer));
      fprintf(score_log->fp, "addplayer %d %d %s\n", game.info.turn,
              player_number(pplayer), player_name(pplayer));
      plrdata_slot_replace(plrdata, player_name(pplayer));
    }
  } players_iterate_end;

  for (i = 0; i < ARRAY_SIZE(score_tags); i++) {
    players_iterate(pplayer) {
      if (!GOOD_PLAYER(pplayer)) {
        continue;
      }

      fprintf(score_log->fp, "data %d %d %d %d\n", game.info.turn, i,
              player_number(pplayer), score_tags[i].get_value(pplayer));
    } players_iterate_end;
  }

  fflush(score_log->fp);

  return;

log_civ_score_disable:

  log_civ_score_free();
}

/**************************************************************************
  ...
**************************************************************************/
void make_history_report(void)
{
  if (player_count() == 1) {
    return;
  }

  if (game.server.scoreturn > game.info.turn) {
    return;
  }

  game.server.scoreturn = (game.info.turn + GAME_DEFAULT_SCORETURN
                           + fc_rand(GAME_DEFAULT_SCORETURN));

  historian_generic(game.server.scoreturn % HISTORIAN_LAST);
}

/**************************************************************************
  Inform clients about player scores and statistics when the game ends.
  Called only from server/srv_main.c srv_scores()
**************************************************************************/
void report_final_scores(struct conn_list *dest)
{
  int i, j = 0;
  struct player_score_entry size[player_count()];
  struct packet_endgame_report packet;

  if (!dest) {
    dest = game.est_connections;
  }

  players_iterate(pplayer) {
    if (GOOD_PLAYER(pplayer)) {
      size[j].value = pplayer->score.game;
      size[j].player = pplayer;
      j++;
    }
  } players_iterate_end;

  qsort(size, j, sizeof(size[0]), secompare);

  packet.nscores = j;
  for (i = 0; i < j; i++) {
    packet.id[i] = player_number(size[i].player);
    packet.score[i] = size[i].value;
    packet.pop[i] = get_pop(size[i].player) * 1000; 
    packet.bnp[i] = get_economics(size[i].player); 
    packet.mfg[i] = get_production(size[i].player); 
    packet.cities[i] = get_cities(size[i].player); 
    packet.techs[i] = get_techs(size[i].player);
    packet.mil_service[i] = get_mil_service(size[i].player); 
    packet.wonders[i] = get_wonders(size[i].player); 
    packet.research[i] = get_research(size[i].player); 
    packet.landarea[i] = get_landarea(size[i].player); 
    packet.settledarea[i] = get_settledarea(size[i].player); 
    packet.literacy[i] = get_literacy(size[i].player); 
    packet.spaceship[i] = get_spaceship(size[i].player); 
  }  

  lsend_packet_endgame_report(dest, &packet);
}	

/**************************************************************************
This function pops up a non-modal message dialog on the player's desktop
**************************************************************************/
void page_conn(struct conn_list *dest, const char *caption, 
	       const char *headline, const char *lines) {
  page_conn_etype(dest, caption, headline, lines, E_REPORT);
}


/**************************************************************************
This function pops up a non-modal message dialog on the player's desktop

event == E_REPORT: message should not be ignored by clients watching
                   AI players with ai_popup_windows off.  Example:
                   Server Options, Demographics Report, etc.

event == E_BROADCAST_REPORT: message can safely be ignored by clients
                   watching AI players with ai_popup_windows off.  For
                   example: Herodot's report... and similar messages.
**************************************************************************/
static void page_conn_etype(struct conn_list *dest, const char *caption,
			    const char *headline, const char *lines,
			    enum event_type event)
{
  int len;
  struct packet_page_msg genmsg;

  len = fc_snprintf(genmsg.message, sizeof(genmsg.message),
                    "%s\n%s\n%s", caption, headline, lines);
  if (len == -1) {
    log_error("Message truncated in page_conn_etype()!");
  }
  genmsg.event = event;
  
  lsend_packet_page_msg(dest, &genmsg);
}
