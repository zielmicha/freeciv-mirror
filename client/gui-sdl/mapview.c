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

/**********************************************************************
                          mapview.c  -  description
                             -------------------
    begin                : Aug 10 2002
    copyright            : (C) 2002 by Rafa� Bursig
    email                : Rafa� Bursig <bursig@poczta.fm>
 *********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

/* #define DRAW_TIMING */

#include "gui_mem.h"

#include "fcintl.h"
#include "game.h"
#include "government.h"		/* government_graphic() */
#include "log.h"
#include "map.h"
#include "player.h"
#include "rand.h"
#include "support.h"

#ifdef DRAW_TIMING
#include "timing.h"
#endif

#include "civclient.h"
#include "climisc.h"
#include "colors.h"
#include "control.h"
#include "goto.h"

#include "chatline.h"

#include "graphics.h"
#include "gui_string.h"
#include "gui_stuff.h"
#include "gui_id.h"
#include "gui_zoom.h"
#include "gui_main.h"
#include "gui_tilespec.h"

#include "menu.h"
#include "dialogs.h" /*sdl_map_get_tile_info_text(...)*/
#include "mapctrl.h"
#include "options.h"
#include "tilespec.h"
#include "citydlg.h"

#include "gui_dither.h"
#include "timing.h"
#include "mapview.h"

extern char *pDataPath;

/* These values are stored in the mapview_canvas struct now. */
#define map_view_x0 mapview_canvas.map_x0
#define map_view_y0 mapview_canvas.map_y0

static Uint8 Mini_map_cell_w, Mini_map_cell_h;

static Uint32 Amask;

static SDL_Surface *pFogSurface;
static SDL_Surface *pTmpSurface;

static SDL_Surface *pBlinkSurfaceA;
static SDL_Surface *pBlinkSurfaceB;

SDL_Surface *pDitherMask;

static SDL_Surface *pOcean_Tile;
static SDL_Surface *pOcean_Foged_Tile;
static SDL_Surface *pDithers[T_LAST][4];

static void init_dither_tiles(void);
static void free_dither_tiles(void);
static void fill_dither_buffers(SDL_Surface **pDitherBufs, int x, int y,
					enum tile_terrain_type terrain);


static void draw_map_cell(SDL_Surface * pDest, Sint16 map_x, Sint16 map_y,
			  Uint16 map_col, Uint16 map_wier, int citymode);

/* ================================================================ */



/**************************************************************************
  This function can be used by mapview_common code to determine the
  location and dimensions of the mapview canvas.
**************************************************************************/
void get_mapview_dimensions(int *map_view_topleft_map_x,
			    int *map_view_topleft_map_y,
			    int *map_view_pixel_width,
			    int *map_view_pixel_height)
{
  *map_view_topleft_map_x = map_view_x0;
  *map_view_topleft_map_y = map_view_y0;
  *map_view_pixel_width = Main.screen->w;
  *map_view_pixel_height = Main.screen->h;
}

/**************************************************************************
  Draw some or all of a tile onto the mapview canvas.
**************************************************************************/
void gui_map_put_tile_iso(int map_x, int map_y,
			  int canvas_x, int canvas_y,
			  int offset_x, int offset_y, int offset_y_unit,
			  int width, int height, int height_unit,
			  enum draw_type draw)
{
  if (draw == D_MB_LR || draw == D_FULL) {
    draw_map_cell(Main.map, canvas_x, canvas_y,
		  (Uint16)map_x, (Uint16)map_y, 0);
  } else {
    static SDL_Rect dest;

    dest.x = canvas_x + offset_x;
    dest.y = canvas_y + offset_y;
    dest.w = width;
    dest.h = height;
    SDL_SetClipRect(Main.map, &dest);
  
    draw_map_cell(Main.map, canvas_x, canvas_y,
		  (Uint16)map_x, (Uint16)map_y, 0);
  
    /* clear ClipRect */
    SDL_SetClipRect(Main.map, NULL);
  }
}

/**************************************************************************
  Draw some or all of a sprite onto the mapview or citydialog canvas.
**************************************************************************/
void gui_put_sprite(struct canvas_store *pCanvas_store,
		    int canvas_x, int canvas_y,
		    struct Sprite *sprite,
		    int offset_x, int offset_y, int width, int height)
{
  /* if you made partial-sprite draws, then you must do decommpres
     procedure on surface commpresed with RLE and this slow down
     drawing */
  SDL_Rect src = {offset_x,offset_y,width,height};
  SDL_Rect dst = {canvas_x, canvas_y, 0, 0};
  SDL_BlitSurface(GET_SURF(sprite), &src, pCanvas_store->map, &dst);
}

/**************************************************************************
  Draw a full sprite onto the mapview or citydialog canvas.
**************************************************************************/
void gui_put_sprite_full(struct canvas_store *pCanvas_store,
			 int canvas_x, int canvas_y,
			 struct Sprite *sprite)
{
  SDL_Rect dst = {canvas_x, canvas_y, 0, 0};
  SDL_BlitSurface(GET_SURF(sprite), NULL, pCanvas_store->map, &dst);
}

/**************************************************************************
  Draw a filled-in colored rectangle onto the mapview or citydialog canvas.
**************************************************************************/
void gui_put_rectangle(struct canvas_store *pCanvas_store,
		       enum color_std color,
		       int canvas_x, int canvas_y, int width, int height)
{
  SDL_Rect dst = {canvas_x, canvas_y, width, height};
  SDL_FillRect(pCanvas_store->map, &dst,
	    get_game_color(color, pCanvas_store->map));
}

/**************************************************************************
  Draw a 1-pixel-width colored line onto the mapview or citydialog canvas.
**************************************************************************/
void gui_put_line(struct canvas_store *pCanvas_store, enum color_std color,
		  int start_x, int start_y, int dx, int dy)
{
  putline(pCanvas_store->map, start_x, start_y, start_x + dx, start_y + dy,
				get_game_color(color, pCanvas_store->map));
}

/**************************************************************************
  Flush the given part of the buffer(s) to the screen.
**************************************************************************/
void flush_mapcanvas(int canvas_x , int canvas_y ,
		     int pixel_width , int pixel_height)
{
  SDL_Rect src, dst = {canvas_x, canvas_y, pixel_width, pixel_height};

  if (correct_rect_region(&dst)) {  
    src = dst;
    SDL_BlitSurface(Main.map, &src, Main.screen, &dst);
    dst = src;
    SDL_BlitSurface(Main.text, &src, Main.screen, &dst);
    dst = src;
    SDL_BlitSurface(Main.gui, &src, Main.screen, &dst);
    /* flush main buffer to framebuffer */
    SDL_UpdateRect(Main.screen, dst.x, dst.y, dst.w, dst.h);  
  }
}

void flush_rect(SDL_Rect rect)
{
  static SDL_Rect dst;
  if (correct_rect_region(&rect)) {
    dst = rect;
    SDL_BlitSurface(Main.map, &rect, Main.screen, &dst);
    dst = rect;
    SDL_BlitSurface(Main.text, &rect, Main.screen, &dst);
    dst = rect;
    SDL_BlitSurface(Main.gui, &rect, Main.screen, &dst);
    /* flush main buffer to framebuffer */
    SDL_UpdateRect(Main.screen, rect.x, rect.y, rect.w, rect.h);
  }
}

/**************************************************************************
  Save Flush area used by "end" flush.
**************************************************************************/
void dirty_rect(int canvas_x , int canvas_y ,
		     int pixel_width , int pixel_height)
{
  SDL_Rect Rect = {canvas_x, canvas_y, pixel_width, pixel_height};
  if ((Main.rects_count < RECT_LIMIT) && correct_rect_region(&Rect)) {
    Main.rects[Main.rects_count++] = Rect;
  }
}

/**************************************************************************
  Save Flush rect used by "end" flush.
**************************************************************************/
void sdl_dirty_rect(SDL_Rect Rect)
{
  if ((Main.rects_count < RECT_LIMIT) && correct_rect_region(&Rect)) {
    Main.rects[Main.rects_count++] = Rect;
  }
}

/**************************************************************************
  Sellect entire screen area to "end" flush and block "save" new areas.
**************************************************************************/
void dirty_all(void)
{
  Main.rects_count = RECT_LIMIT;
}

/**************************************************************************
  Make "end" Flush "saved" parts/areas of the buffer(s) to the screen.
  Function is called in handle_procesing_finished and handle_thaw_hint
**************************************************************************/
void flush_dirty(void)
{
      
  if(!Main.rects_count) {
    return;
  }
    
  if(Main.rects_count >= RECT_LIMIT) {
    SDL_BlitSurface(Main.map, NULL, Main.screen, NULL);
    SDL_BlitSurface(Main.text, NULL, Main.screen, NULL);
    SDL_BlitSurface(Main.gui, NULL, Main.screen, NULL);
    /* flush main buffer to framebuffer */    
    SDL_Flip(Main.screen);
  } else {
    static int i;
    static SDL_Rect dst;
    for(i = 0; i<Main.rects_count; i++) {
      dst = Main.rects[i];
      SDL_BlitSurface(Main.map, &Main.rects[i], Main.screen, &dst);
      dst = Main.rects[i];
      SDL_BlitSurface(Main.text, &Main.rects[i], Main.screen, &dst);
      dst = Main.rects[i];
      SDL_BlitSurface(Main.gui, &Main.rects[i], Main.screen, &dst);
    }
    /* flush main buffer to framebuffer */    
    SDL_UpdateRects(Main.screen, Main.rects_count, Main.rects);
  }
  Main.rects_count = 0;
}

/* ======================================================== */

/**************************************************************************
  normalize_map_pos + (y) corrections.  This function must go to drink!
**************************************************************************/
int correction_map_pos(int *pCol, int *pRow)
{
  int iRet = 0;

  /* correction ... pCol and pRow must be in map :) */
  if (*pCol < 0) {
    *pCol += map.xsize;
    iRet = 1;
  } else {
    if (*pCol >= map.xsize) {
      *pCol -= map.xsize;
      iRet = 1;
    }
  }

  if (*pRow < 0) {
    *pRow += map.ysize;
    iRet = 1;
  } else if (*pRow >= map.ysize) {
    *pRow -= map.ysize;
    iRet = 1;
  }

  return iRet;
}


