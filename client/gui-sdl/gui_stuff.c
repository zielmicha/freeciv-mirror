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
                          gui_stuff.c  -  description
                             -------------------
    begin                : June 30 2002
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <SDL/SDL.h>

#include "log.h"

/* gui-sdl */
#include "colors.h"
#include "graphics.h"
#include "gui_iconv.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "themespec.h"
#include "unistring.h"

#include "gui_stuff.h"

struct widget *pSellected_Widget;
SDL_Rect *pInfo_Area = NULL;
SDL_Surface *pInfo_Label = NULL;

extern Uint32 widget_info_counter;

/* ================================ Private ============================ */

struct MOVE {
  bool moved;
  struct widget *pWindow;
};

struct EDIT {
  struct UniChar *pBeginTextChain;
  struct UniChar *pEndTextChain;
  struct UniChar *pInputChain;
  SDL_Surface *pBg;
  struct widget *pWidget;
  int ChainLen;
  int Start_X;
  int Truelength;
  int InputChain_X;
};

struct UP_DOWN {
  struct widget *pBegin;
  struct widget *pEnd;
  struct widget *pBeginWidgetLIST;
  struct widget *pEndWidgetLIST;
  struct ScrollBar *pVscroll;
  int old_y;
  int step;
};

static struct widget *pBeginMainWidgetList;
/* static struct widget *pEndMainWidgetList; */

#define UpperAdd(pNew_Widget, pAdd_Dock)	\
do {						\
  pNew_Widget->prev = pAdd_Dock;		\
  pNew_Widget->next = pAdd_Dock->next;		\
  if(pAdd_Dock->next) {			\
    pAdd_Dock->next->prev = pNew_Widget;	\
  }						\
  pAdd_Dock->next = pNew_Widget;		\
} while(0)

/**************************************************************************
  ...
**************************************************************************/
struct UniChar {
  struct UniChar *next;
  struct UniChar *prev;
  Uint16 chr[2];
  SDL_Surface *pTsurf;
};

static size_t chainlen(const struct UniChar *pChain);
static void del_chain(struct UniChar *pChain);
static struct UniChar * text2chain(const Uint16 *pInText);
static Uint16 * chain2text(const struct UniChar *pInChain, size_t len);
		       
static void correct_size_bcgnd_surf(SDL_Surface *pTheme,
				    Uint16 *pWidth, Uint16 *pHigh);
#if 0
static SDL_Surface * create_bcgnd_surf(SDL_Surface *pTheme, Uint8 state,
				       Uint16 Width, Uint16 High);
#endif
static SDL_Surface * create_vertical_surface(SDL_Surface *pVert_theme,
					    Uint8 state, Uint16 High);

static void remove_buffer_layer(SDL_Surface *pBuffer);
static int redraw_iconlabel(struct widget *pLabel);
SDL_Surface * get_buffer_layer(int width, int height);

/**************************************************************************
  Correct backgroud size ( set min size ). Used in create widget
  functions.
**************************************************************************/
static void correct_size_bcgnd_surf(SDL_Surface * pTheme,
				    Uint16 * pWidth, Uint16 * pHigh)
{
  *pWidth = MAX(*pWidth, 2 * (pTheme->w / adj_size(16)));
  *pHigh = MAX(*pHigh, 2 * (pTheme->h / adj_size(16)));
}

/**************************************************************************
  Create background image for buttons, iconbuttons and edit fields
  then return  pointer to this image.

  Graphic is taken from pTheme surface and blit to new created image.

  Length and height depend of iText_with, iText_high parameters.

  Type of image depend of "state" parameter.
    state = 0 - normal
    state = 1 - selected
    state = 2 - pressed
    state = 3 - disabled
**************************************************************************/
SDL_Surface *create_bcgnd_surf(SDL_Surface * pTheme, Uint8 state,
                               Uint16 Width, Uint16 High)
{
  bool zoom;
  int iTile_width_len_end, iTile_width_len_mid, iTile_count_len_mid;
  int iTile_width_high_end, iTile_width_high_mid, iTile_count_high_mid;
  int i, j;

  SDL_Rect src, des;
  SDL_Surface *pBackground = NULL;

  int iStart_y = (pTheme->h / 4) * state;

  iTile_width_len_end = pTheme->w / 16;
  iTile_width_len_mid = pTheme->w - (iTile_width_len_end * 2);

  iTile_count_len_mid =
      (Width - (iTile_width_len_end * 2)) / iTile_width_len_mid;

  /* corrections I */
  if (((iTile_count_len_mid *
	iTile_width_len_mid) + (iTile_width_len_end * 2)) < Width) {
    iTile_count_len_mid++;
  }

  iTile_width_high_end = pTheme->h / 16;
  iTile_width_high_mid = (pTheme->h / 4) - (iTile_width_high_end * 2);
  iTile_count_high_mid =
      (High - (iTile_width_high_end * 2)) / iTile_width_high_mid;

  /* corrections II */
  if (((iTile_count_high_mid *
	iTile_width_high_mid) + (iTile_width_high_end * 2)) < High) {
    iTile_count_high_mid++;
  }

  i = MAX(iTile_width_len_end * 2, Width);
  j = MAX(iTile_width_high_end * 2, High);
  zoom = ((i != Width) ||  (j != High));
  
  /* now allocate memory */
  pBackground = create_surf_alpha(i, j, SDL_SWSURFACE);

  /* copy left end */

  /* copy left top end */
  src.x = 0;
  src.y = iStart_y;
  src.w = iTile_width_len_end;
  src.h = iTile_width_high_end;

  des.x = 0;
  des.y = 0;
  alphablit(pTheme, &src, pBackground, &des);

  /* copy left middels parts */
  src.y = iStart_y + iTile_width_high_end;
  src.h = iTile_width_high_mid;
  for (i = 0; i < iTile_count_high_mid; i++) {
    des.y = iTile_width_high_end + i * iTile_width_high_mid;
    alphablit(pTheme, &src, pBackground, &des);
  }

  /* copy left boton end */
  src.y = iStart_y + ((pTheme->h / 4) - iTile_width_high_end);
  src.h = iTile_width_high_end;
  des.y = pBackground->h - iTile_width_high_end;
  clear_surface(pBackground, &des);
  alphablit(pTheme, &src, pBackground, &des);

  /* copy middle parts without right end part */

  src.x = iTile_width_len_end;
  src.y = iStart_y;
  src.w = iTile_width_len_mid;

  for (i = 0; i < iTile_count_len_mid; i++) {

    /* top */
    des.x = iTile_width_len_end + i * iTile_width_len_mid;
    des.y = 0;
    src.y = iStart_y;
    alphablit(pTheme, &src, pBackground, &des);

    /*  middels */
    src.y = iStart_y + iTile_width_high_end;
    src.h = iTile_width_high_mid;
    for (j = 0; j < iTile_count_high_mid; j++) {
      des.y = iTile_width_high_end + j * iTile_width_high_mid;
      alphablit(pTheme, &src, pBackground, &des);
    }

    /* bottom */
    src.y = iStart_y + ((pTheme->h / 4) - iTile_width_high_end);
    src.h = iTile_width_high_end;
    des.y = pBackground->h - iTile_width_high_end;
    clear_surface(pBackground, &des);    
    alphablit(pTheme, &src, pBackground, &des);
  }

  /* copy right end */
  src.x = pTheme->w - iTile_width_len_end;
  src.y = iStart_y;
  src.w = iTile_width_len_end;

  des.x = pBackground->w - iTile_width_len_end;
  des.y = 0;

  alphablit(pTheme, &src, pBackground, &des);

  /*  middels */
  src.y = iStart_y + iTile_width_high_end;
  src.h = iTile_width_high_mid;
  for (i = 0; i < iTile_count_high_mid; i++) {
    des.y = iTile_width_high_end + i * iTile_width_high_mid;
    alphablit(pTheme, &src, pBackground, &des);
  }

  /*boton */
  src.y = iStart_y + ((pTheme->h / 4) - iTile_width_high_end);
  src.h = iTile_width_high_end;
  des.y = pBackground->h - iTile_width_high_end;
  clear_surface(pBackground, &des);  
  alphablit(pTheme, &src, pBackground, &des);
  
  if (zoom)
  {
    SDL_Surface *pZoom = ResizeSurface(pBackground, Width, High, 1);
    FREESURFACE(pBackground);
    pBackground = pZoom;
  }
  
  return pBackground;
}

/* =================================================== */
/* ===================== GUI LIST ==================== */
/* =================================================== */

/**************************************************************************
  Simple "Search and De..." no search in 'pGUI_List' == "Widget's list" and
  return ( not Disabled and not Hiden ) widget under cursor 'pPosition'.
**************************************************************************/
struct widget * WidgetListScaner(const struct widget *pGUI_List, int x, int y)
{
  while (pGUI_List) {
    if (is_in_rect_area(x, y, pGUI_List->size)
       && !((get_wstate(pGUI_List) == FC_WS_DISABLED) ||
	    ((get_wflags(pGUI_List) & WF_HIDDEN) == WF_HIDDEN))) {
      return (struct widget *) pGUI_List;
    }
    pGUI_List = pGUI_List->next;
  }
  return NULL;
}

/**************************************************************************
  Search in 'pGUI_List' == "Widget's list" and
  return ( not Disabled and not Hiden ) widget with 'Key' allias.
  NOTE: This function ignores CapsLock and NumLock Keys.
**************************************************************************/
struct widget *WidgetListKeyScaner(const struct widget *pGUI_List, SDL_keysym Key)
{
  Key.mod &= ~(KMOD_NUM | KMOD_CAPS);
  while (pGUI_List) {
    if ((pGUI_List->key == Key.sym ||
      (pGUI_List->key == SDLK_RETURN && Key.sym == SDLK_KP_ENTER) ||
      (pGUI_List->key == SDLK_KP_ENTER && Key.sym == SDLK_RETURN)) &&
	((pGUI_List->mod & Key.mod) || (pGUI_List->mod == Key.mod))) {
      if (!((get_wstate(pGUI_List) == FC_WS_DISABLED) ||
	    ((get_wflags(pGUI_List) & WF_HIDDEN) == WF_HIDDEN))) {
	return (struct widget *) pGUI_List;
      }
    }
    pGUI_List = pGUI_List->next;
  }
  return NULL;
}

/**************************************************************************
  Pointer to Main Widget list is declared staric in 'gui_stuff.c'
  This function only calls 'WidgetListScaner' in Main list
  'pBeginMainWidgetList'
**************************************************************************/
struct widget *MainWidgetListScaner(int x, int y)
{
  return WidgetListScaner(pBeginMainWidgetList, x, y);
}

/**************************************************************************
  ...
**************************************************************************/
struct widget *MainWidgetListKeyScaner(SDL_keysym Key)
{
  return WidgetListKeyScaner(pBeginMainWidgetList, Key);
}

/**************************************************************************
  Do default Widget action when pressed, and then call widget callback
  function.

  example for Button:
    set flags FW_Pressed
    redraw button ( pressed )
    refresh screen ( to see result )
    wait 300 ms	( to see result :)
    If exist (button callback function) then
      call (button callback function)

    Function normal return Widget ID.
    NOTE: NOZERO return of this function deterninate exit of
        MAIN_SDL_GAME_LOOP
    if ( pWidget->action )
      if ( pWidget->action(pWidget)  ) ID = 0;
    if widget callback function return = 0 then return NOZERO
    I default return (-1) from Widget callback functions.
**************************************************************************/
Uint16 widget_pressed_action(struct widget * pWidget)
{
  Uint16 ID = 0;

  if (!pWidget) {
    return 0;
  }
  
  widget_info_counter = 0;
  if (pInfo_Area) {
    sdl_dirty_rect(*pInfo_Area);
    FC_FREE(pInfo_Area);
    FREESURFACE(pInfo_Label);
  }
  
  switch (get_wtype(pWidget)) {
  case WT_TI_BUTTON:
    if (Main.event.button.button == SDL_BUTTON_LEFT) {
      set_wstate(pWidget, FC_WS_PRESSED);
      real_redraw_tibutton(pWidget);
      sdl_dirty_rect(pWidget->size);
      flush_dirty();
      set_wstate(pWidget, FC_WS_SELLECTED);
      SDL_Delay(300);
    }
    ID = pWidget->ID;
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  case WT_I_BUTTON:
    if (Main.event.button.button == SDL_BUTTON_LEFT) {
      set_wstate(pWidget, FC_WS_PRESSED);
      real_redraw_ibutton(pWidget);
      sdl_dirty_rect(pWidget->size);
      flush_dirty();
      set_wstate(pWidget, FC_WS_SELLECTED);
      SDL_Delay(300);
    }
    ID = pWidget->ID;  
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  case WT_ICON:
    if (Main.event.button.button == SDL_BUTTON_LEFT) {
      set_wstate(pWidget, FC_WS_PRESSED);
      real_redraw_icon(pWidget);
      sdl_dirty_rect(pWidget->size);
      flush_dirty();
      set_wstate(pWidget, FC_WS_SELLECTED);
      SDL_Delay(300);
    }
    ID = pWidget->ID;
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  case WT_ICON2:
    if (Main.event.button.button == SDL_BUTTON_LEFT) {
      set_wstate(pWidget, FC_WS_PRESSED);
      real_redraw_icon2(pWidget);
      sdl_dirty_rect(pWidget->size);
      flush_dirty();
      set_wstate(pWidget, FC_WS_SELLECTED);
      SDL_Delay(300);
    }
    ID = pWidget->ID;
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  case WT_EDIT:
  {
    if (Main.event.button.button == SDL_BUTTON_LEFT) {
      bool ret, loop = ((get_wflags(pWidget) & WF_EDIT_LOOP) == WF_EDIT_LOOP);
      enum Edit_Return_Codes change;
      do {
        ret = FALSE;
        change = edit_field(pWidget);
        if (change != ED_FORCE_EXIT && (!loop || change != ED_RETURN)) {
          redraw_edit(pWidget);
          sdl_dirty_rect(pWidget->size);
          flush_dirty();
        }
        if (change != ED_FORCE_EXIT && change != ED_ESC && pWidget->action) {
          if (pWidget->action(pWidget)) {
            ID = 0;
          }
        }
        if (loop && change == ED_RETURN) {
          ret = TRUE;
        }
      } while(ret);
      ID = 0;
    }
    break;
  }
  case WT_VSCROLLBAR:
    if (Main.event.button.button == SDL_BUTTON_LEFT) {
      set_wstate(pWidget, FC_WS_PRESSED);
      redraw_vert(pWidget);
      sdl_dirty_rect(pWidget->size);
      flush_dirty();
    }
    ID = pWidget->ID;
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  case WT_HSCROLLBAR:
    if (Main.event.button.button == SDL_BUTTON_LEFT) {    
      set_wstate(pWidget, FC_WS_PRESSED);
      redraw_horiz(pWidget);
      flush_rect(pWidget->size, FALSE);
    }
    ID = pWidget->ID;  
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  case WT_CHECKBOX:
    if (Main.event.button.button == SDL_BUTTON_LEFT) {
      set_wstate(pWidget, FC_WS_PRESSED);
      real_redraw_icon(pWidget);
      sdl_dirty_rect(pWidget->size);
      flush_dirty();
      set_wstate(pWidget, FC_WS_SELLECTED);
      togle_checkbox(pWidget);
      SDL_Delay(300);
    }
    ID = pWidget->ID;  
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  case WT_TCHECKBOX:
    if (Main.event.button.button == SDL_BUTTON_LEFT) {
      set_wstate(pWidget, FC_WS_PRESSED);
      redraw_textcheckbox(pWidget);
      flush_rect(pWidget->size, FALSE);
      set_wstate(pWidget, FC_WS_SELLECTED);
      togle_checkbox(pWidget);
      SDL_Delay(300);
    }
    ID = pWidget->ID;
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  default:
    ID = pWidget->ID;
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  }

  return ID;
}

/**************************************************************************
  Unsellect (sellected) widget and redraw this widget;
**************************************************************************/
void unsellect_widget_action(void)
{
  if (pSellected_Widget) {
    if (get_wstate(pSellected_Widget) == FC_WS_DISABLED) {
      goto End;
    }

    set_wstate(pSellected_Widget, FC_WS_NORMAL);

    if (get_wflags(pSellected_Widget) & WF_HIDDEN) {
      goto End;
    }

    switch (get_wtype(pSellected_Widget)) {
    case WT_TI_BUTTON:
      real_redraw_tibutton(pSellected_Widget);
      break;
    case WT_I_BUTTON:
      real_redraw_ibutton(pSellected_Widget);
      break;
    case WT_ICON:
    case WT_CHECKBOX:
      real_redraw_icon(pSellected_Widget);
      break;
    case WT_ICON2:
      real_redraw_icon2(pSellected_Widget);
      break;
    case WT_TCHECKBOX:
      redraw_textcheckbox(pSellected_Widget);
      break;
    case WT_I_LABEL:
    case WT_T2_LABEL:  
      redraw_label(pSellected_Widget);
      break;
    case WT_VSCROLLBAR:
      redraw_vert(pSellected_Widget);
      break;
    case WT_HSCROLLBAR:
      redraw_horiz(pSellected_Widget);
      break;
    default:
      break;
    }

    flush_rect(pSellected_Widget->size, FALSE);

    /* turn off quick info timer/counter */ 
    widget_info_counter = 0;

  }

End:

  if (pInfo_Area) {
    flush_rect(*pInfo_Area, FALSE);
    FC_FREE(pInfo_Area);
    FREESURFACE(pInfo_Label);    
  }

  pSellected_Widget = NULL;
}

/**************************************************************************
  Sellect widget.  Redraw this widget;
**************************************************************************/
void widget_sellected_action(struct widget *pWidget)
{
  if (!pWidget || pWidget == pSellected_Widget) {
    return;
  }

  if (pSellected_Widget) {
    unsellect_widget_action();
  }

  switch (get_wtype(pWidget)) {
  case WT_TI_BUTTON:
    set_wstate(pWidget, FC_WS_SELLECTED);
    real_redraw_tibutton(pWidget);
    break;
  case WT_I_BUTTON:
    set_wstate(pWidget, FC_WS_SELLECTED);
    real_redraw_ibutton(pWidget);
    break;
  case WT_ICON:
  case WT_CHECKBOX:
    set_wstate(pWidget, FC_WS_SELLECTED);
    real_redraw_icon(pWidget);
    break;
  case WT_ICON2:
    set_wstate(pWidget, FC_WS_SELLECTED);
    real_redraw_icon2(pWidget);
    break;
  case WT_TCHECKBOX:
    set_wstate(pWidget, FC_WS_SELLECTED);
    redraw_textcheckbox(pWidget);
    break;
  case WT_I_LABEL:
  case WT_T2_LABEL:  
    set_wstate(pWidget, FC_WS_SELLECTED);
    redraw_label(pWidget);
    break;
  case WT_VSCROLLBAR:
    set_wstate(pWidget, FC_WS_SELLECTED);
    redraw_vert(pWidget);
    break;
  case WT_HSCROLLBAR:
    set_wstate(pWidget, FC_WS_SELLECTED);
    redraw_horiz(pWidget);
    break;
  default:
    break;
  }

  if (get_wstate(pWidget) == FC_WS_SELLECTED) {
    flush_rect(pWidget->size, FALSE);
    pSellected_Widget = pWidget;
    if (get_wflags(pWidget) & WF_WIDGET_HAS_INFO_LABEL) {
      widget_info_counter = 1;
    }
  }
}

/**************************************************************************
  ...
**************************************************************************/
void redraw_widget_info_label(SDL_Rect *rect)
{
  SDL_Surface *pText, *pBcgd;
  SDL_Rect srcrect, dstrect;
  SDL_Color color;

  struct widget *pWidget = pSellected_Widget;

  if (!pWidget) {
    return;
  }

  if (!pInfo_Label) {
  
    pInfo_Area = fc_calloc(1, sizeof(SDL_Rect));
  
    /*pWidget->string16->render = 3;*/
    
    color = pWidget->string16->fgcol;
    pWidget->string16->style |= TTF_STYLE_BOLD;
    pWidget->string16->fgcol = *get_game_colorRGB(COLOR_THEME_QUICK_INFO_TEXT);
    
    /* create string and bcgd theme */
    pText = create_text_surf_from_str16(pWidget->string16);

    pWidget->string16->fgcol = color;
    
    pBcgd = create_filled_surface(pText->w + adj_size(10), pText->h + adj_size(6),
              SDL_SWSURFACE, get_game_colorRGB(COLOR_THEME_QUICK_INFO_BG), TRUE);
    
    /* calculate start position */
    if (pWidget->size.y - pBcgd->h - adj_size(6) < 0) {
      pInfo_Area->y = pWidget->size.y + pWidget->size.h + adj_size(3);
    } else {
      pInfo_Area->y = pWidget->size.y - pBcgd->h - adj_size(5);
    }
  
    if (pWidget->size.x + pBcgd->w + adj_size(5) > Main.screen->w) {
      pInfo_Area->x = pWidget->size.x - pBcgd->w - adj_size(5);
    } else {
      pInfo_Area->x = pWidget->size.x + adj_size(3);
    }
  
    pInfo_Area->w = pBcgd->w + adj_size(2);
    pInfo_Area->h = pBcgd->h + adj_size(3);

    pInfo_Label = SDL_DisplayFormatAlpha(pBcgd);

    FREESURFACE(pBcgd);
    
    /* draw text */
    dstrect.x = 6;
    dstrect.y = 3;
    
    alphablit(pText, NULL, pInfo_Label, &dstrect);
    
    FREESURFACE(pText);    
    
    /* draw frame */
    putframe(pInfo_Label, 0, 0,
         pInfo_Label->w - 1, pInfo_Label->h - 1,
         map_rgba(pInfo_Label->format, *get_game_colorRGB(COLOR_THEME_QUICK_INFO_FRAME)));
    
  }

  if (rect) {
    dstrect.x = MAX(rect->x, pInfo_Area->x);
    dstrect.y = MAX(rect->y, pInfo_Area->y);
    
    srcrect.x = dstrect.x - pInfo_Area->x;
    srcrect.y = dstrect.y - pInfo_Area->y;
    srcrect.w = MIN((pInfo_Area->x + pInfo_Area->w), (rect->x + rect->w)) - dstrect.x;
    srcrect.h = MIN((pInfo_Area->y + pInfo_Area->h), (rect->y + rect->h)) - dstrect.y;

    alphablit(pInfo_Label, &srcrect, Main.screen, &dstrect);
  } else {
    alphablit(pInfo_Label, NULL, Main.screen, pInfo_Area);
  }

  if (correct_rect_region(pInfo_Area)) {
    SDL_UpdateRect(Main.screen, pInfo_Area->x, pInfo_Area->y,
				    pInfo_Area->w, pInfo_Area->h);
  }
  
  return;
}


