/* The pause menu nibOS's apps share: see nibmenu.h. Its pages follow
 * DSperate's (src/frontend/sdl/shelf_menu.cpp in DSperate, GPL-3.0), so a
 * player finds the same rows in the same places in every emulator. */
#include "nibmenu.h"

#ifndef GBADS_NIBKIT

bool nibmenu_available(void) { return false; }
void nibmenu_set_host(const NibMenuHost* host) { (void) host; }
void nibmenu_open(void) {}
void nibmenu_close(void) {}
bool nibmenu_dirty(void) { return false; }
NibMenuResult nibmenu_keys(const bool pressed[10], const bool held[10], uint32_t ms) {
	(void) pressed, (void) held, (void) ms;
	return (NibMenuResult) { NM_NONE, 0 };
}
NibMenuResult nibmenu_touch(float x, float y) {
	(void) x, (void) y;
	return (NibMenuResult) { NM_NONE, 0 };
}
const uint8_t* nibmenu_render(int* w, int* h) { return *w = *h = 0, NULL; }
const uint8_t* nibmenu_render_top(const uint32_t* game, int* w, int* h) { return (void) game, *w = *h = 0, NULL; }
void nibmenu_message(const char* text) { (void) text; }

#else

#include <nibkit_menu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GBA_W 240
#define GBA_H 160
#define N_SLOTS 10
#define SYSTEM "GBADS" /* DSlot's folder name for GBA DS's page: its scope in DSlot's settings */
#define DOT " \xc2\xb7 "  /* the hint's separator, as DSlot's */
#define SEP "   \xc2\xb7   "

enum { T_PLAY, T_STATES, T_GAME, T_CONSOLE, T_SYSTEM, T_CONN, T_COUNT };
static const char* const tabNames[T_COUNT] = { "Play", "States", "Game", "GBA", "System", "Connections" };
enum { SUB_NONE, SUB_CHEATS };

enum {
	R_HEADING, R_RESUME, R_SAVE, R_LOAD, R_FF, R_SHOT, R_RESET, R_QUIT, R_DECLARED, R_CHEATS, R_CHEAT, R_ACH, R_OWN,
	R_VOLUME, R_BRIGHTNESS, R_CLOCK,
};
typedef struct {
	int kind;
	int at; /* the option, the cheat, the screen */
} Row;

static const NibMenuHost* host;
static NibMenu* m;
static NibSettings* settings;
static NibOptions* options;
static NibConnections* conn;
static int connFirst;
static bool isOpen, dirty;
static int tab, sub;
static int rowOfTab[T_COUNT], pendingRow = -1;
static bool pendingFirstSlot;
static bool resetArmed;
static int saveArmed = -100; /* the used slot a first X was pressed on */
static int curSlot;          /* the slot Save state / Load state go to (GBA DS's own menu's slot) */

static Row rows[512];
static int nRows;
static int cards[N_SLOTS + 2];
static int nCards;

/* each slot's picture, read once a time the menu opens: [slot + 2] */
static uint8_t pics[N_SLOTS + 2][GBA_W * GBA_H * 4];
static bool picLoaded[N_SLOTS + 2], picHave[N_SLOTS + 2];
static uint8_t gameRgba[GBA_W * GBA_H * 4];

static char msg[160];
static uint32_t msgMs;
static uint32_t connMs;
static int repeatKey = -1;
static uint32_t repeatMs;
static bool repeating;

bool nibmenu_available(void) {
	return true;
}

void nibmenu_set_host(const NibMenuHost* h) {
	host = h;
}

void nibmenu_message(const char* text) {
	snprintf(msg, sizeof(msg), "%s", text ? text : "");
	msgMs = 2500;
	dirty = true;
}

bool nibmenu_dirty(void) {
	return dirty;
}

static void slot_name(int s, char* out, size_t n) {
	if (s == NM_SLOT_RESUME) snprintf(out, n, "Resume point");
	else if (s == NM_SLOT_QUICK) snprintf(out, n, "Quick state");
	else snprintf(out, n, "Slot %d", s + 1);
}