/**************************************************************************
  Centers the mapview around (col, row).
  col,row are tile cordinate !

  This function callculate:	map_view_x0 , map_view_y0,
				map_view_col0, map_view_row0.

  then redraw all.
**************************************************************************/
void center_tile_mapcanvas(int col, int row)
{
  base_center_tile_mapcanvas(col, row, &map_view_x0, &map_view_y0,
			     mapview_canvas.tile_width,
			     mapview_canvas.tile_height);
  
  update_map_canvas_visible();
  refresh_overview_viewrect();

  if (hover_state == HOVER_GOTO || hover_state == HOVER_PATROL) {
    create_line_at_mouse_pos();
  }
}

/* ===================================================================== */

/**************************************************************************
  Set information for the indicator icons typically shown in the main
  client window.  The parameters tell which sprite to use for the
  indicator.
**************************************************************************/
void set_indicator_icons(int bulb, int sol, int flake, int gov)
{
  struct GUI *pBuf = NULL;

  bulb = CLIP(0, bulb, NUM_TILES_PROGRESS - 1);
  sol = CLIP(0, sol, NUM_TILES_PROGRESS - 1);
  flake = CLIP(0, flake, NUM_TILES_PROGRESS - 1);

  pBuf = get_widget_pointer_form_main_list(ID_WARMING_ICON);
  pBuf->theme = GET_SURF(sprites.warming[sol]);
  redraw_label(pBuf, Main.gui);
  sdl_dirty_rect(pBuf->size);
  
  pBuf = get_widget_pointer_form_main_list(ID_COOLING_ICON);
  pBuf->theme = GET_SURF(sprites.cooling[flake]);
  redraw_label(pBuf, Main.gui);
  sdl_dirty_rect(pBuf->size);
  
  putframe(Main.gui, pBuf->size.x - pBuf->size.w - 1,
	   pBuf->size.y - 1,
	   pBuf->size.x + pBuf->size.w,
	   pBuf->size.y + pBuf->size.h,
	   SDL_MapRGB(Main.gui->format, 255, 255, 255));

  if (SDL_Client_Flags & CF_REVOLUTION) {
    struct Sprite *sprite = NULL;
    if (game.government_count == 0) {
      /* not sure what to do here */
      sprite = get_citizen_sprite(CITIZEN_UNHAPPY, 0, NULL);
    } else {
      sprite = get_government(gov)->sprite;
    }

    pBuf = get_widget_pointer_form_main_list(ID_REVOLUTION);
    set_new_icon_theme(pBuf, create_icon_theme_surf(GET_SURF(sprite)));
    SDL_Client_Flags &= ~CF_REVOLUTION;
  }
#if 0
  pBuf = get_widget_pointer_form_main_list(ID_ECONOMY);
  
  
#endif
  
  pBuf = get_widget_pointer_form_main_list(ID_RESEARCH);
  set_new_icon_theme(pBuf,
  		create_icon_theme_surf(GET_SURF(sprites.bulb[bulb])));
}

/**************************************************************************
  Typically an info box is provided to tell the player about the state
  of their civilization.  This function is called when the label is
  changed.
**************************************************************************/
void update_info_label(void)
{
  SDL_Surface *pTmp = NULL;
  SDL_Rect area = {0, 3, 0, 0};
  Uint32 color = 0x0;/* black */
  SDL_Color col = {0, 0, 0, 80};
  char buffer[512];
  SDL_String16 *pText = create_string16(NULL, 10);

  /* set text settings */
  pText->style |= TTF_STYLE_BOLD;
  pText->forecol.r = 255;
  pText->forecol.g = 255;
  pText->forecol.b = 255;


  my_snprintf(buffer, sizeof(buffer),
	      _("%s Population: %s  Year: %s  "
		"Gold %d Tax: %d Lux: %d Sci: %d "),
	      get_nation_name(game.player_ptr->nation),
	      population_to_text(civ_population(game.player_ptr)),
	      textyear(game.year),
	      game.player_ptr->economic.gold,
	      game.player_ptr->economic.tax,
	      game.player_ptr->economic.luxury,
	      game.player_ptr->economic.science);
  
  pText->render = 3;
  pText->backcol = col;
  
  /* convert to unistr and create text surface */
  pText->text = convert_to_utf16(buffer);
  pTmp = create_text_surf_from_str16(pText);

  area.x = (Main.gui->w - pTmp->w) / 2 - 5;
  area.w = pTmp->w + 8;
  area.h = pTmp->h + 4;

  color = SDL_MapRGBA(Main.gui->format, col.r, col.g, col.b, col.unused);
  SDL_FillRect(Main.gui, &area , color);
  
  color = SDL_MapRGB(Main.gui->format, col.r, col.g, col.b);
  
  /* Horizontal lines */
  putline(Main.gui , area.x + 1, area.y,
		  area.x + area.w - 2, area.y , color);
  putline(Main.gui , area.x + 1, area.y + area.h - 1,
		  area.x + area.w - 2, area.y + area.h - 1, color);

  /* vertical lines */
  putline(Main.gui , area.x + area.w - 1, area.y + 1 ,
  	area.x + area.w - 1, area.y + area.h - 2 , color);
  putline(Main.gui , area.x, area.y + 1, area.x,
	  area.y + area.h - 2, color);

  /* turn off alpha calculate */
  SDL_SetAlpha(pTmp, 0x0, 0x0);

  /* blit text to screen */  
  blit_entire_src(pTmp, Main.gui , area.x + 5, area.y + 2);
  
  sdl_dirty_rect(area);
  
  FREESURFACE(pTmp);

  set_indicator_icons(client_research_sprite(),
		      client_warming_sprite(),
		      client_cooling_sprite(),
		      game.player_ptr->government);

  update_timeout_label();

  FREESTRING16(pText);
  
  flush_dirty();
}

/**************************************************************************
  Read Function Name :)
**************************************************************************/
void redraw_unit_info_label(struct unit *pUnit, struct GUI *pInfo_Window)
{
  SDL_Rect area = { pInfo_Window->size.x, pInfo_Window->size.y, 0, 0 };
  SDL_Surface *pBuf_Surf = NULL;
  Uint16 sy, len;
  
  len = 30;

  if (SDL_Client_Flags & CF_UNIT_INFO_SHOW) {
    /* Unit Window is Show */

    /* blit theme surface */
    SDL_BlitSurface(pInfo_Window->theme, NULL, Main.gui , &area);
    
    if (pUnit) {
      char buffer[512];
      struct city *pCity = player_find_city_by_id(game.player_ptr,
						  pUnit->homecity);
      int infrastructure =
	  get_tile_infrastructure_set(map_get_tile(pUnit->x, pUnit->y));

      change_ptsize16(pInfo_Window->string16, 12);

      /* get and draw unit name (with veteran status) */
      FREE(pInfo_Window->string16->text);
      pInfo_Window->string16->text = convert_to_utf16(unit_type(pUnit)->name);
      
      pInfo_Window->string16->style |= TTF_STYLE_BOLD;
      pBuf_Surf = create_text_surf_from_str16(pInfo_Window->string16);
      pInfo_Window->string16->style &= ~TTF_STYLE_BOLD;
      
      area.x = pInfo_Window->size.x + len
	  + (pInfo_Window->size.w - pBuf_Surf->w - len) / 2;
      area.y = pInfo_Window->size.y + DOUBLE_FRAME_WH;
    
      SDL_SetAlpha(pBuf_Surf, 0x0 , 0x0);
      SDL_BlitSurface(pBuf_Surf, NULL, Main.gui, &area);
      
      if(pUnit->veteran) {
	area.y += pBuf_Surf->h - 3;
        FREESURFACE(pBuf_Surf);
        FREE(pInfo_Window->string16->text);
        pInfo_Window->string16->text = convert_to_utf16(_("veteran"));
        change_ptsize16(pInfo_Window->string16, 10);
	pInfo_Window->string16->forecol.b = 255;
        pBuf_Surf = create_text_surf_from_str16(pInfo_Window->string16);
        pInfo_Window->string16->forecol.b = 0;
        area.x = pInfo_Window->size.x + len
	  + (pInfo_Window->size.w - pBuf_Surf->w - len) / 2;
        
        SDL_SetAlpha(pBuf_Surf, 0x0 , 0x0);
        SDL_BlitSurface(pBuf_Surf, NULL, Main.gui , &area);
      
      }
      sy = area.y + pBuf_Surf->h;
      FREESURFACE(pBuf_Surf);


      /* draw unit sprite */
      pBuf_Surf = GET_SURF(unit_type(pUnit)->sprite);
      area.x = pInfo_Window->size.x + len;
      area.y = pInfo_Window->size.y + FRAME_WH
	  + (pInfo_Window->size.h - pBuf_Surf->h - DOUBLE_FRAME_WH) / 2;

      SDL_BlitSurface(pBuf_Surf, NULL, Main.gui , &area);
      
      area.x += pBuf_Surf->w;
      
      /* get and draw other info (MP, terran, city, etc.) */
      change_ptsize16(pInfo_Window->string16, 10);

      my_snprintf(buffer, sizeof(buffer), "%s\n%s\n%s%s%s",
		  (hover_unit == pUnit->id) ? _("Select destination") :
		  unit_activity_text(pUnit),
		  sdl_map_get_tile_info_text(pUnit->x, pUnit->y),
		  infrastructure ?
		  map_get_infrastructure_text(infrastructure) : "",
		  infrastructure ? "\n" : "", pCity ? pCity->name : "");

      FREE(pInfo_Window->string16->text);
      pInfo_Window->string16->text = convert_to_utf16(buffer);

      pBuf_Surf = create_text_surf_from_str16(pInfo_Window->string16);

      area.y = sy + (Main.gui->h - sy - FRAME_WH - pBuf_Surf->h)/2;

      area.x += (Main.gui->w - area.x - FRAME_WH - pBuf_Surf->w)/2;
      SDL_SetAlpha(pBuf_Surf, 0x0 , 0x0);
      
      /* blit unit info text */
      SDL_BlitSurface(pBuf_Surf, NULL, Main.gui , &area);
    } /* pUnit */
 

    /* ID_ECONOMY */
    pInfo_Window = pInfo_Window->prev;

    if (!pInfo_Window->gfx && pInfo_Window->theme) {
      pInfo_Window->gfx = crop_rect_from_surface(Main.gui, &pInfo_Window->size);
    }

    real_redraw_icon2(pInfo_Window, Main.gui);

    /* ===== */
    /* ID_RESEARCH */
    pInfo_Window = pInfo_Window->prev;

    if (!pInfo_Window->gfx && pInfo_Window->theme)
    {
      pInfo_Window->gfx = crop_rect_from_surface(Main.gui, &pInfo_Window->size);
    }

    real_redraw_icon(pInfo_Window, Main.gui);
    /* ===== */
    /* ID_REVOLUTION */
    pInfo_Window = pInfo_Window->prev;

    if (!pInfo_Window->gfx && pInfo_Window->theme) {
      pInfo_Window->gfx = crop_rect_from_surface(Main.gui, &pInfo_Window->size);
    }

    real_redraw_icon(pInfo_Window, Main.gui);

    /* ===== */
    /* ID_TOGGLE_UNITS_WINDOW_BUTTON */
    pInfo_Window = pInfo_Window->prev;

    if (!pInfo_Window->gfx && pInfo_Window->theme) {
      pInfo_Window->gfx = crop_rect_from_surface(Main.gui, &pInfo_Window->size);
    }

    real_redraw_icon(pInfo_Window, Main.gui);
    
  } else {
#if 0    
    /* draw hidden */
    area.x = Main.screen->w - pBuf_Surf->w - FRAME_WH;
    area.y = Main.screen->h - pBuf_Surf->h - FRAME_WH;
    
    SDL_BlitSurface(pInfo_Window->theme, NULL, Main.gui , &area);
#endif    
  }
}

