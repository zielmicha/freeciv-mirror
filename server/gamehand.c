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
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "capability.h"
#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "rand.h"
#include "registry.h"
#include "support.h"
#include "version.h"

#include "cityturn.h"
#include "civserver.h"
#include "maphand.h"
#include "meta.h"
#include "plrhand.h"
#include "ruleset.h"
#include "unitfunc.h"
#include "unittools.h"

#include "gamehand.h"

extern char metaserver_info_line[];
extern char metaserver_addr[];

#define SAVEFILE_OPTIONS "1.7 startoptions unirandom spacerace2 rulesets \
diplchance_percent"

/**************************************************************************
...
**************************************************************************/
void init_new_game(void)
{
  int i, j, x, y;
  int vx, vy, dx, dy;
  Unit_Type_id utype;
  int start_pos[MAX_NUM_PLAYERS]; /* indices into map.start_positions[] */
  
  if (!map.fixed_start_positions) {
    /* except in a scenario which provides them,
       shuffle the start positions around... */
    assert(game.nplayers==map.num_start_positions);
    for (i=0; i<game.nplayers;i++) { /* no advantage to the romans!! */
      j=myrand(game.nplayers);
      x=map.start_positions[j].x;
      y=map.start_positions[j].y;
      map.start_positions[j].x=map.start_positions[i].x;
      map.start_positions[j].y=map.start_positions[i].y;
      map.start_positions[i].x=x;
      map.start_positions[i].y=y;
    }
    for(i=0; i<game.nplayers; i++) {
      start_pos[i] = i;
    } 
  } else {
  /* In a scenario, choose starting positions by nation.
     If there are too few starts for number of nations, assign
     to nations with specific starts first, then assign rest
     to random from remainder.  (Would be better to label start
     positions by nation etc, but this will do for now. --dwp)
  */
    const int npos = map.num_start_positions;
    int *pos_used = fc_calloc(npos, sizeof(int));
    int nrem = npos;		/* remaining unused starts */
    
    for(i=0; i<game.nplayers; i++) {
      int nation = game.players[i].nation;
      if (nation < npos) {
	start_pos[i] = nation;
	pos_used[nation] = 1;
	nrem--;
      } else {
	start_pos[i] = npos;
      }
    }
    for(i=0; i<game.nplayers; i++) {
      if (start_pos[i] == npos) {
	int k;
	assert(nrem>0);
	k = myrand(nrem);
	for(j=0; j<npos; j++) {
	  if (!pos_used[j] && (0==k--)) {
	    start_pos[i] = j;
	    pos_used[j] = 1;
	    nrem--;
	    break;
	  }
	}
	assert(start_pos[i] != npos);
      }
    }
    free(pos_used);
  }

  /* Loop over all players, creating their initial units... */
  for (i = 0; i < game.nplayers; i++) {
    /* Start positions are warranted to be land. */
    x = map.start_positions[start_pos[i]].x;
    y = map.start_positions[start_pos[i]].y;
    /* Loop over all initial units... */
    for (j = 0; j < (game.settlers + game.explorer); j++) {
      /* Determine a place to put the unit within the dispersion area.
         (Always put first unit on start position.) */
      if ((game.dispersion <= 0) || (j == 0)) {
	dx = x;
	dy = y;
      } else {
	do {
	  dx = x + myrand (2*game.dispersion+1) - game.dispersion;
	  dy = y + myrand (2*game.dispersion+1) - game.dispersion;
	  dx = map_adjust_x(dx);
	} while (!(is_real_tile(dx, dy) &&
		   map_same_continent(x, y, dx, dy) &&
		   (map_get_terrain(dx, dy) != T_OCEAN) &&
		   !is_non_allied_unit_tile(map_get_tile(dx, dy),
					    game.players[i].player_no)));
      }
      /* For scenarios, huts may coincide with player starts;
	 remove any such hut: */
      if (map_get_special(dx, dy) & S_HUT) {
        map_clear_special(dx, dy, S_HUT);
        freelog(LOG_VERBOSE, "Removed hut on start position for %s",
		game.players[i].name);
      }
      /* Expose visible area. */
      for (vx = 1; (vx * vx) <= game.rgame.init_vis_radius_sq; vx++) {
	for (vy = 1; (vy * vy) <= game.rgame.init_vis_radius_sq; vy++) {
	  if (((vx *vx) + (vy *vy)) <= game.rgame.init_vis_radius_sq) {
	    show_area(&game.players[i], dx-vx+1, dy-vy+1, 1);
	    show_area(&game.players[i], dx+vx-1, dy-vy+1, 1);
	    show_area(&game.players[i], dx-vx+1, dy+vy-1, 1);
	    show_area(&game.players[i], dx+vx-1, dy+vy-1, 1);
	  }
	}
      }
      /* Create the unit of an appropriate type. */
      utype = get_role_unit((j < game.settlers) ? F_CITIES : L_EXPLORER, 0);
      create_unit(&game.players[i], dx, dy, utype, 0, 0, -1);
    }
  }
}


