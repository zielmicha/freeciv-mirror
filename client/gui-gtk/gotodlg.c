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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "game.h"
#include "map.h"
#include "packets.h"
#include "player.h"
#include "unit.h"

#include "clinet.h"
#include "civclient.h"
#include "dialogs.h"
#include "gui_stuff.h"
#include "mapctrl.h"
#include "mapview.h"

#include "gotodlg.h"

extern GtkWidget *toplevel;

GtkWidget *goto_dialog_shell;
GtkWidget *goto_label;
GtkWidget *goto_list;
GtkWidget *goto_center_command;
GtkWidget *goto_airlift_command;
GtkWidget *goto_all_toggle;
GtkWidget *goto_cancel_command;

void update_goto_dialog			(GtkWidget *goto_list);

void goto_cancel_command_callback	(GtkWidget *w, gpointer data);
void goto_goto_command_callback		(GtkWidget *w, gpointer data);
void goto_airlift_command_callback	(GtkWidget *w, gpointer data);
void goto_all_toggle_callback		(GtkWidget *w, gpointer data);
void goto_list_callback			(GtkWidget *w, gint row, gint column);
void goto_list_ucallback		(GtkWidget *w, gint row, gint column);

static int original_x, original_y;

void popup_goto_dialog_action(void)
{
  popup_goto_dialog();
}