/**************************************************************************
  Update the information label which gives info on the current unit and
  the square under the current unit, for specified unit.  Note that in
  practice punit is always the focus unit.

  Clears label if punit is NULL.

  Typically also updates the cursor for the map_canvas (this is related
  because the info label may includes  "select destination" prompt etc).
  And it may call update_unit_pix_label() to update the icons for units
  on this square.
**************************************************************************/
void update_unit_info_label(struct unit *pUnit)
{
  struct GUI *pBuf;
  static int mutant = 0;

  /* ugly hack */
  if (SDL_Client_Flags & CF_CITY_DIALOG_IS_OPEN) {
    return;
  }

  pBuf = get_widget_pointer_form_main_list(ID_UNITS_WINDOW);
  
      
  /* draw unit info window */
  redraw_unit_info_label(pUnit, pBuf);

  sdl_dirty_rect(pBuf->size);
  
  if (pUnit) {
    if (hover_unit != pUnit->id)
      set_hover_state(NULL, HOVER_NONE);

    switch (hover_state) {
    case HOVER_NONE:
      if (mutant) {
	SDL_SetCursor(pStd_Cursor);
	mutant = 0;
      }
      break;
    case HOVER_PATROL:
      SDL_SetCursor(pPatrol_Cursor);
      mutant = 1;
      break;
    case HOVER_GOTO:
    case HOVER_CONNECT:
      SDL_SetCursor(pGoto_Cursor);
      mutant = 1;
      break;
    case HOVER_NUKE:
      SDL_SetCursor(pNuke_Cursor);
      mutant = 1;
      break;
    case HOVER_PARADROP:
      SDL_SetCursor(pDrop_Cursor);
      mutant = 1;
      break;
    }

  }

  update_unit_pix_label(pUnit);
}

void update_timeout_label(void)
{
  freelog(LOG_DEBUG, "MAPVIEW: update_timeout_label : PORT ME");
}

void update_turn_done_button(bool do_restore)
{
  freelog(LOG_DEBUG, "MAPVIEW: update_turn_done_button : PORT ME");
}

/**************************************************************************
  Update (refresh) all of the city descriptions on the mapview.
**************************************************************************/
void update_city_descriptions(void)
{
  /* redraw buffer */
  show_city_descriptions();
  dirty_all();  
}

/**************************************************************************
  Clear the city descriptions out of the buffer.
**************************************************************************/
void prepare_show_city_descriptions(void)
{
  SDL_FillRect(Main.text, NULL, 0x0);
}

/**************************************************************************
  Draw a description for the given city onto the surface.
**************************************************************************/
static void put_city_desc_on_surface(SDL_Surface *pDest,
				     struct city *pCity,
				     int canvas_x, int canvas_y)
{
  static char buffer[128];
  SDL_Surface *pCity_Size = NULL, *pCity_Name = NULL, *pCity_Prod = NULL;
  int togrow;
  SDL_String16 *pText = NULL;
  SDL_Rect dst, clear_area = {0, 0, 0, 0};	
  SDL_Color color_size = *(get_game_colorRGB(
			  player_color(get_player(pCity->owner))));
  SDL_Color color_bg = {0, 0, 0, 80};
  Uint32 frame_color;
  
  color_size.unused = 128;
  
  pText = create_string16(NULL, 10);
  pText->style |= (TTF_STYLE_BOLD|SF_CENTER);
  pText->forecol.r = 255;
  pText->forecol.g = 255;
  pText->forecol.b = 255;

  if (SDL_Client_Flags & CF_CIV3_CITY_TEXT_STYLE)
  {
    pText->render = 3;
    pText->backcol = color_bg;
  }
  
  
  canvas_y += NORMAL_TILE_HEIGHT;

  if (draw_city_names) {
    if (draw_city_growth && pCity->owner == game.player_idx) {
      togrow = city_turns_to_grow(pCity);
      switch (togrow) {
      case 0:
	my_snprintf(buffer, sizeof(buffer), "%s: #", pCity->name);
	break;
      case FC_INFINITY:
	my_snprintf(buffer, sizeof(buffer), "%s: --", pCity->name);
	break;
      default:
	my_snprintf(buffer, sizeof(buffer), "%s: %d", pCity->name, togrow);
	break;
      }
    } else {
      /* Force certain behavior below. */
      togrow = 0;
      my_snprintf(buffer, sizeof(buffer), "%s\n%s", pCity->name , 
	    get_nation_name(get_player(pCity->owner)->nation) );
    }

    if (togrow < 0) {
      pText->forecol.g = 0;
      pText->forecol.b = 0;
    }

    pText->text = convert_to_utf16(buffer);
    pCity_Name = create_text_surf_from_str16(pText);

    if (togrow < 0) {
      pText->forecol.g = 255;
      pText->forecol.b = 255;
    }
  }

  /* City Production */
  if (draw_city_productions && pCity->owner == game.player_idx) {
    /*pText->style &= ~TTF_STYLE_BOLD; */
    change_ptsize16(pText, 9);

    /* set text color */
    if (pCity->is_building_unit) {
      pText->forecol.r = 255;
      pText->forecol.g = 255;
      pText->forecol.b = 0;
    } else {
      if (get_improvement_type(pCity->currently_building)->is_wonder) {
	pText->forecol.r = 0xe2;
	pText->forecol.g = 0xc2;
	pText->forecol.b = 0x1f;
      } else {
	pText->forecol.r = 255;
	pText->forecol.g = 255;
	pText->forecol.b = 255;
      }
    }

    get_city_mapview_production(pCity, buffer, sizeof(buffer));

    FREE(pText->text);
    pText->text = convert_to_utf16(buffer);
    pCity_Prod = create_text_surf_from_str16(pText);
    
  }
  else
  {
    if (!pCity_Name && (SDL_Client_Flags & CF_CIV3_CITY_TEXT_STYLE))
    {
      /* Civ3 style don't draw player flag and size
         outside city description and that is reason I made this hack */
      my_snprintf(buffer, sizeof(buffer), "%s", 
	    get_nation_name(get_player(pCity->owner)->nation));
	    
      FREE(pText->text);
      pText->text = convert_to_utf16(buffer);
      pCity_Prod = create_text_surf_from_str16(pText);    
    }
  }
  
  
  if (SDL_Client_Flags & CF_CIV3_CITY_TEXT_STYLE)
  {
  
    /* city size */
    if (pCity_Name || pCity_Prod) {
      my_snprintf(buffer , sizeof(buffer), "%d", pCity->size);
      pText->text = convert_to_utf16(buffer);
      pText->style |= TTF_STYLE_BOLD;
      change_ptsize16(pText, 10);
      pText->backcol = color_size;
      pCity_Size = create_text_surf_from_str16(pText);
	    
      clear_area.y = canvas_y;    
    }
                
    if (pCity_Name)
    {
      clear_area.w = pCity_Name->w + 6;
      clear_area.h = pCity_Name->h;
    }
  
    if (pCity_Prod)
    {
      clear_area.h += pCity_Prod->h;
      clear_area.w = MAX(clear_area.w , pCity_Prod->w + 6);
    }
  
    clear_area.h += 2;
    
    if (pCity_Size)
    {
      /* this isn't a error */	    
      clear_area.w += pCity_Size->h + 1;
      clear_area.h = MAX(clear_area.h , pCity_Size->h + 2);
    }
    
    if (clear_area.w)
    {
      clear_area.x = canvas_x + (NORMAL_TILE_WIDTH - clear_area.w) / 2;
    }
  
    if (pCity_Size)
    {
        
      frame_color = SDL_MapRGB(pDest->format, color_size.r ,color_size.g,
						      color_size.b);
  
      /* solid citi size background */
      dst = clear_area;	    
      SDL_FillRect(pDest, &dst ,
      		SDL_MapRGBA(pDest->format, color_bg.r, color_bg.g,
					color_bg.b, color_bg.unused));
      /* solid text background */
      dst.w = pCity_Size->h + 1;	    
      SDL_FillRect(pDest, &dst ,
	      SDL_MapRGBA(pDest->format, color_size.r, color_size.g,
					color_size.b, color_size.unused));
					
      /* blit city size number */
      SDL_SetAlpha(pCity_Size, 0x0, 0x0);
      /* this isn't error */
      dst.x = clear_area.x + (pCity_Size->h - pCity_Size->w) / 2;
      dst.y = canvas_y + (clear_area.h - pCity_Size->h) / 2;
      SDL_BlitSurface(pCity_Size, NULL, pDest, &dst);

      /* Draw Frame */
      
      /* Horizontal lines */
      putline(pDest, clear_area.x , clear_area.y - 1,
			  clear_area.x + clear_area.w,
                          clear_area.y - 1, frame_color);
      putline(pDest, clear_area.x, clear_area.y + clear_area.h,
			  clear_area.x + clear_area.w,
                          clear_area.y + clear_area.h, frame_color);

      /* vertical lines */
      putline(pDest, clear_area.x + clear_area.w, clear_area.y,
			  clear_area.x + clear_area.w,
                          clear_area.y + clear_area.h, frame_color);
      putline(pDest, clear_area.x - 1, clear_area.y, clear_area.x - 1,
                          clear_area.y + clear_area.h, frame_color);
			  
      putline(pDest, clear_area.x + 1 + pCity_Size->h, clear_area.y,
			  clear_area.x + 1 + pCity_Size->h,
                          clear_area.y + clear_area.h, frame_color);
    }
    
    if (pCity_Name)
    {
      SDL_SetAlpha(pCity_Name, 0x0, 0x0);
      dst.x = clear_area.x + pCity_Size->h + 2 +
		(clear_area.w - (pCity_Size->h + 2) - pCity_Name->w) / 2;
      dst.y = canvas_y;
      SDL_BlitSurface(pCity_Name, NULL, pDest, &dst);
      canvas_y += pCity_Name->h;
    }
  
    if (pCity_Prod)
    {
      SDL_SetAlpha(pCity_Prod, 0x0, 0x0);
      dst.x = clear_area.x + pCity_Size->h + 2 +
		(clear_area.w - (pCity_Size->h + 2) - pCity_Prod->w) / 2;
      dst.y = canvas_y;
      SDL_BlitSurface(pCity_Prod, NULL, pDest, &dst);
    } 
  }
  else
  {
    if (pCity_Name)
    {
      SDL_SetAlpha(pCity_Name, 0x0, 0x0);
      dst.x = canvas_x + (NORMAL_TILE_WIDTH - pCity_Name->w) / 2;
      dst.y = canvas_y;
      SDL_BlitSurface(pCity_Name, NULL, pDest, &dst);
      canvas_y += pCity_Name->h;
    }
  
    if (pCity_Prod)
    {
      SDL_SetAlpha(pCity_Prod, 0x0, 0x0);
      dst.x = canvas_x + (NORMAL_TILE_WIDTH - pCity_Prod->w) / 2;
      dst.y = canvas_y;
      SDL_BlitSurface(pCity_Prod, NULL, pDest, &dst);
    }
    
  }
  
  FREESURFACE(pCity_Prod);
  FREESURFACE(pCity_Name);
  FREESURFACE(pCity_Size);
  FREESTRING16(pText);
}