void nibmenu_open(void) {
	if (!m) m = nib_menu_new();
	/* DSlot doesn't write its file while a game runs, but the player may
	 * have between this game's starts */
	if (settings) nib_settings_reload(settings);
	else settings = nib_settings_open();
	if (!options) options = nib_options_load("gba-ds", SYSTEM);
	if (!conn) conn = nib_connections_new();
	tab = T_PLAY, sub = SUB_NONE;
	for (int t = 0; t < T_COUNT; ++t) rowOfTab[t] = 0;
	pendingRow = 0;
	resetArmed = false, saveArmed = -100;
	msg[0] = 0;
	memset(picLoaded, 0, sizeof(picLoaded)); /* (states may have changed since: the hotkeys, the quick bar) */
	isOpen = true;
	dirty = true;
}

void nibmenu_close(void) {
	if (!isOpen) return;
	nib_backlight_save(); /* (the sliders set it as they move: kept once, on leaving) */
	if (conn) nib_connections_free(conn), conn = NULL;
	isOpen = false;
}

static const char* title(void) {
	return host && host->title ? host->title() : "";
}

static void scope(bool game, char* out, size_t n) {
	if (game) snprintf(out, n, SYSTEM "/%s", title());
	else snprintf(out, n, SYSTEM);
}

static void row(int kind, int at, const char* label, int value, const char* text, int number) {
	if (nRows < (int) (sizeof(rows) / sizeof(rows[0]))) rows[nRows++] = (Row) { kind, at };
	if (kind == R_HEADING) nib_menu_heading(m, label);
	else nib_menu_row(m, label, value, text, number);
}

static void fill_tabs(void) {
	for (int t = 0; t < T_COUNT; ++t) {
		if (t == T_CONN && !conn) continue; /* (always the last, so tab numbers stay put) */
		nib_menu_tab(m, tabNames[t], t == tab);
	}
}

static void fill_play(void) {
	char s[24];
	snprintf(s, sizeof(s), "Slot %d", curSlot + 1);
	row(R_RESUME, 0, "Resume", NIB_VALUE_NONE, NULL, 0);
	row(R_SAVE, 0, "Save state", NIB_VALUE_MORE, s, 0);
	row(R_LOAD, 0, "Load state", NIB_VALUE_MORE, s, 0);
	row(R_FF, 0, "Fast-forward (hold R2)", NIB_VALUE_TEXT, host && host->fast_forward && host->fast_forward() ? "On" : "Off", 0);
	row(R_SHOT, 0, "Take a screenshot", NIB_VALUE_NONE, NULL, 0);
	row(R_RESET, 0, "Reset game", resetArmed ? NIB_VALUE_WARNING : NIB_VALUE_NONE, resetArmed ? "A again to reset" : NULL, 0);
	row(R_QUIT, 0, "Save and quit", NIB_VALUE_NONE, NULL, 0);
}

static const uint8_t* picture(int s) {
	int i = s + 2;
	if (i < 0 || i >= N_SLOTS + 2) return NULL;
	if (!picLoaded[i]) {
		picHave[i] = host && host->slot_picture && host->slot_picture(s, pics[i]);
		picLoaded[i] = true;
	}
	return picHave[i] ? pics[i] : NULL;
}

static bool used(int s) {
	return host && host->slot_used && host->slot_used(s);
}

static void fill_states(void) {
	nCards = 0;
	/* the resume point first, set apart and labelled by when it was
	 * written, as every app's shelf shows it; then the numbered slots */
	if (used(NM_SLOT_RESUME)) {
		char when[64] = "";
		if (host->slot_saved) host->slot_saved(NM_SLOT_RESUME, when, sizeof(when));
		const uint8_t* p = picture(NM_SLOT_RESUME);
		nib_menu_resume_card(m, when, p, p ? GBA_W : 0, p ? GBA_H : 0);
		cards[nCards++] = NM_SLOT_RESUME;
	}
	for (int s = 0; s < N_SLOTS; ++s) {
		bool u = used(s);
		const uint8_t* p = u ? picture(s) : NULL;
		char label[8];
		snprintf(label, sizeof(label), "%d", s + 1);
		nib_menu_card(m, label, p, p ? GBA_W : 0, p ? GBA_H : 0);
		cards[nCards++] = s;
	}
	/* the quick bar's and the hotkeys' state, after them */
	if (used(NM_SLOT_QUICK)) {
		const uint8_t* p = picture(NM_SLOT_QUICK);
		nib_menu_card(m, "Quick", p, p ? GBA_W : 0, p ? GBA_H : 0);
		cards[nCards++] = NM_SLOT_QUICK;
	}
}