/**************************************************************************
  Tell clients the year, and also update turn_done and nturns_idle fields
  for all players.
**************************************************************************/
void send_year_to_clients(int year)
{
  struct packet_new_year apacket;
  int i;
  
  for(i=0; i<game.nplayers; i++) {
    struct player *pplayer = &game.players[i];
    pplayer->turn_done=0;
    pplayer->nturns_idle++;
  }

  apacket.year = year;
  lsend_packet_new_year(&game.est_connections, &apacket);

  /* Hmm, clients could add this themselves based on above packet? */
  notify_conn(&game.est_connections, _("Year: %s"), textyear(year));
}


/**************************************************************************
  Send specifed state; should be a CLIENT_GAME_*_STATE ?
  (But note client also changes state from other events.)
**************************************************************************/
void send_game_state(struct conn_list *dest, int state)
{
  struct packet_generic_integer pack;
  pack.value=state;
  lsend_packet_generic_integer(dest, PACKET_GAME_STATE, &pack);
}


/**************************************************************************
  Send game_info packet; some server options and various stuff...
  dest==NULL means game.est_connections
**************************************************************************/
void send_game_info(struct conn_list *dest)
{
  struct packet_game_info ginfo;
  int i;

  if (dest==NULL) dest = &game.est_connections;
  
  ginfo.gold=game.gold;
  ginfo.tech=game.tech;
  ginfo.techlevel=game.techlevel;
  ginfo.skill_level=game.skill_level;
  ginfo.timeout=game.timeout;
  ginfo.end_year=game.end_year;
  ginfo.year=game.year;
  ginfo.min_players=game.min_players;
  ginfo.max_players=game.max_players;
  ginfo.nplayers=game.nplayers;
  ginfo.globalwarming=game.globalwarming;
  ginfo.heating=game.heating;
  ginfo.nuclearwinter=game.nuclearwinter;
  ginfo.cooling=game.cooling;
  ginfo.techpenalty=game.techpenalty;
  ginfo.foodbox = game.foodbox;
  ginfo.civstyle=game.civstyle;
  ginfo.spacerace = game.spacerace;
  for(i=0; i<A_LAST/*game.num_tech_types*/; i++)
    ginfo.global_advances[i]=game.global_advances[i];
  for(i=0; i<B_LAST; i++)
    ginfo.global_wonders[i]=game.global_wonders[i];
  /* the following values are computed every
     time a packet_game_info packet is created */
  if (game.timeout)
    ginfo.seconds_to_turndone=game.turn_start + game.timeout - time(NULL);

  conn_list_iterate(*dest, pconn) {
    /* ? fixme: check for non-players: */
    ginfo.player_idx = (pconn->player ? pconn->player->player_no : -1);
    send_packet_game_info(pconn, &ginfo);
  }
  conn_list_iterate_end;
}

