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
#ifndef FC__MENU_H
#define FC__MENU_H

#include <X11/Intrinsic.h>

#include "menu_g.h"

/* Indices for the menus. */
enum MenuIndex {
  MENU_GAME,
  MENU_KINGDOM,   
  MENU_VIEW,
  MENU_ORDER,
  MENU_REPORT,
  MENU_HELP,

  MENU_LAST
};

/* IDs for menu items */
enum MenuID {
  MENU_END_OF_LIST=0,

  MENU_GAME_OPTIONS,
  MENU_GAME_MSG_OPTIONS,
  MENU_GAME_SAVE_SETTINGS,
  MENU_GAME_PLAYERS,
  MENU_GAME_MESSAGES,
  MENU_GAME_SERVER_OPTIONS1,
  MENU_GAME_SERVER_OPTIONS2,
  MENU_GAME_OUTPUT_LOG,
  MENU_GAME_CLEAR_OUTPUT,
  MENU_GAME_DISCONNECT,
  MENU_GAME_QUIT,

  MENU_KINGDOM_RATES,   
  MENU_KINGDOM_FIND_CITY,
  MENU_KINGDOM_WORKLISTS,
  MENU_KINGDOM_REVOLUTION,

  MENU_VIEW_SHOW_MAP_GRID,
  MENU_VIEW_SHOW_CITY_NAMES,
  MENU_VIEW_SHOW_CITY_PRODUCTIONS,
  MENU_VIEW_CENTER_VIEW,

  MENU_ORDER_BUILD_CITY,
  MENU_ORDER_ROAD,
  MENU_ORDER_IRRIGATE,
  MENU_ORDER_MINE,
  MENU_ORDER_TRANSFORM,
  MENU_ORDER_FORTRESS,
  MENU_ORDER_AIRBASE,
  MENU_ORDER_POLLUTION,
  MENU_ORDER_FALLOUT,
  MENU_ORDER_FORTIFY,
  MENU_ORDER_SENTRY,
  MENU_ORDER_PILLAGE,
  MENU_ORDER_HOMECITY,
  MENU_ORDER_UNLOAD,
  MENU_ORDER_WAKEUP_OTHERS,
  MENU_ORDER_AUTO_SETTLER,
  MENU_ORDER_AUTO_ATTACK,
  MENU_ORDER_AUTO_EXPLORE,
  MENU_ORDER_CONNECT,
  MENU_ORDER_GOTO,
  MENU_ORDER_GOTO_CITY,
  MENU_ORDER_DISBAND,
  MENU_ORDER_BUILD_WONDER,
  MENU_ORDER_TRADEROUTE,
  MENU_ORDER_NUKE,
  MENU_ORDER_WAIT,
  MENU_ORDER_DONE,

  MENU_REPORT_CITY,
  MENU_REPORT_MILITARY,
  MENU_REPORT_TRADE,
  MENU_REPORT_SCIENCE,
  MENU_REPORT_WOW,
  MENU_REPORT_TOP_CITIES,
  MENU_REPORT_DEMOGRAPHIC,
  MENU_REPORT_SPACESHIP,

  MENU_HELP_LANGUAGES,
  MENU_HELP_CONNECTING,
  MENU_HELP_CONTROLS,
  MENU_HELP_CHATLINE,
  MENU_HELP_PLAYING,
  MENU_HELP_IMPROVEMENTS,
  MENU_HELP_UNITS,
  MENU_HELP_COMBAT,
  MENU_HELP_ZOC,
  MENU_HELP_TECH,
  MENU_HELP_TERRAIN,
  MENU_HELP_WONDERS,
  MENU_HELP_GOVERNMENT,
  MENU_HELP_HAPPINESS,
  MENU_HELP_SPACE_RACE,
  MENU_HELP_COPYING,
  MENU_HELP_ABOUT,

  MENU_SEPARATOR_LINE
};
/* Shared menu IDs */
#define MENU_ORDER_PARADROP MENU_ORDER_POLLUTION

/* Initialize menus. */
void setup_menus(Widget parent_form);

/* Determine whether menu item is active or not. */
int is_menu_item_active(enum MenuIndex menu, enum MenuID id);

#endif  /* FC__MENU_H */