/* A declared option's row as DSlot shows it: its value where this page sets
 * it, else muted, saying what applies instead ("Use GBADS (Off)"). Each
 * string nibkit hands back lasts only until the next call on the same
 * handle: copied before the next one. */
static void add_declared_rows(bool game) {
	if (!options || !settings) return;
	const char* rom = game ? title() : NULL;
	int here = game ? 3 : 2;
	for (int i = 0; i < nib_options_count(options); ++i) {
		int level = 0;
		const char* got = nib_option_value(options, i, settings, rom, &level);
		char value[128] = "", label[128], shown[128];
		bool has = got != NULL;
		if (has) snprintf(value, sizeof(value), "%s", got);
		snprintf(label, sizeof(label), "%s", nib_option_label(options, i));
		if (level == here && has) {
			snprintf(shown, sizeof(shown), "%s", nib_option_show(options, i, value));
			row(R_DECLARED, i, label, NIB_VALUE_TEXT, shown, 0);
		} else {
			snprintf(shown, sizeof(shown), "%s", nib_option_inherit(options, i, settings, rom));
			row(R_DECLARED, i, label, NIB_VALUE_INHERITED, shown, 0);
		}
	}
}

/* The option's value now, for the game: GBA DS's config key and value. */
static void apply_declared(int option) {
	if (!host || !host->apply || !options || !settings) return;
	char key[64], value[128] = "";
	const char* k = nib_option_key(options, option);
	const char* dot = strrchr(k, '.');
	snprintf(key, sizeof(key), "%s", dot ? dot + 1 : k);
	int level = 0;
	const char* v = nib_option_value(options, option, settings, title(), &level);
	if (v && level > 0) snprintf(value, sizeof(value), "%s", v);
	else if ((v = nib_option_default(options, option))) snprintf(value, sizeof(value), "%s", v);
	if (value[0]) host->apply(key, value);
}

/* The first step from a row this page doesn't set lands on the option's
 * default, as DSlot's own rows do; after that, one value on or back. */
static void step_declared(int option, bool game, int dir) {
	if (!options || !settings) return;
	int level = 0;
	const char* v = nib_option_value(options, option, settings, game ? title() : NULL, &level);
	char current[128] = "", next[128];
	if (v) snprintf(current, sizeof(current), "%s", v);
	const char* d;
	if (level == (game ? 3 : 2)) snprintf(next, sizeof(next), "%s", nib_option_step(options, option, current, dir));
	else if ((d = nib_option_default(options, option))) snprintf(next, sizeof(next), "%s", d);
	else snprintf(next, sizeof(next), "%s", nib_option_step(options, option, NULL, dir));
	char sc[300];
	scope(game, sc, sizeof(sc));
	if (nib_option_set(options, option, settings, sc, next) != 0) return;
	if (nib_settings_save(settings) != 0) nibmenu_message("DSlot's settings could not be saved");
	apply_declared(option);
}

static void use_declared(int option, bool game) {
	if (!options || !settings) return;
	char sc[300];
	scope(game, sc, sizeof(sc));
	nib_settings_reset(settings, sc, nib_option_key(options, option));
	if (nib_settings_save(settings) != 0) nibmenu_message("DSlot's settings could not be saved");
	apply_declared(option);
}

static void fill_game(void) {
	row(R_HEADING, 0, title(), NIB_VALUE_NONE, NULL, 0);
	add_declared_rows(true);
	int n = host && host->cheat_count ? host->cheat_count() : 0;
	if (n > 0) {
		int on = 0;
		for (int i = 0; i < n; ++i) on += host->cheat_on(i);
		char v[24];
		snprintf(v, sizeof(v), "%d on", on);
		row(R_CHEATS, 0, "Cheats", NIB_VALUE_MORE, v, 0);
	}
	if (host && host->achievements && host->achievements()) row(R_ACH, 0, "Achievements", NIB_VALUE_MORE, NULL, 0);
	row(R_HEADING, 0, "More", NIB_VALUE_NONE, NULL, 0);
	row(R_OWN, 0, "GBA DS's own menu", NIB_VALUE_MORE, "Patches, overlays, bug reports", 0);
}

static void fill_console(void) {
	row(R_HEADING, 0, "Every GBA DS game", NIB_VALUE_NONE, NULL, 0);
	add_declared_rows(false);
}

/* What DSlot's System page has that matters mid-game: the device's volume
 * and brightness where the system lets an app have them. */
