/**********************************************************************
 Freeciv - Copyright (C) 2001 - R. Falke, M. Kaufman
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

#include <assert.h>

#include <gdk/gdkkeysyms.h>

#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "mem.h"
#include "support.h"

#include "chatline_g.h"
#include "citydlg_g.h"
#include "civclient.h"
#include "cma_fec.h"
#include "messagewin_g.h"

#include "cityrep.h"
#include "dialogs.h"
#include "gui_stuff.h"
#include "inputdlg.h"

#include "cma_fe.h"

#define BUFFER_SIZE             64

#define SPECLIST_TAG dialog
#define SPECLIST_TYPE struct cma_dialog
#define SPECLIST_STATIC1
#include "speclist.h"

#define SPECLIST_TAG dialog
#define SPECLIST_TYPE struct cma_dialog
#define SPECLIST_STATIC1
#include "speclist_c.h"

#define dialog_list_iterate(dialoglist, pdialog) \
    TYPED_LIST_ITERATE(struct cma_dialog, dialoglist, pdialog)
#define dialog_list_iterate_end  LIST_ITERATE_END

static struct dialog_list dialog_list;
static bool dialog_list_has_been_initialised = FALSE;

static int allow_refreshes = 1;

static struct cma_dialog *get_cma_dialog(struct city *pcity);

static void update_cma_preset_list(struct cma_dialog *pdialog);

static gboolean cma_preset_key_pressed_callback(GtkWidget *w, GdkEventKey *ev,
						gpointer data);
static void cma_del_preset_callback(GtkWidget *w, gpointer data);
static void cma_preset_remove(struct cma_dialog *pdialog, int preset_index);
static void cma_preset_remove_response(GtkWidget *w, gint response,
				       gpointer data);

static void cma_add_preset_callback(GtkWidget *w, gpointer data);
static void cma_preset_add_callback_yes(GtkWidget *w, gpointer data);
static void cma_preset_add_callback_no(GtkWidget *w, gpointer data);
static void cma_preset_add_callback_destroy(GtkWidget *w, gpointer data);

static void cma_change_to_callback(GtkWidget *w, gpointer data);
static void cma_change_permanent_to_callback(GtkWidget *w, gpointer data);
static void cma_release_callback(GtkWidget *w, gpointer data);
static void cma_activate_preset_callback(GtkTreeView *view, GtkTreePath *path,
				         GtkTreeViewColumn *col, gpointer data);
static void cma_select_preset_callback(GtkTreeSelection *selection,
				       gpointer data);

static void hscale_changed(GtkAdjustment *get, gpointer data);
static void set_hscales(const struct cm_parameter *const parameter,
			struct cma_dialog *pdialog);

/**************************************************************************
...
**************************************************************************/
static void ensure_initialised_dialog_list(void)
{
  if (!dialog_list_has_been_initialised) {
    dialog_list_init(&dialog_list);
    dialog_list_has_been_initialised = TRUE;
  }
}

/**************************************************************************
 only called when the city dialog is closed.
**************************************************************************/
void close_cma_dialog(struct city *pcity)
{
  struct cma_dialog *pdialog = get_cma_dialog(pcity);

  gtk_widget_destroy(pdialog->shell);
}

/**************************************************************************
...
**************************************************************************/
static void cma_dialog_destroy_callback(GtkWidget *w, gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;

  g_object_unref(pdialog->tips);

  dialog_list_unlink(&dialog_list, pdialog);
  free(pdialog);
}

/****************************************************************
 return the cma_dialog for a given city.
*****************************************************************/
struct cma_dialog *get_cma_dialog(struct city *pcity)
{
  ensure_initialised_dialog_list();

  dialog_list_iterate(dialog_list, pdialog) {
    if (pdialog->pcity == pcity) {
      return pdialog;
    }
  } dialog_list_iterate_end;

  return NULL;
}

/**************************************************************************
 instantiates a new struct for each city_dialog window that is open.
**************************************************************************/
struct cma_dialog *create_cma_dialog(struct city *pcity)
{
  struct cma_dialog *pdialog;
  struct cm_parameter param;
  GtkWidget *frame, *page, *hbox, *label, *table;
  GtkWidget *vbox, *sw, *hscale;
  int i;
  GtkListStore *store;
  GtkCellRenderer *rend;
  GtkWidget *view;