/***************************************************************
 Assign values to ord_city and ord_map for each unit, so the
 values can be saved.
***************************************************************/
static void calc_unit_ordering(void)
{
  int i, j, x, y;
  
  for(i=0; i<game.nplayers; i++) {
    /* to avoid junk values for unsupported units: */
    unit_list_iterate(get_player(i)->units, punit) 
      punit->ord_city = 0;
    unit_list_iterate_end;
    city_list_iterate(get_player(i)->cities, pcity) {
      j = 0;
      unit_list_iterate(pcity->units_supported, punit) 
	punit->ord_city = j++;
      unit_list_iterate_end;
    }
    city_list_iterate_end;
  }

  for(y=0; y<map.ysize; y++) {
    for(x=0; x<map.xsize; x++) {
      j = 0;
      unit_list_iterate(map_get_tile(x,y)->units, punit) 
	punit->ord_map = j++;
      unit_list_iterate_end;
    }
  }
}
/***************************************************************
 For each city and tile, sort unit lists according to
 ord_city and ord_map values.
***************************************************************/
static void apply_unit_ordering(void)
{
  int i, x, y;
  
  for(i=0; i<game.nplayers; i++) {
    city_list_iterate(get_player(i)->cities, pcity) {
      unit_list_sort_ord_city(&pcity->units_supported);
    }
    city_list_iterate_end;
  }

  for(y=0; y<map.ysize; y++) {
    for(x=0; x<map.xsize; x++) { 
      unit_list_sort_ord_map(&map_get_tile(x,y)->units);
    }
  }
}