/**************************************************************************
  Find ID in Widget's List ('pGUI_List') and return pointer to this
  Widgets.
**************************************************************************/
struct widget *get_widget_pointer_form_ID(const struct widget *pGUI_List,
				       Uint16 ID, enum scan_direction direction)
{
  while (pGUI_List) {
    if (pGUI_List->ID == ID) {
      return (struct widget *) pGUI_List;
    }
    if (direction == SCAN_FORWARD)
    pGUI_List = pGUI_List->next;
    else
      pGUI_List = pGUI_List->prev;  
  }
  return NULL;
}

/**************************************************************************
  Find ID in MAIN Widget's List ( pBeginWidgetList ) and return pointer to
  this Widgets.
**************************************************************************/
struct widget *get_widget_pointer_form_main_list(Uint16 ID)
{
  return get_widget_pointer_form_ID(pBeginMainWidgetList, ID, SCAN_FORWARD);
}

/**************************************************************************
  INIT Main Widget's List ( pBeginWidgetList )
**************************************************************************/
void init_gui_list(Uint16 ID, struct widget *pGUI)
{
  /* pEndWidgetList = pBeginWidgetList = pGUI; */
  pBeginMainWidgetList = pGUI;
  pBeginMainWidgetList->ID = ID;
}

/**************************************************************************
  Add Widget to Main Widget's List ( pBeginWidgetList )
**************************************************************************/
void add_to_gui_list(Uint16 ID, struct widget *pGUI)
{
  pGUI->next = pBeginMainWidgetList;
  pGUI->ID = ID;
  pBeginMainWidgetList->prev = pGUI;
  pBeginMainWidgetList = pGUI;
}

/**************************************************************************
  Add Widget to Widget's List at pAdd_Dock position on 'prev' slot.
**************************************************************************/
void DownAdd(struct widget *pNew_Widget, struct widget *pAdd_Dock)
{
  pNew_Widget->next = pAdd_Dock;
  pNew_Widget->prev = pAdd_Dock->prev;
  if (pAdd_Dock->prev) {
    pAdd_Dock->prev->next = pNew_Widget;
  }
  pAdd_Dock->prev = pNew_Widget;
  if (pAdd_Dock == pBeginMainWidgetList) {
    pBeginMainWidgetList = pNew_Widget;
  }
}

/**************************************************************************
  Delete Widget from Main Widget's List ( pBeginWidgetList )

  NOTE: This function does not destroy Widget, only remove his pointer from
  list. To destroy this Widget totaly ( free mem... ) call macro:
  del_widget_from_gui_list( pWidget ).  This macro call this function.
**************************************************************************/
void del_widget_pointer_from_gui_list(struct widget *pGUI)
{
  if (!pGUI) {
    return;
  }

  if (pGUI == pBeginMainWidgetList && pBeginMainWidgetList->next) {
    pBeginMainWidgetList = pBeginMainWidgetList->next;
  }

  if (pGUI->prev && pGUI->next) {
    pGUI->prev->next = pGUI->next;
    pGUI->next->prev = pGUI->prev;
  } else {
    if (pGUI->prev) {
      pGUI->prev->next = NULL;
    }

    if (pGUI->next) {
      pGUI->next->prev = NULL;
    }

  }

  if (pSellected_Widget == pGUI) {
    pSellected_Widget = NULL;
  }
}

/**************************************************************************
  Determinate if 'pGui' is first on WidgetList

  NOTE: This is used by My (move) GUI Window mechanism.  Return TRUE if is
  first.
**************************************************************************/
bool is_this_widget_first_on_list(struct widget *pGUI)
{
  return (pBeginMainWidgetList == pGUI);
}

/**************************************************************************
  Move pointer to Widget to list begin.

  NOTE: This is used by My GUI Window mechanism.
**************************************************************************/
void move_widget_to_front_of_gui_list(struct widget *pGUI)
{
  if (!pGUI || pGUI == pBeginMainWidgetList) {
    return;
  }

  /* pGUI->prev always exists because
     we don't do this to pBeginMainWidgetList */
  if (pGUI->next) {
    pGUI->prev->next = pGUI->next;
    pGUI->next->prev = pGUI->prev;
  } else {
    pGUI->prev->next = NULL;
  }

  pGUI->next = pBeginMainWidgetList;
  pBeginMainWidgetList->prev = pGUI;
  pBeginMainWidgetList = pGUI;
  pGUI->prev = NULL;
}

/**************************************************************************
  Delete Main Widget's List.
**************************************************************************/
void del_main_list(void)
{
  del_gui_list(pBeginMainWidgetList);
}

/**************************************************************************
   Delete Wideget's List ('pGUI_List').
**************************************************************************/
void del_gui_list(struct widget *pGUI_List)
{
  while (pGUI_List) {
    if (pGUI_List->next) {
      pGUI_List = pGUI_List->next;
      FREEWIDGET(pGUI_List->prev);
    } else {
      FREEWIDGET(pGUI_List);
    }
  }
}

/* ===================================================================== */
/* ================================ Universal ========================== */
/* ===================================================================== */

/**************************************************************************
  Universal redraw Group of Widget function.  Function is optimized to
  WindowGroup: start draw from 'pEnd' and stop on 'pBegin', in theory
  'pEnd' is window widget;
**************************************************************************/
Uint16 redraw_group(const struct widget *pBeginGroupWidgetList,
		    const struct widget *pEndGroupWidgetList,
		      int add_to_update)
{
  Uint16 count = 0;
  struct widget *pTmpWidget = (struct widget *) pEndGroupWidgetList;

  while (pTmpWidget) {

    if ((get_wflags(pTmpWidget) & WF_HIDDEN) != WF_HIDDEN) {

      if (!(pTmpWidget->gfx) &&
	  (get_wflags(pTmpWidget) & WF_RESTORE_BACKGROUND)) {
	refresh_widget_background(pTmpWidget);
      }

      redraw_widget(pTmpWidget);

      if (get_wflags(pTmpWidget) & WF_DRAW_FRAME_AROUND_WIDGET) {
	if(get_wtype(pTmpWidget) & WT_WINDOW) {
	  draw_frame_inside_widget_on_surface(pTmpWidget, pTmpWidget->dst);
	} else {
	  draw_frame_around_widget_on_surface(pTmpWidget, pTmpWidget->dst);
	}
      }

      if (add_to_update) {
	sdl_dirty_rect(pTmpWidget->size);
      }

      count++;
    }

    if (pTmpWidget == pBeginGroupWidgetList) {
      break;
    }

    pTmpWidget = pTmpWidget->prev;

  }

  return count;
}

/**************************************************************************
  ...
**************************************************************************/
void set_new_group_start_pos(const struct widget *pBeginGroupWidgetList,
			     const struct widget *pEndGroupWidgetList,
			     Sint16 Xrel, Sint16 Yrel)
{
  struct widget *pTmpWidget = (struct widget *) pEndGroupWidgetList;

  while (pTmpWidget) {

    pTmpWidget->size.x += Xrel;
    pTmpWidget->size.y += Yrel;

    if (get_wtype(pTmpWidget) == WT_VSCROLLBAR
      && pTmpWidget->private_data.adv_dlg
      && pTmpWidget->private_data.adv_dlg->pScroll) {
      pTmpWidget->private_data.adv_dlg->pScroll->max += Yrel;
      pTmpWidget->private_data.adv_dlg->pScroll->min += Yrel;
    }
    
    if (get_wtype(pTmpWidget) == WT_HSCROLLBAR
      && pTmpWidget->private_data.adv_dlg
      && pTmpWidget->private_data.adv_dlg->pScroll) {
      pTmpWidget->private_data.adv_dlg->pScroll->max += Xrel;
      pTmpWidget->private_data.adv_dlg->pScroll->min += Xrel;
    }
    
    if (pTmpWidget == pBeginGroupWidgetList) {
      break;
    }

    pTmpWidget = pTmpWidget->prev;
  }
}

/**************************************************************************
  Move group of Widget's pointers to begin of main list.
  Move group destination buffer to end of buffer array.
  NOTE: This is used by My GUI Window(group) mechanism.
**************************************************************************/
void move_group_to_front_of_gui_list(struct widget *pBeginGroupWidgetList,
				     struct widget *pEndGroupWidgetList)
{
  struct widget *pTmpWidget = pEndGroupWidgetList , *pPrev = NULL;
  struct gui_layer *gui_layer = get_gui_layer(pEndGroupWidgetList->dst);
  
  /* Widget Pointer Menagment */
  while (pTmpWidget) {
    
    pPrev = pTmpWidget->prev;
    
    /* pTmpWidget->prev always exists because we
       don't do this to pBeginMainWidgetList */
    if (pTmpWidget->next) {
      pTmpWidget->prev->next = pTmpWidget->next;
      pTmpWidget->next->prev = pTmpWidget->prev;
    } else {
      pTmpWidget->prev->next = NULL;
    }

    pTmpWidget->next = pBeginMainWidgetList;
    pBeginMainWidgetList->prev = pTmpWidget;
    pBeginMainWidgetList = pTmpWidget;
    pBeginMainWidgetList->prev = NULL;
    
    if (pTmpWidget == pBeginGroupWidgetList) {
      break;
    }

    pTmpWidget = pPrev;
  }

  /* Window Buffer Menagment */  
  if (gui_layer) {
    int i = 0;
    while ((i < Main.guis_count - 1) && Main.guis[i]) {
      if (Main.guis[i] && Main.guis[i + 1] && (Main.guis[i] == gui_layer)) {
        Main.guis[i] = Main.guis[i + 1];
	Main.guis[i + 1] = gui_layer;
      }
      i++;
    }
  }
}

/**************************************************************************
  ...
**************************************************************************/
void del_group_of_widgets_from_gui_list(struct widget *pBeginGroupWidgetList,
					struct widget *pEndGroupWidgetList)
{
  struct widget *pBufWidget = NULL;
  struct widget *pTmpWidget = pEndGroupWidgetList;

  if (!pEndGroupWidgetList)
	return;
  
  if (pBeginGroupWidgetList == pEndGroupWidgetList) {
    del_widget_from_gui_list(pTmpWidget);
    return;
  }

  pTmpWidget = pTmpWidget->prev;

  while (pTmpWidget) {

    pBufWidget = pTmpWidget->next;
    del_widget_from_gui_list(pBufWidget);

    if (pTmpWidget == pBeginGroupWidgetList) {
      del_widget_from_gui_list(pTmpWidget);
      break;
    }

    pTmpWidget = pTmpWidget->prev;
  }

}

/**************************************************************************
  ...
**************************************************************************/
void set_group_state(struct widget *pBeginGroupWidgetList,
		     struct widget *pEndGroupWidgetList, enum WState state)
{
  struct widget *pTmpWidget = pEndGroupWidgetList;
  while (pTmpWidget) {
    set_wstate(pTmpWidget, state);
    if (pTmpWidget == pBeginGroupWidgetList) {
      break;
    }
    pTmpWidget = pTmpWidget->prev;
  }
}

/**************************************************************************
  ...
**************************************************************************/
void hide_group(struct widget *pBeginGroupWidgetList,
		struct widget *pEndGroupWidgetList)
{
  struct widget *pTmpWidget = pEndGroupWidgetList;

  while (pTmpWidget) {
    set_wflag(pTmpWidget, WF_HIDDEN);

    if (pTmpWidget == pBeginGroupWidgetList) {
      break;
    }

    pTmpWidget = pTmpWidget->prev;
  }
}

/**************************************************************************
  ...
**************************************************************************/
void show_group(struct widget *pBeginGroupWidgetList,
		struct widget *pEndGroupWidgetList)
{
  struct widget *pTmpWidget = pEndGroupWidgetList;

  while (pTmpWidget) {
    clear_wflag(pTmpWidget, WF_HIDDEN);

    if (pTmpWidget == pBeginGroupWidgetList) {
      break;
    }

    pTmpWidget = pTmpWidget->prev;
  }
}

/**************************************************************************
  Universal redraw Widget function.
**************************************************************************/
int redraw_widget(struct widget *pWidget)
{
  if (!pWidget && (get_wflags(pWidget) & WF_HIDDEN)) {
    return -1;
  }

  switch (get_wtype(pWidget)) {
  case WT_TI_BUTTON:
    return real_redraw_tibutton(pWidget);
  case WT_I_BUTTON:
    return real_redraw_ibutton(pWidget);
  case WT_ICON:
  case WT_CHECKBOX:
    return real_redraw_icon(pWidget);
  case WT_ICON2:
    return real_redraw_icon2(pWidget);
  case WT_TCHECKBOX:
    return redraw_textcheckbox(pWidget);
  case WT_I_LABEL:
  case WT_T_LABEL:
  case WT_T2_LABEL:  
  case WT_TI_LABEL:
    return redraw_label(pWidget);
  case WT_WINDOW:
    return redraw_window(pWidget);
  case WT_EDIT:
    return redraw_edit(pWidget);
  case WT_VSCROLLBAR:
    return redraw_vert(pWidget);
  case WT_HSCROLLBAR:
    return redraw_horiz(pWidget);
  default:
    return -2;
  }
  return 0;
}

/* ===================================================================== *
 * =========================== Window Group ============================ *
 * ===================================================================== */

/*
 *	Window Group  -	group with 'pBegin' and 'pEnd' where
 *	windowed type widget is last on list ( 'pEnd' ).
 */

/**************************************************************************
  Undraw and destroy Window Group  The Trick is simple. We undraw only
  last member of group: the window.
**************************************************************************/
void popdown_window_group_dialog(struct widget *pBeginGroupWidgetList,
				 struct widget *pEndGroupWidgetList)
{
  if ((pBeginGroupWidgetList) && (pEndGroupWidgetList)) {
    if(pEndGroupWidgetList->dst == Main.gui) {
      /* undraw window */
      SDL_Rect dstrect;
      dstrect.x = pEndGroupWidgetList->size.x;
      dstrect.y = pEndGroupWidgetList->size.y;
      dstrect.w = pEndGroupWidgetList->size.w;
      dstrect.h = pEndGroupWidgetList->size.h;
      clear_surface(pEndGroupWidgetList->dst, &dstrect);
      blit_entire_src(pEndGroupWidgetList->gfx, pEndGroupWidgetList->dst,
		    pEndGroupWidgetList->size.x,
		    pEndGroupWidgetList->size.y);
    } else {
      remove_buffer_layer(pEndGroupWidgetList->dst);
    }

    sdl_dirty_rect(pEndGroupWidgetList->size);

    del_group(pBeginGroupWidgetList, pEndGroupWidgetList);
  }
}

/**************************************************************************
  Sellect Window Group. (move widget group up the widgets list)
  Function return TRUE when group was sellected.
**************************************************************************/
bool sellect_window_group_dialog(struct widget *pBeginWidgetList,
							 struct widget *pWindow)
{
  if (!is_this_widget_first_on_list(pBeginWidgetList)) {
    move_group_to_front_of_gui_list(pBeginWidgetList, pWindow);
    return TRUE;
  }
  return FALSE;
}

/**************************************************************************
  Move Window Group.  The Trick is simple.  We move only last member of
  group: the window , and then set new position to all members of group.

  Function return 1 when group was moved.
**************************************************************************/
bool move_window_group_dialog(struct widget *pBeginGroupWidgetList,
			     struct widget *pEndGroupWidgetList)
{
  bool ret = FALSE;
  Sint16 oldX = pEndGroupWidgetList->size.x,
      oldY = pEndGroupWidgetList->size.y;

  if (move_window(pEndGroupWidgetList)) {
    set_new_group_start_pos(pBeginGroupWidgetList,
			    pEndGroupWidgetList->prev,
			    pEndGroupWidgetList->size.x - oldX,
			    pEndGroupWidgetList->size.y - oldY);
    ret = TRUE;
  }

  return ret;
}

/**************************************************************************
  Standart Window Group Widget Callback (window)
  When Pressed check mouse move;
  if move then move window and redraw else
  if not on fron then move window up to list and redraw;  
**************************************************************************/
void move_window_group(struct widget *pBeginWidgetList, struct widget *pWindow)
{
  if (sellect_window_group_dialog(pBeginWidgetList, pWindow)) {
    flush_rect(pWindow->size, FALSE);
  }
  
  move_window_group_dialog(pBeginWidgetList, pWindow);
}


/* =================================================== */
/* ============ Vertical Scroll Group List =========== */
/* =================================================== */

/**************************************************************************
  scroll pointers on list.
  dir == directions: up == -1, down == 1.
**************************************************************************/
static struct widget *vertical_scroll_widget_list(struct widget *pActiveWidgetLIST,
				      struct widget *pBeginWidgetLIST,
				      struct widget *pEndWidgetLIST,
				      int active, int step, int dir)
{
  struct widget *pBegin = pActiveWidgetLIST;
  struct widget *pBuf = pActiveWidgetLIST;
  struct widget *pTmp = NULL;  
  int count = active; /* row */
  int count_step = step; /* col */
    
  if (dir < 0) {
    bool real = TRUE;
    /* up */
    if (pBuf != pEndWidgetLIST) {
      /*
       move pointers to positions and unhidde scrolled widgets
       B = pBuf - new top
       T = pTmp - current top == pActiveWidgetLIST
       [B] [ ] [ ]
       -----------
       [T] [ ] [ ]
       [ ] [ ] [ ]
       -----------
       [ ] [ ] [ ]
    */
      pTmp = pBuf; /* now pBuf == pActiveWidgetLIST == current Top */
      while(count_step) {
      	pBuf = pBuf->next;
	clear_wflag(pBuf, WF_HIDDEN);
	count_step--;
      }
      count_step = step;
      
      /* setup new ActiveWidget pointer */
      pBegin = pBuf;
      
      /*
       scroll pointers up
       B = pBuf
       T = pTmp
       [B0] [B1] [B2]
       -----------
       [T0] [T1] [T2]   => B position = T position
       [T3] [T4] [T5]
       -----------
       [  ] [  ] [  ]
      
       start from B0 and go downd list
       B0 = T0, B1 = T1, B2 = T2
       T0 = T3, T1 = T4, T2 = T5
       etc...
    */
      while (count) {
	if(real) {
	  pBuf->gfx = pTmp->gfx;
	  pBuf->size.x = pTmp->size.x;
	  pBuf->size.y = pTmp->size.y;
	  pTmp->gfx = NULL;
	  if(count == 1) {
	    set_wflag(pTmp, WF_HIDDEN);
	  }
	  if(pTmp == pBeginWidgetLIST) {
	    real = FALSE;
	  }
	  pTmp = pTmp->prev;
	} else {
	  /*
	     unsymetric list support.
	     This is big problem becouse we can't take position from no exist
	     list memeber. We must put here some hypotetical positions
	  
	     [B0] [B1] [B2]
             --------------
             [T0] [T1]
	  
	  */
	  if (active > 1) {
	    /* this work good if active > 1 but is buggy when active == 1 */
	    pBuf->size.y += pBuf->size.h;
	  } else {
	    /* this work good if active == 1 but may be broken if "next"
	       element have another "y" position */
	    pBuf->size.y = pBuf->next->size.y;
	  }
	  pBuf->gfx = NULL;
	}
	
	pBuf = pBuf->prev;
	count_step--;
	if(!count_step) {
	  count_step = step;
	  count--;
	}
      }
      
    }
  } else {
    SDL_Rect dst;
    /* down */
    count = active * step; /* row * col */
    
    /*
       find end
       B = pBuf
       A - start (pBuf == pActiveWidgetLIST)
       [ ] [ ] [ ]
       -----------
       [A] [ ] [ ]
       [ ] [ ] [ ]
       -----------
       [B] [ ] [ ]
    */
    while (count && pBuf != pBeginWidgetLIST->prev) {
      pBuf = pBuf->prev;
      count--;
    }

    if (!count && pBuf != pBeginWidgetLIST->prev) {
      /*
       move pointers to positions and unhidde scrolled widgets
       B = pBuf
       T = pTmp
       A - start (pActiveWidgetLIST)
       [ ] [ ] [ ]
       -----------
       [A] [ ] [ ]
       [ ] [ ] [T]
       -----------
       [ ] [ ] [B]
    */
      pTmp = pBuf->next;
      count_step = step - 1;
      while(count_step && pBuf != pBeginWidgetLIST) {
	clear_wflag(pBuf, WF_HIDDEN);
	pBuf = pBuf->prev;
	count_step--;
      }
      clear_wflag(pBuf, WF_HIDDEN);

      /*
       Unsymetric list support.
       correct pTmp and undraw empty fields
       B = pBuf
       T = pTmp
       A - start (pActiveWidgetLIST)
       [ ] [ ] [ ]
       -----------
       [A] [ ] [ ]
       [ ] [T] [U]  <- undraw U
       -----------
       [ ] [B]
    */
      count = count_step;
      while(count) {
	/* hack - clear area under no exist list members */
	dst = pTmp->size;
        fix_rect(pTmp->dst, &dst);
        clear_surface(pTmp->dst, &dst);
        alphablit(pTmp->gfx, NULL, pTmp->dst, &dst);
	sdl_dirty_rect(pTmp->size);
	FREESURFACE(pTmp->gfx);
	if (active == 1) {
	  set_wflag(pTmp, WF_HIDDEN);
	}
	pTmp = pTmp->next;
	count--;
      }
      
      /* reset counters */
      count = active;
      if(count_step) {
        count_step = step - count_step;
      } else {
	count_step = step;
      }
      
      /*
       scroll pointers down
       B = pBuf
       T = pTmp
       [  ] [  ] [  ]
       -----------
       [  ] [  ] [  ]
       [T2] [T1] [T0]   => B position = T position
       -----------
       [B2] [B1] [B0]
    */
      while (count) {
	pBuf->gfx = pTmp->gfx;
	pBuf->size.y = pTmp->size.y;
	pBuf->size.x = pTmp->size.x;
        pTmp->gfx = NULL;
	if(count == 1) {
	  set_wflag(pTmp, WF_HIDDEN);
	}
	pTmp = pTmp->next;
	pBuf = pBuf->next;
	count_step--;
	if(!count_step) {
	  count_step = step;
	  count--;
	}
      }
      /* setup new ActiveWidget pointer */
      pBegin = pBuf->prev;
    }
  }

  return pBegin;
}