/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_goto_dialog(void)
{
  GtkWidget *scrolled;
  GtkAccelGroup *accel=gtk_accel_group_new();

  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE)
    return;
  if(get_unit_in_focus()==0)
    return;

  get_center_tile_mapcanvas(&original_x, &original_y);
  
  gtk_widget_set_sensitive(toplevel, FALSE);
  
  goto_dialog_shell=gtk_dialog_new();
  gtk_signal_connect(GTK_OBJECT(goto_dialog_shell), "delete_event",
	GTK_SIGNAL_FUNC(deleted_callback), NULL);
  gtk_accel_group_attach(accel, GTK_OBJECT(goto_dialog_shell));
  gtk_window_set_position (GTK_WINDOW(goto_dialog_shell), GTK_WIN_POS_MOUSE);

  gtk_window_set_title(GTK_WINDOW(goto_dialog_shell), "Goto/Airlift Unit");

  goto_label=gtk_frame_new("Select destination");
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(goto_dialog_shell)->vbox),
	goto_label, TRUE, TRUE, 0);

  goto_list=gtk_clist_new(1);
  gtk_clist_set_column_width(GTK_CLIST(goto_list), 0,
			     GTK_CLIST(goto_list)->clist_window_width);
  gtk_clist_set_auto_sort (GTK_CLIST (goto_list), TRUE);
  scrolled=gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(scrolled), goto_list);

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				 GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
  gtk_widget_set_usize(scrolled, 250, 300);
  gtk_container_add(GTK_CONTAINER(goto_label), scrolled);

  goto_center_command=gtk_accelbutton_new("_Goto", accel);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(goto_dialog_shell)->action_area),
	goto_center_command, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(goto_center_command, GTK_CAN_DEFAULT);
  gtk_widget_grab_default (goto_center_command);

  goto_airlift_command=gtk_accelbutton_new("Air_lift", accel);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(goto_dialog_shell)->action_area),
	goto_airlift_command, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(goto_airlift_command, GTK_CAN_DEFAULT);

  goto_all_toggle=gtk_toggle_button_new_with_label("All Cities");
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(goto_dialog_shell)->action_area),
	goto_all_toggle, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(goto_all_toggle, GTK_CAN_DEFAULT);

  goto_cancel_command=gtk_accelbutton_new("_Cancel", accel);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(goto_dialog_shell)->action_area),
	goto_cancel_command, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(goto_cancel_command, GTK_CAN_DEFAULT);

  gtk_widget_add_accelerator(goto_cancel_command, "clicked",
	accel, GDK_Escape, 0, 0);

  gtk_signal_connect(GTK_OBJECT(goto_list), "select_row",
		GTK_SIGNAL_FUNC(goto_list_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(goto_list), "unselect_row",
		GTK_SIGNAL_FUNC(goto_list_ucallback), NULL);
  gtk_signal_connect(GTK_OBJECT(goto_center_command), "clicked",
		GTK_SIGNAL_FUNC(goto_goto_command_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(goto_airlift_command), "clicked",
		GTK_SIGNAL_FUNC(goto_airlift_command_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(goto_all_toggle), "toggled",
		GTK_SIGNAL_FUNC(goto_all_toggle_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(goto_cancel_command), "clicked",
		GTK_SIGNAL_FUNC(goto_cancel_command_callback), NULL);

  gtk_widget_show_all(GTK_DIALOG(goto_dialog_shell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(goto_dialog_shell)->action_area);

  update_goto_dialog(goto_list);
  gtk_widget_show(goto_dialog_shell);
}

/**************************************************************************
...
**************************************************************************/
static struct city *get_selected_city(void)
{
  GList *selection;
  gchar *string;

  if (!(selection=GTK_CLIST(goto_list)->selection))
    return 0;
  
  gtk_clist_get_text(GTK_CLIST(goto_list), (gint)selection->data, 0, &string);

  if(strlen(string)>3 && strcmp(string+strlen(string)-3, "(A)")==0) {
    char name[MAX_LEN_NAME];
    strncpy(name, string, strlen(string)-3);
    name[strlen(string)-3]='\0';
    return game_find_city_by_name(name);
  }
  return game_find_city_by_name(string);
}

/**************************************************************************
...
**************************************************************************/
void update_goto_dialog(GtkWidget *goto_list)
{
  int    i, j;
  int    all_cities;
  gchar *row	[1];
  char   name	[MAX_LEN_NAME+3];
  
  all_cities=GTK_TOGGLE_BUTTON(goto_all_toggle)->active;

  gtk_clist_freeze(GTK_CLIST(goto_list));
  gtk_clist_clear(GTK_CLIST(goto_list));
  
  row[0]=name;

  for(i=0, j=0; i<game.nplayers; i++) {
    if(!all_cities && i!=game.player_idx) continue;
    city_list_iterate(game.players[i].cities, pcity) {
      strcpy(name, pcity->name);
      if(pcity->improvements[B_AIRPORT]==1)
	strcat(name, "(A)");
      gtk_clist_append( GTK_CLIST( goto_list ), row );
    }
    city_list_iterate_end;
  }
  gtk_clist_thaw(GTK_CLIST(goto_list));
  gtk_widget_show_all(goto_list);
}

/**************************************************************************
...
**************************************************************************/
static void popdown_goto_dialog(void)
{
  gtk_widget_destroy(goto_dialog_shell);
  gtk_widget_set_sensitive(toplevel, TRUE);
}

/**************************************************************************
...
**************************************************************************/
void goto_list_callback(GtkWidget *w, gint row, gint column)
{
  struct city *pdestcity;

  if((pdestcity=get_selected_city())) {
    struct unit *punit=get_unit_in_focus();
    center_tile_mapcanvas(pdestcity->x, pdestcity->y);
    if(punit && unit_can_airlift_to(punit, pdestcity)) {
      gtk_widget_set_sensitive(goto_airlift_command, TRUE);
      return;
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void goto_list_ucallback(GtkWidget *w, gint row, gint column)
{
  gtk_widget_set_sensitive(goto_airlift_command, FALSE);
}

/**************************************************************************
...
**************************************************************************/
void goto_airlift_command_callback(GtkWidget *w, gpointer data)
{
  struct city *pdestcity=get_selected_city();
  if(pdestcity) {
    struct unit *punit=get_unit_in_focus();
    if(punit) {
      struct unit req_unit;
      req_unit=*punit;
      req_unit.x=pdestcity->x;
      req_unit.y=pdestcity->y;
      send_unit_info(&req_unit);
    }
  }
  popdown_goto_dialog();
}

/**************************************************************************
...
**************************************************************************/
void goto_all_toggle_callback(GtkWidget *w, gpointer data)
{
  update_goto_dialog(goto_list);
}

/**************************************************************************
...
**************************************************************************/
void goto_goto_command_callback(GtkWidget *w, gpointer data)
{
  struct city *pdestcity=get_selected_city();
  if(pdestcity) {
    struct unit *punit=get_unit_in_focus();
    if(punit) {
      struct packet_unit_request req;
      req.unit_id=punit->id;
      req.name[0]='\0';
      req.x=pdestcity->x;
      req.y=pdestcity->y;
      send_packet_unit_request(&aconnection, &req, PACKET_UNIT_GOTO_TILE);
    }
  }
  popdown_goto_dialog();
}

/**************************************************************************
...
**************************************************************************/
void goto_cancel_command_callback(GtkWidget *w, gpointer data)
{
  center_tile_mapcanvas(original_x, original_y);
  popdown_goto_dialog();
}