/**************************************************************************
  Draw a description for the given city.  (canvas_x, canvas_y) is the
  canvas position of the city itself.
**************************************************************************/
void show_city_desc(struct city *pcity, int canvas_x, int canvas_y)
{
  put_city_desc_on_surface(Main.text, pcity,
                     canvas_x, canvas_y - NORMAL_TILE_HEIGHT / 6 );
}

/* ===================================================================== */
/* =============================== Mini Map ============================ */
/* ===================================================================== */

/**************************************************************************
  Set the dimensions for the map overview, in map units (tiles).
  Typically each tile will be a 2x2 rectangle, although this may vary.
**************************************************************************/
void set_overview_dimensions(int w, int h)
{

  struct GUI *pMMap = NULL;

  freelog(LOG_DEBUG,
    "MAPVIEW: 'set_overview_dimensions' call with x = %d  y = %d", w, h);

  if (map.xsize > 160) {
    Mini_map_cell_w = 1;
  } else {
    Mini_map_cell_w = 160 / w;
  }

  if (map.ysize > 100) {
    Mini_map_cell_h = 1;
  } else {
    Mini_map_cell_h = 100 / h;
  }

  enable(ID_TOGGLE_UNITS_WINDOW_BUTTON);

  enable(ID_TOGGLE_MAP_WINDOW_BUTTON);
  enable(ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON);
  enable(ID_REVOLUTION);
  enable(ID_ECONOMY);
  enable(ID_RESEARCH);
  enable(ID_FIND_CITY);

  /* del init string */
  pMMap = get_widget_pointer_form_main_list(ID_WAITING_LABEL);

  if (pMMap) {
    del_widget_from_gui_list(pMMap);
  }
  
  free_dither_tiles();
  init_dither_tiles();
  
}

/**************************************************************************
  Update the tile for the given map position on the overview.
**************************************************************************/
void overview_update_tile(int x, int y)
{
  struct GUI *pMMap =
      get_widget_pointer_form_main_list(ID_MINI_MAP_WINDOW);
  SDL_Rect cell_size = {x * Mini_map_cell_w + FRAME_WH,
			 y * Mini_map_cell_h + FRAME_WH,
    			 Mini_map_cell_w, Mini_map_cell_h};
  SDL_Color color = *(get_game_colorRGB(overview_tile_color(x, y)));

  freelog(LOG_DEBUG, "MAPVIEW: overview_update_tile (x = %d y = %d )", x, y);
			 
  SDL_FillRect(pMMap->theme, &cell_size,
	    SDL_MapRGBA(pMMap->theme->format, color.r,color.g,
    					color.b,color.unused));
}

/**************************************************************************
  Refresh (update) the entire map overview.
**************************************************************************/
void refresh_overview_canvas(void)
{

  SDL_Rect map_area = {FRAME_WH, FRAME_WH, 160, 100}; 
  SDL_Rect cell_size = {FRAME_WH, FRAME_WH, Mini_map_cell_w, Mini_map_cell_h };

  SDL_Color std_color;
  Uint16 col = 0, row = 0;

  struct GUI *pMMap =
      get_widget_pointer_form_main_list(ID_MINI_MAP_WINDOW);

  /* clear map area */
  SDL_FillRect(pMMap->theme, &map_area, 
  	SDL_MapRGB(pMMap->theme->format, 0x0, 0x0, 0x0));
  
  while (TRUE) { /* mini map draw loop */
    std_color = *(get_game_colorRGB(overview_tile_color(col, row))); 
    SDL_FillRect(pMMap->theme, &cell_size,
	    SDL_MapRGBA(pMMap->theme->format, std_color.r,std_color.g,
    					std_color.b,std_color.unused));

    cell_size.x += Mini_map_cell_w;
    col++;

    if (col == map.xsize) {
      cell_size.y += Mini_map_cell_h;
      cell_size.x = FRAME_WH;
      row++;
      if (row == map.ysize) {
	break;
      }
      col = 0;
    }

  } /* end mini map draw loop */
}

/**************************************************************************
  draw line on mini map.
  this algoritm is far from optim.
**************************************************************************/
static void putline_on_mini_map(SDL_Rect * pMapArea, Sint16 x0, Sint16 y0,
		Sint16 x1, Sint16 y1, Uint32 color, SDL_Surface *pDest)
{
  register float m;
  register int x, y;
  int xloop, w = (x1 - x0);


  m = (float) (y1 - y0) / w;

  if (x0 < 0) {
    x0 += pMapArea->w;
  } else if (x0 >= pMapArea->w) {
    x0 -= pMapArea->w;
  }

  if (x1 < 0) {
    x1 += pMapArea->w;
  } else if (x1 >= pMapArea->w) {
    x1 -= pMapArea->w;
  }

  if (y0 < 0) {
    y0 += pMapArea->h;
  } else if (y0 >= pMapArea->h) {
    y0 -= pMapArea->h;
  }

  lock_surf(pDest);

  for (xloop = 0; xloop < w; xloop++) {

    x = x0 + xloop;

    y = m * (x - x0) + y0;

    if (x >= pMapArea->w) {
      x -= pMapArea->w;
    }

    if (y < 0) {
      y += pMapArea->h;
    } else if (y >= pMapArea->h) {
      y -= pMapArea->h;
    }

    x += pMapArea->x;
    y += pMapArea->y;

    putpixel(pDest, x, y, color);

    x -= pMapArea->x;

  }

  unlock_surf(pDest);
}

/**************************************************************************
  Refresh (update) the viewrect on the overview. This is the rectangle
  showing the area covered by the mapview.
**************************************************************************/
void refresh_overview_viewrect(void)
{

  int Nx, Ny, Ex, Ey, Wx, Wy, Sx, Sy, map_w, map_h;
  Uint32 color;
  struct GUI *pMMap, *pBuf;
  SDL_Rect map_area;

  if (get_client_state() != CLIENT_GAME_RUNNING_STATE) {
    return;
  }
  
  pMMap = get_widget_pointer_form_main_list(ID_MINI_MAP_WINDOW);
  pBuf = pMMap->prev;
  
  map_area.x = 0;
  map_area.y = Main.gui->h - pMMap->theme->h;
  map_area.w = 160;
  map_area.h = 100;
  
  if (SDL_Client_Flags & CF_MINI_MAP_SHOW) {

    SDL_BlitSurface(pMMap->theme, NULL, Main.gui, &map_area);
    
    map_area.x += FRAME_WH;
    map_area.y += FRAME_WH;
    map_area.w = 160;
    map_area.h = 100;
    /* The x's and y's are in overview coordinates. */

    map_w = mapview_canvas.tile_width;
    map_h = mapview_canvas.tile_height;
    
#if 0
    get_map_xy(0, 0, &Wx, &Wy);	/* take from Main Map */
#endif
    
    Wx = map_view_x0 * Mini_map_cell_w + FRAME_WH;
    Wy = map_view_y0 * Mini_map_cell_h + FRAME_WH;

    Nx = Wx + Mini_map_cell_w * map_w + FRAME_WH;
    Ny = Wy - Mini_map_cell_h * map_w + FRAME_WH;
    Sx = Wx + Mini_map_cell_w * map_h + FRAME_WH;
    Sy = Wy + Mini_map_cell_h * map_h + FRAME_WH;
    Ex = Nx + Mini_map_cell_w * map_h + FRAME_WH;
    Ey = Ny + Mini_map_cell_h * map_h + FRAME_WH;

    color = SDL_MapRGBA(pMMap->theme->format, 255, 255, 255, 255);

    putline_on_mini_map(&map_area, Nx, Ny, Ex, Ey, color, Main.gui);

    putline_on_mini_map(&map_area, Sx, Sy, Ex, Ey, color, Main.gui);

    putline_on_mini_map(&map_area, Wx, Wy, Sx, Sy, color, Main.gui);

    putline_on_mini_map(&map_area, Wx, Wy, Nx, Ny, color, Main.gui);

    freelog(LOG_DEBUG, "wx,wy: %d,%d nx,ny:%d,%x ex,ey:%d,%d, sx,sy:%d,%d",
	    Wx, Wy, Nx, Ny, Ex, Ey, Sx, Sy);
    /* ===== */


    /* redraw widgets */

    /* ID_NEW_TURN */
    if (!pBuf->gfx) {
      pBuf->gfx = crop_rect_from_surface(Main.gui , &pBuf->size);
    }

    real_redraw_icon(pBuf, Main.gui);

    /* ===== */
    /* ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON */
    pBuf = pBuf->prev;

    if (!pBuf->gfx) {
      pBuf->gfx = crop_rect_from_surface(Main.gui, &pBuf->size);
    }

    real_redraw_icon(pBuf, Main.gui);

    /* ===== */
    /* ID_FIND_CITY */
    pBuf = pBuf->prev;

    if (!pBuf->gfx) {
      pBuf->gfx = crop_rect_from_surface(Main.gui, &pBuf->size);
    }

    real_redraw_icon(pBuf, Main.gui);


    /* ===== */
    /* ID_TOGGLE_MAP_WINDOW_BUTTON */
    pBuf = pBuf->prev;

    if (!pBuf->gfx && pBuf->theme) {
      pBuf->gfx = crop_rect_from_surface(Main.gui, &pBuf->size);
    }

    real_redraw_icon(pBuf, Main.gui);


    /*add_refresh_rect(pMMap->size);*/

  } else {/* map hiden */

#if 0
    src.y = 0;
    src.h = pMMap->theme->h;
    src.x = pMMap->theme->w - pMMap->size.w;
    src.w = pMMap->size.w;
    map_area.w = pMMap->theme->w;
    map_area.h = pMMap->theme->h;
    
    /* clear area under old map window */
    SDL_FillRect(Main.gui, &map_area , 0x0 );
    
    /*map_area.x = 0;
    map_area.y = Main.gui->h - pMMap->size.h;*/
    SDL_BlitSurface(pMMap->theme, &src, Main.gui, &map_area);
    
    
    SDL_Surface *pBuf_Surf = 
    	ResizeSurface(pTheme->FR_Vert, pTheme->FR_Vert->w,
				pMMap->size.h - DOUBLE_FRAME_WH + 2, 1);

    map_area.y += 2;
    SDL_BlitSurface(pBuf_Surf, NULL , Main.gui, &map_area);
    FREESURFACE(pBuf_Surf);

    /* ID_NEW_TURN */
    real_redraw_icon(pBuf, Main.gui);

    /* ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON */
    pBuf = pBuf->prev;
    real_redraw_icon(pBuf, Main.gui);

    /* ID_FIND_CITY */
    pBuf = pBuf->prev;
    real_redraw_icon(pBuf, Main.gui);

    /* ID_TOGGLE_MAP_WINDOW_BUTTON */
    pBuf = pBuf->prev;
    real_redraw_icon(pBuf, Main.gui);


    /*add_refresh_rect(pMMap->size);*/
#endif
  }
  
}