/***************************************************************
...
***************************************************************/
void game_load(struct section_file *file)
{
  int i, o;
  enum server_states tmp_server_state;
  char *savefile_options=" ";
  char *string;

  game.version = secfile_lookup_int_default(file, 0, "game.version");
  tmp_server_state = (enum server_states)
    secfile_lookup_int_default(file, RUN_GAME_STATE, "game.server_state");

  /* grr, hardcoded sizes --dwp */
  mystrlcpy(metaserver_info_line,
	    secfile_lookup_str_default(file, DEFAULT_META_SERVER_INFO_STRING,
				       "game.metastring"),
	    256);
  mystrlcpy(metaserver_addr,
	    secfile_lookup_str_default(file, DEFAULT_META_SERVER_ADDR,
				       "game.metaserver"),
	    256);
  meta_addr_split();

  game.gold          = secfile_lookup_int(file, "game.gold");
  game.tech          = secfile_lookup_int(file, "game.tech");
  game.skill_level   = secfile_lookup_int(file, "game.skill_level");
  if (game.skill_level==0)
    game.skill_level = GAME_OLD_DEFAULT_SKILL_LEVEL;
  game.timeout       = secfile_lookup_int(file, "game.timeout");
  game.end_year      = secfile_lookup_int(file, "game.end_year");
  game.techlevel     = secfile_lookup_int(file, "game.techlevel");
  game.year          = secfile_lookup_int(file, "game.year");
  game.min_players   = secfile_lookup_int(file, "game.min_players");
  game.max_players   = secfile_lookup_int(file, "game.max_players");
  game.nplayers      = secfile_lookup_int(file, "game.nplayers");
  game.globalwarming = secfile_lookup_int(file, "game.globalwarming");
  game.warminglevel  = secfile_lookup_int(file, "game.warminglevel");
  game.nuclearwinter = secfile_lookup_int_default(file, 0, "game.nuclearwinter");
  game.coolinglevel  = secfile_lookup_int_default(file, 8, "game.coolinglevel");
  game.unhappysize   = secfile_lookup_int(file, "game.unhappysize");

  if (game.version >= 10100) {
    game.cityfactor = secfile_lookup_int(file, "game.cityfactor");
    game.civilwarsize =
      secfile_lookup_int_default(file, GAME_DEFAULT_CIVILWARSIZE,
				 "game.civilwarsize");
    game.diplcost   = secfile_lookup_int(file, "game.diplcost");
    game.freecost   = secfile_lookup_int(file, "game.freecost");
    game.conquercost   = secfile_lookup_int(file, "game.conquercost");
    game.foodbox     = secfile_lookup_int(file, "game.foodbox");
    game.techpenalty = secfile_lookup_int(file, "game.techpenalty");
    game.razechance  = secfile_lookup_int(file, "game.razechance");

    /* suppress warnings about unused entries in old savegames: */
    section_file_lookup(file, "game.rail_food");
    section_file_lookup(file, "game.rail_prod");
    section_file_lookup(file, "game.rail_trade");
    section_file_lookup(file, "game.farmfood");
  }
  if (game.version >= 10300) {
    game.civstyle = secfile_lookup_int_default(file, 0, "game.civstyle");
    game.save_nturns = secfile_lookup_int(file, "game.save_nturns");
  }

  if ((game.version == 10604 && section_file_lookup(file,"savefile.options"))
      || (game.version > 10604)) 
    savefile_options = secfile_lookup_str(file,"savefile.options");
  /* else leave savefile_options empty */

  /* Note -- as of v1.6.4 you should use savefile_options (instead of
     game.version) to determine which variables you can expect to 
     find in a savegame file.  Or alternatively you can use
     secfile_lookup_int_default() or secfile_lookup_str_default().
  */

  sz_strlcpy(game.save_name,
	     secfile_lookup_str_default(file, GAME_DEFAULT_SAVE_NAME,
					"game.save_name"));

  game.aifill = secfile_lookup_int_default(file, 0, "game.aifill");

  game.scorelog = secfile_lookup_int_default(file, 0, "game.scorelog");

  game.fogofwar = secfile_lookup_int_default(file, 0, "game.fogofwar");
  game.fogofwar_old = game.fogofwar;
  
  if(has_capability("diplchance_percent", savefile_options)) {
    game.diplchance = secfile_lookup_int_default(file, game.diplchance,
						 "game.diplchance");
  } else {
    game.diplchance = secfile_lookup_int_default(file, 3, /* old default */
						 "game.diplchance");
    if (game.diplchance < 2) {
      game.diplchance = GAME_MAX_DIPLCHANCE;
    } else if (game.diplchance > 10) {
      game.diplchance = GAME_MIN_DIPLCHANCE;
    } else {
      game.diplchance = 100 - (10 * (game.diplchance - 1));
    }
  }
  game.aqueductloss = secfile_lookup_int_default(file, game.aqueductloss,
						 "game.aqueductloss");
  game.killcitizen = secfile_lookup_int_default(file, game.killcitizen,
						 "game.killcitizen");
  game.turnblock = secfile_lookup_int_default(file,game.turnblock,
					      "game.turnblock");
  game.fixedlength = secfile_lookup_int_default(file,game.fixedlength,
						"game.fixedlength");
  game.barbarianrate = secfile_lookup_int_default(file, game.barbarianrate,
						  "game.barbarians");
  game.onsetbarbarian = secfile_lookup_int_default(file, game.onsetbarbarian,
						   "game.onsetbarbs");
  game.nbarbarians = 0; /* counted in player_load for compatibility with 
			   1.10.0 */
  game.occupychance = secfile_lookup_int_default(file, game.occupychance,
  						 "game.occupychance");
  
  if(has_capability("unirandom", savefile_options)) {
    RANDOM_STATE rstate;
    game.randseed = secfile_lookup_int(file, "game.randseed");
    rstate.j = secfile_lookup_int(file,"random.index_J");
    rstate.k = secfile_lookup_int(file,"random.index_K");
    rstate.x = secfile_lookup_int(file,"random.index_X");
    for(i=0;i<8;i++) {
      char name[20];
      my_snprintf(name, sizeof(name), "random.table%d",i);
      string=secfile_lookup_str(file,name);
      sscanf(string,"%8X %8X %8X %8X %8X %8X %8X", &rstate.v[7*i],
	     &rstate.v[7*i+1], &rstate.v[7*i+2], &rstate.v[7*i+3],
	     &rstate.v[7*i+4], &rstate.v[7*i+5], &rstate.v[7*i+6]);
    }
    rstate.is_init = 1;
    set_myrand_state(rstate);
  }

  sz_strlcpy(game.ruleset.techs,
	     secfile_lookup_str_default(file, "default", "game.ruleset.techs"));
  sz_strlcpy(game.ruleset.units,
	     secfile_lookup_str_default(file, "default", "game.ruleset.units"));
  sz_strlcpy(game.ruleset.buildings,
	     secfile_lookup_str_default(file, "default",
					"game.ruleset.buildings"));
  sz_strlcpy(game.ruleset.terrain,
	     secfile_lookup_str_default(file, "classic",
					"game.ruleset.terrain"));
  sz_strlcpy(game.ruleset.governments,
	     secfile_lookup_str_default(file, "default",
					"game.ruleset.governments"));
  sz_strlcpy(game.ruleset.nations,
	     secfile_lookup_str_default(file, "default",
					"game.ruleset.nations"));
  sz_strlcpy(game.ruleset.cities,
	     secfile_lookup_str_default(file, "default", "game.ruleset.cities"));
  if(game.civstyle == 1) {
    string = "civ1";
  } else {
    string = "default";
    game.civstyle = GAME_DEFAULT_CIVSTYLE;
  }
  sz_strlcpy(game.ruleset.game,
	     secfile_lookup_str_default(file, string,
					"game.ruleset.game"));

  sz_strlcpy(game.demography,
	     secfile_lookup_str_default(file, GAME_DEFAULT_DEMOGRAPHY,
					"game.demography"));

  game.spacerace = secfile_lookup_int_default(file, game.spacerace,
					      "game.spacerace");

  game.auto_ai_toggle = secfile_lookup_int_default(file, game.auto_ai_toggle,
						   "game.auto_ai_toggle");

  game.heating=0;
  game.cooling=0;
  if(tmp_server_state==PRE_GAME_STATE 
     || has_capability("startoptions", savefile_options)) {
    if (game.version >= 10300) {
      game.settlers = secfile_lookup_int(file, "game.settlers");
      game.explorer = secfile_lookup_int(file, "game.explorer");
      game.dispersion =
	secfile_lookup_int_default(file, GAME_DEFAULT_DISPERSION, "game.dispersion");

      map.riches = secfile_lookup_int(file, "map.riches");
      map.huts = secfile_lookup_int(file, "map.huts");
      map.generator = secfile_lookup_int(file, "map.generator");
      map.seed = secfile_lookup_int(file, "map.seed");
      map.landpercent = secfile_lookup_int(file, "map.landpercent");
      map.grasssize =
	secfile_lookup_int_default(file, MAP_DEFAULT_GRASS, "map.grasssize");
      map.swampsize = secfile_lookup_int(file, "map.swampsize");
      map.deserts = secfile_lookup_int(file, "map.deserts");
      map.riverlength = secfile_lookup_int(file, "map.riverlength");
      map.mountains = secfile_lookup_int(file, "map.mountains");
      map.forestsize = secfile_lookup_int(file, "map.forestsize");

      if (has_capability("startoptions", savefile_options)) {
	map.xsize = secfile_lookup_int(file, "map.width");
	map.ysize = secfile_lookup_int(file, "map.height");
      } else {
	/* old versions saved with these names in PRE_GAME_STATE: */
	map.xsize = secfile_lookup_int(file, "map.xsize");
	map.ysize = secfile_lookup_int(file, "map.ysize");
      }

      if (tmp_server_state==PRE_GAME_STATE &&
	  map.generator == 0) {
	/* generator 0 = map done with map editor */
	/* aka a "scenario" */
        if (has_capability("specials",savefile_options)) {
          map_load(file);
	  map.fixed_start_positions = 1;
          section_file_check_unused(file, NULL);
          section_file_free(file);
          return;
        }
        map_tiles_load(file);
        if (has_capability("riversoverlay",savefile_options)) {
	  map_rivers_overlay_load(file);
	}
        if (has_capability("startpos",savefile_options)) {
          map_startpos_load(file);
	  map.fixed_start_positions = 1;
          return;
        }
	return;
      }
    }
    if(tmp_server_state==PRE_GAME_STATE) {
      return;
    }
  }

  game.is_new_game = 0;
  
  load_rulesets();

  map_load(file);

  /* destroyed wonders: */
  string = secfile_lookup_str_default(file, "", "game.destroyed_wonders");
  for(i=0; i<B_LAST && string[i]; i++) {
    if (string[i] == '1') {
      game.global_wonders[i] = -1; /* destroyed! */
    }
  }
  
  for(i=0; i<game.nplayers; i++) {
    player_load(&game.players[i], i, file); 
  }
  /* Since the cities must be placed on the map to put them on the
     player map we do this afterwards */
  for(i=0; i<game.nplayers; i++) {
    player_map_load(&game.players[i], i, file); 
  }
  /* FIXME: This is a kluge to keep the AI working for the moment. */
  /*        When the AI is taught to handle diplomacy, remove this. */
  for (i = 0; i < game.nplayers; i++) {
    struct player *pplayer = get_player(i);
    if (pplayer->ai.control) {
      for (o = 0; o < game.nplayers; o++) {
	struct player *pother = get_player(o);
	if (pplayer != pother) {
	  pplayer->diplstates[pother->player_no].type =
	    pother->diplstates[pplayer->player_no].type =
	    DS_WAR;
	  pplayer->diplstates[pother->player_no].turns_left =
	    pother->diplstates[pplayer->player_no].turns_left =
	    16;
	}
      }
    }
  }

  initialize_globals();
  apply_unit_ordering();

  for(i=0; i<game.nplayers; i++) {
    update_research(&game.players[i]);
    city_list_iterate(game.players[i].cities, pcity)
      city_refresh(pcity);
    city_list_iterate_end;
  }

  game.player_idx=0;
  game.player_ptr=&game.players[0];  

  return;
}