  cmafec_get_fe_parameter(pcity, &param);
  pdialog = fc_malloc(sizeof(struct cma_dialog));
  pdialog->pcity = pcity;
  pdialog->shell = gtk_vbox_new(FALSE, 0);
  gtk_container_set_border_width(GTK_CONTAINER(pdialog->shell), 8);
  g_signal_connect(pdialog->shell, "destroy",
		   G_CALLBACK(cma_dialog_destroy_callback), pdialog);

  pdialog->tips = gtk_tooltips_new();
  g_object_ref(pdialog->tips);
  gtk_object_sink(GTK_OBJECT(pdialog->tips));

  page = gtk_hbox_new(FALSE, 12);
  gtk_box_pack_start(GTK_BOX(pdialog->shell), page, TRUE, TRUE, 0);

  vbox = gtk_vbox_new(FALSE, 2);
  gtk_box_pack_start(GTK_BOX(page), vbox, TRUE, TRUE, 0);

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
				      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

  store = gtk_list_store_new(1, G_TYPE_STRING);
  pdialog->store = store;

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  g_object_unref(store);
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);
  pdialog->preset_list = view;
  pdialog->selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));

  gtk_tooltips_set_tip(pdialog->tips, view,
		       _("For information on:\n"
		         "CMA and presets\n"
			 "including sample presets,\n"
		         "see README.cma."),
		       "");

  rend = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, NULL,
    rend, "text", 0, NULL);

  label = g_object_new(GTK_TYPE_LABEL,
                       "use-underline", TRUE,
                       "mnemonic-widget", view,
                       "label", _("_Presets:"),
                       "xalign", 0.0, "yalign", 0.5, NULL);
  gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

  gtk_container_add(GTK_CONTAINER(sw), view);
  gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 0);

  g_signal_connect(pdialog->selection, "changed",
		   G_CALLBACK(cma_select_preset_callback), pdialog);
  g_signal_connect(view, "row_activated",
		   G_CALLBACK(cma_activate_preset_callback), pdialog);
  g_signal_connect(view, "key-press-event",
		   G_CALLBACK(cma_preset_key_pressed_callback), pdialog);

  hbox = gtk_hbutton_box_new();
  gtk_button_box_set_layout(GTK_BUTTON_BOX(hbox), GTK_BUTTONBOX_SPREAD);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

  pdialog->add_preset_command = gtk_button_new_from_stock(GTK_STOCK_NEW);
  gtk_container_add(GTK_CONTAINER(hbox), pdialog->add_preset_command);
  g_signal_connect(pdialog->add_preset_command, "clicked",
		   G_CALLBACK(cma_add_preset_callback), pdialog);

  pdialog->del_preset_command = gtk_button_new_from_stock(GTK_STOCK_DELETE);
  gtk_container_add(GTK_CONTAINER(hbox), pdialog->del_preset_command);
  g_signal_connect(pdialog->del_preset_command, "clicked",
		   G_CALLBACK(cma_del_preset_callback), pdialog);

  /* the right-hand side */

  vbox = gtk_vbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(page), vbox, FALSE, FALSE, 2);

  /* Result */

  frame = gtk_frame_new(_("Results"));
  gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, FALSE, 0);

  pdialog->result_label =
      gtk_label_new("food\n prod\n trade\n\n people\n grow\n prod\n name");
  gtk_widget_set_name(pdialog->result_label, "city label");
  gtk_container_add(GTK_CONTAINER(frame), pdialog->result_label);
  gtk_label_set_justify(GTK_LABEL(pdialog->result_label), GTK_JUSTIFY_LEFT);

  /* Minimal Surplus and Factor */

  table = gtk_table_new(NUM_STATS + 2, 3, FALSE);
  gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, FALSE, 2);

  label = gtk_label_new(_("Minimal Surplus"));
  gtk_misc_set_alignment(GTK_MISC(label), 0.1, 0.5);
  gtk_table_attach_defaults(GTK_TABLE(table), label, 1, 2, 0, 1);
  label = gtk_label_new(_("Factor"));
  gtk_misc_set_alignment(GTK_MISC(label), 0.1, 0.5);
  gtk_table_attach_defaults(GTK_TABLE(table), label, 2, 3, 0, 1);

  for (i = 0; i < NUM_STATS; i++) {
    label = gtk_label_new(cm_get_stat_name(i));
    gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, i + 1, i + 2);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

    pdialog->minimal_surplus[i] =
	GTK_ADJUSTMENT(gtk_adjustment_new(-20, -20, 20, 1, 1, 0));

    hscale = gtk_hscale_new(GTK_ADJUSTMENT(pdialog->minimal_surplus[i]));
    gtk_table_attach_defaults(GTK_TABLE(table), hscale, 1, 2, i + 1, i + 2);
    gtk_scale_set_digits(GTK_SCALE(hscale), 0);
    gtk_scale_set_value_pos(GTK_SCALE(hscale), GTK_POS_LEFT);

    g_signal_connect(pdialog->minimal_surplus[i],
		     "value_changed",
		     G_CALLBACK(hscale_changed), pdialog);

    pdialog->factor[i] =
	GTK_ADJUSTMENT(gtk_adjustment_new(1, 0, 25, 1, 1, 0));

    hscale = gtk_hscale_new(GTK_ADJUSTMENT(pdialog->factor[i]));
    gtk_table_attach_defaults(GTK_TABLE(table), hscale, 2, 3, i + 1, i + 2);
    gtk_scale_set_digits(GTK_SCALE(hscale), 0);
    gtk_scale_set_value_pos(GTK_SCALE(hscale), GTK_POS_LEFT);

    g_signal_connect(pdialog->factor[i], "value_changed",
		     G_CALLBACK(hscale_changed), pdialog);
  }

  /* Happy Surplus and Factor */

  label = gtk_label_new(_("Celebrate"));
  gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1,
			    NUM_STATS + 1, NUM_STATS + 2);
  gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

  hbox = gtk_hbox_new(FALSE, 0);
  gtk_table_attach_defaults(GTK_TABLE(table), hbox, 1, 2,
			    NUM_STATS + 1, NUM_STATS + 2);

  pdialog->happy_button = gtk_check_button_new();
  gtk_box_pack_start(GTK_BOX(hbox), pdialog->happy_button, FALSE, FALSE,
		     20);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pdialog->happy_button),
			       FALSE);

  g_signal_connect(pdialog->happy_button, "toggled",
		   G_CALLBACK(hscale_changed), pdialog);

  pdialog->factor[NUM_STATS] =
      GTK_ADJUSTMENT(gtk_adjustment_new(0, 0, 50, 1, 0, 0));

  hscale = gtk_hscale_new(GTK_ADJUSTMENT(pdialog->factor[NUM_STATS]));
  gtk_table_attach_defaults(GTK_TABLE(table), hscale, 2, 3,
			    NUM_STATS + 1, NUM_STATS + 2);
  gtk_scale_set_digits(GTK_SCALE(hscale), 0);
  gtk_scale_set_value_pos(GTK_SCALE(hscale), GTK_POS_LEFT);

  g_signal_connect(pdialog->factor[NUM_STATS],
		   "value_changed",
		   G_CALLBACK(hscale_changed), pdialog);

  /* buttons */

  hbox = gtk_hbutton_box_new();
  gtk_button_box_set_layout(GTK_BUTTON_BOX(hbox), GTK_BUTTONBOX_SPREAD);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

  pdialog->change_command = gtk_button_new_with_mnemonic(_("Apply onc_e"));
  gtk_container_add(GTK_CONTAINER(hbox), pdialog->change_command);
  g_signal_connect(pdialog->change_command, "clicked",
		   G_CALLBACK(cma_change_to_callback), pdialog);

  pdialog->perm_command = gtk_button_new_with_mnemonic(_("Control c_ity"));
  gtk_container_add(GTK_CONTAINER(hbox), pdialog->perm_command);
  g_signal_connect(pdialog->perm_command, "clicked",
		   G_CALLBACK(cma_change_permanent_to_callback), pdialog);

  pdialog->release_command = gtk_button_new_with_mnemonic(_("_Release city"));
  gtk_container_add(GTK_CONTAINER(hbox), pdialog->release_command);
  g_signal_connect(pdialog->release_command, "clicked",
		   G_CALLBACK(cma_release_callback), pdialog);

  gtk_widget_show_all(pdialog->shell);

  ensure_initialised_dialog_list();

  dialog_list_insert(&dialog_list, pdialog);

  update_cma_preset_list(pdialog);

  /* refresh is done in refresh_city_dialog */

  return pdialog;
}

