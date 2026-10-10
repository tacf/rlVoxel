#ifndef RLVOXEL_UI_MENU_H
#define RLVOXEL_UI_MENU_H

#include "world/world_save.h"
#include "ui/text_edit.h"
#include <clay.h>

typedef enum MenuScreen {
  MENU_MAIN,
  MENU_WORLDS,
  MENU_CREATE,
  MENU_RENAME,
  MENU_ARCHIVE
} MenuScreen;
typedef enum MenuAction { MENU_NONE, MENU_START_WORLD, MENU_QUIT } MenuAction;

typedef struct MenuWorldEntry {
  WorldSaveInfo info;
  bool valid;
  char error[WORLD_SAVE_ERROR_SIZE];
  char detail[192];
} MenuWorldEntry;

typedef struct Menu {
  MenuScreen screen;
  char root[WORLD_SAVE_PATH_SIZE];
  MenuWorldEntry *worlds;
  int world_count;
  int selected;
  char name[WORLD_SAVE_NAME_SIZE];
  char seed[32];
  TextEdit name_edit;
  TextEdit seed_edit;
  double caret_blink_start;
  int mode;
  int focused_field;
  char error[WORLD_SAVE_ERROR_SIZE];
} Menu;

void Menu_Init(Menu *menu, const char *root, int64_t seed);
void Menu_Shutdown(Menu *menu);
void Menu_RefreshWorlds(Menu *menu);
MenuAction Menu_Draw(Menu *menu, Font *font, WorldSaveInfo *selected_world);
void Menu_DrawLoading(Font font);

/* Text used by the loading screen outside a Clay layout. */
void Menu_CenteredText(Font font, const char *text, Rectangle area, float size, Color color);

typedef enum MenuPauseAction {
  MENU_PAUSE_NONE,
  MENU_PAUSE_RESUME,
  MENU_PAUSE_OPTIONS,
  MENU_PAUSE_BACK,
  MENU_PAUSE_CLOUDS,
  MENU_PAUSE_PIXEL_LOOK,
  MENU_PAUSE_RETURN,
  MENU_PAUSE_QUIT
} MenuPauseAction;

typedef struct MenuPauseView {
  bool show_options;
  bool remote_mode;
  bool clouds_enabled;
  bool pixel_look_enabled;
  const char *error;
} MenuPauseView;

/* Call inside a Clay layout. Shared components own layout, styling, and hit testing. */
bool Menu_BuildButton(Clay_ElementId id, const char *label, bool enabled);
MenuPauseAction Menu_BuildPause(MenuPauseView view);

#endif