/**************************************************************************
  ...
**************************************************************************/
static int get_step(struct ScrollBar *pScroll)
{
  float step = pScroll->max - pScroll->min;
  step *= (float) (1.0 - (float) (pScroll->active * pScroll->step) /
						  (float)pScroll->count);
  step /= (float)(pScroll->count - pScroll->active * pScroll->step);
  step *= (float)pScroll->step;
  step++;
  return (int)step;
}

/**************************************************************************
  ...
**************************************************************************/
static int get_position(struct ADVANCED_DLG *pDlg)
{
  struct widget *pBuf = pDlg->pActiveWidgetList;
  int count = pDlg->pScroll->active * pDlg->pScroll->step - 1;
  int step = get_step(pDlg->pScroll);
  
  /* find last seen widget */
  while(count) {
    if(pBuf == pDlg->pBeginActiveWidgetList) {
      break;
    }
    count--;
    pBuf = pBuf->prev;
  }
  
  count = 0;
  if(pBuf != pDlg->pBeginActiveWidgetList) {
    do {
      count++;
      pBuf = pBuf->prev;
    } while (pBuf != pDlg->pBeginActiveWidgetList);
  }
 
  if (pDlg->pScroll->pScrollBar) {
  return pDlg->pScroll->max - pDlg->pScroll->pScrollBar->size.h -
					count * (float)step / pDlg->pScroll->step;
  } else {
    return pDlg->pScroll->max - count * (float)step / pDlg->pScroll->step;
  }
}

/**************************************************************************
  ...
**************************************************************************/
static void inside_scroll_down_loop(void *pData)
{
  struct UP_DOWN *pDown = (struct UP_DOWN *)pData;
  SDL_Rect dest;  
  
  if (pDown->pEnd != pDown->pBeginWidgetLIST) {
      if (pDown->pVscroll->pScrollBar
	&& pDown->pVscroll->pScrollBar->size.y <=
	  pDown->pVscroll->max - pDown->pVscroll->pScrollBar->size.h) {
            
	/* draw bcgd */
        dest = pDown->pVscroll->pScrollBar->size;
        fix_rect(pDown->pVscroll->pScrollBar->dst, &dest);
        clear_surface(pDown->pVscroll->pScrollBar->dst, &dest);
	blit_entire_src(pDown->pVscroll->pScrollBar->gfx,
	    		pDown->pVscroll->pScrollBar->dst,
			dest.x, dest.y);
	sdl_dirty_rect(pDown->pVscroll->pScrollBar->size);

	if (pDown->pVscroll->pScrollBar->size.y + pDown->step >
	    pDown->pVscroll->max - pDown->pVscroll->pScrollBar->size.h) {
	  pDown->pVscroll->pScrollBar->size.y =
	      pDown->pVscroll->max - pDown->pVscroll->pScrollBar->size.h;
	} else {
	  pDown->pVscroll->pScrollBar->size.y += pDown->step;
	}
      }

      pDown->pBegin = vertical_scroll_widget_list(pDown->pBegin,
			  pDown->pBeginWidgetLIST, pDown->pEndWidgetLIST,
			  pDown->pVscroll->active, pDown->pVscroll->step, 1);

      pDown->pEnd = pDown->pEnd->prev;

      redraw_group(pDown->pBeginWidgetLIST, pDown->pEndWidgetLIST, TRUE);

      if (pDown->pVscroll->pScrollBar) {
	/* redraw scrollbar */
	refresh_widget_background(pDown->pVscroll->pScrollBar);
	redraw_vert(pDown->pVscroll->pScrollBar);

	sdl_dirty_rect(pDown->pVscroll->pScrollBar->size);
      }

      flush_dirty();
    }
}

/**************************************************************************
  ...
**************************************************************************/
static void inside_scroll_up_loop(void *pData)
{
  struct UP_DOWN *pUp = (struct UP_DOWN *)pData;
  SDL_Rect dest;  
  
  if (pUp && pUp->pBegin != pUp->pEndWidgetLIST) {

    if (pUp->pVscroll->pScrollBar
      && (pUp->pVscroll->pScrollBar->size.y >= pUp->pVscroll->min)) {

      /* draw bcgd */
      dest = pUp->pVscroll->pScrollBar->size;
      fix_rect(pUp->pVscroll->pScrollBar->dst, &dest);
      clear_surface(pUp->pVscroll->pScrollBar->dst, &dest);
      blit_entire_src(pUp->pVscroll->pScrollBar->gfx,
			pUp->pVscroll->pScrollBar->dst,
			dest.x, dest.y);
      sdl_dirty_rect(pUp->pVscroll->pScrollBar->size);

      if (((pUp->pVscroll->pScrollBar->size.y - pUp->step) < pUp->pVscroll->min)) {
	pUp->pVscroll->pScrollBar->size.y = pUp->pVscroll->min;
      } else {
	pUp->pVscroll->pScrollBar->size.y -= pUp->step;
      }
    }

    pUp->pBegin = vertical_scroll_widget_list(pUp->pBegin,
			pUp->pBeginWidgetLIST, pUp->pEndWidgetLIST,
			pUp->pVscroll->active, pUp->pVscroll->step, -1);

    redraw_group(pUp->pBeginWidgetLIST, pUp->pEndWidgetLIST, TRUE);

    if (pUp->pVscroll->pScrollBar) {
      /* redraw scroolbar */
      refresh_widget_background(pUp->pVscroll->pScrollBar);
      redraw_vert(pUp->pVscroll->pScrollBar);
      sdl_dirty_rect(pUp->pVscroll->pScrollBar->size);
    }

    flush_dirty();
  }
}

/**************************************************************************
  FIXME
**************************************************************************/
static Uint16 scroll_mouse_motion_handler(SDL_MouseMotionEvent *pMotionEvent, void *pData)
{
  struct UP_DOWN *pMotion = (struct UP_DOWN *)pData;
  SDL_Rect dest;  
  
  if (pMotion && pMotionEvent->yrel &&
    /*(old_y >= pMotion->pVscroll->min) && (old_y <= pMotion->pVscroll->max) &&*/
    (pMotionEvent->y >= pMotion->pVscroll->min) &&
    (pMotionEvent->y <= pMotion->pVscroll->max)) {
    int count;
    div_t tmp;
      
    /* draw bcgd */
    dest = pMotion->pVscroll->pScrollBar->size;
    fix_rect(pMotion->pVscroll->pScrollBar->dst, &dest);
    clear_surface(pMotion->pVscroll->pScrollBar->dst, &dest);
    blit_entire_src(pMotion->pVscroll->pScrollBar->gfx,
       			pMotion->pVscroll->pScrollBar->dst,
			dest.x, dest.y);
    sdl_dirty_rect(pMotion->pVscroll->pScrollBar->size);

    if ((pMotion->pVscroll->pScrollBar->size.y + pMotionEvent->yrel) >
	 (pMotion->pVscroll->max - pMotion->pVscroll->pScrollBar->size.h)) {
      pMotion->pVscroll->pScrollBar->size.y =
	      pMotion->pVscroll->max - pMotion->pVscroll->pScrollBar->size.h;
    } else {
      if ((pMotion->pVscroll->pScrollBar->size.y + pMotionEvent->yrel) <
	  pMotion->pVscroll->min) {
	pMotion->pVscroll->pScrollBar->size.y = pMotion->pVscroll->min;
      } else {
	pMotion->pVscroll->pScrollBar->size.y += pMotionEvent->yrel;
      }
    }
    
    count = pMotion->pVscroll->pScrollBar->size.y - pMotion->old_y;
    tmp = div(count, pMotion->step);
    count = tmp.quot;

    if (count) {

      /* correct div */
      if (tmp.rem) {
	if (count > 0) {
	  count++;
	} else {
	  count--;
	}
      }
      
      while (count) {

	pMotion->pBegin = vertical_scroll_widget_list(pMotion->pBegin,
			pMotion->pBeginWidgetLIST, pMotion->pEndWidgetLIST,
				pMotion->pVscroll->active,
				pMotion->pVscroll->step, count);

	if (count > 0) {
	  count--;
	} else {
	  count++;
	}

      }	/* while (count) */
      
      pMotion->old_y = pMotion->pVscroll->pScrollBar->size.y;
      redraw_group(pMotion->pBeginWidgetLIST, pMotion->pEndWidgetLIST, TRUE);
    }

    /* redraw scroolbar */
    refresh_widget_background(pMotion->pVscroll->pScrollBar);
    redraw_vert(pMotion->pVscroll->pScrollBar);
    sdl_dirty_rect(pMotion->pVscroll->pScrollBar->size);

    flush_dirty();
  }				/* if (count) */

  return ID_ERROR;
}

/**************************************************************************
  ...
**************************************************************************/
static Uint16 scroll_mouse_button_up(SDL_MouseButtonEvent *pButtonEvent, void *pData)
{
  return (Uint16)ID_SCROLLBAR;
}

/**************************************************************************
  ...
**************************************************************************/
static struct widget *down_scroll_widget_list(struct ScrollBar *pVscroll,
				    struct widget *pBeginActiveWidgetLIST,
				    struct widget *pBeginWidgetLIST,
				    struct widget *pEndWidgetLIST)
{
  struct UP_DOWN pDown;
  struct widget *pBegin = pBeginActiveWidgetLIST;
  int step = pVscroll->active * pVscroll->step - 1;

  while (step--) {
    pBegin = pBegin->prev;
  }

  pDown.step = get_step(pVscroll);
  pDown.pBegin = pBeginActiveWidgetLIST;
  pDown.pEnd = pBegin;
  pDown.pBeginWidgetLIST = pBeginWidgetLIST;
  pDown.pEndWidgetLIST = pEndWidgetLIST;
  pDown.pVscroll = pVscroll;
  
  gui_event_loop((void *)&pDown, inside_scroll_down_loop,
	NULL, NULL, NULL, scroll_mouse_button_up, NULL);
  
  return pDown.pBegin;
}

/**************************************************************************
  ...
**************************************************************************/
static struct widget *up_scroll_widget_list(struct ScrollBar *pVscroll,
				  struct widget *pBeginActiveWidgetLIST,
				  struct widget *pBeginWidgetLIST,
				  struct widget *pEndWidgetLIST)
{
  struct UP_DOWN pUp;
      
  pUp.step = get_step(pVscroll);
  pUp.pBegin = pBeginActiveWidgetLIST;
  pUp.pBeginWidgetLIST = pBeginWidgetLIST;
  pUp.pEndWidgetLIST = pEndWidgetLIST;
  pUp.pVscroll = pVscroll;
  
  gui_event_loop((void *)&pUp, inside_scroll_up_loop,
	NULL, NULL, NULL, scroll_mouse_button_up, NULL);
  
  return pUp.pBegin;
}

/**************************************************************************
  FIXME
**************************************************************************/
static struct widget *vertic_scroll_widget_list(struct ScrollBar *pVscroll,
				      struct widget *pBeginActiveWidgetLIST,
				      struct widget *pBeginWidgetLIST,
				      struct widget *pEndWidgetLIST)
{
  struct UP_DOWN pMotion;
      
  pMotion.step = get_step(pVscroll);
  pMotion.pBegin = pBeginActiveWidgetLIST;
  pMotion.pBeginWidgetLIST = pBeginWidgetLIST;
  pMotion.pEndWidgetLIST = pEndWidgetLIST;
  pMotion.pVscroll = pVscroll;
  pMotion.old_y = pVscroll->pScrollBar->size.y;
  
  MOVE_STEP_X = 0;
  MOVE_STEP_Y = 3;
  /* Filter mouse motion events */
  SDL_SetEventFilter(FilterMouseMotionEvents);
  gui_event_loop((void *)&pMotion, NULL, NULL, NULL, NULL,
		  scroll_mouse_button_up, scroll_mouse_motion_handler);
  /* Turn off Filter mouse motion events */
  SDL_SetEventFilter(NULL);
  MOVE_STEP_X = DEFAULT_MOVE_STEP;
  MOVE_STEP_Y = DEFAULT_MOVE_STEP;
  
  return pMotion.pBegin;
}

/* ==================================================================== */

/**************************************************************************
  Add new widget to srolled list and set draw position of all changed widgets.
  dir :
    TRUE - upper add => pAdd_Dock->next = pNew_Widget.
    FALSE - down add => pAdd_Dock->prev = pNew_Widget.
  start_x, start_y - positions of first seen widget (pActiveWidgetList).
  pDlg->pScroll ( scrollbar ) must exist.
  It isn't full secure to multi widget list.
**************************************************************************/
bool add_widget_to_vertical_scroll_widget_list(struct ADVANCED_DLG *pDlg,
				      struct widget *pNew_Widget,
				      struct widget *pAdd_Dock, bool dir,
					Sint16 start_x, Sint16 start_y)
{
  struct widget *pBuf = NULL;
  struct widget *pEnd = NULL, *pOld_End = NULL;
  int count = 0;
  bool last = FALSE, seen = TRUE;
  SDL_Rect dest;
  
  assert(pNew_Widget != NULL);
  assert(pDlg != NULL);
  assert(pDlg->pScroll != NULL);
  
  if(!pAdd_Dock) {
    pAdd_Dock = pDlg->pBeginWidgetList;
  }
  
  pDlg->pScroll->count++;
  
  if(pDlg->pScroll->count > pDlg->pScroll->active * pDlg->pScroll->step) {
    if(pDlg->pActiveWidgetList) {
      int i = 0;
      /* find last active widget */
      pOld_End = pAdd_Dock;
      while(pOld_End != pDlg->pActiveWidgetList) {
        pOld_End = pOld_End->next;
	i++;
	if (pOld_End == pDlg->pEndActiveWidgetList) {
	  seen = FALSE;
	  break;
	}
      }
      if (seen) {
        count = pDlg->pScroll->active * pDlg->pScroll->step - 1;
        if (i > count) {
	  seen = FALSE;
        } else {
          while(count) {
	    pOld_End = pOld_End->prev;
	    count--;
          }
          if(pOld_End == pAdd_Dock) {
	    last = TRUE;
          }
	}
      }
    } else {
      last = TRUE;
      pDlg->pActiveWidgetList = pDlg->pEndActiveWidgetList;
      show_scrollbar(pDlg->pScroll);
    }
  }

  count = 0;
  /* add Pointer to list */
  if(dir) {
    /* upper add */
    UpperAdd(pNew_Widget, pAdd_Dock);
    
    if(pAdd_Dock == pDlg->pEndWidgetList) {
      pDlg->pEndWidgetList = pNew_Widget;
    }
    if(pAdd_Dock == pDlg->pEndActiveWidgetList) {
      pDlg->pEndActiveWidgetList = pNew_Widget;
    }
    if(pAdd_Dock == pDlg->pActiveWidgetList) {
      pDlg->pActiveWidgetList = pNew_Widget;
    }
  } else {
    /* down add */
    DownAdd(pNew_Widget, pAdd_Dock);
        
    if(pAdd_Dock == pDlg->pBeginWidgetList) {
      pDlg->pBeginWidgetList = pNew_Widget;
    }
    
    if(pAdd_Dock == pDlg->pBeginActiveWidgetList) {
      pDlg->pBeginActiveWidgetList = pNew_Widget;
    }
  }
  
  /* setup draw positions */
  if (seen) {
    if(!pDlg->pBeginActiveWidgetList) {
      /* first element ( active list empty ) */
      if(dir) {
	die("Forbided List Operation");
      }
      pNew_Widget->size.x = start_x;
      pNew_Widget->size.y = start_y;
      pDlg->pBeginActiveWidgetList = pNew_Widget;
      pDlg->pEndActiveWidgetList = pNew_Widget;
      if(!pDlg->pBeginWidgetList) {
        pDlg->pBeginWidgetList = pNew_Widget;
        pDlg->pEndWidgetList = pNew_Widget;
      }
    } else { /* there are some elements on local active list */
      if(last) {
        /* We add to last seen position */
        if(dir) {
	  /* only swap pAdd_Dock with pNew_Widget on last seen positions */
	  pNew_Widget->size.x = pAdd_Dock->size.x;
	  pNew_Widget->size.y = pAdd_Dock->size.y;
	  pNew_Widget->gfx = pAdd_Dock->gfx;
	  pAdd_Dock->gfx = NULL;
	  set_wflag(pAdd_Dock, WF_HIDDEN);
        } else {
	  /* repositon all widgets */
	  pBuf = pNew_Widget;
          do {
	    pBuf->size.x = pBuf->next->size.x;
	    pBuf->size.y = pBuf->next->size.y;
	    pBuf->gfx = pBuf->next->gfx;
	    pBuf->next->gfx = NULL;
	    pBuf = pBuf->next;
          } while(pBuf != pDlg->pActiveWidgetList);
          pBuf->gfx = NULL;
          set_wflag(pBuf, WF_HIDDEN);
	  pDlg->pActiveWidgetList = pDlg->pActiveWidgetList->prev;
        }
      } else { /* !last */
        pBuf = pNew_Widget;
        /* find last seen widget */
        if(pDlg->pActiveWidgetList) {
	  pEnd = pDlg->pActiveWidgetList;
	  count = pDlg->pScroll->active * pDlg->pScroll->step - 1;
          while(count && pEnd != pDlg->pBeginActiveWidgetList) {
	    pEnd = pEnd->prev;
	    count--;
          }
        }
        while(pBuf) {
          if(pBuf == pDlg->pBeginActiveWidgetList) {
	    struct widget *pTmp = pBuf;
	    count = pDlg->pScroll->step;
	    while(count) {
	      pTmp = pTmp->next;
	      count--;
	    }
	    pBuf->size.x = pTmp->size.x;
	    pBuf->size.y = pTmp->size.y + pTmp->size.h;
	    /* break when last active widget or last seen widget */
	    break;
          } else {
	    pBuf->size.x = pBuf->prev->size.x;
	    pBuf->size.y = pBuf->prev->size.y;
	    pBuf->gfx = pBuf->prev->gfx;
	    pBuf->prev->gfx = NULL;
	    if(pBuf == pEnd) {
	      break;
	    } 
          }
          pBuf = pBuf->prev;
        }
        if(pOld_End && pBuf->prev == pOld_End) {
          set_wflag(pOld_End, WF_HIDDEN);
        }
      }/* !last */
    } /* pDlg->pBeginActiveWidgetList */
  } else {/* !seen */
    set_wflag(pNew_Widget, WF_HIDDEN);
  }
  
  if(pDlg->pActiveWidgetList && pDlg->pScroll->pScrollBar) {
    dest = pDlg->pScroll->pScrollBar->size;
    fix_rect(pDlg->pScroll->pScrollBar->dst, &dest);
    clear_surface(pDlg->pScroll->pScrollBar->dst, &dest);
    blit_entire_src(pDlg->pScroll->pScrollBar->gfx,
    		    pDlg->pScroll->pScrollBar->dst,
		    dest.x, dest.y);
    sdl_dirty_rect(pDlg->pScroll->pScrollBar->size);
    pDlg->pScroll->pScrollBar->size.h = scrollbar_size(pDlg->pScroll);
    if(last) {
      pDlg->pScroll->pScrollBar->size.y = get_position(pDlg);
    }
    refresh_widget_background(pDlg->pScroll->pScrollBar);
    if (!seen) {
      redraw_vert(pDlg->pScroll->pScrollBar);
    }
  }

  return last;  
}

/**************************************************************************
  Del widget from srolled list and set draw position of all changed widgets
  Don't free pDlg and pDlg->pScroll (if exist)
  It is full secure for multi widget list case.
**************************************************************************/
bool del_widget_from_vertical_scroll_widget_list(struct ADVANCED_DLG *pDlg, 
  						struct widget *pWidget)
{
  int count = 0;
  struct widget *pBuf = pWidget;
  assert(pWidget != NULL);
  assert(pDlg != NULL);
  SDL_Rect dst;

  /* if begin == end -> size = 1 */
  if (pDlg->pBeginActiveWidgetList ==
      pDlg->pEndActiveWidgetList) {
    if(pDlg->pScroll) {
      pDlg->pScroll->count = 0;
      if(!pDlg->pScroll->pUp_Left_Button && 
	!pDlg->pScroll->pScrollBar &&
        !pDlg->pScroll->pDown_Right_Button) {
        pDlg->pBeginWidgetList = NULL;
        pDlg->pEndWidgetList = NULL;
      }
      if(pDlg->pBeginActiveWidgetList == pDlg->pBeginWidgetList) {
	pDlg->pBeginWidgetList = pDlg->pBeginWidgetList->next;
      }
      if(pDlg->pEndActiveWidgetList == pDlg->pEndWidgetList) {
	pDlg->pEndWidgetList = pDlg->pEndWidgetList->prev;
      }
      pDlg->pBeginActiveWidgetList = NULL;
      pDlg->pActiveWidgetList = NULL;
      pDlg->pEndActiveWidgetList = NULL;
    } else {
      pDlg->pBeginWidgetList = NULL;
      pDlg->pEndWidgetList = NULL;
    }
    dst = pWidget->size;
    fix_rect(pWidget->dst, &dst);
    clear_surface(pWidget->dst, &dst);
    alphablit(pWidget->gfx, NULL, pWidget->dst, &dst);
    sdl_dirty_rect(pWidget->size);
    del_widget_from_gui_list(pWidget);
    return FALSE;
  }
    