/**************************************************************************
 refreshes the cma dialog
**************************************************************************/
void refresh_cma_dialog(struct city *pcity, enum cma_refresh refresh)
{
  struct cm_result result;
  struct cm_parameter param;
  struct cma_dialog *pdialog = get_cma_dialog(pcity);
  int controlled = cma_is_city_under_agent(pcity, NULL);
  int preset_index;

  cmafec_get_fe_parameter(pcity, &param);

  /* fill in result label */
  cm_query_result(pcity, &param, &result);
  gtk_label_set_text(GTK_LABEL(pdialog->result_label),
		     cmafec_get_result_descr(pcity, &result, &param));

  /* if called from a hscale, we _don't_ want to do this */
  if (refresh != DONT_REFRESH_HSCALES) {
    set_hscales(&param, pdialog);
  }

  if (refresh != DONT_REFRESH_SELECT) {
    /* highlight preset if parameter matches */
    preset_index = cmafec_preset_get_index_of_parameter(&param);
    if (preset_index != -1) {
      GtkTreePath *path;

      path = gtk_tree_path_new();
      gtk_tree_path_append_index(path, preset_index);

      allow_refreshes = 0;
      gtk_tree_view_set_cursor(GTK_TREE_VIEW(pdialog->preset_list), path,
			       NULL, FALSE);
      allow_refreshes = 1;

      gtk_tree_path_free(path);
    } else {
      gtk_tree_selection_unselect_all(pdialog->selection);
    }
  }

  gtk_widget_set_sensitive(pdialog->change_command,
			   can_client_issue_orders() &&
			   result.found_a_valid && !controlled);
  gtk_widget_set_sensitive(pdialog->perm_command,
			   can_client_issue_orders() &&
			   result.found_a_valid && !controlled);
  gtk_widget_set_sensitive(pdialog->release_command,
			   can_client_issue_orders() &&
			   controlled);
}

