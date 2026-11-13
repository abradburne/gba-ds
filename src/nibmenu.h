/* The pause menu nibOS's apps share (nibkit's menu, through its C library
 * nibkit-menu-c): the same tabs, rows, face and buttons as DSlot's own pages
 * and the other emulators' pause menus, drawn as one 1024 x 768 picture for
 * the touchscreen while the game is paused; the other screen shows the game,
 * dimmed, or the highlighted save state.
 *
 * Built with GBADS_NIBKIT (make NIBKIT=<nibkit-menu-c folder>), and used
 * when menu = nibkit (gba-ds.cfg or --menu=nibkit). Tabs: Play, States, Game
 * (this game's settings, cheats, achievements, GBA DS's own menu), GBA (every
 * game's settings), System, and Connections where the system has them.
 * Settings are the options GBA DS declares to DSlot (its description,
 * gba-ds.toml), kept in DSlot's settings file, so DSlot's pages and this menu
 * show and set the same values. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Slots as the menu knows them: 0-9 GBA DS's numbered slots, and these. */
enum { NM_SLOT_RESUME = -1, NM_SLOT_QUICK = -2 };

/* What only the game knows (game.c). */
typedef struct {
	const char* (*title)(void);  /* the ROM's file name without its extension: DSlot's name for the game */
	bool (*slot_used)(int slot);
	/* the slot's picture, 240 x 160 RGBA: false when it has none */
	bool (*slot_picture)(int slot, uint8_t* rgba);
	void (*slot_saved)(int slot, char* out, size_t n); /* "Today 4:41 PM", or "" */
	int (*cheat_count)(void);
	const char* (*cheat_name)(int i);
	bool (*cheat_on)(int i);
	void (*cheat_toggle)(int i);
	bool (*achievements)(void); /* signed in, with a list to show */
	bool (*fast_forward)(void);
	int (*volume)(void); /* GBA DS's own, where the system has none */
	void (*set_volume)(int percent);
	/* a declared option's value now applies: GBA DS's config key and value
	 * ("shader", "subpixels"; a toggle "true" / "false") */
	void (*apply)(const char* key, const char* value);
} NibMenuHost;

/* What a button or touch asks of the game. */
enum {
	NM_NONE,
	NM_RESUME,
	NM_SAVE,      /* to .slot */
	NM_LOAD,      /* from .slot */
	NM_RESET,
	NM_QUIT,      /* save the resume point (as set) and quit */
	NM_SHOT,      /* a screenshot of the next frame, without the menu */
	NM_FF,        /* fast-forward (hold R2) on or off */
	NM_OWN_MENU,  /* GBA DS's own menu, for what isn't here */
	NM_ACH,       /* GBA DS's achievements list */
};
typedef struct {
	int action;
	int slot;
} NibMenuResult;

/* Whether the library is there and can show DSlot's settings: false on a
 * build without it. */
bool nibmenu_available(void);
void nibmenu_set_host(const NibMenuHost* host);
void nibmenu_open(void);
void nibmenu_close(void);
bool nibmenu_dirty(void); /* drawn again only when something changed */
/* The buttons, by what is printed on them (NIB_KEY_*: up, down, left, right,
 * A, B, X, Y, L, R as 0-9): presses, and held directions (for repeat). */
NibMenuResult nibmenu_keys(const bool pressed[10], const bool held[10], uint32_t ms);
/* A finger down on the touchscreen at (x, y) in screen units (1024 x 768). */
NibMenuResult nibmenu_touch(float x, float y);
/* The touchscreen's picture and the other screen's (`game`: the frame on
 * screen, 240 x 160, bytes R, G, B, X): RGBA, valid until the next call. */
const uint8_t* nibmenu_render(int* w, int* h);
const uint8_t* nibmenu_render_top(const uint32_t* game, int* w, int* h);
/* A short line in the hint's place for a few seconds ("Saved to slot 3"). */
void nibmenu_message(const char* text);
/* The slot Save state and Load state go to: GBA DS's own menu's, kept the same. */
void nibmenu_set_slot(int slot);
int nibmenu_slot(void);