static void fill_system(void) {
	int vol = nib_volume_get();
	if (vol < 0 && host && host->volume) vol = host->volume();
	row(R_VOLUME, 0, "Volume", NIB_VALUE_SLIDER, NULL, vol < 0 ? 100 : vol);
	int top = nib_backlight_get(0), bottom = nib_backlight_get(1);
	if (top >= 0) row(R_BRIGHTNESS, 0, "Top screen brightness", NIB_VALUE_SLIDER, NULL, top);
	if (bottom >= 0) row(R_BRIGHTNESS, 1, "Bottom screen brightness", NIB_VALUE_SLIDER, NULL, bottom);
	row(R_CLOCK, 0, "Clock", NIB_VALUE_TEXT, nib_clock_hour12() ? "12-hour" : "24-hour", 0);
}

static void fill_cheats(void) {
	int n = host && host->cheat_count ? host->cheat_count() : 0;
	for (int i = 0; i < n; ++i) row(R_CHEAT, i, host->cheat_name(i), NIB_VALUE_TEXT, host->cheat_on(i) ? "On" : "Off", 0);
}

/* The card highlighted on the States tab, as a slot; -100 when none. */
static int top_slot(void) {
	if (!isOpen || tab != T_STATES || sub != SUB_NONE) return -100;
	int at = nib_menu_card_at(m);
	return at >= 0 && at < nCards ? cards[at] : -100;
}

static void hint(char* out, size_t n) {
	if (msg[0]) {
		snprintf(out, n, "%s", msg);
		return;
	}
	if (sub == SUB_CHEATS) {
		snprintf(out, n, "A on / off" SEP "D-pad move" SEP "B back");
		return;
	}
	if (tab == T_PLAY) {
		snprintf(out, n, "A choose" SEP "D-pad move" SEP "L / R page" SEP "B resume");
		return;
	}
	if (tab == T_STATES) {
		int s = top_slot();
		if (s == NM_SLOT_RESUME || s == NM_SLOT_QUICK) snprintf(out, n, "A load" SEP "L / R page" SEP "B back");
		else if (saveArmed == s) snprintf(out, n, "X again to save over slot %d", s + 1);
		else snprintf(out, n, "A load" SEP "X save here" SEP "Y the menu's slot" SEP "L / R page" SEP "B back");
		return;
	}
	int at = nib_menu_row_at(m);
	if (at >= 0 && at < nRows && rows[at].kind == R_DECLARED) {
		snprintf(out, n, "D-pad change" SEP "Y use %s" SEP "L / R page" SEP "B back", tab == T_GAME ? "GBA" : "default");
		return;
	}
	snprintf(out, n, "D-pad change" SEP "L / R page" SEP "B back");
}

static void fill(void) {
	char t[300];
	if (sub == SUB_CHEATS) snprintf(t, sizeof(t), "CHEATS");
	else {
		snprintf(t, sizeof(t), "%s", title());
		for (char* c = t; *c; ++c)
			if (*c >= 'a' && *c <= 'z') *c -= 32;
	}
	/* the hint reads the highlight, so it goes in after the rows: filled
	 * once for the rows, then again with the hint they call for */
	char h[200] = "";
	for (int pass = 0; pass < 2; ++pass) {
		if (pass) hint(h, sizeof(h));
		nRows = 0;
		bool c = sub == SUB_NONE && tab == T_CONN && conn;
		if (c) nib_menu_begin(m, nib_connections_title(conn), nib_connections_hint());
		else nib_menu_begin(m, t, h);
		fill_tabs();
		if (c) connFirst = nib_connections_fill(conn, m);
		if (sub == SUB_CHEATS) fill_cheats();
		else switch (tab) {
			case T_PLAY: fill_play(); break;
			case T_STATES: fill_states(); break;
			case T_GAME: fill_game(); break;
			case T_CONSOLE: fill_console(); break;
			case T_SYSTEM: fill_system(); break;
			}
		int charging = 0, battery = nib_battery(&charging);
		const char* clock = nib_menu_clock(m);
		char ck[32];
		snprintf(ck, sizeof(ck), "%s", clock ? clock : "");
		nib_menu_status(m, ck, battery, charging, 2, 560.0f); /* (slot-2, a GBA cart's width) */
		/* a page just opened: its highlight, once its rows are there to move on */
		if (pass == 0 && pendingRow >= 0) nib_menu_set_row(m, pendingRow), pendingRow = -1;
		if (pass == 0 && pendingFirstSlot && tab == T_STATES) {
			nib_menu_set_card(m, nib_menu_first_slot(m));
			pendingFirstSlot = false;
		}
	}
}