/**************************************************************************
 fills in the preset list
**************************************************************************/
static void update_cma_preset_list(struct cma_dialog *pdialog)
{
  char buf[BUFFER_SIZE];
  GtkTreeIter it;
  int i;

  /* Fill preset list */
  gtk_list_store_clear(pdialog->store);

  /* Append the presets */
  if (cmafec_preset_num()) {
    gtk_tooltips_disable(pdialog->tips);

    for (i = 0; i < cmafec_preset_num(); i++) {
      mystrlcpy(buf, cmafec_preset_get_descr(i), sizeof(buf));
      gtk_list_store_append(pdialog->store, &it);
      gtk_list_store_set(pdialog->store, &it, 0, buf, -1);
    }
  } else {
    gtk_tooltips_enable(pdialog->tips);
  }
}

/****************************************************************
 callback for removing a preset from the preset view
*****************************************************************/
static void cma_activate_preset_callback(GtkTreeView *view, GtkTreePath *path,
				         GtkTreeViewColumn *col, gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;
  int preset_index;

  preset_index = gtk_tree_path_get_indices(path) [0];

  /* Double-click to remove preset from list */
  cma_preset_remove(pdialog, preset_index);
}

/****************************************************************
 callback for selecting a preset from the preset view
*****************************************************************/
static void cma_select_preset_callback(GtkTreeSelection *selection,
				       gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;
  int preset_index;
  const struct cm_parameter *pparam;

  if ((preset_index = gtk_tree_selection_get_row(selection)) == -1) {
    return;
  }

  pparam = cmafec_preset_get_parameter(preset_index);

  /* save the change */
  cmafec_set_fe_parameter(pdialog->pcity, pparam);

  if (allow_refreshes) {
    if (cma_is_city_under_agent(pdialog->pcity, NULL)) {
      cma_release_city(pdialog->pcity);
      cma_put_city_under_agent(pdialog->pcity, pparam);

      /* unfog the city map if we were unable to put back under */
      if (!cma_is_city_under_agent(pdialog->pcity, NULL)) {
	refresh_city_dialog(pdialog->pcity);
	return;			/* refreshing the city, refreshes cma */
      } else {
        city_report_dialog_update_city(pdialog->pcity);
      }
    }
    refresh_cma_dialog(pdialog->pcity, DONT_REFRESH_SELECT);
  }
}