  if (pDlg->pScroll && pDlg->pActiveWidgetList) {
    /* scrollbar exist and active, start mod. from last seen label */
    
    struct widget *pLast;
      
    /* this is always true becouse no-scrolbar case (active*step < count)
       will be suported in other part of code (see "else" part) */
    count = pDlg->pScroll->active * pDlg->pScroll->step;
        
    /* find last */
    pBuf = pDlg->pActiveWidgetList;
    while (count) {
      pBuf = pBuf->prev;
      count--;
    }

    if(!pBuf) {
      pLast = pDlg->pBeginActiveWidgetList;
    } else {
      pLast = pBuf->next;
    }
    
    if(pLast == pDlg->pBeginActiveWidgetList) {
      if(pDlg->pScroll->step == 1) {
        pBuf = pDlg->pActiveWidgetList->next;
        pDlg->pActiveWidgetList = pBuf;
        clear_wflag(pBuf, WF_HIDDEN);
	while (pBuf != pWidget) {
          pBuf->gfx = pBuf->prev->gfx;
          pBuf->prev->gfx = NULL;
          pBuf->size.x = pBuf->prev->size.x;
          pBuf->size.y = pBuf->prev->size.y;
          pBuf = pBuf->prev;
        }
      } else {
	pBuf = pLast;
	/* undraw last widget */
        dst = pBuf->size;
        fix_rect(pBuf->dst, &dst);
        clear_surface(pBuf->dst, &dst);
        blit_entire_src(pBuf->gfx, pBuf->dst, dst.x, dst.y);
        sdl_dirty_rect(pBuf->size);
        FREESURFACE(pBuf->gfx);
	goto STD;
      }  
    } else {
      clear_wflag(pBuf, WF_HIDDEN);
STD:  while (pBuf != pWidget) {
        pBuf->gfx = pBuf->next->gfx;
        pBuf->next->gfx = NULL;
        pBuf->size.x = pBuf->next->size.x;
        pBuf->size.y = pBuf->next->size.y;
        pBuf = pBuf->next;
      }
    }
    
    if((pDlg->pScroll->count - 1 <=
	      (pDlg->pScroll->active * pDlg->pScroll->step))) {
      hide_scrollbar(pDlg->pScroll);
      pDlg->pActiveWidgetList = NULL;
    }
    pDlg->pScroll->count--;
    
    if(pDlg->pActiveWidgetList) {
      if (pDlg->pScroll->pScrollBar) {
      pDlg->pScroll->pScrollBar->size.h = scrollbar_size(pDlg->pScroll);
    }
    }
    
  } else { /* no scrollbar */
    pBuf = pDlg->pBeginActiveWidgetList;
    
    /* undraw last widget */
    dst = pBuf->size;
    fix_rect(pBuf->dst, &dst);
    clear_surface(pBuf->dst, &dst);
    blit_entire_src(pBuf->gfx, pBuf->dst, dst.x, dst.y);
    sdl_dirty_rect(pBuf->size);
    FREESURFACE(pBuf->gfx);

    while (pBuf != pWidget) {
      pBuf->gfx = pBuf->next->gfx;
      pBuf->next->gfx = NULL;
      pBuf->size.x = pBuf->next->size.x;
      pBuf->size.y = pBuf->next->size.y;
      pBuf = pBuf->next;
    }

    if (pDlg->pScroll) {
      pDlg->pScroll->count--;
    }
        
  }

  if (pWidget == pDlg->pBeginActiveWidgetList) {
    pDlg->pBeginActiveWidgetList = pWidget->next;
  }

  if (pWidget == pDlg->pBeginWidgetList) {
    pDlg->pBeginWidgetList = pWidget->next;
  }
  
  if (pWidget == pDlg->pEndActiveWidgetList) {

    if (pDlg->pActiveWidgetList == pWidget) {
      pDlg->pActiveWidgetList = pWidget->prev;
    }

    if (pWidget == pDlg->pEndWidgetList) {
      pDlg->pEndWidgetList = pWidget->prev;
    }
    
    pDlg->pEndActiveWidgetList = pWidget->prev;

  }

  if (pDlg->pActiveWidgetList && pDlg->pActiveWidgetList == pWidget) {
    pDlg->pActiveWidgetList = pWidget->prev;
  }

  del_widget_from_gui_list(pWidget);
  
  if(pDlg->pScroll && pDlg->pScroll->pScrollBar && pDlg->pActiveWidgetList) {
    pDlg->pScroll->pScrollBar->size.y = get_position(pDlg);
  }
  
  return TRUE;
}

/**************************************************************************
  			Vertical ScrollBar
**************************************************************************/

/**************************************************************************
  ...
**************************************************************************/
static int std_up_advanced_dlg_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    struct ADVANCED_DLG *pDlg = pWidget->private_data.adv_dlg;
    struct widget *pBegin = up_scroll_widget_list(
                          pDlg->pScroll,
                          pDlg->pActiveWidgetList,
                          pDlg->pBeginActiveWidgetList,
                          pDlg->pEndActiveWidgetList);
  
    if (pBegin) {
      pDlg->pActiveWidgetList = pBegin;
    }
    
    unsellect_widget_action();
    pSellected_Widget = pWidget;
    set_wstate(pWidget, FC_WS_SELLECTED);
    redraw_tibutton(pWidget);
    flush_rect(pWidget->size, FALSE);
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int std_down_advanced_dlg_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    struct ADVANCED_DLG *pDlg = pWidget->private_data.adv_dlg;
    struct widget *pBegin = down_scroll_widget_list(
                          pDlg->pScroll,
                          pDlg->pActiveWidgetList,
                          pDlg->pBeginActiveWidgetList,
                          pDlg->pEndActiveWidgetList);
  
    if (pBegin) {
      pDlg->pActiveWidgetList = pBegin;
    }
  
    unsellect_widget_action();
    pSellected_Widget = pWidget;
    set_wstate(pWidget, FC_WS_SELLECTED);
    redraw_tibutton(pWidget);
    flush_rect(pWidget->size, FALSE);
  }
  return -1;
}

/**************************************************************************
  FIXME : fix main funct : vertic_scroll_widget_list(...)
**************************************************************************/
static int std_vscroll_advanced_dlg_callback(struct widget *pScrollBar)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    struct ADVANCED_DLG *pDlg = pScrollBar->private_data.adv_dlg;
    struct widget *pBegin = vertic_scroll_widget_list(
                          pDlg->pScroll,
                          pDlg->pActiveWidgetList,
                          pDlg->pBeginActiveWidgetList,
                          pDlg->pEndActiveWidgetList);
  
    if (pBegin) {
      pDlg->pActiveWidgetList = pBegin;
    }
    unsellect_widget_action();
    set_wstate(pScrollBar, FC_WS_SELLECTED);
    pSellected_Widget = pScrollBar;
    redraw_vert(pScrollBar);
    flush_rect(pScrollBar->size, FALSE);
  }
  return -1;
}


/**************************************************************************
  ...
**************************************************************************/
Uint32 create_vertical_scrollbar(struct ADVANCED_DLG *pDlg,
		  Uint8 step, Uint8 active,
		  bool create_scrollbar, bool create_buttons)
{
  Uint16 count = 0;
  struct widget *pBuf = NULL, *pWindow = NULL;
    
  assert(pDlg != NULL);
  
  pWindow = pDlg->pEndWidgetList;
  
  if(!pDlg->pScroll) {
    pDlg->pScroll = fc_calloc(1, sizeof(struct ScrollBar));
        
    pBuf = pDlg->pEndActiveWidgetList;
    while(pBuf && pBuf != pDlg->pBeginActiveWidgetList->prev) {
      pBuf = pBuf->prev;
      count++;
    }
  
    pDlg->pScroll->count = count;
  }
  
  pDlg->pScroll->active = active;
  pDlg->pScroll->step = step;
  
  if(create_buttons) {
    /* create up button */
    pBuf = create_themeicon_button(pTheme->UP_Icon, pWindow->dst, NULL, 0);
    
    pBuf->ID = ID_BUTTON;
    pBuf->private_data.adv_dlg = pDlg;
    pBuf->action = std_up_advanced_dlg_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
    
    pDlg->pScroll->pUp_Left_Button = pBuf;
    DownAdd(pBuf, pDlg->pBeginWidgetList);
    pDlg->pBeginWidgetList = pBuf;
    
    count = pBuf->size.w;
    
    /* create down button */
    pBuf = create_themeicon_button(pTheme->DOWN_Icon, pWindow->dst, NULL, 0);
    
    pBuf->ID = ID_BUTTON;
    pBuf->private_data.adv_dlg = pDlg;
    pBuf->action = std_down_advanced_dlg_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
    
    pDlg->pScroll->pDown_Right_Button = pBuf;
    DownAdd(pBuf, pDlg->pBeginWidgetList);
    pDlg->pBeginWidgetList = pBuf;
    
  }
  
  if(create_scrollbar) {
    /* create vsrollbar */
    pBuf = create_vertical(pTheme->Vertic, pWindow->dst,
				10, WF_RESTORE_BACKGROUND);
    
    pBuf->ID = ID_SCROLLBAR;
    pBuf->private_data.adv_dlg = pDlg;
    pBuf->action = std_vscroll_advanced_dlg_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
  
    pDlg->pScroll->pScrollBar = pBuf;
    DownAdd(pBuf, pDlg->pBeginWidgetList);
    pDlg->pBeginWidgetList = pBuf;
    
    if(!count) {
      count = pBuf->size.w;
    }
  }
  
  return count;
}

/**************************************************************************
  ...
**************************************************************************/
void setup_vertical_scrollbar_area(struct ScrollBar *pScroll,
	Sint16 start_x, Sint16 start_y, Uint16 hight, bool swap_start_x)
{
  bool buttons_exist;
  
  assert(pScroll != NULL);
  
  buttons_exist = (pScroll->pDown_Right_Button && pScroll->pUp_Left_Button);
  
  if(buttons_exist) {
    /* up */
    pScroll->pUp_Left_Button->size.y = start_y;
    if(swap_start_x) {
      pScroll->pUp_Left_Button->size.x = start_x - 
    					pScroll->pUp_Left_Button->size.w;
    } else {
      pScroll->pUp_Left_Button->size.x = start_x;
    }
    pScroll->min = start_y + pScroll->pUp_Left_Button->size.h;
    /* -------------------------- */
    /* down */
    pScroll->pDown_Right_Button->size.y = start_y + hight -
				  pScroll->pDown_Right_Button->size.h;
    if(swap_start_x) {
      pScroll->pDown_Right_Button->size.x = start_x -
				pScroll->pDown_Right_Button->size.w;
    } else {
      pScroll->pDown_Right_Button->size.x = start_x;
    }
    pScroll->max = pScroll->pDown_Right_Button->size.y;
  }
  /* --------------- */
  /* scrollbar */
  if (pScroll->pScrollBar) {
    
    if(swap_start_x) {
      pScroll->pScrollBar->size.x = start_x - pScroll->pScrollBar->size.w + 2;
    } else {
      pScroll->pScrollBar->size.x = start_x;
    } 

    if(buttons_exist) {
      pScroll->pScrollBar->size.y = start_y +
				      pScroll->pUp_Left_Button->size.h;
      if(pScroll->count > pScroll->active * pScroll->step) {
	pScroll->pScrollBar->size.h = scrollbar_size(pScroll);
      } else {
	pScroll->pScrollBar->size.h = pScroll->max - pScroll->min;
      }
    } else {
      pScroll->pScrollBar->size.y = start_y;
      pScroll->pScrollBar->size.h = hight;
      pScroll->min = start_y;
      pScroll->max = start_y + hight;
    }
  }
}

/**************************************************************************
  ...
**************************************************************************/
int setup_vertical_widgets_position(int step,
	Sint16 start_x, Sint16 start_y, Uint16 w, Uint16 h,
				struct widget *pBegin, struct widget *pEnd)
{
  struct widget *pBuf = pEnd;
  register int count = 0;
  register int real_start_x = start_x;
  int ret = 0;
  
  while(pBuf)
  {
    pBuf->size.x = real_start_x;
    pBuf->size.y = start_y;
     
    if(w) {
      pBuf->size.w = w;
    }
    
    if(h) {
      pBuf->size.h = h;
    }
    
    if(!((count + 1) % step)) {
      real_start_x = start_x;
      start_y += pBuf->size.h;
      if (((get_wflags(pBuf) & WF_HIDDEN) != WF_HIDDEN))
      {
        ret += pBuf->size.h;
      }
    } else {
      real_start_x += pBuf->size.w;
    }
    
    if(pBuf == pBegin) {
      
      break;
    }
    count++;
    pBuf = pBuf->prev;
  }
  
  return ret;
}


/**************************************************************************
  ...
**************************************************************************/
void setup_vertical_scrollbar_default_callbacks(struct ScrollBar *pScroll)
{
  assert(pScroll != NULL);
  if(pScroll->pUp_Left_Button) {
    pScroll->pUp_Left_Button->action = std_up_advanced_dlg_callback;
  }
  if(pScroll->pDown_Right_Button) {
    pScroll->pDown_Right_Button->action = std_down_advanced_dlg_callback;
  }
  if(pScroll->pScrollBar) {
    pScroll->pScrollBar->action = std_vscroll_advanced_dlg_callback;
  }
}

/**************************************************************************
  			Horizontal Scrollbar
**************************************************************************/


/**************************************************************************
  ...
**************************************************************************/
Uint32 create_horizontal_scrollbar(struct ADVANCED_DLG *pDlg,
		  Sint16 start_x, Sint16 start_y, Uint16 width, Uint16 active,
		  bool create_scrollbar, bool create_buttons, bool swap_start_y)
{
  Uint16 count = 0;
  struct widget *pBuf = NULL, *pWindow = NULL;
    
  assert(pDlg != NULL);
  
  pWindow = pDlg->pEndWidgetList;
  
  if(!pDlg->pScroll) {
    pDlg->pScroll = fc_calloc(1, sizeof(struct ScrollBar));
    
    pDlg->pScroll->active = active;
    
    pBuf = pDlg->pEndActiveWidgetList;
    while(pBuf && pBuf != pDlg->pBeginActiveWidgetList->prev) {
      pBuf = pBuf->prev;
      count++;
    }
  
    pDlg->pScroll->count = count;
  }
  
  if(create_buttons) {
    /* create up button */
    pBuf = create_themeicon_button(pTheme->LEFT_Icon, pWindow->dst, NULL, 0);
    
    pBuf->ID = ID_BUTTON;
    pBuf->data.ptr = (void *)pDlg;
    set_wstate(pBuf, FC_WS_NORMAL);
    
    pBuf->size.x = start_x;
    if(swap_start_y) {
      pBuf->size.y = start_y - pBuf->size.h;
    } else {
      pBuf->size.y = start_y;
    }
    
    pDlg->pScroll->min = start_x + pBuf->size.w;
    pDlg->pScroll->pUp_Left_Button = pBuf;
    DownAdd(pBuf, pDlg->pBeginWidgetList);
    pDlg->pBeginWidgetList = pBuf;
    
    
    count = pBuf->size.h;
    
    /* create down button */
    pBuf = create_themeicon_button(pTheme->RIGHT_Icon, pWindow->dst, NULL, 0);
    
    pBuf->ID = ID_BUTTON;
    pBuf->data.ptr = (void *)pDlg;
    set_wstate(pBuf, FC_WS_NORMAL);
    
    pBuf->size.x = start_x + width - pBuf->size.w;
    if(swap_start_y) {
      pBuf->size.y = start_y - pBuf->size.h;
    } else {
      pBuf->size.y = start_y;
    }
    
    pDlg->pScroll->max = pBuf->size.x;
    pDlg->pScroll->pDown_Right_Button = pBuf;
    DownAdd(pBuf, pDlg->pBeginWidgetList);
    pDlg->pBeginWidgetList = pBuf;
    
  }
  
  if(create_scrollbar) {
    /* create vsrollbar */
    pBuf = create_horizontal(pTheme->Horiz, pWindow->dst,
				width, WF_RESTORE_BACKGROUND);
    
    pBuf->ID = ID_SCROLLBAR;
    pBuf->data.ptr = (void *)pDlg;
    set_wstate(pBuf, FC_WS_NORMAL);
     
    if(swap_start_y) {
      pBuf->size.y = start_y - pBuf->size.h;
    } else {
      pBuf->size.y = start_y;
    } 

    if(create_buttons) {
      pBuf->size.x = start_x + pDlg->pScroll->pUp_Left_Button->size.w;
      if(pDlg->pScroll->count > pDlg->pScroll->active) {
	pBuf->size.w = scrollbar_size(pDlg->pScroll);
      } else {
	pBuf->size.w = pDlg->pScroll->max - pDlg->pScroll->min;
      }
    } else {
      pBuf->size.x = start_x;
      pDlg->pScroll->min = start_x;
      pDlg->pScroll->max = start_x + width;
    }
    
    pDlg->pScroll->pScrollBar = pBuf;
    DownAdd(pBuf, pDlg->pBeginWidgetList);
    pDlg->pBeginWidgetList = pBuf;
    
    if(!count) {
      count = pBuf->size.h;
    }
    
  }
  
  return count;
}


/* =================================================== */
/* ===================== ICON ======================== */
/* =================================================== */

/**************************************************************************
  set new theme and callculate new size.
**************************************************************************/
void set_new_icon_theme(struct widget *pIcon_Widget, SDL_Surface * pNew_Theme)
{
  if ((pNew_Theme) && (pIcon_Widget)) {
    FREESURFACE(pIcon_Widget->theme);
    pIcon_Widget->theme = pNew_Theme;
    pIcon_Widget->size.w = pNew_Theme->w / 4;
    pIcon_Widget->size.h = pNew_Theme->h;
  }
}

/**************************************************************************
  Ugly hack to create 4-state icon theme from static icon;
**************************************************************************/
SDL_Surface *create_icon_theme_surf(SDL_Surface * pIcon)
{
  SDL_Color bg_color = { 255, 255, 255, 128 };
  
  SDL_Rect dest, src = get_smaller_surface_rect(pIcon);
  SDL_Surface *pTheme = create_surf_alpha((src.w + adj_size(4)) * 4, src.h + adj_size(4),
				    SDL_SWSURFACE);
  dest.x = adj_size(2);
  dest.y = (pTheme->h - src.h) / 2;

  /* normal */
  alphablit(pIcon, &src, pTheme, &dest);

  /* sellected */
  dest.x += (src.w + adj_size(4));
  alphablit(pIcon, &src, pTheme, &dest);
  /* draw sellect frame */
  putframe(pTheme, dest.x - 1, dest.y - 1, dest.x + (src.w), dest.y + src.h,
    map_rgba(pTheme->format, *get_game_colorRGB(COLOR_THEME_CUSTOM_WIDGET_SELECTED_FRAME)));

  /* pressed */
  dest.x += (src.w + adj_size(4));
  alphablit(pIcon, &src, pTheme, &dest);
  /* draw sellect frame */
  putframe(pTheme, dest.x - 1, dest.y - 1, dest.x + src.w, dest.y + src.h,
    map_rgba(pTheme->format, *get_game_colorRGB(COLOR_THEME_CUSTOM_WIDGET_SELECTED_FRAME)));
  /* draw press frame */
  putframe(pTheme, dest.x - adj_size(2), dest.y - adj_size(2), dest.x + (src.w + 1),
	   dest.y + (src.h + 1),
    map_rgba(pTheme->format, *get_game_colorRGB(COLOR_THEME_CUSTOM_WIDGET_PRESSED_FRAME)));

  /* disabled */
  dest.x += (src.w + adj_size(4));
  alphablit(pIcon, &src, pTheme, &dest);
  dest.w = src.w;
  dest.h = src.h;

  SDL_FillRectAlpha(pTheme, &dest, &bg_color);

  return pTheme;
}

/**************************************************************************
  Create ( malloc ) Icon Widget ( flat Button )
**************************************************************************/
struct widget * create_themeicon(SDL_Surface *pIcon_theme, SDL_Surface *pDest,
							  Uint32 flags)
{
  struct widget *pIcon_Widget = fc_calloc(1, sizeof(struct widget));

  pIcon_Widget->theme = pIcon_theme;

  set_wflag(pIcon_Widget, (WF_FREE_STRING | flags));
  set_wstate(pIcon_Widget, FC_WS_DISABLED);
  set_wtype(pIcon_Widget, WT_ICON);
  pIcon_Widget->mod = KMOD_NONE;
  pIcon_Widget->dst = pDest;
  
  if (pIcon_theme) {
    pIcon_Widget->size.w = pIcon_theme->w / 4;
    pIcon_Widget->size.h = pIcon_theme->h;
  }

  return pIcon_Widget;
}

/**************************************************************************
  ...
**************************************************************************/
int draw_icon(struct widget *pIcon, Sint16 start_x, Sint16 start_y)
{
  pIcon->size.x = start_x;
  pIcon->size.y = start_y;

  if (get_wflags(pIcon) & WF_RESTORE_BACKGROUND) {
    refresh_widget_background(pIcon);
  }

  return draw_icon_from_theme(pIcon->theme, get_wstate(pIcon), pIcon->dst,
			      pIcon->size.x, pIcon->size.y);
}

/**************************************************************************
  ...
**************************************************************************/
int real_redraw_icon(struct widget *pIcon)
{
  SDL_Rect src, area = pIcon->size;

  fix_rect(pIcon->dst, &area);
  
  if (!pIcon->theme) {
    return -3;
  }

  if (pIcon->gfx) {
    clear_surface(pIcon->dst, &area);
    alphablit(pIcon->gfx, NULL, pIcon->dst, &area);
  }

  src.x = (pIcon->theme->w / 4) * (Uint8) (get_wstate(pIcon));
  src.y = 0;
  src.w = (pIcon->theme->w / 4);
  src.h = pIcon->theme->h;

  if (pIcon->size.w != src.w) {
    area.x += (pIcon->size.w - src.w) / 2;
  }

  if (pIcon->size.h != src.h) {
    area.y += (pIcon->size.h - src.h) / 2;
  }

  return alphablit(pIcon->theme, &src, pIcon->dst, &area);
}