/***************************************************************
...
***************************************************************/
void game_save(struct section_file *file)
{
  int i;
  int version;
  char temp[100], temp1[100], *temp2;

  version = MAJOR_VERSION *10000 + MINOR_VERSION *100 + PATCH_VERSION; 
  secfile_insert_int(file, version, "game.version");
  secfile_insert_int(file, (int) server_state, "game.server_state");
  secfile_insert_str(file, metaserver_info_line, "game.metastring");
  secfile_insert_str(file, meta_addr_port(), "game.metaserver");

  if(server_state!=PRE_GAME_STATE) {
    secfile_insert_str(file, SAVEFILE_OPTIONS, "savefile.options");
  } else { /* cut out unirandom, and insert startpos if necessary */
    sz_strlcpy(temp, SAVEFILE_OPTIONS);
    temp2=strtok(temp," ");
    *temp1='\0';
    while(temp2 != NULL) {
      /* we don't have unirandom in settings and scenarios */
      if(strcmp(temp2,"unirandom")!=0) {
        sz_strlcat(temp1, " ");
        sz_strlcat(temp1, temp2);
      }
      temp2=strtok(NULL," ");
    }
    if(map.num_start_positions>0) {
      sz_strlcat(temp1, " startpos");
    }
    if(map.have_specials) {
      sz_strlcat(temp1, " specials");
    }
    secfile_insert_str(file, temp1+1, "savefile.options");
  }

  secfile_insert_int(file, game.gold, "game.gold");
  secfile_insert_int(file, game.tech, "game.tech");
  secfile_insert_int(file, game.skill_level, "game.skill_level");
  secfile_insert_int(file, game.timeout, "game.timeout");
  secfile_insert_int(file, game.end_year, "game.end_year");
  secfile_insert_int(file, game.year, "game.year");
  secfile_insert_int(file, game.techlevel, "game.techlevel");
  secfile_insert_int(file, game.min_players, "game.min_players");
  secfile_insert_int(file, game.max_players, "game.max_players");
  secfile_insert_int(file, game.nplayers, "game.nplayers");
  secfile_insert_int(file, game.globalwarming, "game.globalwarming");
  secfile_insert_int(file, game.warminglevel, "game.warminglevel");
  secfile_insert_int(file, game.nuclearwinter, "game.nuclearwinter");
  secfile_insert_int(file, game.coolinglevel, "game.coolinglevel");
  secfile_insert_int(file, game.unhappysize, "game.unhappysize");
  secfile_insert_int(file, game.cityfactor, "game.cityfactor");
  secfile_insert_int(file, game.civilwarsize, "game.civilwarsize");
  secfile_insert_int(file, game.diplcost, "game.diplcost");
  secfile_insert_int(file, game.freecost, "game.freecost");
  secfile_insert_int(file, game.conquercost, "game.conquercost");
  secfile_insert_int(file, game.foodbox, "game.foodbox");
  secfile_insert_int(file, game.techpenalty, "game.techpenalty");
  secfile_insert_int(file, game.razechance, "game.razechance");
  secfile_insert_int(file, game.civstyle, "game.civstyle");
  secfile_insert_int(file, game.save_nturns, "game.save_nturns");
  secfile_insert_str(file, game.save_name, "game.save_name");
  secfile_insert_int(file, game.aifill, "game.aifill");
  secfile_insert_int(file, game.scorelog, "game.scorelog");
  secfile_insert_int(file, game.fogofwar, "game.fogofwar");
  secfile_insert_int(file, game.spacerace, "game.spacerace");
  secfile_insert_int(file, game.auto_ai_toggle, "game.auto_ai_toggle");
  secfile_insert_int(file, game.diplchance, "game.diplchance");
  secfile_insert_int(file, game.aqueductloss, "game.aqueductloss");
  secfile_insert_int(file, game.killcitizen, "game.killcitizen");
  secfile_insert_int(file, game.turnblock, "game.turnblock");
  secfile_insert_int(file, game.fixedlength, "game.fixedlength");
  secfile_insert_int(file, game.barbarianrate, "game.barbarians");
  secfile_insert_int(file, game.onsetbarbarian, "game.onsetbarbs");
  secfile_insert_int(file, game.occupychance, "game.occupychance");
  secfile_insert_str(file, game.ruleset.techs, "game.ruleset.techs");
  secfile_insert_str(file, game.ruleset.units, "game.ruleset.units");
  secfile_insert_str(file, game.ruleset.buildings, "game.ruleset.buildings");
  secfile_insert_str(file, game.ruleset.terrain, "game.ruleset.terrain");
  secfile_insert_str(file, game.ruleset.governments, "game.ruleset.governments");
  secfile_insert_str(file, game.ruleset.nations, "game.ruleset.nations");
  secfile_insert_str(file, game.ruleset.cities, "game.ruleset.cities");
  secfile_insert_str(file, game.ruleset.game, "game.ruleset.game");
  secfile_insert_str(file, game.demography, "game.demography");

  if (1) {
    /* Now always save these, so the server options reflect the
     * actual values used at the start of the game.
     * The first two used to be saved as "map.xsize" and "map.ysize"
     * when PRE_GAME_STATE, but I'm standardizing on width,height --dwp
     */
    secfile_insert_int(file, map.xsize, "map.width");
    secfile_insert_int(file, map.ysize, "map.height");
    secfile_insert_int(file, game.settlers, "game.settlers");
    secfile_insert_int(file, game.explorer, "game.explorer");
    secfile_insert_int(file, game.dispersion, "game.dispersion");
    secfile_insert_int(file, map.seed, "map.seed");
    secfile_insert_int(file, map.landpercent, "map.landpercent");
    secfile_insert_int(file, map.riches, "map.riches");
    secfile_insert_int(file, map.swampsize, "map.swampsize");
    secfile_insert_int(file, map.deserts, "map.deserts");
    secfile_insert_int(file, map.riverlength, "map.riverlength");
    secfile_insert_int(file, map.mountains, "map.mountains");
    secfile_insert_int(file, map.forestsize, "map.forestsize");
    secfile_insert_int(file, map.huts, "map.huts");
    secfile_insert_int(file, map.generator, "map.generator");
  } 
  if ((server_state==PRE_GAME_STATE) && game.is_new_game) {
    return; /* want to save scenarios as well */
  }
  if (server_state!=PRE_GAME_STATE) {
    RANDOM_STATE rstate = get_myrand_state();
    assert(rstate.is_init);

    secfile_insert_int(file, game.randseed, "game.randseed");

    secfile_insert_int(file, rstate.j, "random.index_J");
    secfile_insert_int(file, rstate.k, "random.index_K");
    secfile_insert_int(file, rstate.x, "random.index_X");

    for(i=0;i<8;i++) {
      char name[20], vec[100];
      my_snprintf(name, sizeof(name), "random.table%d", i);
      my_snprintf(vec, sizeof(vec),
		  "%8X %8X %8X %8X %8X %8X %8X", rstate.v[7*i],
		  rstate.v[7*i+1], rstate.v[7*i+2], rstate.v[7*i+3],
		  rstate.v[7*i+4], rstate.v[7*i+5], rstate.v[7*i+6]);
      secfile_insert_str(file, vec, name);
    }
     
  }

  /* destroyed wonders: */
  for(i=0; i<B_LAST; i++) {
    if (is_wonder(i) && game.global_wonders[i]!=0
	&& !find_city_by_id(game.global_wonders[i])) {
      temp[i] = '1';
    } else {
      temp[i] = '0';
    }
  }
  temp[i] = '\0';
  secfile_insert_str(file, temp, "game.destroyed_wonders");

  calc_unit_ordering();
  
  for(i=0; i<game.nplayers; i++)
    player_save(&game.players[i], i, file);

  map_save(file);

}