/**************************************************************************
 pops up a dialog to allow to name your new preset
**************************************************************************/
static void cma_add_preset_callback(GtkWidget *w, gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;
  char *default_name;
  GtkWidget *parent = gtk_widget_get_toplevel(pdialog->shell);
  int index;

  if ((index = gtk_tree_selection_get_row(pdialog->selection)) != -1) {
    default_name = cmafec_preset_get_descr(index);
  } else {
    default_name = _("new preset");
  }

  pdialog->name_shell = input_dialog_create(GTK_WINDOW(parent),
				    _("Name new preset"),
				    _("What should we name the preset?"),
				    default_name,
				    G_CALLBACK(cma_preset_add_callback_yes),
				    pdialog,
				    G_CALLBACK(cma_preset_add_callback_no),
				    pdialog);

  g_signal_connect(pdialog->name_shell, "destroy",
		   G_CALLBACK(cma_preset_add_callback_destroy), data);
}

/****************************************************************
 callback for the add_preset popup (delete popup)
*****************************************************************/
static void cma_preset_add_callback_destroy(GtkWidget *w, gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;

  pdialog->name_shell = NULL;
}

/****************************************************************
 callback for the add_preset popup (don't add it)
*****************************************************************/
static void cma_preset_add_callback_no(GtkWidget *w, gpointer data)
{
  input_dialog_destroy(w);
}

/****************************************************************
 callback for the add_preset popup (add it)
*****************************************************************/
static void cma_preset_add_callback_yes(GtkWidget *w, gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;

  if (pdialog) {
    struct cm_parameter param;

    cmafec_get_fe_parameter(pdialog->pcity, &param);
    cmafec_preset_add(input_dialog_get_input(w), &param);
    update_cma_preset_list(pdialog);
    refresh_cma_dialog(pdialog->pcity, DONT_REFRESH_HSCALES);
    /* if this or other cities have this set as "custom" */
    city_report_dialog_update();
  }

  input_dialog_destroy(w);
}

/****************************************************************
  Key pressed in preset list
*****************************************************************/
static gboolean cma_preset_key_pressed_callback(GtkWidget *w, GdkEventKey *ev,
						gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;
  int index;

  if ((index = gtk_tree_selection_get_row(pdialog->selection)) == -1) {
    return FALSE;
  }

  if (ev->type == GDK_KEY_PRESS) {
    switch (ev->keyval) {
    case GDK_Delete:
      cma_preset_remove(pdialog, index);
      break;
    default:
      return FALSE;
    }
    return TRUE;
  }
  return FALSE;
}


/**************************************************************************
 callback for del_preset 
**************************************************************************/
static void cma_del_preset_callback(GtkWidget *w, gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;
  int index;

  if ((index = gtk_tree_selection_get_row(pdialog->selection)) == -1) {
    return;
  }

  cma_preset_remove(pdialog, index);
}

/**************************************************************************
 pops up a dialog to remove a preset
**************************************************************************/
static void cma_preset_remove(struct cma_dialog *pdialog, int preset_index)
{
  GtkWidget *parent = gtk_widget_get_toplevel(pdialog->shell), *shl;

  pdialog->id = preset_index;
  shl = gtk_message_dialog_new(GTK_WINDOW(parent),
			       GTK_DIALOG_DESTROY_WITH_PARENT,
			       GTK_MESSAGE_QUESTION,
			       GTK_BUTTONS_YES_NO,
			       _("Remove this preset?"));
  setup_dialog(shl, parent);
  pdialog->preset_remove_shell = shl;

  gtk_window_set_title(GTK_WINDOW(shl), cmafec_preset_get_descr(preset_index));
  gtk_window_set_position(GTK_WINDOW(shl), GTK_WIN_POS_CENTER_ON_PARENT);

  g_signal_connect(shl, "response",
		   G_CALLBACK(cma_preset_remove_response), pdialog);

  gtk_window_present(GTK_WINDOW(shl));
}