/**************************************************************************
  Blit Icon image to pDest(ination) on positon
  start_x , start_y. WARRING: pDest must exist.

  Graphic is taken from pIcon_theme surface.

  Type of Icon depend of "state" parametr.
    state = 0 - normal
    state = 1 - selected
    state = 2 - pressed
    state = 3 - disabled

  Function return: -3 if pIcon_theme is NULL. 
  std return of alphablit(...) function.
**************************************************************************/
int draw_icon_from_theme(SDL_Surface * pIcon_theme, Uint8 state,
			 SDL_Surface * pDest, Sint16 start_x, Sint16 start_y)
{
  SDL_Rect src, des = {start_x, start_y, 0, 0};

  fix_rect(pIcon_theme, &des);
  
  if (!pIcon_theme) {
    return -3;
  }
  /* src.x = 0 + pIcon_theme->w/4 * state */
  src.x = 0 + (pIcon_theme->w / 4) * state;
  src.y = 0;
  src.w = pIcon_theme->w / 4;
  src.h = pIcon_theme->h;
  return alphablit(pIcon_theme, &src, pDest, &des);
}

/**************************************************************************
  Create Icon image then return pointer to this image.
  
  Graphic is take from pIcon_theme surface and blit to new created image.

  Type of Icon depend of "state" parametr.
    state = 0 - normal
    state = 1 - selected
    state = 2 - pressed
    state = 3 - disabled

  Function return NULL if pIcon_theme is NULL or blit fail. 
**************************************************************************/
SDL_Surface * create_icon_from_theme(SDL_Surface *pIcon_theme, Uint8 state)
{
  SDL_Rect src;
  
  if (!pIcon_theme) {
    return NULL;
  }
  
  src.w = pIcon_theme->w / 4;  
  src.x = src.w * state;
  src.y = 0;
  src.h = pIcon_theme->h;
  
  return crop_rect_from_surface(pIcon_theme, &src);
}

/* =================================================== */
/* ===================== ICON2 ======================== */
/* =================================================== */

/**************************************************************************
  set new theme and callculate new size.
**************************************************************************/
void set_new_icon2_theme(struct widget *pIcon_Widget, SDL_Surface *pNew_Theme,
			  bool free_old_theme)
{
  if ((pNew_Theme) && (pIcon_Widget)) {
    if(free_old_theme) {
      FREESURFACE(pIcon_Widget->theme);
    }
    pIcon_Widget->theme = pNew_Theme;
    pIcon_Widget->size.w = pNew_Theme->w + adj_size(4);
    pIcon_Widget->size.h = pNew_Theme->h + adj_size(4);
  }
}

/**************************************************************************
  Create ( malloc ) Icon2 Widget ( flat Button )
**************************************************************************/
struct widget * create_icon2(SDL_Surface *pIcon, SDL_Surface *pDest, Uint32 flags)
{

  struct widget *pIcon_Widget = fc_calloc(1, sizeof(struct widget));

  pIcon_Widget->theme = pIcon;

  set_wflag(pIcon_Widget, (WF_FREE_STRING | flags));
  set_wstate(pIcon_Widget, FC_WS_DISABLED);
  set_wtype(pIcon_Widget, WT_ICON2);
  pIcon_Widget->mod = KMOD_NONE;
  pIcon_Widget->dst = pDest;
  
  if (pIcon) {
    pIcon_Widget->size.w = pIcon->w + adj_size(4);
    pIcon_Widget->size.h = pIcon->h + adj_size(4);
  }

  return pIcon_Widget;
}

/**************************************************************************
  ...
**************************************************************************/
int real_redraw_icon2(struct widget *pIcon)
{
  int ret;
  SDL_Rect dest = pIcon->size;
  Uint32 state;

  if(!pIcon) {
    return -3;
  }
  
  if(!pIcon->theme) {
    return -4;
  }
  
  fix_rect(pIcon->dst, &dest);
  
  state = get_wstate(pIcon);
    
  if (pIcon->gfx) {
    clear_surface(pIcon->dst, &dest);
    ret = alphablit(pIcon->gfx, NULL, pIcon->dst, &dest);
    if (ret) {
      return ret;
    }
  }

  dest.x = pIcon->size.x;
  dest.y = pIcon->size.y;
  dest.w = pIcon->theme->w;
  dest.h = pIcon->theme->h;
  fix_rect(pIcon->dst, &dest);  

  if (state == FC_WS_SELLECTED) {
    putframe(pIcon->dst, dest.x + 1, dest.y + 1,
      dest.x + dest.w + adj_size(2), dest.y + dest.h + adj_size(2),
      map_rgba(pIcon->dst->format, *get_game_colorRGB(COLOR_THEME_CUSTOM_WIDGET_SELECTED_FRAME)));
  }

  if (state == FC_WS_PRESSED) {
    putframe(pIcon->dst, dest.x + 1, dest.y + 1,
      dest.x + dest.w + adj_size(2), dest.y + dest.h + adj_size(2),
      map_rgba(pIcon->dst->format, *get_game_colorRGB(COLOR_THEME_CUSTOM_WIDGET_SELECTED_FRAME)));

    putframe(pIcon->dst, dest.x, dest.y,
	     dest.x + dest.w + adj_size(3), dest.y + dest.h + adj_size(3),
	     map_rgba(pIcon->dst->format, *get_game_colorRGB(COLOR_THEME_CUSTOM_WIDGET_PRESSED_FRAME)));
  }

  if (state == FC_WS_DISABLED) {
    putframe(pIcon->dst, dest.x + 1, dest.y + 1,
	     dest.x + dest.w + adj_size(2), dest.y + dest.h + adj_size(2),
	     map_rgba(pIcon->dst->format, *get_game_colorRGB(COLOR_THEME_WIDGET_DISABLED_TEXT)));
  }
  dest.x += adj_size(2);
  dest.y += adj_size(2);
  ret = alphablit(pIcon->theme, NULL, pIcon->dst, &dest);
  if (ret) {
    return ret;
  }
    
  return 0;
}

/* =================================================== */
/* ================== ICON BUTTON ==================== */
/* =================================================== */

/**************************************************************************
  Create ( malloc ) Icon (theme)Button Widget structure.

  Icon graphic is taken from 'pIcon' surface (don't change with button
  changes );  Button Theme graphic is taken from pTheme->Button surface;
  Text is taken from 'pString16'.

  This function determinate future size of Button ( width, high ) and
  save this in: pWidget->size rectangle ( SDL_Rect )

  function return pointer to allocated Button Widget.
**************************************************************************/
struct widget * create_icon_button(SDL_Surface *pIcon, SDL_Surface *pDest,
			  SDL_String16 *pStr, Uint32 flags)
{
  SDL_Rect buf = {0, 0, 0, 0};
  Uint16 w = 0, h = 0;
  struct widget *pButton;

  if (!pIcon && !pStr) {
    return NULL;
  }

  pButton = fc_calloc(1, sizeof(struct widget));

  pButton->theme = pTheme->Button;
  pButton->gfx = pIcon;
  pButton->string16 = pStr;
  set_wflag(pButton, (WF_FREE_STRING | flags));
  set_wstate(pButton, FC_WS_DISABLED);
  set_wtype(pButton, WT_I_BUTTON);
  pButton->mod = KMOD_NONE;
  pButton->dst = pDest;
  
  if (pStr && !(flags & WF_WIDGET_HAS_INFO_LABEL)) {
    pButton->string16->style |= SF_CENTER;
    /* if BOLD == true then longest wight */
    if (!(pStr->style & TTF_STYLE_BOLD)) {
      pStr->style |= TTF_STYLE_BOLD;
      buf = str16size(pStr);
      pStr->style &= ~TTF_STYLE_BOLD;
    } else {
      buf = str16size(pStr);
    }

    w = MAX(w, buf.w);
    h = MAX(h, buf.h);
  }

  if (pIcon) {
    if (pStr) {
      if ((flags & WF_ICON_UNDER_TEXT) || (flags & WF_ICON_ABOVE_TEXT)) {
	w = MAX(w, pIcon->w + adj_size(2));
	h = MAX(h, buf.h + pIcon->h + adj_size(4));
      } else {
	w = MAX(w, buf.w + pIcon->w + adj_size(20));
	h = MAX(h, pIcon->h + adj_size(2));
      }
    } else {
      w = MAX(w, pIcon->w + adj_size(2));
      h = MAX(h, pIcon->h + adj_size(2));
    }
  } else {
    w += adj_size(10);
    h += adj_size(2);
  }

  correct_size_bcgnd_surf(pTheme->Button, &w, &h);

  pButton->size.w = w;
  pButton->size.h = h;

  return pButton;
}

/**************************************************************************
  Create ( malloc ) Theme Icon (theme)Button Widget structure.

  Icon Theme graphic is taken from 'pIcon_theme' surface ( change with
  button changes ); Button Theme graphic is taken from pTheme->Button
  surface; Text is taken from 'pString16'.

  This function determinate future size of Button ( width, high ) and
  save this in: pWidget->size rectangle ( SDL_Rect )

  function return pointer to allocated Button Widget.
**************************************************************************/
struct widget * create_themeicon_button(SDL_Surface *pIcon_theme,
		SDL_Surface *pDest, SDL_String16 *pString16, Uint32 flags)
{
  SDL_Surface *pIcon = create_icon_from_theme(pIcon_theme, 1);
  struct widget *pButton = create_icon_button(pIcon, pDest, pString16, flags);

  FREESURFACE(pButton->gfx);	/* pButton->gfx == pIcon */
  pButton->gfx = pIcon_theme;
  set_wtype(pButton, WT_TI_BUTTON);

  return pButton;
}

/**************************************************************************
  Steate Button image with text and Icon.  Then blit to Main.screen on
  positon start_x , start_y.

  Text with atributes is taken from pButton->string16 parameter.

  Graphic for button is taken from pButton->theme surface and blit to new
  created image.

  Graphic for Icon theme is taken from pButton->gfx surface and blit to
  new created image.

  function return (-1) if there are no Icon and Text.
  Else return 0.
**************************************************************************/
int draw_tibutton(struct widget *pButton, Sint16 start_x, Sint16 start_y)
{
  pButton->size.x = start_x;
  pButton->size.y = start_y;
  return real_redraw_tibutton(pButton);
}

/**************************************************************************
  Create Button image with text and Icon.
  Then blit to Main.screen on positon start_x , start_y.

   Text with atributes is taken from pButton->string16 parameter.

   Graphic for button is taken from pButton->theme surface 
   and blit to new created image.

  Graphic for Icon is taken from pButton->gfx surface 
  and blit to new created image.

  function return (-1) if there are no Icon and Text.
  Else return 0.
**************************************************************************/
int draw_ibutton(struct widget *pButton, Sint16 start_x, Sint16 start_y)
{
  pButton->size.x = start_x;
  pButton->size.y = start_y;
  return real_redraw_ibutton(pButton);
}

/**************************************************************************
  Create Icon Button image with text and Icon then blit to Dest(ination)
  on positon pIButton->size.x , pIButton->size.y.
  WARRING: pDest must exist.

  Text with atributes is taken from pIButton->string16 parameter.

  Graphic for button is taken from pIButton->theme surface 
  and blit to new created image.

  Graphic for Icon is taken from pIButton->gfx surface and blit to new
  created image.

  function return (-1) if there are no Icon and Text.
  Else return 0.
**************************************************************************/
int real_redraw_ibutton(struct widget *pIButton)
{
  SDL_Rect dest = { 0, 0, 0, 0 };
  SDL_String16 TMPString;
  SDL_Surface *pButton = NULL, *pText = NULL, *pIcon = pIButton->gfx;
  Uint16 Ix, Iy, x;
  Uint16 y = 0; /* FIXME: possibly uninitialized */
  int ret;

  if (pIButton->string16 && !(get_wflags(pIButton) & WF_WIDGET_HAS_INFO_LABEL)) {

    /* make copy of string16 */
    TMPString = *pIButton->string16;

    if (get_wstate(pIButton) == FC_WS_NORMAL) {
      TMPString.fgcol = *get_game_colorRGB(COLOR_THEME_WIDGET_NORMAL_TEXT);
    } else if (get_wstate(pIButton) == FC_WS_SELLECTED) {
      TMPString.fgcol = *get_game_colorRGB(COLOR_THEME_WIDGET_SELECTED_TEXT);
      TMPString.style |= TTF_STYLE_BOLD;
    } else if (get_wstate(pIButton) == FC_WS_PRESSED) {
      TMPString.fgcol = *get_game_colorRGB(COLOR_THEME_WIDGET_PRESSED_TEXT);
    } else if (get_wstate(pIButton) == FC_WS_DISABLED) {
      TMPString.fgcol = *get_game_colorRGB(COLOR_THEME_WIDGET_DISABLED_TEXT);
    }

    pText = create_text_surf_from_str16(&TMPString);
  }

  if (!pText && !pIcon) {
    return -1;
  }

  /* create Button graphic */
  pButton = create_bcgnd_surf(pIButton->theme, get_wstate(pIButton),
			      pIButton->size.w, pIButton->size.h);

  dest = pIButton->size;
  fix_rect(pIButton->dst, &dest);  
  clear_surface(pIButton->dst, &dest);
  alphablit(pButton, NULL, pIButton->dst, &dest);
  FREESURFACE(pButton);

  if (pIcon) {			/* Icon */
    if (pText) {
      if (get_wflags(pIButton) & WF_ICON_CENTER_RIGHT) {
	Ix = pIButton->size.w - pIcon->w - 5;
      } else {
	if (get_wflags(pIButton) & WF_ICON_CENTER) {
	  Ix = (pIButton->size.w - pIcon->w) / 2;
	} else {
	  Ix = 5;
	}
      }

      if (get_wflags(pIButton) & WF_ICON_ABOVE_TEXT) {
	Iy = 3;
	y = 3 + pIcon->h + 3 + (pIButton->size.h -
				(pIcon->h + 6) - pText->h) / 2;
      } else {
	if (get_wflags(pIButton) & WF_ICON_UNDER_TEXT) {
	  y = 3 + (pIButton->size.h - (pIcon->h + 3) - pText->h) / 2;
	  Iy = y + pText->h + 3;
	} else {		/* center */
	  Iy = (pIButton->size.h - pIcon->h) / 2;
	  y = (pIButton->size.h - pText->h) / 2;
	}
      }
    } else {			/* no text */
      Iy = (pIButton->size.h - pIcon->h) / 2;
      Ix = (pIButton->size.w - pIcon->w) / 2;
    }

    if (get_wstate(pIButton) == FC_WS_PRESSED) {
      Ix += 1;
      Iy += 1;
    }


    dest.x = pIButton->size.x + Ix;
    dest.y = pIButton->size.y + Iy;
    fix_rect(pIButton->dst, &dest);
    ret = alphablit(pIcon, NULL, pIButton->dst, &dest);
    if (ret) {
      FREESURFACE(pText);
      return ret - 10;
    }
  }

  if (pText) {

    if (pIcon) {
      if (!(get_wflags(pIButton) & WF_ICON_ABOVE_TEXT) &&
	  !(get_wflags(pIButton) & WF_ICON_UNDER_TEXT)) {
	if (get_wflags(pIButton) & WF_ICON_CENTER_RIGHT) {
	  if (pIButton->string16->style & SF_CENTER) {
	    x = (pIButton->size.w - (pIcon->w + 5) - pText->w) / 2;
	  } else {
	    if (pIButton->string16->style & SF_CENTER_RIGHT) {
	      x = pIButton->size.w - (pIcon->w + 7) - pText->w;
	    } else {
	      x = 5;
	    }
	  }
	} /* end WF_ICON_CENTER_RIGHT */
	else {
	  if (get_wflags(pIButton) & WF_ICON_CENTER) {
	    /* text is blit on icon */
	    goto Alone;
	  } /* end WF_ICON_CENTER */
	  else {		/* icon center left - default */
	    if (pIButton->string16->style & SF_CENTER) {
	      x = 5 + pIcon->w + ((pIButton->size.w -
				   (pIcon->w + 5) - pText->w) / 2);
	    } else {
	      if (pIButton->string16->style & SF_CENTER_RIGHT) {
		x = pIButton->size.w - pText->w - 5;
	      } else {		/* text center left */
		x = 5 + pIcon->w + 3;
	      }
	    }

	  }			/* end icon center left - default */

	}
	/* 888888888888888888 */
      } else {
	goto Alone;
      }
    } else {
      /* !pIcon */
      y = (pIButton->size.h - pText->h) / 2;
    Alone:
      if (pIButton->string16->style & SF_CENTER) {
	x = (pIButton->size.w - pText->w) / 2;
      } else {
	if (pIButton->string16->style & SF_CENTER_RIGHT) {
	  x = pIButton->size.w - pText->w - 5;
	} else {
	  x = 5;
	}
      }
    }

    if (get_wstate(pIButton) == FC_WS_PRESSED) {
      x += 1;
    } else {
      y -= 1;
    }

    dest.x = pIButton->size.x + x;
    dest.y = pIButton->size.y + y;
    fix_rect(pIButton->dst, &dest);
    ret = alphablit(pText, NULL, pIButton->dst, &dest);
  }

  FREESURFACE(pText);

  return 0;
}

/**************************************************************************
  Create Icon Button image with text and Icon then blit to Dest(ination)
  on positon pTIButton->size.x , pTIButton->size.y. WARRING: pDest must
  exist.

  Text with atributes is taken from pTIButton->string16 parameter.

  Graphic for button is taken from pTIButton->theme surface 
  and blit to new created image.

  Graphic for Icon Theme is taken from pTIButton->gfx surface 
  and blit to new created image.

  function return (-1) if there are no Icon and Text.  Else return 0.
**************************************************************************/
int real_redraw_tibutton(struct widget *pTIButton)
{
  int iRet = 0;
  SDL_Surface *pIcon = create_icon_from_theme(pTIButton->gfx,
					      get_wstate(pTIButton));
  SDL_Surface *pCopy_Of_Icon_Theme = pTIButton->gfx;

  pTIButton->gfx = pIcon;

  iRet = real_redraw_ibutton(pTIButton);

  FREESURFACE(pTIButton->gfx);
  pTIButton->gfx = pCopy_Of_Icon_Theme;

  return iRet;
}

/* =================================================== */
/* ===================== EDIT ======================== */
/* =================================================== */
  
/**************************************************************************
  Return length of UniChar chain.
  WARRNING: if struct UniChar has 1 member and UniChar->chr == 0 then
  this function return 1 ( not 0 like in strlen )
**************************************************************************/
static size_t chainlen(const struct UniChar *pChain)
{
  size_t length = 0;

  if (pChain) {
    while (1) {
      length++;
      if (pChain->next == NULL) {
	break;
      }
      pChain = pChain->next;
    }
  }

  return length;
}

/**************************************************************************
  Delete UniChar structure.
**************************************************************************/
static void del_chain(struct UniChar *pChain)
{
  int i, len = 0;

  if (!pChain) {
    return;
  }

  len = chainlen(pChain);

  if (len > 1) {
    pChain = pChain->next;
    for (i = 0; i < len - 1; i++) {
      FREESURFACE(pChain->prev->pTsurf);
      FC_FREE(pChain->prev);
      pChain = pChain->next;
    }
  }

  FC_FREE(pChain);
}

/**************************************************************************
  Convert Unistring ( Uint16[] ) to UniChar structure.
  Memmory alocation -> after all use need call del_chain(...) !
**************************************************************************/
static struct UniChar *text2chain(const Uint16 * pInText)
{
  int i, len;
  struct UniChar *pOutChain = NULL;
  struct UniChar *chr_tmp = NULL;

  len = unistrlen(pInText);

  if (len == 0) {
    return pOutChain;
  }

  pOutChain = fc_calloc(1, sizeof(struct UniChar));
  pOutChain->chr[0] = pInText[0];
  pOutChain->chr[1] = 0;
  chr_tmp = pOutChain;

  for (i = 1; i < len; i++) {
    chr_tmp->next = fc_calloc(1, sizeof(struct UniChar));
    chr_tmp->next->chr[0] = pInText[i];
    chr_tmp->next->chr[1] = 0;
    chr_tmp->next->prev = chr_tmp;
    chr_tmp = chr_tmp->next;
  }

  return pOutChain;
}

/**************************************************************************
  Convert UniChar structure to Unistring ( Uint16[] ).
  WARRING: Do not free UniChar structure but allocate new Unistring.   
**************************************************************************/
static Uint16 *chain2text(const struct UniChar *pInChain, size_t len)
{
  int i;
  Uint16 *pOutText = NULL;

  if (!(len && pInChain)) {
    return pOutText;
  }

  pOutText = fc_calloc(len + 1, sizeof(Uint16));
  for (i = 0; i < len; i++) {
    pOutText[i] = pInChain->chr[0];
    pInChain = pInChain->next;
  }

  return pOutText;
}