/* ===================================================================== */
/* ===================================================================== */
/* ===================================================================== */


/**************************************************************************
  Draw City's surfaces to 'pDest' on position 'map_x' , 'map_y'.
**************************************************************************/
static void put_city_pixmap_draw(struct city *pCity, SDL_Surface * pDest,
				 Sint16 map_x, Sint16 map_y)
{
  struct Sprite *pSprites[80];
  SDL_Rect src , des = {map_x, map_y, 0, 0};
  int i;
  int count =
      fill_city_sprite_array_iso(pSprites, pCity);
  
  if (!(SDL_Client_Flags & CF_CIV3_CITY_TEXT_STYLE))
  {
    src = get_smaller_surface_rect(GET_SURF(pSprites[0]));
    des.x += NORMAL_TILE_WIDTH / 4 + NORMAL_TILE_WIDTH / 8;
    des.y += NORMAL_TILE_HEIGHT / 4;
      
    /* blit flag/shield */
    SDL_BlitSurface(GET_SURF(pSprites[0]), &src, pDest, &des);
  }
      
  des.x = map_x;
  des.y = map_y;
  src = des;
  for (i = 1; i < count; i++) {
    if (pSprites[i]) {
      SDL_BlitSurface(GET_SURF(pSprites[i]), NULL, pDest, &des);
      des = src;	    
    }
  }
  

}

/**************************************************************************
  Draw Unit's surfaces to 'pDest' on position 'map_x' , 'map_y'.
**************************************************************************/
void put_unit_pixmap_draw(struct unit *pUnit, SDL_Surface * pDest,
			  Sint16 map_x, Sint16 map_y)
{
  struct Sprite *pSprites[10];
  SDL_Rect copy, des = { map_x, map_y, 0, 0 };
  static SDL_Rect src_hp = {0,0,0,0};
  static SDL_Rect src_flag = {0,0,0,0};
  int dummy;

  int count =
      fill_unit_sprite_array(pSprites, pUnit, &dummy);

  des.x += NORMAL_TILE_WIDTH / 4;

  /* blit hp bar */
  if (!src_hp.x)
  {
    src_hp = get_smaller_surface_rect(GET_SURF(pSprites[count - 1]));
  }
  copy = des;
  SDL_BlitSurface(GET_SURF(pSprites[count - 1]), &src_hp, pDest, &des);

  des = copy;
  des.y += src_hp.h;
  des.x++;
  
  /* blit flag/shield */
  if (!src_flag.x)
  {
    src_flag = get_smaller_surface_rect(GET_SURF(pSprites[0]));
  }
  
  SDL_BlitSurface(GET_SURF(pSprites[0]), &src_flag, pDest, &des);

  des.x = map_x;
  des.y = map_y;
  copy = des;    
  for (dummy = 1; dummy < count - 1; dummy++) {
    if (pSprites[dummy]) {
      SDL_BlitSurface(GET_SURF(pSprites[dummy]), NULL, pDest, &des);
      des = copy;	    
    }
  }
}

static bool is_full_ocean(enum tile_terrain_type t, int x, int y)
{
  
  if (is_ocean(t))
  {
    enum tile_terrain_type ter;
    adjc_iterate(x, y, x1, y1) {
      ter = map_get_terrain(x1, y1);
      if (!is_ocean(ter) && ter != T_UNKNOWN) {
        return FALSE;
      }
    } adjc_iterate_end;
    return TRUE;
  }
  
  return FALSE;
}