static void go_tab(int t) {
	if (t < 0 || t >= T_COUNT || (t == T_CONN && !conn)) return;
	if (sub == SUB_NONE) rowOfTab[tab] = nib_menu_row_at(m);
	tab = t, sub = SUB_NONE;
	pendingRow = rowOfTab[t];
	if (t == T_STATES) pendingFirstSlot = true; /* (on the first numbered slot: Left reaches the resume point) */
	resetArmed = false, saveArmed = -100;
	dirty = true;
}

static void change(int index, int step) {
	if (index < 0 || index >= nRows) return;
	Row r = rows[index];
	switch (r.kind) {
	case R_DECLARED: step_declared(r.at, tab == T_GAME, step); break;
	case R_CHEAT: host->cheat_toggle(r.at); break;
	case R_VOLUME: {
		int vol = nib_volume_get();
		if (vol >= 0) nib_volume_set(vol + step * 10 < 0 ? 0 : vol + step * 10 > 100 ? 100 : vol + step * 10);
		else if (host && host->volume && host->set_volume) {
			vol = host->volume() + step * 10;
			host->set_volume(vol < 0 ? 0 : vol > 100 ? 100 : vol);
		}
		break;
	}
	case R_BRIGHTNESS: {
		int b = nib_backlight_get(r.at);
		if (b >= 0) nib_backlight_set(r.at, b + step * 5 < 5 ? 5 : b + step * 5 > 100 ? 100 : b + step * 5);
		break;
	}
	case R_CLOCK: nib_clock_set_hour12(!nib_clock_hour12()); break;
	}
}

static NibMenuResult choose(int index) {
	NibMenuResult out = { NM_NONE, 0 };
	if (index < 0 || index >= nRows) return out;
	Row r = rows[index];
	if (r.kind != R_RESET) resetArmed = false;
	switch (r.kind) {
	case R_RESUME: out.action = NM_RESUME; break;
	case R_SAVE:
	case R_LOAD:
		/* the menu's slot highlighted on the shelf, so A or X acts on it at once */
		go_tab(T_STATES);
		pendingFirstSlot = false;
		fill();
		for (int i = 0; i < nCards; ++i)
			if (cards[i] == curSlot) nib_menu_set_card(m, i);
		break;
	case R_FF: out.action = NM_FF; break;
	case R_SHOT: out.action = NM_SHOT; break;
	case R_RESET:
		if (!resetArmed) resetArmed = true;
		else resetArmed = false, out.action = NM_RESET;
		break;
	case R_QUIT: out.action = NM_QUIT; break;
	case R_DECLARED:
	case R_CLOCK:
	case R_CHEAT: change(index, 1); break;
	case R_CHEATS:
		rowOfTab[tab] = index, sub = SUB_CHEATS, pendingRow = 0;
		break;
	case R_ACH: out.action = NM_ACH; break;
	case R_OWN: out.action = NM_OWN_MENU; break;
	}
	return out;
}

static NibMenuResult card(int index, int key) {
	NibMenuResult out = { NM_NONE, 0 };
	if (index < 0 || index >= nCards) return out;
	int s = cards[index];
	char name[32], line[96];
	slot_name(s, name, sizeof(name));
	if (key != NIB_KEY_X) saveArmed = -100;
	if (key == NIB_KEY_A) {
		if (!used(s)) {
			snprintf(line, sizeof(line), "Nothing saved in %s yet", name);
			nibmenu_message(line);
		} else {
			out.action = NM_LOAD, out.slot = s;
		}
	} else if (key == NIB_KEY_X) {
		if (s == NM_SLOT_RESUME) nibmenu_message("The resume point is saved by Save and quit");
		else if (s == NM_SLOT_QUICK) nibmenu_message("The quick state is saved from the quick bar");
		else if (used(s) && saveArmed != s) saveArmed = s;
		else {
			saveArmed = -100;
			out.action = NM_SAVE, out.slot = s;
			int i = s + 2;
			picLoaded[i] = false; /* (its picture is the new one) */
		}
	} else if (key == NIB_KEY_Y && s >= 0) {
		curSlot = s;
		snprintf(line, sizeof(line), "Save state and Load state use slot %d", s + 1);
		nibmenu_message(line);
	}
	return out;
}