/**************************************************************************
...
**************************************************************************/
static void redraw_edit_chain(struct EDIT *pEdt)
{
  struct UniChar *pInputChain_TMP;
  SDL_Rect Dest, Dest_Copy = {0, 0, 0, 0};
  int iStart_Mod_X;

  Dest_Copy.x = pEdt->pWidget->size.x;
  Dest_Copy.y = pEdt->pWidget->size.y;

  /* blit backgroud ( if any ) */
  if (get_wflags(pEdt->pWidget) & WF_RESTORE_BACKGROUND) {
    Dest = Dest_Copy;
    fix_rect(pEdt->pWidget->dst, &Dest);
    clear_surface(pEdt->pWidget->dst, &Dest);
    alphablit(pEdt->pWidget->gfx, NULL, pEdt->pWidget->dst, &Dest);
  }

  /* blit theme */
  Dest = Dest_Copy;
  fix_rect(pEdt->pWidget->dst, &Dest);
  alphablit(pEdt->pBg, NULL, pEdt->pWidget->dst, &Dest);

  /* set start parametrs */
  pInputChain_TMP = pEdt->pBeginTextChain;
  iStart_Mod_X = 0;

  Dest_Copy.y += (pEdt->pBg->h - pInputChain_TMP->pTsurf->h) / 2;
  Dest_Copy.x += pEdt->Start_X;

  /* draw loop */
  while (pInputChain_TMP) {
    Dest_Copy.x += iStart_Mod_X;
    /* chech if we draw inside of edit rect */
    if (Dest_Copy.x > pEdt->pWidget->size.x + pEdt->pBg->w - 4) {
      break;
    }

    if (Dest_Copy.x > pEdt->pWidget->size.x) {
      Dest = Dest_Copy;
      fix_rect(pEdt->pWidget->dst, &Dest);
      alphablit(pInputChain_TMP->pTsurf, NULL, pEdt->pWidget->dst, &Dest);
    }

    iStart_Mod_X = pInputChain_TMP->pTsurf->w;

    /* draw cursor */
    if (pInputChain_TMP == pEdt->pInputChain) {
      Dest = Dest_Copy;
      fix_rect(pEdt->pWidget->dst, &Dest);
      putline(pEdt->pWidget->dst, Dest.x - 1,
		  Dest.y + (pEdt->pBg->h / 8), Dest.x - 1,
		  Dest.y + pEdt->pBg->h - (pEdt->pBg->h / 4),
		  map_rgba(pEdt->pWidget->dst->format,
                  *get_game_colorRGB(COLOR_THEME_EDITFIELD_CARET)));
      /* save active element position */
      pEdt->InputChain_X = Dest_Copy.x;
    }
	
    pInputChain_TMP = pInputChain_TMP->next;
  }	/* while - draw loop */

  flush_rect(pEdt->pWidget->size, FALSE);
  
}

/* =================================================== */

/**************************************************************************
  Create ( malloc ) Edit Widget structure.

  Edit Theme graphic is taken from pTheme->Edit surface;
  Text is taken from 'pString16'.

  'length' parametr determinate width of Edit rect.

  This function determinate future size of Edit ( width, high ) and
  save this in: pWidget->size rectangle ( SDL_Rect )

  function return pointer to allocated Edit Widget.
**************************************************************************/
struct widget * create_edit(SDL_Surface *pBackground, SDL_Surface *pDest,
		SDL_String16 *pString16, Uint16 length, Uint32 flags)
{
  SDL_Rect buf = {0, 0, 0, 0};

  struct widget *pEdit = fc_calloc(1, sizeof(struct widget));

  pEdit->theme = pTheme->Edit;
  pEdit->gfx = pBackground;
  pEdit->string16 = pString16;
  set_wflag(pEdit, (WF_FREE_STRING | WF_FREE_GFX | flags));
  set_wstate(pEdit, FC_WS_DISABLED);
  set_wtype(pEdit, WT_EDIT);
  pEdit->mod = KMOD_NONE;
  
  if (pString16) {
    pEdit->string16->style |= SF_CENTER;
    buf = str16size(pString16);
    buf.h += adj_size(4);
  }

  length = MAX(length, buf.w + adj_size(10));

  correct_size_bcgnd_surf(pTheme->Edit, &length, &buf.h);

  pEdit->size.w = length;
  pEdit->size.h = buf.h;

  if(pDest) {
    pEdit->dst = pDest;
  } else {
    pEdit->dst = get_buffer_layer(pEdit->size.w, pEdit->size.h);
  }

  return pEdit;
}

/**************************************************************************
  set new x, y position and redraw edit.
**************************************************************************/
int draw_edit(struct widget *pEdit, Sint16 start_x, Sint16 start_y)
{
  pEdit->size.x = start_x;
  pEdit->size.y = start_y;

  if (get_wflags(pEdit) & WF_RESTORE_BACKGROUND) {
    refresh_widget_background(pEdit);
  }

  return redraw_edit(pEdit);
}

/**************************************************************************
  Create Edit Field surface ( with Text) and blit them to Main.screen,
  on position 'pEdit_Widget->size.x , pEdit_Widget->size.y'

  Graphic is taken from 'pEdit_Widget->theme'
  Text is taken from	'pEdit_Widget->sting16'

  if flag 'FW_DRAW_THEME_TRANSPARENT' is set theme will be blit
  transparent ( Alpha = 128 )

  function return Hight of created surfaces or (-1) if theme surface can't
  be created.
**************************************************************************/
int redraw_edit(struct widget *pEdit_Widget)
{
  if (get_wstate(pEdit_Widget) == FC_WS_PRESSED) {
    redraw_edit_chain((struct EDIT *)pEdit_Widget->data.ptr);
  } else {
    int iRet = 0;
    SDL_Rect rDest = {pEdit_Widget->size.x, pEdit_Widget->size.y, 0, 0};
    SDL_Surface *pEdit = NULL;
    SDL_Surface *pText;
  
    fix_rect(pEdit_Widget->dst, &rDest);
    
    if (pEdit_Widget->string16->text &&
    	get_wflags(pEdit_Widget) & WF_PASSWD_EDIT) {
      Uint16 *backup = pEdit_Widget->string16->text;
      size_t len = unistrlen(backup) + 1;
      char *cBuf = fc_calloc(1, len);
    
      memset(cBuf, '*', len - 1);
      cBuf[len - 1] = '\0';
      pEdit_Widget->string16->text = convert_to_utf16(cBuf);
      pText = create_text_surf_from_str16(pEdit_Widget->string16);
      FC_FREE(pEdit_Widget->string16->text);
      FC_FREE(cBuf);
      pEdit_Widget->string16->text = backup;
    } else {
      pText = create_text_surf_from_str16(pEdit_Widget->string16);
    }
  
    pEdit = create_bcgnd_surf(pEdit_Widget->theme, get_wstate(pEdit_Widget),
                              pEdit_Widget->size.w, pEdit_Widget->size.h);

    if (!pEdit) {
      return -1;
    }
    
    if (get_wflags(pEdit_Widget) & WF_RESTORE_BACKGROUND) {
      /* blit background */
      clear_surface(pEdit_Widget->dst, &rDest);
      alphablit(pEdit_Widget->gfx, NULL, pEdit_Widget->dst, &rDest);
    }

    /* blit theme */
    alphablit(pEdit, NULL, pEdit_Widget->dst, &rDest);

    /* set position and blit text */
    if (pText) {
      rDest.y += (pEdit->h - pText->h) / 2;
      /* blit centred text to botton */
      if (pEdit_Widget->string16->style & SF_CENTER) {
        rDest.x += (pEdit->w - pText->w) / 2;
      } else {
        if (pEdit_Widget->string16->style & SF_CENTER_RIGHT) {
	  rDest.x += pEdit->w - pText->w - adj_size(5);
        } else {
	  rDest.x += adj_size(5);		/* cennter left */
        }
      }

      alphablit(pText, NULL, pEdit_Widget->dst, &rDest);
    }
    /* pText */
    iRet = pEdit->h;

    /* Free memory */
    FREESURFACE(pText);
    FREESURFACE(pEdit);
    return iRet;
  }
  return 0;
}

/**************************************************************************
  This functions are pure madness :)
  Create Edit Field surface ( with Text) and blit them to Main.screen,
  on position 'pEdit_Widget->size.x , pEdit_Widget->size.y'

  Main role of this functions are been text input to GUI.
  This code allow you to add, del unichar from unistring.

  Graphic is taken from 'pEdit_Widget->theme'
  OldText is taken from	'pEdit_Widget->sting16'

  NewText is returned to 'pEdit_Widget->sting16' ( after free OldText )

  if flag 'FW_DRAW_THEME_TRANSPARENT' is set theme will be blit
  transparent ( Alpha = 128 )

  NOTE: This functions can return NULL in 'pEdit_Widget->sting16->text' but
        never free 'pEdit_Widget->sting16' struct.
**************************************************************************/
static Uint16 edit_key_down(SDL_keysym Key, void *pData)
{
  struct EDIT *pEdt = (struct EDIT *)pData;
  struct UniChar *pInputChain_TMP;
  bool Redraw = FALSE;
      
  /* find which key is pressed */
  switch (Key.sym) {
    case SDLK_ESCAPE:
      /* exit from loop without changes */
      return ED_ESC;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
      /* exit from loop */
      return ED_RETURN;
    case SDLK_KP6:
      if(Key.mod & KMOD_NUM) {
	goto INPUT;
      }
    case SDLK_RIGHT:
    {
      /* move cursor right */
      if (pEdt->pInputChain->next) {
	
       if (pEdt->InputChain_X >= (pEdt->pWidget->size.x + pEdt->pBg->w - adj_size(10))) {
	pEdt->Start_X -= pEdt->pInputChain->pTsurf->w -
		(pEdt->pWidget->size.x + pEdt->pBg->w - adj_size(5) - pEdt->InputChain_X);
       }

	pEdt->pInputChain = pEdt->pInputChain->next;
	Redraw = TRUE;
      }
    }
    break;
    case SDLK_KP4:
      if(Key.mod & KMOD_NUM) {
	goto INPUT;
      }
    case SDLK_LEFT:
    {
      /* move cursor left */
      if (pEdt->pInputChain->prev) {
        pEdt->pInputChain = pEdt->pInputChain->prev;
	if ((pEdt->InputChain_X <=
	       (pEdt->pWidget->size.x + adj_size(9))) && (pEdt->Start_X != adj_size(5))) {
	  if (pEdt->InputChain_X != (pEdt->pWidget->size.x + adj_size(5))) {
	      pEdt->Start_X += (pEdt->pWidget->size.x - pEdt->InputChain_X + adj_size(5));
	  }

	  pEdt->Start_X += (pEdt->pInputChain->pTsurf->w);
	}
	Redraw = TRUE;
      }
    }
    break;
    case SDLK_KP7:
      if(Key.mod & KMOD_NUM) {
	goto INPUT;
      }  
    case SDLK_HOME:
    {
      /* move cursor to begin of chain (and edit field) */
      pEdt->pInputChain = pEdt->pBeginTextChain;
      Redraw = TRUE;
      pEdt->Start_X = adj_size(5);
    }
    break;
    case SDLK_KP1:
      if(Key.mod & KMOD_NUM) {
	goto INPUT;
      }
    case SDLK_END:
    {
	/* move cursor to end of chain (and edit field) */
      pEdt->pInputChain = pEdt->pEndTextChain;
      Redraw = TRUE;

      if (pEdt->pWidget->size.w - pEdt->Truelength < 0) {
	  pEdt->Start_X = pEdt->pWidget->size.w - pEdt->Truelength - adj_size(5);
      }
    }
    break;
    case SDLK_BACKSPACE:
    {
	/* del element of chain (and move cursor left) */
      if (pEdt->pInputChain->prev) {

	if ((pEdt->InputChain_X <=
	       (pEdt->pWidget->size.x + adj_size(9))) && (pEdt->Start_X != adj_size(5))) {
	  if (pEdt->InputChain_X != (pEdt->pWidget->size.x + adj_size(5))) {
	      pEdt->Start_X += (pEdt->pWidget->size.x - pEdt->InputChain_X + adj_size(5));
	  }
	  pEdt->Start_X += (pEdt->pInputChain->prev->pTsurf->w);
	}

	if (pEdt->pInputChain->prev->prev) {
	  pEdt->pInputChain->prev->prev->next = pEdt->pInputChain;
	  pInputChain_TMP = pEdt->pInputChain->prev->prev;
	  pEdt->Truelength -= pEdt->pInputChain->prev->pTsurf->w;
	  FREESURFACE(pEdt->pInputChain->prev->pTsurf);
	  FC_FREE(pEdt->pInputChain->prev);
	  pEdt->pInputChain->prev = pInputChain_TMP;
	} else {
	  pEdt->Truelength -= pEdt->pInputChain->prev->pTsurf->w;
	  FREESURFACE(pEdt->pInputChain->prev->pTsurf);
	  FC_FREE(pEdt->pInputChain->prev);
	  pEdt->pBeginTextChain = pEdt->pInputChain;
	}
	
	pEdt->ChainLen--;
	Redraw = TRUE;
      }
    }
    break;
    case SDLK_KP_PERIOD:
      if(Key.mod & KMOD_NUM) {
	goto INPUT;
      }  
    case SDLK_DELETE:
    {
	/* del element of chain */
      if (pEdt->pInputChain->next && pEdt->pInputChain->prev) {
	pEdt->pInputChain->prev->next = pEdt->pInputChain->next;
	pEdt->pInputChain->next->prev = pEdt->pInputChain->prev;
	pInputChain_TMP = pEdt->pInputChain->next;
	pEdt->Truelength -= pEdt->pInputChain->pTsurf->w;
	FREESURFACE(pEdt->pInputChain->pTsurf);
	FC_FREE(pEdt->pInputChain);
	pEdt->pInputChain = pInputChain_TMP;
	pEdt->ChainLen--;
	Redraw = TRUE;
      }

      if (pEdt->pInputChain->next && !pEdt->pInputChain->prev) {
	pEdt->pInputChain = pEdt->pInputChain->next;
	pEdt->Truelength -= pEdt->pInputChain->prev->pTsurf->w;
	FREESURFACE(pEdt->pInputChain->prev->pTsurf);
	FC_FREE(pEdt->pInputChain->prev);
	pEdt->pBeginTextChain = pEdt->pInputChain;
	pEdt->ChainLen--;
	Redraw = TRUE;
      }
    }
    break;
    default:
    {
INPUT:/* add new element of chain (and move cursor right) */
      if (Key.unicode) {
	if (pEdt->pInputChain != pEdt->pBeginTextChain) {
	  pInputChain_TMP = pEdt->pInputChain->prev;
	  pEdt->pInputChain->prev = fc_calloc(1, sizeof(struct UniChar));
	  pEdt->pInputChain->prev->next = pEdt->pInputChain;
	  pEdt->pInputChain->prev->prev = pInputChain_TMP;
	  pInputChain_TMP->next = pEdt->pInputChain->prev;
	} else {
	  pEdt->pInputChain->prev = fc_calloc(1, sizeof(struct UniChar));
	  pEdt->pInputChain->prev->next = pEdt->pInputChain;
	  pEdt->pBeginTextChain = pEdt->pInputChain->prev;
	}
        
        pEdt->pInputChain->prev->chr[0] = Key.unicode;        
	pEdt->pInputChain->prev->chr[1] = '\0';

	if (pEdt->pInputChain->prev->chr) {
	  if (get_wflags(pEdt->pWidget) & WF_PASSWD_EDIT) {
	    Uint16 passwd_chr[2] = {'*', '\0'};
	    
	    pEdt->pInputChain->prev->pTsurf =
	      TTF_RenderUNICODE_Blended(pEdt->pWidget->string16->font,
					  passwd_chr,
					  pEdt->pWidget->string16->fgcol);
	  } else {
	    pEdt->pInputChain->prev->pTsurf =
	      TTF_RenderUNICODE_Blended(pEdt->pWidget->string16->font,
					  pEdt->pInputChain->prev->chr,
					  pEdt->pWidget->string16->fgcol);
	  }
	  pEdt->Truelength += pEdt->pInputChain->prev->pTsurf->w;
	}

	if (pEdt->InputChain_X >= pEdt->pWidget->size.x + pEdt->pBg->w - adj_size(10)) {
	  if (pEdt->pInputChain == pEdt->pEndTextChain) {
	    pEdt->Start_X = pEdt->pBg->w - adj_size(5) - pEdt->Truelength;
	  } else {
	    pEdt->Start_X -= pEdt->pInputChain->prev->pTsurf->w -
		  (pEdt->pWidget->size.x + pEdt->pBg->w - adj_size(5) - pEdt->InputChain_X);
	  }
	}
	
	pEdt->ChainLen++;
	Redraw = TRUE;
      }
    }
    break;
  }				/* key pressed switch */
    
  if (Redraw) {
    redraw_edit_chain(pEdt);
  }
    
  return ID_ERROR;
}

static Uint16 edit_mouse_button_down(SDL_MouseButtonEvent *pButtonEvent, void *pData)
{
  struct EDIT *pEdt = (struct EDIT *)pData;
    
  if (pButtonEvent->button == SDL_BUTTON_LEFT) {
    if (!(pButtonEvent->x >= pEdt->pWidget->size.x &&
              pButtonEvent->x < pEdt->pWidget->size.x + pEdt->pBg->w &&
              pButtonEvent->y >= pEdt->pWidget->size.y &&
              pButtonEvent->y < pEdt->pWidget->size.y + pEdt->pBg->h)) {
          /* exit from loop */
          return (Uint16)ED_MOUSE;
    }
  }
  return (Uint16)ID_ERROR;
}

enum Edit_Return_Codes edit_field(struct widget *pEdit_Widget)
{
  struct EDIT pEdt;
  struct UniChar ___last;
  struct UniChar *pInputChain_TMP = NULL;
  enum Edit_Return_Codes ret;
  void *backup = pEdit_Widget->data.ptr;
  
  pEdt.pWidget = pEdit_Widget;
  pEdt.ChainLen = 0;
  pEdt.Truelength = 0;
  pEdt.Start_X = adj_size(5);
  pEdt.InputChain_X = 0;
  
  pEdit_Widget->data.ptr = (void *)&pEdt;
  
  SDL_EnableUNICODE(1);

  SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);

  pEdt.pBg = create_bcgnd_surf(pEdit_Widget->theme, 2,
			       pEdit_Widget->size.w, pEdit_Widget->size.h);

  /* Creating Chain */
  pEdt.pBeginTextChain = text2chain(pEdit_Widget->string16->text);


  /* Creating Empty (Last) pice of Chain */
  pEdt.pInputChain = &___last;
  pEdt.pEndTextChain = pEdt.pInputChain;
  pEdt.pEndTextChain->chr[0] = 32;	/*spacebar */
  pEdt.pEndTextChain->chr[1] = 0;	/*spacebar */
  pEdt.pEndTextChain->next = NULL;
  pEdt.pEndTextChain->prev = NULL;
  
  /* set font style (if any ) */
  if (!((pEdit_Widget->string16->style & 0x0F) & TTF_STYLE_NORMAL)) {
    TTF_SetFontStyle(pEdit_Widget->string16->font,
		     (pEdit_Widget->string16->style & 0x0F));
  }


  pEdt.pEndTextChain->pTsurf =
      TTF_RenderUNICODE_Blended(pEdit_Widget->string16->font,
			      pEdt.pEndTextChain->chr,
			      pEdit_Widget->string16->fgcol);
  
  /* create surface for each font in chain and find chain length */
  if (pEdt.pBeginTextChain) {
    pInputChain_TMP = pEdt.pBeginTextChain;
    while (TRUE) {
      pEdt.ChainLen++;

      pInputChain_TMP->pTsurf =
	  TTF_RenderUNICODE_Blended(pEdit_Widget->string16->font,
				    pInputChain_TMP->chr,
				    pEdit_Widget->string16->fgcol);

      pEdt.Truelength += pInputChain_TMP->pTsurf->w;

      if (pInputChain_TMP->next == NULL) {
	break;
      }

      pInputChain_TMP = pInputChain_TMP->next;
    }
    /* set terminator of list */
    pInputChain_TMP->next = pEdt.pInputChain;
    pEdt.pInputChain->prev = pInputChain_TMP;
    pInputChain_TMP = NULL;
  } else {
    pEdt.pBeginTextChain = pEdt.pInputChain;
  }

  redraw_edit_chain(&pEdt);
  
  set_wstate(pEdit_Widget, FC_WS_PRESSED);
  {
    /* local loop */  
    Uint16 rety = gui_event_loop((void *)&pEdt, NULL,
  	edit_key_down, NULL, edit_mouse_button_down, NULL, NULL);
    
    if (pEdt.pBeginTextChain == pEdt.pEndTextChain) {
      pEdt.pBeginTextChain = NULL;
    }
    
    if (rety == MAX_ID) {
      ret = ED_FORCE_EXIT;
    } else {
      ret = (enum Edit_Return_Codes) rety;
      
      /* this is here becouse we have no knowladge that pEdit_Widget exist
         or nor in force exit mode from gui loop */
  
      /* reset font settings */
      if (!((pEdit_Widget->string16->style & 0x0F) & TTF_STYLE_NORMAL)) {
        TTF_SetFontStyle(pEdit_Widget->string16->font, TTF_STYLE_NORMAL);
      }
      
      if(ret != ED_ESC) {
        FC_FREE(pEdit_Widget->string16->text);
        pEdit_Widget->string16->text =
  	    chain2text(pEdt.pBeginTextChain, pEdt.ChainLen);
        pEdit_Widget->string16->n_alloc = (pEdt.ChainLen + 1) * sizeof(Uint16);
      }
      
      pEdit_Widget->data.ptr = backup;
      set_wstate(pEdit_Widget, FC_WS_NORMAL);    
    }
  }
  
  FREESURFACE(pEdt.pEndTextChain->pTsurf);
   
  del_chain(pEdt.pBeginTextChain);
  
  FREESURFACE(pEdt.pBg);
    
  /* disable repeate key */
  SDL_EnableKeyRepeat(0, SDL_DEFAULT_REPEAT_INTERVAL);

  /* disable Unicode */
  SDL_EnableUNICODE(0);
    
  return ret;
}

/* =================================================== */
/* ===================== VSCROOLBAR ================== */
/* =================================================== */

/*
 */