/**************************************************************************
  This function draw 1x1 [map_col]x[map_row] map cell to 'pDest' surface
  on map_x , map_y position.
**************************************************************************/
static void draw_map_cell(SDL_Surface * pDest, Sint16 map_x, Sint16 map_y,
			  Uint16 map_col, Uint16 map_row, int citymode)
{
  SDL_Surface *pTile_sprs[80];
  SDL_Surface *pCoasts[4] = {NULL, NULL, NULL, NULL};
  SDL_Surface *pDither[4] = {NULL, NULL, NULL, NULL};
  SDL_Surface *pDitherBufs[4];
  SDL_Surface *pBufSurface = NULL;
  SDL_Rect dst , des = { map_x, map_y, 0, 0 };
  struct tile *pTile = map_get_tile(map_col, map_row);
  struct city *pCity = pTile->city;
  struct unit *pUnit = NULL, *pFocus = NULL;
  enum tile_special_type special = pTile->special;
  int count, i = 0;
  int fog;
  int full_ocean;
  int solid_bg;
  
  count = fill_tile_sprite_array_iso((struct Sprite **) pTile_sprs,
				     (struct Sprite **) pCoasts,
				     (struct Sprite **) pDither,
				     map_col, map_row, citymode,
				     &solid_bg);
    				     

  if (count == -1) { /* tile is unknown */
    SDL_BlitSurface(GET_SURF(sprites.black_tile),
		    NULL, pDest, &des);
  
    return;
  }
  
#if 0
  /* Replace with check for is_normal_tile later */
  assert(is_real_tile(map_col, map_row));
#endif
  
  /* normalize_map_pos(&map_col, &map_wier); */

  fog = pTile->known == TILE_KNOWN_FOGGED && draw_fog_of_war;
  pUnit = get_drawable_unit(map_col, map_row, citymode);
  pFocus = get_unit_in_focus();
  full_ocean = is_full_ocean(pTile->terrain, map_col, map_row );

  if (!full_ocean && (SDL_Client_Flags & CF_DRAW_MAP_DITHER))
  { 
    fill_dither_buffers(pDitherBufs, map_col, map_row, pTile->terrain);
  }
  
  if (fog && !full_ocean) {
    des.x = 0;
    des.y = NORMAL_TILE_HEIGHT / 2;
    pBufSurface = pTmpSurface;
  } else {
    pBufSurface = pDest;
  }

  dst = des;
  
  if (draw_terrain) {
    if (is_ocean(pTile->terrain)) {
      if (full_ocean) 
      {
	if (fog)
	{
	  SDL_BlitSurface(pOcean_Foged_Tile, NULL, pBufSurface, &des);
	}
	else
	{
	  SDL_BlitSurface(pOcean_Tile, NULL, pBufSurface, &des);
	}
      }
      else
      {  /* coasts */
        /* top */
        des.x += NORMAL_TILE_WIDTH / 4;
        SDL_BlitSurface(pCoasts[0], NULL, pBufSurface, &des);
        des = dst;
      
        /* bottom */
        des.x += NORMAL_TILE_WIDTH / 4;
        des.y += NORMAL_TILE_HEIGHT / 2;
        SDL_BlitSurface(pCoasts[1], NULL, pBufSurface, &des);
        des = dst;
      
        /* left */
        des.y += NORMAL_TILE_HEIGHT / 4;
        SDL_BlitSurface(pCoasts[2], NULL, pBufSurface, &des);
        des = dst;
      
        /* right */
        des.y += NORMAL_TILE_HEIGHT / 4;
        des.x += NORMAL_TILE_WIDTH / 2;
        SDL_BlitSurface(pCoasts[3], NULL, pBufSurface, &des);
      }
    } else {
      SDL_BlitSurface(pTile_sprs[0], NULL, pBufSurface, &des);
      i++;
    }

    des = dst;
    /*** Dither base terrain ***/

    if (!full_ocean && (SDL_Client_Flags & CF_DRAW_MAP_DITHER))
    {
      /* north */
      if (pDitherBufs[0]) {
        des.x += NORMAL_TILE_WIDTH / 2;
        SDL_BlitSurface(pDitherBufs[0] , NULL, pBufSurface, &des);
        des = dst;
      }
      /* south */
      if (pDitherBufs[1]) {
        des.y += NORMAL_TILE_HEIGHT / 2;
        SDL_BlitSurface(pDitherBufs[1] , NULL, pBufSurface, &des);
        des = dst;
      }
      /* east */
      if (pDitherBufs[2]) {
        des.y += NORMAL_TILE_HEIGHT / 2;
        des.x += NORMAL_TILE_WIDTH / 2;
        SDL_BlitSurface(pDitherBufs[2] , NULL, pBufSurface, &des);
        des = dst;
      }
      /* west */
      if (pDitherBufs[3]) {
        SDL_BlitSurface(pDitherBufs[3] , NULL, pBufSurface, &des);
        des = dst;
      }
    }
    
  }

    /*** Rest of terrain and specials ***/
  for (; i < count; i++) {
    if (pTile_sprs[i]) {
      if (pTile_sprs[i]->w - NORMAL_TILE_WIDTH > 0
	  || pTile_sprs[i]->h - NORMAL_TILE_HEIGHT > 0) {
	des.x -= (pTile_sprs[i]->w - NORMAL_TILE_WIDTH);
	des.y -= (pTile_sprs[i]->h - NORMAL_TILE_HEIGHT);
	SDL_BlitSurface(pTile_sprs[i], NULL, pBufSurface, &des);
      } else {
	SDL_BlitSurface(pTile_sprs[i], NULL, pBufSurface, &des);
      }
      des = dst;
    } else {
      freelog(LOG_ERROR, _("sprite is NULL"));
    }
  }

    /*** Map grid ***/
  if (draw_map_grid) {
    /* we draw the 2 lines on top of the tile; the buttom lines will be
       drawn by the tiles underneath. */
    Uint32 grid_color = 64;
    putline(pBufSurface, des.x, des.y + NORMAL_TILE_HEIGHT / 2,
	    des.x + NORMAL_TILE_WIDTH / 2, des.y, grid_color);

    putline(pBufSurface, des.x + NORMAL_TILE_WIDTH / 2, des.y,
	    des.x + NORMAL_TILE_WIDTH,
	    des.y + NORMAL_TILE_HEIGHT / 2, grid_color);
  }

  if (draw_coastline && !draw_terrain) {
    enum tile_terrain_type t1 = pTile->terrain , t2;
    int x1, y1;
    Uint32 coast_color = SDL_MapRGB(pBufSurface->format, 255, 255, 0);
    x1 = map_col;
    y1 = map_row - 1;
    if (normalize_map_pos(&x1, &y1)) {
      t2 = map_get_terrain(x1, y1);
      if ((is_ocean(t1) ^ is_ocean(t2))) {
	putline( pBufSurface ,
		      des.x + NORMAL_TILE_WIDTH / 2, des.y,
		      des.x + NORMAL_TILE_WIDTH,
		      des.y + NORMAL_TILE_HEIGHT / 2 , coast_color );
      }
    }
    x1 = map_col - 1;
    y1 = map_row;
    if (normalize_map_pos(&x1, &y1)) {
      t2 = map_get_terrain(x1, y1);
      if ((is_ocean(t1) ^ is_ocean(t2))) {
	putline( pBufSurface , 
		      des.x, des.y + NORMAL_TILE_HEIGHT / 2,
		      des.x + NORMAL_TILE_WIDTH / 2, des.y , coast_color );
      }
    }
  }

  /*** City and various terrain improvements ***/
  /*if (contains_special(special, S_FORTRESS) && draw_fortress_airbase) {*/
  if ((special & S_FORTRESS) && draw_fortress_airbase) {
    SDL_BlitSurface(GET_SURF(sprites.tx.fortress_back),
		    NULL, pBufSurface, &des);
    des = dst;	  
  }

  if (pCity && draw_cities) {
    put_city_pixmap_draw(pCity, pBufSurface,
			 des.x, des.y - NORMAL_TILE_HEIGHT / 2);
  }

  /*if (contains_special(special, S_AIRBASE) && draw_fortress_airbase) {*/
  if ((special & S_AIRBASE) && draw_fortress_airbase) {
    SDL_BlitSurface(GET_SURF(sprites.tx.airbase),
		    NULL, pBufSurface, &des);
    des = dst;
  }

  /*if (contains_special(special, S_FALLOUT) && draw_pollution) {*/
  if ((special & S_FALLOUT) && draw_pollution) {
    SDL_BlitSurface(GET_SURF(sprites.tx.fallout),
		    NULL, pBufSurface, &des);
    des = dst;
  }

  /*if (contains_special(special, S_POLLUTION) && draw_pollution) {*/
  if ((special & S_POLLUTION) && draw_pollution) {
    SDL_BlitSurface(GET_SURF(sprites.tx.pollution),
		    NULL, pBufSurface, &des);
    des = dst;	  
  }

    /*** city size ***/
  /* Not fogged as it would be unreadable */
  if (!( SDL_Client_Flags & CF_CIV3_CITY_TEXT_STYLE ) &&
				     pCity && draw_cities) {
    if (pCity->size >= 10) {
      SDL_BlitSurface(GET_SURF(sprites.city.size_tens[pCity->size / 10]),
	      NULL, pBufSurface, &des);
      des = dst;	    
    }

    SDL_BlitSurface(GET_SURF(sprites.city.size[pCity->size % 10]),
		    NULL, pBufSurface, &des);
    des = dst;    
  }
  

    /*** Unit ***/
  if (pUnit && (draw_units || (pUnit == pFocus && draw_focus_unit))) {
    put_unit_pixmap_draw(pUnit, pBufSurface, des.x,
			 des.y - NORMAL_TILE_HEIGHT / 2);

    if (!pCity
	&& unit_list_size( &(pTile->units) ) > 1) {
      des.y -= NORMAL_TILE_HEIGHT / 2;	  
      SDL_BlitSurface(GET_SURF(sprites.unit.stack), NULL,
		      pBufSurface, &des);
      des = dst;		
    }
  }

  /*if (contains_special(special, S_FORTRESS) && draw_fortress_airbase) {*/
  if ((special & S_FORTRESS) && draw_fortress_airbase) {
    SDL_BlitSurface(GET_SURF(sprites.tx.fortress),
		    NULL, pBufSurface, &des);
    des = dst;	  
  }


  if (fog && !full_ocean)  {
	  
    SDL_BlitSurface(pFogSurface, NULL, pBufSurface, NULL);
	  
    des.x = map_x;
    des.y = map_y - NORMAL_TILE_HEIGHT / 2;
    SDL_BlitSurface(pBufSurface, NULL, pDest, &des);
      
    /* clear pBufSurface */
    /* if BytesPerPixel == 4 and Amask == 0 ( 24 bit coding ) alpha blit 
     * functions Set A = 255 in all pixels and then pixel val 
     * ( r=0, g=0, b=0) != 0. Real val of this pixel is 0xff000000/0x000000ff.*/
    if (pBufSurface->format->BytesPerPixel == 4
	&& !pBufSurface->format->Amask) {
      SDL_FillRect(pBufSurface, NULL, Amask );
    } else {
      SDL_FillRect(pBufSurface, NULL, 0x0 );
    }

  }
  
}

static int frame = 0;

/**************************************************************************
...
**************************************************************************/
void real_blink_active_unit(void)
{
  static int oldCol = 0, oldRow = 0;
  static struct unit *pPrevUnit = NULL;
  
  struct unit *pUnit = NULL;
  static SDL_Rect area;
  
  if ((pUnit = get_unit_in_focus())) {
    get_canvas_xy(pUnit->x, pUnit->y, (int *) &area.x, (int *) &area.y);
    area.y -= (NORMAL_TILE_HEIGHT >> 1);
    if (pUnit == pPrevUnit && pUnit->x == oldCol && pUnit->y == oldRow) {
      /* blit clear area */    
      SDL_BlitSurface(pBlinkSurfaceA, NULL, Main.map, &area);
      /* blit frame of animation */
      area.y += (NORMAL_TILE_HEIGHT >> 1);
      SDL_BlitSurface(pTheme->ActiveUnit[frame++], NULL, Main.map, &area);
      area.y -= (NORMAL_TILE_HEIGHT >> 1);
      /* blit unit graphic */
      SDL_BlitSurface(pBlinkSurfaceB, NULL, Main.map, &area);
     
      flush_mapcanvas(area.x, area.y, UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT);
          
    } else {
      /* refresh clear area */
      area.w = UNIT_TILE_WIDTH;
      area.h = UNIT_TILE_HEIGHT;
      SDL_BlitSurface(Main.map, &area, pBlinkSurfaceA, NULL);
      
      /* create unit graphic */
      if (pPrevUnit != pUnit) {
        SDL_FillRect(pBlinkSurfaceB,NULL, 0x0);
        put_unit_pixmap_draw(pUnit, pBlinkSurfaceB, 0, 0);
      }
      
      /* draw animation frame */
      area.y += (NORMAL_TILE_HEIGHT >> 1);
      SDL_BlitSurface(pTheme->ActiveUnit[frame++], NULL, Main.map, &area);
      area.y -= (NORMAL_TILE_HEIGHT >> 1);
      
      /* draw unit graphic */
      SDL_BlitSurface(pBlinkSurfaceB, NULL, Main.map, &area);
      
      flush_rect(area);
      
      pPrevUnit = pUnit;
      oldCol = pUnit->x;
      oldRow = pUnit->y;
    }

  }
  
  if(frame > 3) {
    frame = 0;
  }
    
}

/**************************************************************************
  Draw a single frame of animation.  This function needs to clear the old
  image and draw the new one.  It must flush output to the display.
**************************************************************************/
void draw_unit_animation_frame(struct unit *punit,
			       bool first_frame, bool last_frame,	
			       int old_canvas_x, int old_canvas_y,
			       int new_canvas_x, int new_canvas_y)
{
  SDL_Rect src =
      { old_canvas_x, old_canvas_y, UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT };
  SDL_Rect dest =
      { new_canvas_x, new_canvas_y, UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT };
  static SDL_Surface *pMap_Copy, *pUnit_Surf;

  if (first_frame) {
    /* Create extra backing stores. */
    pMap_Copy =
	create_surf(UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT, SDL_SWSURFACE);
    pUnit_Surf =
	create_surf(UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT, SDL_SWSURFACE);
    SDL_FillRect(pUnit_Surf, NULL, SDL_MapRGB(pUnit_Surf->format, 255, 0, 255));
    SDL_SetColorKey(pUnit_Surf, SDL_SRCCOLORKEY|SDL_RLEACCEL,
			    SDL_MapRGB(pUnit_Surf->format, 255, 0, 255));
    put_unit_pixmap_draw(punit, pUnit_Surf, 0, 0);
  }
  else
  {
    /* Clear old sprite. */
    SDL_BlitSurface(pMap_Copy, NULL, Main.map, &src);
    sdl_dirty_rect(src);
  }
  
  /* Draw the new sprite. */
  SDL_BlitSurface(Main.map, &dest, pMap_Copy, NULL);
  
  /* draw animation frame */
  dest.y += (NORMAL_TILE_HEIGHT >> 1);
  SDL_BlitSurface(pTheme->ActiveUnit[frame++], NULL, Main.map, &dest);
  dest.y -= (NORMAL_TILE_HEIGHT >> 1);
  
  SDL_BlitSurface(pUnit_Surf, NULL, Main.map, &dest);
  sdl_dirty_rect(dest);

  /* Write to screen. */
  
  flush_dirty();
  
  if(frame > 3) {
    frame = 0;
  }
  
  if (last_frame) {
    FREESURFACE(pMap_Copy);
    FREESURFACE(pUnit_Surf);
  }
}