static NibMenuResult effect(NibEffect e) {
	NibMenuResult none = { NM_NONE, 0 };
	switch (e.kind) {
	case NIB_EFFECT_PAGE: go_tab(e.index); return none;
	case NIB_EFFECT_BACK:
		if (sub != SUB_NONE) {
			sub = SUB_NONE, pendingRow = rowOfTab[tab];
			return none;
		}
		if (tab != T_PLAY) {
			go_tab(T_PLAY);
			return none;
		}
		return (NibMenuResult) { NM_RESUME, 0 };
	case NIB_EFFECT_CHOOSE:
	case NIB_EFFECT_CHANGE:
		if (tab == T_CONN && sub == SUB_NONE) {
			if (conn) nib_connections_act(conn, e.index - connFirst);
			return none;
		}
		if (e.kind == NIB_EFFECT_CHOOSE) return choose(e.index);
		change(e.index, e.step);
		return none;
	case NIB_EFFECT_CARD: return card(e.index, e.key);
	case NIB_EFFECT_USE:
		if (e.index >= 0 && e.index < nRows && rows[e.index].kind == R_DECLARED) use_declared(rows[e.index].at, tab == T_GAME);
		return none;
	}
	return none;
}

NibMenuResult nibmenu_keys(const bool pressed[10], const bool held[10], uint32_t ms) {
	NibMenuResult out = { NM_NONE, 0 };
	bool p[10];
	memcpy(p, pressed, sizeof(p));
	/* the Connections page shows how its work is going: drawn again 4 times a second */
	if (tab == T_CONN && conn && (connMs += ms) >= 250) connMs = 0, dirty = true;
	if (msg[0]) {
		if (msgMs <= ms) msg[0] = 0, dirty = true;
		else msgMs -= ms;
	}
	/* a held direction repeats after a moment, as DSlot's do */
	int rep = -1;
	for (int k = NIB_KEY_UP; k <= NIB_KEY_RIGHT; ++k)
		if (held[k]) {
			rep = k;
			break;
		}
	if (rep != repeatKey || (rep >= 0 && p[rep])) repeatKey = rep, repeatMs = 0, repeating = false;
	else if (rep >= 0) {
		repeatMs += ms;
		uint32_t wait = repeating ? 55 : 400;
		if (repeatMs >= wait) repeatMs -= wait, repeating = true, p[rep] = true;
	}
	for (int k = 0; k < 10; ++k) {
		if (!p[k]) continue;
		if (!nRows && !nCards) fill(); /* (nibkit needs the page before it can move on it) */
		NibMenuResult r = effect(nib_menu_key(m, k));
		dirty = true;
		if (r.action != NM_NONE) out = r;
	}
	return out;
}

NibMenuResult nibmenu_touch(float x, float y) {
	if (!nRows && !nCards) fill();
	dirty = true;
	return effect(nib_menu_touch(m, x, y));
}

const uint8_t* nibmenu_render(int* w, int* h) {
	fill();
	dirty = false;
	return nib_menu_render(m, w, h);
}

const uint8_t* nibmenu_render_top(const uint32_t* game, int* w, int* h) {
	int s = top_slot();
	if (s != -100) {
		bool u = used(s);
		const uint8_t* p = u ? picture(s) : NULL;
		char name[32], saved[64] = "";
		slot_name(s, name, sizeof(name));
		if (u && host->slot_saved) host->slot_saved(s, saved, sizeof(saved));
		const char* labels[] = { "Saved" };
		const char* values[] = { saved };
		const char* under = !u ? "Empty" : s == curSlot ? "The menu's slot" : NULL;
		return nib_top_state(m, p, p ? GBA_W : 0, p ? GBA_H : 0, name, under, labels, values, saved[0] ? 1 : 0, w, h);
	}
	for (int i = 0; i < GBA_W * GBA_H; ++i) {
		uint32_t c = game ? game[i] : 0; /* (bytes R, G, B, X) */
		memcpy(gameRgba + i * 4, &c, 3);
		gameRgba[i * 4 + 3] = 255;
	}
	return nib_top_paused(m, gameRgba, GBA_W, GBA_H, "Paused", title(), w, h);
}

/* The slot Save state and Load state use: GBA DS's own menu's (game.c keeps
 * the two the same). */
void nibmenu_set_slot(int s) {
	curSlot = s;
}
int nibmenu_slot(void) {
	return curSlot;
}

#endif