/**************************************************************************
  Create background image for vscrollbars
  then return pointer to this image.

  Graphic is taken from pVert_theme surface and blit to new created image.

  hight depend of 'High' parametr.

  Type of image depend of "state" parametr.
    state = 0 - normal
    state = 1 - selected
    state = 2 - pressed
    state = 3 - disabled
**************************************************************************/
static SDL_Surface *create_vertical_surface(SDL_Surface * pVert_theme,
					    Uint8 state, Uint16 High)
{
  SDL_Surface *pVerSurf = NULL;
  SDL_Rect src, des;

  Uint16 i;
  Uint16 start_y;

  Uint16 tile_count_midd;
  Uint8 tile_len_end;
  Uint8 tile_len_midd;


  tile_len_end = pVert_theme->h / 16;

  start_y = 0 + state * (pVert_theme->h / 4);

  /* tile_len_midd = pVert_theme->h/4  - tile_len_end*2; */
  tile_len_midd = pVert_theme->h / 4 - tile_len_end * 2;

  tile_count_midd = (High - tile_len_end * 2) / tile_len_midd;

  /* correction */
  if (tile_len_midd * tile_count_midd + tile_len_end * 2 < High)
    tile_count_midd++;

  if (!tile_count_midd) {
    pVerSurf = create_surf_alpha(pVert_theme->w, tile_len_end * 2, SDL_SWSURFACE);
  } else {
    pVerSurf = create_surf_alpha(pVert_theme->w, High, SDL_SWSURFACE);
  }

  src.x = 0;
  src.y = start_y;
  src.w = pVert_theme->w;
  src.h = tile_len_end;
  alphablit(pVert_theme, &src, pVerSurf, NULL);

  src.y = start_y + tile_len_end;
  src.h = tile_len_midd;

  des.x = 0;

  for (i = 0; i < tile_count_midd; i++) {
    des.y = tile_len_end + i * tile_len_midd;
    alphablit(pVert_theme, &src, pVerSurf, &des);
  }

  src.y = start_y + tile_len_end + tile_len_midd;
  src.h = tile_len_end;
  des.y = pVerSurf->h - tile_len_end;
  alphablit(pVert_theme, &src, pVerSurf, &des);

  return pVerSurf;
}

/**************************************************************************
  Create ( malloc ) VSrcrollBar Widget structure.

  Theme graphic is taken from pVert_theme surface;

  This function determinate future size of VScrollBar
  ( width = 'pVert_theme->w' , high = 'high' ) and
  save this in: pWidget->size rectangle ( SDL_Rect )

  function return pointer to allocated Widget.
**************************************************************************/
struct widget * create_vertical(SDL_Surface *pVert_theme, SDL_Surface *pDest,
  			Uint16 high, Uint32 flags)
{
  struct widget *pVer = fc_calloc(1, sizeof(struct widget));

  pVer->theme = pVert_theme;
  pVer->size.w = pVert_theme->w;
  pVer->size.h = high;
  set_wflag(pVer, (WF_FREE_STRING | flags));
  set_wstate(pVer, FC_WS_DISABLED);
  set_wtype(pVer, WT_VSCROLLBAR);
  pVer->mod = KMOD_NONE;
  pVer->dst = pDest;

  return pVer;
}

/**************************************************************************
  ...
**************************************************************************/
int redraw_vert(struct widget *pVert)
{
  int ret;
  SDL_Rect dest;
  
  dest = pVert->size;
  fix_rect(pVert->dst, &dest);
  
  SDL_Surface *pVert_Surf = create_vertical_surface(pVert->theme,
						    get_wstate(pVert),
						    pVert->size.h);
  ret =
      blit_entire_src(pVert_Surf, pVert->dst, dest.x, dest.y);
  
  FREESURFACE(pVert_Surf);

  return ret;
}

/**************************************************************************
  ...
**************************************************************************/
int draw_vert(struct widget *pVert, Sint16 x, Sint16 y)
{
  pVert->size.x = x;
  pVert->size.y = y;
  pVert->gfx = crop_rect_from_surface(pVert->dst, &pVert->size);
  return redraw_vert(pVert);
}

/* =================================================== */
/* ===================== HSCROOLBAR ================== */
/* =================================================== */

/**************************************************************************
  Create background image for hscrollbars
  then return	pointer to this image.

  Graphic is taken from pHoriz_theme surface and blit to new created image.

  hight depend of 'Width' parametr.

  Type of image depend of "state" parametr.
    state = 0 - normal
    state = 1 - selected
    state = 2 - pressed
    state = 3 - disabled
**************************************************************************/
static SDL_Surface *create_horizontal_surface(SDL_Surface * pHoriz_theme,
					      Uint8 state, Uint16 Width)
{
  SDL_Surface *pHorSurf = NULL;
  SDL_Rect src, des;

  Uint16 i;
  Uint16 start_x;

  Uint16 tile_count_midd;
  Uint8 tile_len_end;
  Uint8 tile_len_midd;

  tile_len_end = pHoriz_theme->w / 16;

  start_x = 0 + state * (pHoriz_theme->w / 4);

  tile_len_midd = pHoriz_theme->w / 4 - tile_len_end * 2;

  tile_count_midd = (Width - tile_len_end * 2) / tile_len_midd;

  /* correction */
  if (tile_len_midd * tile_count_midd + tile_len_end * 2 < Width) {
    tile_count_midd++;
  }

  if (!tile_count_midd) {
    pHorSurf = create_surf_alpha(tile_len_end * 2, pHoriz_theme->h, SDL_SWSURFACE);
  } else {
    pHorSurf = create_surf_alpha(Width, pHoriz_theme->h, SDL_SWSURFACE);
  }

  src.y = 0;
  src.x = start_x;
  src.h = pHoriz_theme->h;
  src.w = tile_len_end;
  alphablit(pHoriz_theme, &src, pHorSurf, NULL);

  src.x = start_x + tile_len_end;
  src.w = tile_len_midd;

  des.y = 0;

  for (i = 0; i < tile_count_midd; i++) {
    des.x = tile_len_end + i * tile_len_midd;
    alphablit(pHoriz_theme, &src, pHorSurf, &des);
  }

  src.x = start_x + tile_len_end + tile_len_midd;
  src.w = tile_len_end;
  des.x = pHorSurf->w - tile_len_end;
  alphablit(pHoriz_theme, &src, pHorSurf, &des);

  return pHorSurf;
}

/**************************************************************************
  Create ( malloc ) VSrcrollBar Widget structure.

  Theme graphic is taken from pVert_theme surface;

  This function determinate future size of VScrollBar
  ( width = 'pVert_theme->w' , high = 'high' ) and
  save this in: pWidget->size rectangle ( SDL_Rect )

  function return pointer to allocated Widget.
**************************************************************************/
struct widget * create_horizontal(SDL_Surface *pHoriz_theme, SDL_Surface *pDest,
  		Uint16 width, Uint32 flags)
{
  struct widget *pHor = fc_calloc(1, sizeof(struct widget));

  pHor->theme = pHoriz_theme;
  pHor->size.w = width;
  pHor->size.h = pHoriz_theme->h;
  set_wflag(pHor, WF_FREE_STRING | flags);
  set_wstate(pHor, FC_WS_DISABLED);
  set_wtype(pHor, WT_HSCROLLBAR);
  pHor->mod = KMOD_NONE;
  pHor->dst = pDest;
  
  return pHor;
}

/**************************************************************************
  ...
**************************************************************************/
int redraw_horiz(struct widget *pHoriz)
{
  SDL_Rect dest;
  
  dest = pHoriz->size;
  fix_rect(pHoriz->dst, &dest);
  
  SDL_Surface *pHoriz_Surf = create_horizontal_surface(pHoriz->theme,
						       get_wstate(pHoriz),
						       pHoriz->size.w);
  int ret = blit_entire_src(pHoriz_Surf, pHoriz->dst, dest.x, dest.y);
  FREESURFACE(pHoriz_Surf);

  return ret;
}

/**************************************************************************
  ...
**************************************************************************/
int draw_horiz(struct widget *pHoriz, Sint16 x, Sint16 y)
{
  pHoriz->size.x = x;
  pHoriz->size.y = y;
  pHoriz->gfx = crop_rect_from_surface(pHoriz->dst, &pHoriz->size);
  return redraw_horiz(pHoriz);
}

/* =================================================== */
/* ======================= WINDOWs ==================== */
/* =================================================== */

/**************************************************************************
  Window Menager Mechanism.
  Idea is simple each window/dialog has own buffer layer which is draw
  on screen during flush operations.
  This consume lots of memory but is extremly effecive.

  Each widget has own "destination" parm == where ( on what buffer )
  will be draw.
**************************************************************************/

/**************************************************************************
  Buffer allocation function.
  This function is call by "create_window(...)" function and allocate 
  buffer layer for this function.

  Pointer for this buffer is put in buffer array on last position that 
  flush functions will draw this layer last.
**************************************************************************/
SDL_Surface *get_buffer_layer(int width, int height)
{
  SDL_Surface *pBuffer;

  pBuffer = create_surf_alpha(/*Main.screen->w*/width, /*Main.screen->h*/height, SDL_SWSURFACE);
  
  /* add to buffers array */
  if (Main.guis) {
    int i;
    /* find NULL element */
    for(i = 0; i < Main.guis_count; i++) {
      if(!Main.guis[i]) {
        Main.guis[i] = gui_layer_new(0, 0, pBuffer);
	return pBuffer;
      }
    }
    Main.guis_count++;
    Main.guis = fc_realloc(Main.guis, Main.guis_count * sizeof(struct gui_layer *));
    Main.guis[Main.guis_count - 1] = gui_layer_new(0, 0, pBuffer);
  } else {
    Main.guis = fc_calloc(1, sizeof(struct gui_layer *));
    Main.guis[0] = gui_layer_new(0, 0, pBuffer);
    Main.guis_count = 1;
  }
  
  return pBuffer;
}

/**************************************************************************
  Free buffer layer ( call by popdown_window_group_dialog(...) funct )
  Funct. free buffer layer and cleare buffer array entry.
**************************************************************************/
static void remove_buffer_layer(SDL_Surface *pBuffer)
{
  int i;
  
  for(i = 0; i < Main.guis_count - 1; i++) {
    if(Main.guis[i] && (Main.guis[i]->surface == pBuffer)) {
      gui_layer_destroy(&Main.guis[i]);
      Main.guis[i] = Main.guis[i + 1];
      Main.guis[i + 1] = NULL;
    } else {
      if(!Main.guis[i]) {
	Main.guis[i] = Main.guis[i + 1];
        Main.guis[i + 1] = NULL;
      }
    }
  }

  if (Main.guis[Main.guis_count - 1]) {
    gui_layer_destroy(&Main.guis[Main.guis_count - 1]);
  }
}

/**************************************************************************
	Window mechanism.

	Active Window schould be first on list (All Widgets on this
	Widndow that are on List must be above)

	LIST:

	*pFirst_Widget_on_Active_Window.

	*pN__Widget_on_Active_Window.
	*pActive_Window. <------
	*pRest_Widgets.

	This trick give us:
	-	if any Widget is under ( area of ) this Window and Mouse
		cursor is above them, 'WidgetListScaner(...)' return
		pointer to Active Window not to this Widget.
**************************************************************************/

/**************************************************************************
  Allocate Widow Widget Structute.
  Text to titelbar is taken from 'pTitle'.
**************************************************************************/
struct widget * create_window(SDL_Surface *pDest, SDL_String16 *pTitle, 
  			Uint16 w, Uint16 h, Uint32 flags)
{
  struct widget *pWindow = fc_calloc(1, sizeof(struct widget));

  pWindow->string16 = pTitle;
  set_wflag(pWindow, WF_FREE_STRING | WF_FREE_GFX | WF_FREE_THEME |
				  WF_DRAW_FRAME_AROUND_WIDGET| flags);
  set_wstate(pWindow, FC_WS_DISABLED);
  set_wtype(pWindow, WT_WINDOW);
  pWindow->mod = KMOD_NONE;
  if(pDest) {
    pWindow->dst = pDest;
  } else {
    pWindow->dst = get_buffer_layer(w, h);
  }

  if (pTitle) {
    SDL_Rect size = str16size(pTitle);
    w = MAX(w, size.w + adj_size(10));
    h = MAX(h, size.h);
    h = MAX(h, WINDOW_TITLE_HEIGHT + 1);
  }

  pWindow->size.w = w;
  pWindow->size.h = h;

  return pWindow;
}

/**************************************************************************
  Resize Window 'pWindow' to 'new_w' and 'new_h'.
  and refresh window background ( save screen under window ).

  If pBcgd == NULL then theme is set to
  white transparent ( ALPHA = 128 ).

  Return 1 if allocate new surface and 0 if used 'pBcgd' surface.

  Exp.
  if ( resize_window( pWindow , pBcgd , new_w , new_h ) ) {
    FREESURFACE( pBcgd );
  }
**************************************************************************/
int resize_window(struct widget *pWindow,
		  SDL_Surface * pBcgd,
		  SDL_Color * pColor, Uint16 new_w, Uint16 new_h)
{
  struct gui_layer *gui_layer;  
  struct widget *pWidget;

  /* window */
  
  pWindow->size.w = new_w;
  pWindow->size.h = new_h;

  refresh_widget_background(pWindow);

  /* allocate new buffer */
  if (pWindow->dst != Main.gui) {
    gui_layer = get_gui_layer(pWindow->dst);
    FREESURFACE(gui_layer->surface);
    gui_layer->surface = create_surf_alpha(/*Main.screen->w*/pWindow->size.w,
                                           /*Main.screen->h*/pWindow->size.h,
                                                               SDL_SWSURFACE);
    /* assign new buffer to all widgets on this window */
    pWidget = pWindow;
    while(pWidget) {
      pWidget->dst = gui_layer->surface;
      pWidget = pWidget->prev;
    }
  }

  if (pWindow->theme != pBcgd) {
    FREESURFACE(pWindow->theme);
  }

  if (pBcgd) {
    if (pBcgd->w != new_w || pBcgd->h != new_h) {
      pWindow->theme = ResizeSurface(pBcgd, new_w, new_h, 2);
      return 1;
    } else {
      pWindow->theme = pBcgd;
      return 0;
    }
  }

  pBcgd = create_surf_alpha(new_w, new_h, SDL_SWSURFACE);
  
  pWindow->theme = pBcgd;
  
  if (!pColor) {
    SDL_Color color = { 255, 255, 255, 128 };
    pColor = &color;
  }

  SDL_FillRect(pWindow->theme, NULL, map_rgba(pWindow->theme->format, *pColor));

  return 1;
}

void set_window_pos(struct widget *pWindow, int x, int y)
{
  struct gui_layer *gui_layer;
  
  pWindow->size.x = x;
  pWindow->size.y = y;
  
  if (pWindow->dst != Main.gui) {
    gui_layer = get_gui_layer(pWindow->dst);
    gui_layer->dest_rect = pWindow->size;
  }
}

/**************************************************************************
...
**************************************************************************/
static Uint16 move_window_motion(SDL_MouseMotionEvent *pMotionEvent, void *pData)
{
  struct MOVE *pMove = (struct MOVE *)pData;
  
  if (!pMove->moved) {
    pMove->moved = TRUE;
  }
  
  sdl_dirty_rect(pMove->pWindow->size);
  
  pMove->pWindow->size.x += pMotionEvent->xrel;
  pMove->pWindow->size.y += pMotionEvent->yrel;
  set_window_pos(pMove->pWindow, pMove->pWindow->size.x,
                                 pMove->pWindow->size.y);
  
  sdl_dirty_rect(pMove->pWindow->size);
  flush_dirty();
  
  return ID_ERROR;
}

/**************************************************************************
...
**************************************************************************/
static Uint16 move_window_button_up(SDL_MouseButtonEvent * pButtonEvent, void *pData)
{
  struct MOVE *pMove = (struct MOVE *)pData;

  if (pMove && pMove->moved) {
    return (Uint16)ID_MOVED_WINDOW;
  }
  
  return (Uint16)ID_WINDOW;
}


/**************************************************************************
  ...
**************************************************************************/
bool move_window(struct widget *pWindow)
{
  bool ret;
  struct MOVE pMove;
  pMove.pWindow = pWindow;
  pMove.moved = FALSE;
  /* Filter mouse motion events */
  SDL_SetEventFilter(FilterMouseMotionEvents);
  ret = (gui_event_loop((void *)&pMove, NULL, NULL, NULL, NULL,
	  move_window_button_up, move_window_motion) == ID_MOVED_WINDOW);
  /* Turn off Filter mouse motion events */
  SDL_SetEventFilter(NULL);
  return ret;
}

/**************************************************************************
  Redraw Window Graphic ( without other Widgets )
**************************************************************************/
int redraw_window(struct widget *pWindow)
{
  SDL_Color title_bg_color = {255, 255, 255, 200};
  
  SDL_Surface *pTmp = NULL;
  SDL_Rect dst = pWindow->size;

  fix_rect(pWindow->dst, &dst);

  /* Draw theme */
  clear_surface(pWindow->dst, &dst);
  alphablit(pWindow->theme, NULL, pWindow->dst, &dst);

  /* window has title string == has title bar */
  if (pWindow->string16) {
    /* Draw Window's TitelBar */
    dst.h = WINDOW_TITLE_HEIGHT;
    SDL_FillRectAlpha(pWindow->dst, &dst, &title_bg_color);
    
    /* Draw Text on Window's TitelBar */
    pTmp = create_text_surf_from_str16(pWindow->string16);
    dst.x += 10;
    if(pTmp) {
      dst.y += ((WINDOW_TITLE_HEIGHT - pTmp->h) / 2);
      alphablit(pTmp, NULL, pWindow->dst, &dst);
      FREESURFACE(pTmp);
    }

    dst = pWindow->size;    
    fix_rect(pWindow->dst, &dst);
    
    putline(pWindow->dst, dst.x + pTheme->FR_Left->w,
	  dst.y + WINDOW_TITLE_HEIGHT,
	  dst.x + pWindow->size.w - pTheme->FR_Right->w,
	  dst.y + WINDOW_TITLE_HEIGHT, 
          map_rgba(pWindow->dst->format, 
           *get_game_colorRGB(COLOR_THEME_WINDOW_TITLEBAR_SEPARATOR)));    
  }
  
  return 0;
}

/* =================================================== */
/* ======================== MISC ===================== */
/* =================================================== */

/**************************************************************************
  Draw Themed Frame.
**************************************************************************/
void draw_frame(SDL_Surface * pDest, Sint16 start_x, Sint16 start_y,
		Uint16 w, Uint16 h)
{
  SDL_Surface *pTmpLeft = 
    ResizeSurface(pTheme->FR_Left, pTheme->FR_Left->w, h, 1);
  SDL_Surface *pTmpRight = 
    ResizeSurface(pTheme->FR_Right, pTheme->FR_Right->w, h, 1);
  SDL_Surface *pTmpTop =
    ResizeSurface(pTheme->FR_Top, w, pTheme->FR_Top->h, 1);
  SDL_Surface *pTmpBottom =
    ResizeSurface(pTheme->FR_Bottom, w, pTheme->FR_Bottom->h, 1);
  
  SDL_Rect tmp,dst = {start_x, start_y, 0, 0};

  tmp = dst;
  alphablit(pTmpLeft, NULL, pDest, &tmp);
  
  dst.x += w - pTmpRight->w;
  tmp = dst;
  alphablit(pTmpRight, NULL, pDest, &tmp);

  dst.x = start_x;
  tmp = dst;
  alphablit(pTmpTop, NULL, pDest, &tmp);
  
  dst.y += h - pTmpBottom->h;
  tmp = dst;
  alphablit(pTmpBottom, NULL, pDest, &tmp);

  FREESURFACE(pTmpLeft);
  FREESURFACE(pTmpRight);
  FREESURFACE(pTmpTop);
  FREESURFACE(pTmpBottom);
}

/**************************************************************************
  ...
**************************************************************************/
void refresh_widget_background(struct widget *pWidget)
{
  SDL_Rect dest;
  
  if (pWidget) {
    dest = pWidget->size;
    fix_rect(pWidget->dst, &dest);    

    if (pWidget->gfx && pWidget->gfx->w == pWidget->size.w &&
      				pWidget->gfx->h == pWidget->size.h) {
      clear_surface(pWidget->gfx, NULL);
      alphablit(pWidget->dst, &dest, pWidget->gfx, NULL);
    } else {
      FREESURFACE(pWidget->gfx);
      pWidget->gfx = crop_rect_from_surface(pWidget->dst, &dest);
    }
  }
}

/* =================================================== */
/* ======================= LABELs ==================== */
/* =================================================== */

/**************************************************************************
  ...
**************************************************************************/
void remake_label_size(struct widget *pLabel)
{
  SDL_Surface *pIcon = pLabel->theme;
  SDL_String16 *pText = pLabel->string16;
  Uint32 flags = get_wflags(pLabel);
  SDL_Rect buf = { 0, 0, 0, 0 };
  Uint16 w = 0, h = 0, space;

  if (flags & WF_DRAW_TEXT_LABEL_WITH_SPACE) {
    space = adj_size(10);
  } else {
    space = 0;
  }

  if (pText) {
    bool without_box = ((get_wflags(pLabel) & WF_SELLECT_WITHOUT_BAR) == WF_SELLECT_WITHOUT_BAR);
    bool bold = TRUE;
    
    if (without_box)
    {
      bold = ((pText->style & TTF_STYLE_BOLD) == TTF_STYLE_BOLD);
      pText->style |= TTF_STYLE_BOLD;
    }
    
    buf = str16size(pText);

    if (without_box && !bold)
    {
      pText->style &= ~TTF_STYLE_BOLD;
    }
    
    w = MAX(w, buf.w + space);
    h = MAX(h, buf.h);
  }

  if (pIcon) {
    if (pText) {
      if ((flags & WF_ICON_UNDER_TEXT) || (flags & WF_ICON_ABOVE_TEXT)) {
	w = MAX(w, pIcon->w + space);
	h = MAX(h, buf.h + pIcon->h + adj_size(3));
      } else {
	if (flags & WF_ICON_CENTER) {
	  w = MAX(w, pIcon->w + space);
	  h = MAX(h, pIcon->h);
	} else {
	  w = MAX(w, buf.w + pIcon->w + adj_size(5) + space);
	  h = MAX(h, pIcon->h);
	}
      }
    } /* pText */
    else {
      w = MAX(w, pIcon->w + space);
      h = MAX(h, pIcon->h);
    }
  }

  /* pIcon */
  pLabel->size.w = w;
  pLabel->size.h = h;
}