/**************************************************************************
 This function is called to decrease a unit's HP smoothly in battle when
 combat_animation is turned on.
**************************************************************************/
void decrease_unit_hp_smooth(struct unit *punit0, int hp0,
			     struct unit *punit1, int hp1)
{

  static struct timer *anim_timer = NULL;
  struct unit *losing_unit = (hp0 == 0 ? punit0 : punit1);
  int i, canvas_x, canvas_y;
  SDL_Rect src =
      { 0, 0, NORMAL_TILE_WIDTH, 1.5 * NORMAL_TILE_HEIGHT }, dest;

  set_units_in_combat(punit0, punit1);

  do {
    anim_timer = renew_timer_start(anim_timer, TIMER_USER, TIMER_ACTIVE);

    if (punit0->hp > hp0
	&& myrand((punit0->hp - hp0) + (punit1->hp - hp1)) <
	punit0->hp - hp0) {
      punit0->hp--;
    } else {
      if (punit1->hp > hp1) {
	punit1->hp--;
      } else {
	punit0->hp--;
      }
    }

    refresh_tile_mapcanvas(punit0->x, punit0->y, FALSE);
    refresh_tile_mapcanvas(punit1->x, punit1->y, FALSE);
    
    flush_dirty();

    usleep_since_timer_start(anim_timer, 10000);

  } while (punit0->hp > hp0 || punit1->hp > hp1);


  if (num_tiles_explode_unit &&
      get_canvas_xy(losing_unit->x, losing_unit->y, &canvas_x, &canvas_y)) {
    /* copy screen area */
    src.x = canvas_x;
    src.y = canvas_y;
    SDL_BlitSurface(Main.screen, &src, pTmpSurface, NULL);

    dest.y = canvas_y;
    for (i = 0; i < num_tiles_explode_unit; i++) {

      anim_timer = renew_timer_start(anim_timer, TIMER_USER, TIMER_ACTIVE);

      /* blit explosion */
      dest.x = canvas_x + NORMAL_TILE_WIDTH / 4;

      SDL_BlitSurface(GET_SURF(sprites.explode.unit[i]), 
      						NULL, Main.map, &dest);
      
      flush_rect(dest);

      usleep_since_timer_start(anim_timer, 20000);

      /* clear explosion */
      dest.x = canvas_x;
      SDL_BlitSurface(pTmpSurface, NULL, Main.map, &dest);
    }
  }

  flush_rect(dest);

  /* clear pTmpSurface */
  if ((pTmpSurface->format->BytesPerPixel == 4) &&
      (!pTmpSurface->format->Amask)) {
    SDL_FillRect(pTmpSurface, NULL, Amask);
  } else {
    SDL_FillRect(pTmpSurface, NULL, 0x0);
  }

  set_units_in_combat(NULL, NULL);
  /*
     refresh_tile_mapcanvas(punit0->x, punit0->y, TRUE);
     refresh_tile_mapcanvas(punit1->x, punit1->y, TRUE);
   */
}

/**************************************************************************
  Update (refresh) the locations of the mapview scrollbars (if it uses
  them).
**************************************************************************/
void update_map_canvas_scrollbars(void)
{
  /* No scrollbars. */
}

/**************************************************************************
  Draw a cross-hair overlay on a tile.
**************************************************************************/
void put_cross_overlay_tile(int x, int y)
{
  freelog(LOG_DEBUG, "MAPVIEW: put_cross_overlay_tile : PORT ME");
}

/**************************************************************************
  Draw in information about city workers on the mapview in the given
  color.
**************************************************************************/
void put_city_workers(struct city *pcity, int color)
{
  freelog(LOG_DEBUG, "MAPVIEW: put_city_workers : PORT ME");
}

/**************************************************************************
  Draw a nuke mushroom cloud at the given tile.
**************************************************************************/
void put_nuke_mushroom_pixmaps(int x, int y)
{
  freelog(LOG_DEBUG, "MAPVIEW: put_nuke_mushroom_pixmaps : PORT ME");
}

/**************************************************************************
  Draw a segment (e.g., a goto line) from the given tile in the given
  direction.
**************************************************************************/
void draw_segment(int src_x, int src_y, int dir)
{
  freelog(LOG_DEBUG, "MAPVIEW: draw_segment : PORT ME");
}

/**************************************************************************
  This function is called when the tileset is changed.
**************************************************************************/
void tileset_changed(void)
{
  /* PORTME */
  /* Here you should do any necessary redraws (for instance, the city
   * dialogs usually need to be resized). */
  
  free_dither_tiles();
  init_dither_tiles();
  
}

/* =====================================================================
				City MAP
   ===================================================================== */

/**************************************************************************
  ...
**************************************************************************/
SDL_Surface *create_city_map(struct city *pCity)
{

  register Uint16 col = 0, row;
  SDL_Rect dest;
  int draw_units_backup = draw_units;
  int draw_map_grid_backup = draw_map_grid;
  int sx, sy, row0, real_col = pCity->x, real_row = pCity->y;

  Sint16 x0 = 3 * NORMAL_TILE_WIDTH / 2;
  Sint16 y0 = -NORMAL_TILE_HEIGHT / 2;
  SDL_Surface *pTile = NULL;
  SDL_Surface *pDest = create_surf(get_citydlg_canvas_width(),
				   get_citydlg_canvas_height(),
				   SDL_SWSURFACE);
  real_col -= 2;
  real_row -= 2;
  correction_map_pos((int *) &real_col, (int *) &real_row);
  row0 = real_row;

  /* turn off drawing units and map_grid */
  draw_units = 0;
  draw_map_grid = 0;

  /* draw loop */
  for (; col < CITY_MAP_SIZE; col++) {
    for (row = 0; row < CITY_MAP_SIZE; row++) {
#if 1
      sx = x0 + (col - row) * (NORMAL_TILE_WIDTH / 2);
      sy = y0 + (row + col) * (NORMAL_TILE_HEIGHT / 2);
#else
      /* ugly hack for speed.  Assumes:
       *   NORMAL_TILE_WIDTH == 64
       *   NORMAL_TILE_HEIGHT == 32  */
      sx = x0 + ((col - row) << 5);
      sy = y0 + ((row + col) << 4);
#endif
      if (sx > -NORMAL_TILE_WIDTH && sx < pDest->w + NORMAL_TILE_WIDTH
	  && sy > -NORMAL_TILE_HEIGHT
	  && sy < pDest->h + NORMAL_TILE_HEIGHT) {
	dest.x = sx;
	dest.y = sy;
	if ((!col && !row) ||
	    (!col && row == CITY_MAP_SIZE - 1) ||
	    (!row && col == CITY_MAP_SIZE - 1) ||
	    (col == CITY_MAP_SIZE - 1 && row == CITY_MAP_SIZE - 1)) {
	  /* draw black corners */

	  SDL_BlitSurface(GET_SURF(sprites.black_tile),
			  NULL, pDest, &dest);
	} else {
	  /* draw map cell */
	  draw_map_cell(pDest, dest.x, dest.y, real_col, real_row, 1);
	  if (get_worker_city(pCity, col, row) == C_TILE_UNAVAILABLE &&
	      map_get_terrain(real_col, real_row) != T_UNKNOWN) {
	    if (!pTile) {
	      /* make mask */
	      SDL_BlitSurface(GET_SURF(sprites.black_tile),
			      NULL, pTmpSurface, NULL);

	      SDL_SetColorKey(pTmpSurface, SDL_SRCCOLORKEY,
			      getpixel(pTmpSurface, NORMAL_TILE_WIDTH / 2,
				       NORMAL_TILE_HEIGHT / 2));
	      /* create pTile */
	      pTile = create_surf(NORMAL_TILE_WIDTH,
				  NORMAL_TILE_HEIGHT, SDL_SWSURFACE);
	      
	      /* fill pTile with RED */
	      SDL_FillRect(pTile, NULL,
			   SDL_MapRGBA(pTile->format, 255, 0, 0, 96));

	      /* blit mask */
	      SDL_BlitSurface(pTmpSurface, NULL, pTile, NULL);

	      SDL_SetColorKey(pTile, SDL_SRCCOLORKEY | SDL_RLEACCEL,
			      get_first_pixel(pTile));

	      SDL_SetAlpha(pTile, SDL_SRCALPHA, 96);

	      /* clear pTmpSurface */
	      if (pTmpSurface->format->BytesPerPixel == 4 &&
		  !pTmpSurface->format->Amask) {
		SDL_SetColorKey(pTmpSurface, SDL_SRCCOLORKEY, Amask);
		SDL_FillRect(pTmpSurface, NULL, Amask);
	      } else {
		SDL_SetColorKey(pTmpSurface, SDL_SRCCOLORKEY, 0x0);
		SDL_FillRect(pTmpSurface, NULL, 0);
	      }
	    }
	    SDL_BlitSurface(pTile, NULL, pDest, &dest);
	  }
	}

      }
      if (++real_row >= map.ysize) {
        real_row = 0;
      }      
    }
    real_row = row0;
    if (++real_col >= map.xsize) {
      real_col = 0;
    }
  }

  draw_units = draw_units_backup;
  draw_map_grid = draw_map_grid_backup;

  FREESURFACE(pTile);

  return pDest;
}


