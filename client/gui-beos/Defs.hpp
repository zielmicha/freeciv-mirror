// Defs.hpp -- Messages and their types, and other definitions
#ifndef DEFS_HPP
#define DEFS_HPP	1

#include "About.hpp"

class App;
extern App *app;
class Backend;
extern Backend *backend;
class MainWindow;
extern MainWindow *ui;

//-----------------------------------------------------------------
// Messages to app
enum App_Whats {
	APP_WHATS_first = 0xFCBA0000,

	APP_WHATS_last
};


//-----------------------------------------------------------------
// Messages to backend
enum Backend_Whats {
	BACKEND_WHATS_first = 0xFCBB0000,

	PREGAME_STATE,
	PLEASE_CONNECT_TO,
	INPUTVIEW_GENERATES_TEXT,

	BACKEND_WHATS_last
};


//-----------------------------------------------------------------
// Messages to UI
enum UI_Whats {
	UI_WHATS_first = 0xFCB10000,

	UI_POPUP_CONNECT_DIALOG,
	UI_UPDATE_MENUS,
	UI_APPEND_OUTPUT_WINDOW,
	UI_LOG_OUTPUT_WINDOW,
	UI_CLEAR_OUTPUT_WINDOW,
	UI_POPDOWN_CITY_DIALOG,
	UI_POPUP_CITY_DIALOG,
	UI_REFRESH_CITY_DIALOG,
	UI_REFRESH_UNIT_CITY_DIALOGS,
	UI_POPUP_CITY_REPORT_DIALOG,
	UI_UPDATE_CITY_REPORT_DIALOG,
	UI_POPUP_NOTIFY_GOTO_DIALOG,
	UI_POPUP_NOTIFY_DIALOG,
	UI_POPUP_RACES_DIALOG,
	UI_POPDOWN_RACES_DIALOG,
	UI_POPUP_UNIT_SELECT_DIALOG,
	UI_RACES_TOGGLES_SET_SENSITIVE_DIALOG,
	UI_POPUP_REVOLUTION_DIALOG,
	UI_POPUP_GOVERNMENT_DIALOG,
	UI_POPUP_CARAVAN_DIALOG,
	UI_POPUP_DIPLOMAT_DIALOG,
	UI_POPUP_INCITE_DIALOG,
	UI_POPUP_BRIBE_DIALOG,
	UI_POPUP_PILLAGE_DIALOG,
	UI_POPUP_SABOTAGE_DIALOG,
	UI_POPUP_UNIT_CONNECT_DIALOG,
	UI_HANDLE_DIPLO_ACCEPT_TREATY,
	UI_HANDLE_DIPLO_INIT_MEETING,
	UI_HANDLE_DIPLO_CREATE_CLAUSE,
	UI_HANDLE_DIPLO_CANCEL_MEETING,
	UI_HANDLE_DIPLO_REMOVE_CLAUSE,
	UI_POPUP_FIND_DIALOG,
	UI_POPUP_GOTO_DIALOG,
	UI_POPUP_HELP_DIALOG,
	UI_POPUP_HELP_DIALOG_STRING,
	UI_POPUP_HELP_DIALOG_TYPED,
	UI_POPDOWN_HELP_DIALOG, 
	UI_POPUP_INTEL_DIALOG,
	UI_POPUP_NEWCITY_DIALOG,
	UI_SET_TURN_DONE_BUTTON_STATE,
	UI_CENTER_ON_UNIT,

	UI_UPDATE_INFO_LABEL,
	UI_UPDATE_UNIT_INFO_LABEL,
	UI_UPDATE_TIMEOUT_LABEL,
	UI_UPDATE_UNIT_PIX_LABEL,
	UI_UPDATE_TURN_DONE_BUTTON,
	UI_SET_BULB_SOL_GOV,
	UI_SET_OVERVIEW_DIMENSIONS,
	UI_OVERVIEW_UPDATE_TILE,
	UI_CENTER_TILE_MAPCANVAS,
	UI_UPDATE_MAP_CANVAS,
	UI_UPDATE_MAP_CANVAS_VISIBLE,
	UI_UPDATE_MAP_CANVAS_SCROLLBARS,
	UI_UPDATE_CITY_DESCRIPTIONS,
	UI_PUT_CROSS_OVERLAY_TILE,
	UI_PUT_CITY_WORKERS,
	UI_MOVE_UNIT_MAP_CANVAS,
	UI_DECREASE_UNIT_HP,
	UI_PUT_NUKE_MUSHROOM_PIXMAPS,
	UI_REFRESH_OVERVIEW_CANVAS,
	UI_REFRESH_OVERVIEW_VIEWRECT,
	UI_REFRESH_TILE_MAPCANVAS,
	UI_POPUP_MESSAGEOPT_DIALOG,
	UI_POPUP_MESWIN_DIALOG,	
	UI_UPDATE_MESWIN_DIALOG,
	UI_CLEAR_NOTIFY_WINDOW,
	UI_ADD_NOTIFY_WINDOW,
	UI_MESWIN_UPDATE_DELAY,
	UI_POPUP_OPTION_DIALOG,
	UI_POPUP_PLAYERS_DIALOG,
	UI_UPDATE_PLAYERS_DIALOG,
	UI_POPUP_RATES_DIALOG,
	UI_REPORT_UPDATE_DELAY,
	UI_UPDATE_REPORT_DIALOGS,
	UI_UPDATE_SCIENCE_DIALOG,
	UI_POPUP_SCIENCE_DIALOG,
	UI_UPDATE_TRADE_REPORT_DIALOG,
	UI_POPUP_TRADE_REPORT_DIALOG,
	UI_UPDATE_ACTIVEUNITS_REPORT_DIALOG,
	UI_POPUP_ACTIVEUNITS_REPORT_DIALOG,
	UI_POPUP_SPACESHIP_DIALOG,
	UI_POPDOWN_SPACESHIP_DIALOG,
	UI_REFRESH_SPACESHIP_DIALOG,
	UI_POPUP_WORKLISTS_DIALOG,
	UI_UPDATE_WORKLIST_REPORT_DIALOG,

	UI_WHATS_last
};


//-----------------------------------------------------------------
// Utility functions

// Helpful debugging and reminder
#define NOT_FINISHED(routine)   \
	{ BAlert *a = new BAlert( APP_NAME, routine, "NOT FINISHED" ); a->Go(); }

// Ellipsis character
#define ELLIPSIS        "\xE2\x80\xA6"

// Cast to a type (similar to cast_as)
#define cast_to(type, expr)     (reinterpret_cast<type *>(expr))


// BMessage manipulations
class BMessage;
extern void      Message_ReplaceString( BMessage *msg, const char *name,
							const char *string );

// BFont info
class BFont;
extern float HeightOf( const BFont *font );

#endif // DEFS_HPP