/****************************************************************
 callback for the remove_preset popup
*****************************************************************/
static void cma_preset_remove_response(GtkWidget *w, gint response,
				       gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;

  if (response == GTK_RESPONSE_YES) {
    cmafec_preset_remove(pdialog->id);
    pdialog->id = -1;
    update_cma_preset_list(pdialog);
    refresh_cma_dialog(pdialog->pcity, DONT_REFRESH_HSCALES);
    /* if this or other cities have this set, reset to "custom" */
    city_report_dialog_update();
  }
  gtk_widget_destroy(w);
  
  pdialog->preset_remove_shell = NULL;
}

/**************************************************************************
 changes the workers of the city to the cma parameters
**************************************************************************/
static void cma_change_to_callback(GtkWidget *w, gpointer data)
{
  struct cm_result result;
  struct cma_dialog *pdialog = (struct cma_dialog *) data;
  struct cm_parameter param;

  cmafec_get_fe_parameter(pdialog->pcity, &param);
  cm_query_result(pdialog->pcity, &param, &result);
  cma_apply_result(pdialog->pcity, &result);
}

/**************************************************************************
 changes the workers of the city to the cma parameters and puts the
 city under agent control
**************************************************************************/
static void cma_change_permanent_to_callback(GtkWidget *w, gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;
  struct cm_parameter param;

  cmafec_get_fe_parameter(pdialog->pcity, &param);
  cma_put_city_under_agent(pdialog->pcity, &param);
  refresh_city_dialog(pdialog->pcity);
}

/**************************************************************************
 releases the city from agent control
**************************************************************************/
static void cma_release_callback(GtkWidget *w, gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;

  cma_release_city(pdialog->pcity);
  refresh_city_dialog(pdialog->pcity);
}

/****************************************************************
 called to adjust the sliders when a preset is selected
 notice that we don't want to call update_result here. 
*****************************************************************/
static void set_hscales(const struct cm_parameter *const parameter,
			struct cma_dialog *pdialog)
{
  int i;

  allow_refreshes = 0;
  for (i = 0; i < NUM_STATS; i++) {
    gtk_adjustment_set_value(pdialog->minimal_surplus[i],
			     parameter->minimal_surplus[i]);
    gtk_adjustment_set_value(pdialog->factor[i], parameter->factor[i]);
  }
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pdialog->happy_button),
			       parameter->require_happy);
  gtk_adjustment_set_value(pdialog->factor[NUM_STATS],
			   parameter->happy_factor);
  allow_refreshes = 1;
}

/************************************************************************
 callback if we moved the sliders.
*************************************************************************/
static void hscale_changed(GtkAdjustment *get, gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;
  struct cm_parameter param;
  int i;

  if (!allow_refreshes) {
    return;
  }

  cmafec_get_fe_parameter(pdialog->pcity, &param);
  for (i = 0; i < NUM_STATS; i++) {
    param.minimal_surplus[i] = (int) (pdialog->minimal_surplus[i]->value);
    param.factor[i] = (int) (pdialog->factor[i]->value);
  }
  param.factor_target = FT_SURPLUS;
  param.require_happy =
      (GTK_TOGGLE_BUTTON(pdialog->happy_button)->active ? 1 : 0);
  param.happy_factor = (int) (pdialog->factor[NUM_STATS]->value);

  /* save the change */
  cmafec_set_fe_parameter(pdialog->pcity, &param);

  /* refreshes the cma */
  if (cma_is_city_under_agent(pdialog->pcity, NULL)) {
    cma_release_city(pdialog->pcity);
    cma_put_city_under_agent(pdialog->pcity, &param);

    /* unfog the city map if we were unable to put back under */
    if (!cma_is_city_under_agent(pdialog->pcity, NULL)) {
      refresh_city_dialog(pdialog->pcity);
      return;			/* refreshing city refreshes cma */
    } else {
      city_report_dialog_update_city(pdialog->pcity);
    }
  }

  refresh_cma_dialog(pdialog->pcity, DONT_REFRESH_HSCALES);
}