SDL_Surface * get_terrain_surface(int x , int y)
{
  SDL_Surface *pSurf = create_surf(UNIT_TILE_WIDTH,
			           UNIT_TILE_HEIGHT,
			           SDL_SWSURFACE);
  int fg = draw_fog_of_war,
     ci = draw_cities ,
     tr = draw_terrain ,
     rr = draw_roads_rails,
     ir = draw_irrigation,
     un = draw_units,
     gr = draw_map_grid,
     pl = draw_pollution ,
     fa = draw_fortress_airbase,
     mi = draw_mines;
  
  draw_fog_of_war = FALSE;
  draw_cities = FALSE;
  draw_terrain = TRUE;
  draw_roads_rails = FALSE;
  draw_irrigation = FALSE;
  draw_units = FALSE;
  draw_map_grid = FALSE;
  draw_pollution = FALSE;
  draw_fortress_airbase = FALSE;
  draw_mines = FALSE;
  
  SDL_Client_Flags &= ~CF_DRAW_MAP_DITHER;
  
  draw_map_cell(pSurf, 0, NORMAL_TILE_HEIGHT / 2, x , y , 0);
    
  SDL_SetColorKey(pSurf , SDL_SRCCOLORKEY|SDL_RLEACCEL , 0x0);
  
  SDL_Client_Flags |= CF_DRAW_MAP_DITHER;
  
  draw_fog_of_war = fg;
  draw_cities = ci;
  draw_terrain = tr;
  draw_roads_rails = rr;
  draw_irrigation = ir;
  draw_units = un;
  draw_map_grid = gr;
  draw_pollution = pl;
  draw_fortress_airbase = fa;
  draw_mines = mi;
  
  return pSurf;
}

/* =====================================================================
				Init Functions
   ===================================================================== */

/**************************************************************************
  This Function is used when resize Main.screen.
**************************************************************************/
void get_new_view_rectsize(void)
{
  /* I leave this function to future use */  
}

static SDL_Surface * create_ocean_tile(void)
{
  SDL_Surface *pOcean = create_surf(NORMAL_TILE_WIDTH ,
			  	     NORMAL_TILE_HEIGHT ,
			    	     SDL_SWSURFACE);
  SDL_Rect des = { 0 , 0 , 0, 0 };
  SDL_Surface *pBuf[4];
  
  pBuf[0] = GET_SURF(sprites.tx.coast_cape_iso[0][0]);
  pBuf[1] = GET_SURF(sprites.tx.coast_cape_iso[0][1]);
  pBuf[2] = GET_SURF(sprites.tx.coast_cape_iso[0][2]);
  pBuf[3] = GET_SURF(sprites.tx.coast_cape_iso[0][3]);
  
  /* top */
  des.y = 0;
  des.x = NORMAL_TILE_WIDTH / 4;
  SDL_BlitSurface(pBuf[0], NULL, pOcean, &des);

  /* bottom */
  des.y = NORMAL_TILE_HEIGHT / 2;
  SDL_BlitSurface(pBuf[1], NULL, pOcean, &des);

  /* left */
  des.x = 0;
  des.y = NORMAL_TILE_HEIGHT / 4;
  SDL_BlitSurface(pBuf[2], NULL, pOcean, &des);

  /* right */
  des.x = NORMAL_TILE_WIDTH / 2;
  SDL_BlitSurface(pBuf[3], NULL, pOcean, &des);
  
  SDL_SetColorKey(pOcean, SDL_SRCCOLORKEY|SDL_RLEACCEL, 0x0);
  
  return pOcean;
}


static void fill_dither_buffers(SDL_Surface **pDitherBufs, int x, int y,
					enum tile_terrain_type terrain)
{
  enum tile_terrain_type __ter[8];
  enum direction8 dir;
  int x1, y1;
  
  /* Loop over all adjacent tiles.  We should have an iterator for this. */
  for (dir = 0; dir < 8; dir++) {
  
    if (MAPSTEP(x1, y1, x, y, dir)) {
      __ter[dir] = map_get_terrain(x1, y1);
      
      /* hacking away the river here... */
      if (is_isometric && __ter[dir] == T_RIVER) {
	__ter[dir] = T_GRASSLAND;
      }
    } else {
      /* We draw the edges of the map as if the same terrain just
       * continued off the edge of the map. */
      
      __ter[dir] = terrain;
    }
  }
  
  /* north */
  pDitherBufs[0] = pDithers[__ter[DIR8_NORTH]][0];
  
  /* south */
  pDitherBufs[1] = pDithers[__ter[DIR8_SOUTH]][1];
  
  /* east */
  pDitherBufs[2] = pDithers[__ter[DIR8_EAST]][2];
  
  /* weast */
  pDitherBufs[3] = pDithers[__ter[DIR8_WEST]][3];
}

static void init_dither_tiles(void)
{
  enum tile_terrain_type terrain;
  int i , w = NORMAL_TILE_WIDTH / 2, h = NORMAL_TILE_HEIGHT / 2;
  SDL_Rect src = { 0 , 0 , w , h };
  SDL_Surface *pTerrain_Surface , *pBuf = create_surf( NORMAL_TILE_WIDTH,
  					NORMAL_TILE_HEIGHT , SDL_SWSURFACE);
  
  SDL_SetColorKey(pBuf , SDL_SRCCOLORKEY , 0x0);
  
  for (terrain = T_ARCTIC ; terrain < T_LAST ; terrain++)
  {
    
    if (terrain == T_RIVER || is_ocean(terrain) || terrain == T_UNKNOWN)
    {
      for(i = 0; i < 4; i++)
      {
        pDithers[terrain][i] = NULL;
      }
      continue;
    }
      
    pTerrain_Surface = GET_SURF(get_tile_type(terrain)->sprite[0]);
    
    for( i = 0; i < 4; i++ )
    {
      pDithers[terrain][i] = create_surf(w, h , SDL_SWSURFACE);
    }
  
    /* north */
    dither_north(pTerrain_Surface, pDitherMask, pBuf);
    
    /* south */
    dither_south(pTerrain_Surface, pDitherMask, pBuf);
    
    /* east */
    dither_east(pTerrain_Surface, pDitherMask, pBuf);

    /* west */
    dither_west(pTerrain_Surface, pDitherMask, pBuf);
    
    /* north */
    src.x = w;
    SDL_BlitSurface(pBuf, &src, pDithers[terrain][0], NULL);
    
    /* south */
    src.x = 0;
    src.y = h;
    SDL_BlitSurface(pBuf, &src, pDithers[terrain][1], NULL);
    
    /* east */
    src.x = w;
    src.y = h;
    SDL_BlitSurface(pBuf, &src, pDithers[terrain][2], NULL);
    
    /* west */
    src.x = 0;
    src.y = 0;
    SDL_BlitSurface(pBuf, &src, pDithers[terrain][3], NULL);
    
    SDL_FillRect(pBuf, NULL, 0x0);
    
    for(i = 0; i < 4; i++)
    {
      SDL_SetColorKey(pDithers[terrain][i] , SDL_SRCCOLORKEY|SDL_RLEACCEL, 0x0);
    }
    
  }
  
  FREESURFACE(pBuf);
}

static void free_dither_tiles(void)
{
  enum tile_terrain_type terrain;
  int i;
    
  for (terrain = T_ARCTIC ; terrain < T_LAST ; terrain++)
  {
    for(i = 0; i < 4; i++)
    {
      FREESURFACE(pDithers[terrain][i]);
    }
  }
  
}

static void clear_dither_tiles(void)
{
  enum tile_terrain_type terrain;
  int i;
  for (terrain = T_ARCTIC ; terrain < T_LAST ; terrain++)
  {
    for(i = 0; i < 4; i++)
    {
      pDithers[terrain][i] = NULL;
    }
  }
}

/**************************************************************************
  ...
**************************************************************************/
void tmp_map_surfaces_init(void)
{
  SDL_Color fog = *(get_game_colorRGB(COLOR_STD_FOG_OF_WAR));

  #if SDL_BYTEORDER == SDL_BIG_ENDIAN
    Amask = 0x000000ff;
  #else
    Amask = 0xff000000;
  #endif
  
  clear_dither_tiles();
  
  pFogSurface = create_surf(UNIT_TILE_WIDTH,
			    UNIT_TILE_HEIGHT ,
			    SDL_SWSURFACE);

  if (Main.screen->format->BytesPerPixel == 4 )
  {
    pTmpSurface = SDL_DisplayFormatAlpha(pFogSurface);
    FREESURFACE(pFogSurface);
    pFogSurface = pTmpSurface;
  }
  
  SDL_FillRect(pFogSurface, NULL,
	     SDL_MapRGBA(pFogSurface->format, fog.r, fog.g, fog.b, fog.unused));
	
  SDL_SetAlpha(pFogSurface, SDL_SRCALPHA, fog.unused);

  /* =========================== */
  pTmpSurface = create_surf(UNIT_TILE_WIDTH,
			    UNIT_TILE_HEIGHT,
			    SDL_SWSURFACE);
  SDL_SetAlpha(pTmpSurface, 0x0, 0x0);

  /* if BytesPerPixel == 4 and Amask == 0 ( 24 bit coding ) alpha blit
   * functions; Set A = 255 in all pixels and then pixel val
   *  ( r=0, g=0, b=0) != 0.  Real val this pixel is 0xff000000/0x000000ff. */
  if (pTmpSurface->format->BytesPerPixel == 4
      && !pTmpSurface->format->Amask) {
    SDL_SetColorKey(pTmpSurface, SDL_SRCCOLORKEY, Amask);
  } else {
    SDL_SetColorKey(pTmpSurface, SDL_SRCCOLORKEY, 0x0);
  }
  
  pOcean_Tile = create_ocean_tile();
  
  pOcean_Foged_Tile = create_surf( NORMAL_TILE_WIDTH ,
			    NORMAL_TILE_HEIGHT ,
			    SDL_SWSURFACE);
  
  SDL_BlitSurface(pOcean_Tile , NULL , pOcean_Foged_Tile, NULL);
  SDL_BlitSurface(pFogSurface , NULL , pOcean_Foged_Tile, NULL);
  SDL_SetColorKey(pOcean_Foged_Tile , SDL_SRCCOLORKEY|SDL_RLEACCEL,
			  get_first_pixel(pOcean_Foged_Tile));
  
  pBlinkSurfaceA =
      create_surf(UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT,
		  SDL_SWSURFACE);
  
  pBlinkSurfaceB =
      create_surf(UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT,
		  SDL_SWSURFACE);
/*  SDL_SetColorKey(pBlinkSurfaceB , SDL_SRCCOLORKEY|SDL_RLEACCEL, 0x0);*/
   SDL_SetColorKey(pBlinkSurfaceB , SDL_SRCCOLORKEY, 0x0);
   
  get_new_view_rectsize();


}
