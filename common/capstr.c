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

#include <string.h>
#include <stdlib.h>		/* getenv() */

#include "packets.h"		/* MAX_LEN_CAPSTR */
#include "support.h"

#include "capstr.h"

static char our_capability_internal[MAX_LEN_CAPSTR];
const char * const our_capability = our_capability_internal;

/* Capabilities: original author: Mitch Davis (mjd@alphalink.com.au)
 *
 * The capability string is a string clients and servers trade to find
 * out if they can talk to each other, and using which protocol version,
 * and which features and behaviors to expect.  The string is a list of
 * words, separated by whitespace and/or commas, where each word indicates
 * a capability that this version of Freeciv understands.
 * If a capability word is mandatory, it should start with a "+".
 *
 * eg, #define CAPABILITY "+1.6, MapScroll, +AutoSettlers"
 *
 * Client and server functions can test these strings for a particular
 * capability by calling the functions in capability.c
 *
 * Each executable has a string our_capability (above), which gives the
 * capabilities of the running executable.  This is normally initialised
 * with CAPABILITY, but can be changed at run-time by setting the
 * FREECIV_CAPS environment variable, though that is probably mainly
 * useful for testing purposes.
 *
 * For checking the connections of other executables, each
 * "struct connection" has a capability string, which gives the
 * capability of the executable at the other end of the connection.
 * So for the client, the capability of the server is in
 * aconnection.capability, and for the server, the capabilities of 
 * connected clients are in game.players[i]->conn.capability
 * The client now also knows the capabilities of other clients,
 * via game.players[i]->conn.capability.
 *
 * Note the connection struct is a parameter to the functions to send and
 * receive packets, which may be convenient for adjusting how a packet is
 * sent or interpreted based on the capabilities of the connection.
 *
 * At the time of a major release, the capability string may be
 * simplified; eg, the example string above could be replaced by "+1.7".
 * (This should probably only happen if a mandatory capability has
 * been introduced since the previous release.)
 * Whoever makes such a change has responsibility to search the Freeciv
 * code, and look for places where people are using has_capability.
 * If you're taking a capability out of the string, because now every
 * client and server supports it, then you should take out the
 * if(has_capability()) code so that this code is always executed.
 *
 * (The savefile and ruleset files have strings which are used similarly,
 * and checked by the same has_capability function, but the strings there
 * are not directly related to the capability strings discussed here.)
 */

#define CAPABILITY "+1.10 +fog_of_war +fortify_two_step +get_sabotage_list \
ocean_reclamation +dipl_cli_pop_dlg advance_focus_packet +30players \
submarine_flags +gen_impr +dipl_states select_unit send_secs_to_turn_done \
+ds_no_contact"

/* "+1.10" is protocol for 1.10.0 stable release

   "fog_of_war" is protocol extension for Fog of War.

   "fortify_two_step" is protocol and rule change for fortifying in two steps.

   "get_sabotage_list" is protocol extension for fetching an up-to-date
   list of improvements when a Spy is attempting sabotage in a city.

   "ocean_reclamation" is protocol extension for allowing changing ocean
   tiles into "land" (non-ocean) tiles.

   "dipl_cli_pop_dlg" is protocol extension for notification to client
   of need to popup diplomat dialog.

   "advance_focus_packet" is a protocol extension that the server uses to
   tell the client that the server does not intend to move a unit further.

   "30players" is raising the MAX_NUM_PLAYERS value from 14 to 30.

   "submarine_flags" is F_SUBMARINE split into F_PARTIAL_INVIS,
   F_MISSILE_CARRIER, F_NO_LAND_ATTACK

   "gen_impr" is protocol extension for generalized improvements.

   "dipl_states" is for servers and clients that understand diplomatic
   states: alliances, cease-fires, and what-have-you.

   "select_unit" is protocol extension for explicit handling of user
   selection of units by clicking on the map.

   "send_secs_to_turn_done" is that timeout information is sent in the
   game_info packet, explicitly stating how many seconds are left in the
   turn, for when clients re-connect, or when the timeout changes mid-turn.

   "ds_no_contact" is the DS_NO_CONTACT diplomatic state. Also changes the
   meaning of DS_NEUTRAL.
*/

void init_our_capability(void)
{
  char *s;

  s = getenv("FREECIV_CAPS");
  if (s == NULL) {
    s = CAPABILITY;
  }
  sz_strlcpy(our_capability_internal, s);
}