/**************************************************************************
  ThemeLabel is String16 with Background ( pIcon ).
**************************************************************************/
struct widget * create_themelabel(SDL_Surface *pIcon, SDL_Surface *pDest,
  		SDL_String16 *pText, Uint16 w, Uint16 h, Uint32 flags)
{
  struct widget *pLabel = NULL;
  
  if (!pIcon && !pText) {
    return NULL;
  }

  pLabel = fc_calloc(1, sizeof(struct widget));
  pLabel->theme = pIcon;
  pLabel->string16 = pText;
  set_wflag(pLabel,
	    (WF_ICON_CENTER | WF_FREE_STRING | WF_FREE_GFX |
	     WF_RESTORE_BACKGROUND | flags));
  set_wstate(pLabel, FC_WS_DISABLED);
  set_wtype(pLabel, WT_T_LABEL);
  pLabel->mod = KMOD_NONE;
  pLabel->dst = pDest;
  
  remake_label_size(pLabel);

  pLabel->size.w = MAX(pLabel->size.w, w);
  pLabel->size.h = MAX(pLabel->size.h, h);
  
  return pLabel;
}

/**************************************************************************
  this Label is String16 with Icon.
**************************************************************************/
struct widget * create_iconlabel(SDL_Surface *pIcon, SDL_Surface *pDest,
  		SDL_String16 *pText, Uint32 flags)
{
  struct widget *pILabel = NULL;

  pILabel = fc_calloc(1, sizeof(struct widget));

  pILabel->theme = pIcon;
  pILabel->string16 = pText;
  set_wflag(pILabel, WF_FREE_STRING | WF_FREE_GFX | flags);
  set_wstate(pILabel, FC_WS_DISABLED);
  set_wtype(pILabel, WT_I_LABEL);
  pILabel->mod = KMOD_NONE;
  pILabel->dst = pDest;
  
  remake_label_size(pILabel);

  return pILabel;
}

/**************************************************************************
  ThemeLabel is String16 with Background ( pIcon ).
**************************************************************************/
struct widget * create_themelabel2(SDL_Surface *pIcon, SDL_Surface *pDest,
  		SDL_String16 *pText, Uint16 w, Uint16 h, Uint32 flags)
{
  struct widget *pLabel = NULL;
  SDL_Surface *pBuf = NULL, *pTheme = NULL;
  SDL_Rect area;
  SDL_Color store = {0, 0, 0, 0};
  SDL_Color bg_color = *get_game_colorRGB(COLOR_THEME_THEMELABEL2_BG);
  Uint32 colorkey;
  
  if (!pIcon && !pText) {
    return NULL;
  }

  pLabel = fc_calloc(1, sizeof(struct widget));
  pLabel->theme = pIcon;
  pLabel->string16 = pText;
  set_wflag(pLabel, (WF_FREE_THEME | WF_FREE_STRING | WF_FREE_GFX | flags));
  set_wstate(pLabel, FC_WS_DISABLED);
  set_wtype(pLabel, WT_T2_LABEL);
  pLabel->mod = KMOD_NONE;
  
  
  remake_label_size(pLabel);

  pLabel->size.w = MAX(pLabel->size.w, w);
  pLabel->size.h = MAX(pLabel->size.h, h);
  
  pBuf = create_surf_alpha(pLabel->size.w, pLabel->size.h * 2, SDL_SWSURFACE);
    
  if(flags & WF_RESTORE_BACKGROUND) {
    pTheme = SDL_DisplayFormatAlpha(pBuf);
    FREESURFACE(pBuf);
  } else {
    pTheme = pBuf;
  }
  
  colorkey = SDL_MapRGBA(pTheme->format, pText->bgcol.r,
  		pText->bgcol.g, pText->bgcol.b, pText->bgcol.unused);
  SDL_FillRect(pTheme, NULL, colorkey);
    
  pLabel->size.x = 0;
  pLabel->size.y = 0;
  area = pLabel->size;
  pLabel->dst = pTheme;
  
  /* normal */
  redraw_iconlabel(pLabel);
  
  /* sellected */
  area.x = 0;
  area.y = pLabel->size.h;
    
  if(flags & WF_RESTORE_BACKGROUND) {
    SDL_FillRect(pTheme, &area, map_rgba(pTheme->format, bg_color));
    store = pText->bgcol;
    SDL_GetRGBA(getpixel(pTheme, area.x , area.y), pTheme->format,
	      &pText->bgcol.r, &pText->bgcol.g,
      		&pText->bgcol.b, &pText->bgcol.unused);
  } else {
    SDL_FillRectAlpha(pTheme, &area, &bg_color);
  }
  
  pLabel->size.y = pLabel->size.h;
  redraw_iconlabel(pLabel);
  
  if(flags & WF_RESTORE_BACKGROUND) {
    pText->bgcol = store;
  }
  
  pLabel->size.x = 0;
  pLabel->size.y = 0;
  if(flags & WF_FREE_THEME) {
    FREESURFACE(pLabel->theme);
  }
  pLabel->theme = pTheme;
  pLabel->dst = pDest;
  return pLabel;
}

struct widget * convert_iconlabel_to_themeiconlabel2(struct widget *pIconLabel)
{
  SDL_Rect start, area;
  SDL_Color store = {0, 0, 0, 0};
  SDL_Color bg_color = *get_game_colorRGB(COLOR_THEME_THEMELABEL2_BG);
  Uint32 colorkey, flags = get_wflags(pIconLabel);
  SDL_Surface *pDest, *pTheme, *pBuf = create_surf_alpha(pIconLabel->size.w,
				  pIconLabel->size.h * 2, SDL_SWSURFACE);
  
  if(flags & WF_RESTORE_BACKGROUND) {
    pTheme = SDL_DisplayFormatAlpha(pBuf);
    FREESURFACE(pBuf);
  } else {
    pTheme = pBuf;
  }
  
  colorkey = SDL_MapRGBA(pTheme->format, pIconLabel->string16->bgcol.r,
  		pIconLabel->string16->bgcol.g,
		pIconLabel->string16->bgcol.b,
		pIconLabel->string16->bgcol.unused);
  SDL_FillRect(pTheme, NULL, colorkey);
  
  start = pIconLabel->size;
  pIconLabel->size.x = 0;
  pIconLabel->size.y = 0;
  area = start;
  pDest = pIconLabel->dst;
  pIconLabel->dst = pTheme;
  
  /* normal */
  redraw_iconlabel(pIconLabel);
  
  /* sellected */
  area.x = 0;
  area.y = pIconLabel->size.h;
    
  if(flags & WF_RESTORE_BACKGROUND) {
    SDL_FillRect(pTheme, &area, map_rgba(pTheme->format, bg_color));
    store = pIconLabel->string16->bgcol;
    SDL_GetRGBA(getpixel(pTheme, area.x , area.y), pTheme->format,
	      &pIconLabel->string16->bgcol.r, &pIconLabel->string16->bgcol.g,
      		&pIconLabel->string16->bgcol.b,
			&pIconLabel->string16->bgcol.unused);
  } else {
    SDL_FillRectAlpha(pTheme, &area, &bg_color);
  }
  
  pIconLabel->size.y = pIconLabel->size.h;
  redraw_iconlabel(pIconLabel);
  
  if(flags & WF_RESTORE_BACKGROUND) {
    pIconLabel->string16->bgcol = store;
  }
  
  pIconLabel->size = start;
  if(flags & WF_FREE_THEME) {
    FREESURFACE(pIconLabel->theme);
  }
  pIconLabel->theme = pTheme;
  if(flags & WF_FREE_STRING) {
    FREESTRING16(pIconLabel->string16);
  }
  pIconLabel->dst = pDest;
  set_wtype(pIconLabel, WT_T2_LABEL);
  return pIconLabel;
}

#if 0
/**************************************************************************
  ...
**************************************************************************/
static int redraw_themelabel(struct widget *pLabel)
{
  int ret;
  Sint16 x, y;
  SDL_Surface *pText = NULL;
  SDL_Rect dest;

  if (!pLabel) {
    return -3;
  }

  if ((pText = create_text_surf_from_str16(pLabel->string16)) == NULL) {
    return (-4);
  }

  if (pLabel->string16->style & SF_CENTER) {
    x = (pLabel->size.w - pText->w) / 2;
  } else {
    if (pLabel->string16->style & SF_CENTER_RIGHT) {
      x = pLabel->size.w - pText->w - adj_size(5);
    } else {
      x = adj_size(5);
    }
  }

  y = (pLabel->size.h - pText->h) / 2;

  /* redraw theme */
  if (pLabel->theme) {
    dest = pLabel->size;
    fix_rect(pLabel->dst, &dest);
    ret = blit_entire_src(pLabel->theme, pLabel->dst, dest.x, dest.y);
    if (ret) {
      return ret;
    }
  }

  dest = pLabel->size;
  fix_rect(pLabel->dst, &dest);
  ret = blit_entire_src(pText, pLabel->dst, dest.x + x, dest.y + y);

  FREESURFACE(pText);

  return ret;
}
#endif

/**************************************************************************
  ...
**************************************************************************/
static inline int redraw_themelabel2(struct widget *pLabel)
{
    
  SDL_Rect src = {0,0, pLabel->size.w, pLabel->size.h};
  SDL_Rect dst = {pLabel->size.x, pLabel->size.y, 0, 0};
/* 
  if (!pLabel) {
    return -3;
  }
*/
  if(get_wstate(pLabel) == FC_WS_SELLECTED) {
    src.y = pLabel->size.h;
  }

  fix_rect(pLabel->dst, &dst);
  
  return alphablit(pLabel->theme, &src, pLabel->dst, &dst);
}

/**************************************************************************
  ...
**************************************************************************/
static int redraw_iconlabel(struct widget *pLabel)
{
  int space, ret = 0; /* FIXME: possibly uninitialized */
  Sint16 x, xI, yI;
  Sint16 y = 0; /* FIXME: possibly uninitialized */
  SDL_Surface *pText;
  SDL_Rect dst;
  Uint32 flags;

  if (!pLabel) {
    return -3;
  }

  dst = pLabel->size;
  fix_rect(pLabel->dst, &dst);

  SDL_SetClipRect(pLabel->dst, &dst);
  
  flags = get_wflags(pLabel);

  if (flags & WF_DRAW_TEXT_LABEL_WITH_SPACE) {
    space = adj_size(5);
  } else {
    space = 0;
  }

  pText = create_text_surf_from_str16(pLabel->string16);
  
  if (pLabel->theme) {		/* Icon */
    if (pText) {
      if (flags & WF_ICON_CENTER_RIGHT) {
	xI = pLabel->size.w - pLabel->theme->w - space;
      } else {
	if (flags & WF_ICON_CENTER) {
	  xI = (pLabel->size.w - pLabel->theme->w) / 2;
	} else {
	  xI = space;
	}
      }

      if (flags & WF_ICON_ABOVE_TEXT) {
	yI = 0;
	y = pLabel->theme->h + adj_size(3)
	    + (pLabel->size.h - (pLabel->theme->h + adj_size(3)) - pText->h) / 2;
      } else {
	if (flags & WF_ICON_UNDER_TEXT) {
	  y = (pLabel->size.h - (pLabel->theme->h + adj_size(3)) - pText->h) / 2;
	  yI = y + pText->h + adj_size(3);
	} else {
	  yI = (pLabel->size.h - pLabel->theme->h) / 2;
	  y = (pLabel->size.h - pText->h) / 2;
	}
      }
    } /* pText */
    else {
#if 0
      yI = (pLabel->size.h - pLabel->theme->h) / 2;
      xI = (pLabel->size.w - pLabel->theme->w) / 2;
#endif
      yI = 0;
      xI = space;
    }

    dst.x = pLabel->size.x + xI;
    dst.y = pLabel->size.y + yI;
    fix_rect(pLabel->dst, &dst);
    ret = alphablit(pLabel->theme, NULL, pLabel->dst, &dst);
    
    if (ret) {
      return ret - 10;
    }
  }

  if (pText) {
    if (pLabel->theme) { /* Icon */
      if (!(flags & WF_ICON_ABOVE_TEXT) && !(flags & WF_ICON_UNDER_TEXT)) {
	if (flags & WF_ICON_CENTER_RIGHT) {
	  if (pLabel->string16->style & SF_CENTER) {
	    x = (pLabel->size.w - (pLabel->theme->w + 5 + space) -
		 pText->w) / 2;
	  } else {
	    if (pLabel->string16->style & SF_CENTER_RIGHT) {
	      x = pLabel->size.w - (pLabel->theme->w + 5 + space) - pText->w;
	    } else {
	      x = space;
	    }
	  }
	} /* WF_ICON_CENTER_RIGHT */
	else {
	  if (flags & WF_ICON_CENTER) {
	    /* text is blit on icon */
	    goto Alone;
	  } else {		/* WF_ICON_CENTER_LEFT */
	    if (pLabel->string16->style & SF_CENTER) {
	      x = space + pLabel->theme->w + adj_size(5) + ((pLabel->size.w -
						   (space +
						    pLabel->theme->w + adj_size(5)) -
						   pText->w) / 2);
	    } else {
	      if (pLabel->string16->style & SF_CENTER_RIGHT) {
		x = pLabel->size.w - pText->w - space;
	      } else {
		x = space + pLabel->theme->w + adj_size(5);
	      }
	    }
	  }			/* WF_ICON_CENTER_LEFT */
	}
      } /* !WF_ICON_ABOVE_TEXT && !WF_ICON_UNDER_TEXT */
      else {
	goto Alone;
      }
    } /* pLabel->theme == Icon */
    else {
      y = (pLabel->size.h - pText->h) / 2;
    Alone:
      if (pLabel->string16->style & SF_CENTER) {
	x = (pLabel->size.w - pText->w) / 2;
      } else {
	if (pLabel->string16->style & SF_CENTER_RIGHT) {
	  x = pLabel->size.w - pText->w - space;
	} else {
	  x = space;
	}
      }
    }

    dst.x = pLabel->size.x + x;
    dst.y = pLabel->size.y + y;
    fix_rect(pLabel->dst, &dst);
    ret = alphablit(pText, NULL, pLabel->dst, &dst);
    FREESURFACE(pText);

  }

  SDL_SetClipRect(pLabel->dst, NULL);
  return ret;
}

/**************************************************************************
  ...
**************************************************************************/
int redraw_label(struct widget *pLabel)
{
  int ret;
  SDL_Rect area = pLabel->size;
  SDL_Color bar_color = *get_game_colorRGB(COLOR_THEME_LABEL_BAR);
  SDL_Color backup_color = {0, 0, 0, 0};
  
  fix_rect(pLabel->dst, &area);
  /* if label transparen then clear background under widget
   * or save this background */
  if (get_wflags(pLabel) & WF_RESTORE_BACKGROUND) {
    if (pLabel->gfx) {
      clear_surface(pLabel->dst, &area);
      alphablit(pLabel->gfx, NULL, pLabel->dst, &area);
    } else {
      pLabel->gfx = crop_rect_from_surface(pLabel->dst, &pLabel->size);
    }
  }

  if(get_wtype(pLabel) == WT_T2_LABEL) {
    return redraw_themelabel2(pLabel);
  }
  
  /* redraw sellect bar */
  if (get_wstate(pLabel) == FC_WS_SELLECTED) {
    
    if(get_wflags(pLabel) & WF_SELLECT_WITHOUT_BAR) {
      if (pLabel->string16) {
        backup_color = pLabel->string16->fgcol;
        pLabel->string16->fgcol = bar_color;
	if(pLabel->string16->style & TTF_STYLE_BOLD) {
	  pLabel->string16->style |= TTF_STYLE_UNDERLINE;
	} else {
	  pLabel->string16->style |= TTF_STYLE_BOLD;
	}
      }
    } else {
      SDL_FillRectAlpha(pLabel->dst, &area, &bar_color);
            
      if (pLabel->string16 && (pLabel->string16->render == 3)) {
        backup_color = pLabel->string16->bgcol;
        SDL_GetRGBA(getpixel(pLabel->dst, area.x , area.y), pLabel->dst->format,
	      &pLabel->string16->bgcol.r, &pLabel->string16->bgcol.g,
      		&pLabel->string16->bgcol.b, &pLabel->string16->bgcol.unused);
      }
    }
  }

  /* redraw icon label */
  ret = redraw_iconlabel(pLabel);
  
  if ((get_wstate(pLabel) == FC_WS_SELLECTED) && (pLabel->string16)) {
    if(get_wflags(pLabel) & WF_SELLECT_WITHOUT_BAR) {
      if (pLabel->string16->style & TTF_STYLE_UNDERLINE) {
	pLabel->string16->style &= ~TTF_STYLE_UNDERLINE;
      } else {
	pLabel->string16->style &= ~TTF_STYLE_BOLD;
      }
      pLabel->string16->fgcol = backup_color;
    } else {
      if(pLabel->string16->render == 3) {
	pLabel->string16->bgcol = backup_color;
      }
    } 
  }
  
  return ret;
}

/**************************************************************************
  ...
**************************************************************************/
int draw_label(struct widget *pLabel, Sint16 start_x, Sint16 start_y)
{
  pLabel->size.x = start_x;
  pLabel->size.y = start_y;
  return redraw_label(pLabel);
}

/* =================================================== */
/* ======================= CHECK BOX ================= */
/* =================================================== */

/**************************************************************************
  ...
**************************************************************************/
struct widget *create_checkbox(SDL_Surface *pDest, bool state, Uint32 flags)
{
  struct widget *pCBox = fc_calloc(1, sizeof(struct widget));
  struct CHECKBOX *pTmp = fc_calloc(1, sizeof(struct CHECKBOX));
    
  if (state) {
    pCBox->theme = pTheme->CBOX_Sell_Icon;
  } else {
    pCBox->theme = pTheme->CBOX_Unsell_Icon;
  }

  set_wflag(pCBox, (WF_FREE_STRING | WF_FREE_GFX | WF_FREE_PRIVATE_DATA | flags));
  set_wstate(pCBox, FC_WS_DISABLED);
  set_wtype(pCBox, WT_CHECKBOX);
  pCBox->mod = KMOD_NONE;
  pCBox->dst = pDest;
  pTmp->state = state;
  pTmp->pTRUE_Theme = pTheme->CBOX_Sell_Icon;
  pTmp->pFALSE_Theme = pTheme->CBOX_Unsell_Icon;
  pCBox->private_data.cbox = pTmp;
  
  pCBox->size.w = pCBox->theme->w / 4;
  pCBox->size.h = pCBox->theme->h;

  return pCBox;
}

/**************************************************************************
  ...
**************************************************************************/
struct widget * create_textcheckbox(SDL_Surface *pDest, bool state,
		  SDL_String16 *pStr, Uint32 flags)
{
  struct widget *pCBox;
  struct CHECKBOX *pTmp;
  SDL_Surface *pSurf, *pIcon;

  if (!pStr) {
    return create_checkbox(pDest, state, flags);
  }
  
  pTmp = fc_calloc(1, sizeof(struct CHECKBOX));
    
  if (state) {
    pSurf = pTheme->CBOX_Sell_Icon;
  } else {
    pSurf = pTheme->CBOX_Unsell_Icon;
  }
    
  pIcon = create_icon_from_theme(pSurf, 0);
  pCBox = create_iconlabel(pIcon, pDest, pStr, (flags | WF_FREE_PRIVATE_DATA));

  pStr->style &= ~SF_CENTER;

  pCBox->theme = pSurf;
  FREESURFACE(pIcon);

  set_wtype(pCBox, WT_TCHECKBOX);
  pTmp->state = state;
  pTmp->pTRUE_Theme = pTheme->CBOX_Sell_Icon;
  pTmp->pFALSE_Theme = pTheme->CBOX_Unsell_Icon;
  pCBox->private_data.cbox = pTmp;
  
  return pCBox;
}

int set_new_checkbox_theme(struct widget *pCBox ,
				SDL_Surface *pTrue, SDL_Surface *pFalse)
{
  struct CHECKBOX *pTmp;
  
  if(!pCBox || (get_wtype(pCBox) != WT_CHECKBOX)) {
    return -1;
  }
  
  if(!pCBox->private_data.cbox) {
    pCBox->private_data.cbox = fc_calloc(1, sizeof(struct CHECKBOX));
    set_wflag(pCBox, WF_FREE_PRIVATE_DATA);
    pCBox->private_data.cbox->state = FALSE;
  }
  
  pTmp = pCBox->private_data.cbox;
  pTmp->pTRUE_Theme = pTrue;
  pTmp->pFALSE_Theme = pFalse;
  if(pTmp->state) {
    pCBox->theme = pTrue;
  } else {
    pCBox->theme = pFalse;
  }
  return 0;
}

void togle_checkbox(struct widget *pCBox)
{
  if(pCBox->private_data.cbox->state) {
    pCBox->theme = pCBox->private_data.cbox->pFALSE_Theme;
    pCBox->private_data.cbox->state = FALSE;
  } else {
    pCBox->theme = pCBox->private_data.cbox->pTRUE_Theme;
    pCBox->private_data.cbox->state = TRUE;
  }
}

bool get_checkbox_state(struct widget *pCBox)
{
  return pCBox->private_data.cbox->state;
}

int redraw_textcheckbox(struct widget *pCBox)
{
  int ret;
  SDL_Surface *pTheme_Surface, *pIcon;
  SDL_Rect dest;

  if(!pCBox->string16) {
    return redraw_icon(pCBox);
  }

  pTheme_Surface = pCBox->theme;
  pIcon = create_icon_from_theme(pTheme_Surface, get_wstate(pCBox));
  
  if (!pIcon) {
    return -3;
  }
  
  pCBox->theme = pIcon;

  /* if label transparen then clear background under widget or save this background */
  if (get_wflags(pCBox) & WF_RESTORE_BACKGROUND) {
    dest = pCBox->size;
    fix_rect(pCBox->dst, &dest);
    if (pCBox->gfx) {
      clear_surface(pCBox->dst, &dest);
      blit_entire_src(pCBox->gfx, pCBox->dst, dest.x, dest.y);
    } else {
      pCBox->gfx = crop_rect_from_surface(pCBox->dst, &dest);
    }
  }

  /* redraw icon label */
  ret = redraw_iconlabel(pCBox);

  FREESURFACE(pIcon);
  pCBox->theme = pTheme_Surface;

  return ret;
}
