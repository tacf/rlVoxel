#include "ui/menu.h"
#include "raylib.h"
#include "ui/clay_render.h"
#include "ui/text_edit.h"
#include "world/world_save.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Flat forest, soil, and parchment tones keep the pixel UI not too off of
 * the world colors.
 */
static const Color background = {32, 43, 36, 255};
static const Clay_Color panel = {45, 56, 44, 255};
static const Clay_Color surface = {74, 70, 54, 255};
static const Clay_Color hovered = {89, 99, 74, 255};
static const Clay_Color outline = {121, 129, 101, 255};
static const Clay_Color muted = {183, 190, 163, 255};
static const Clay_Color accent = {180, 204, 123, 255};
static const Clay_Color white = {239, 231, 206, 255};
static const Clay_Color error_color = {230, 160, 129, 255};

typedef enum MenuCommand {
  COMMAND_NONE,
  COMMAND_BROWSE,
  COMMAND_CREATE,
  COMMAND_PLAY,
  COMMAND_RENAME,
  COMMAND_ARCHIVE,
  COMMAND_CONFIRM,
  COMMAND_BACK,
  COMMAND_QUIT
} MenuCommand;

static Clay_String string(const char *text) {
  return (Clay_String){.length = (int32_t)strlen(text), .chars = text};
}

static void label(const char *text, uint16_t size, Clay_Color color) {
  CLAY_TEXT(string(text), CLAY_TEXT_CONFIG({.fontSize = size, .textColor = color}));
}

static bool clicked(Clay_ElementId id) {
  return Clay_PointerOver(id) && IsMouseButtonReleased(MOUSE_LEFT_BUTTON);
}

static Clay_Vector2 scroll_offset(Clay_ElementId id) {
  Clay_ScrollContainerData data = Clay_GetScrollContainerData(id);
  return data.found ? *data.scrollPosition : (Clay_Vector2){0};
}

bool Menu_BuildButton(Clay_ElementId id, const char *text, bool enabled) {
  bool hover = enabled && Clay_PointerOver(id);
  CLAY({.id = id,
        .layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(44)},
                   .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
        .backgroundColor = hover ? hovered : surface,
        .border = {.color = hover ? accent : outline, .width = CLAY_BORDER_ALL(1)}}) {
    label(text, 20, enabled ? white : muted);
  }
  return enabled && clicked(id);
}

static void heading(const char *text) {
  CLAY({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(48)},
                   .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}}}) {
    label(text, 32, white);
  }
}

static float prefix_width(Font *font, const char *text, size_t length) {
  char prefix[WORLD_SAVE_NAME_SIZE];
  memcpy(prefix, text, length);
  prefix[length] = 0;
  return MeasureTextEx(*font, prefix, 20, 0).x;
}

static void field_overlay(float x, float width, Clay_Color color) {
  CLAY({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(width), .height = CLAY_SIZING_FIXED(20)}},
        .backgroundColor = color,
        .floating = {.attachTo = CLAY_ATTACH_TO_PARENT,
                     .offset = {x, 12},
                     .zIndex = 1,
                     .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
                     .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT}}) {}
}

static void build_text_field(Clay_ElementId id, const char *text, const TextEdit *edit,
                             bool focused, Font *font, double blink_start) {
  float caret_x = prefix_width(font, text, edit->cursor);
  Clay_ScrollContainerData scroll = Clay_GetScrollContainerData(id);
  Clay_ElementData element = Clay_GetElementData(id);
  if (focused && scroll.found && element.found) {
    float visible_width = element.boundingBox.width - 26;
    if (caret_x + scroll.scrollPosition->x > visible_width)
      scroll.scrollPosition->x = visible_width - caret_x;
    if (caret_x + scroll.scrollPosition->x < 0)
      scroll.scrollPosition->x = -caret_x;
  }
  Clay_Vector2 offset = scroll_offset(id);
  /* CLAY_BORDER_ALL also adds dividers between children, which look like extra carets. */
  CLAY({.id = id,
        .layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(44)},
                   .padding = CLAY_PADDING_ALL(12),
                   .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
        .backgroundColor = {25, 33, 26, 255},
        .border = {.color = focused ? accent : muted,
                   .width = {.left = 1, .right = 1, .top = 1, .bottom = 1}},
        .clip = {.horizontal = true, .childOffset = offset}}) {
    CLAY_TEXT(
        string(text),
        CLAY_TEXT_CONFIG({.fontSize = 20, .textColor = white, .wrapMode = CLAY_TEXT_WRAP_NONE}));
    if (focused && edit->cursor != edit->anchor) {
      size_t start = edit->cursor < edit->anchor ? edit->cursor : edit->anchor;
      size_t end = edit->cursor > edit->anchor ? edit->cursor : edit->anchor;
      float selection_x = prefix_width(font, text, start);
      field_overlay(12 + offset.x + selection_x, prefix_width(font, text, end) - selection_x,
                    (Clay_Color){180, 204, 123, 85});
    }
    if (focused && (int)((GetTime() - blink_start) * 2) % 2 == 0)
      field_overlay(12 + offset.x + caret_x, 2, accent);
  }
}

static bool key_pressed_or_repeated(int key) {
  return IsKeyPressed(key) || IsKeyPressedRepeat(key);
}

static bool edit_text(TextEdit *edit, char *text, size_t capacity) {
  bool acted = false;
  bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
  bool control = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
  if (control && IsKeyPressed(KEY_A)) {
    TextEdit_SelectAll(edit, text);
    acted = true;
  }
  const int keys[] = {KEY_LEFT, KEY_RIGHT, KEY_HOME, KEY_END};
  const TextEditMovement moves[] = {TEXT_EDIT_LEFT, TEXT_EDIT_RIGHT, TEXT_EDIT_HOME, TEXT_EDIT_END};
  for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
    if (key_pressed_or_repeated(keys[i])) {
      TextEdit_Move(edit, text, moves[i], shift);
      acted = true;
    }
  }
  if (key_pressed_or_repeated(KEY_BACKSPACE))
    acted |= TextEdit_Delete(edit, text, true);
  if (key_pressed_or_repeated(KEY_DELETE))
    acted |= TextEdit_Delete(edit, text, false);
  int key;
  while ((key = GetCharPressed()) != 0) {
    if (!control && key >= 32 && key <= 126) {
      char insert[] = {(char)key, 0};
      acted |= TextEdit_Insert(edit, text, capacity, insert);
    }
  }
  return acted;
}

static void place_cursor(TextEdit *edit, const char *text, Clay_ElementId id, Font *font) {
  Clay_ElementData element = Clay_GetElementData(id);
  float target = GetMousePosition().x - element.boundingBox.x - 12 - scroll_offset(id).x;
  TextEdit_Reset(edit, text);
  TextEdit_Move(edit, text, TEXT_EDIT_HOME, false);
  float left = 0;
  while (text[edit->cursor]) {
    size_t previous = edit->cursor;
    TextEdit_Move(edit, text, TEXT_EDIT_RIGHT, false);
    float right = prefix_width(font, text, edit->cursor);
    if (target < (left + right) / 2) {
      edit->cursor = edit->anchor = previous;
      return;
    }
    left = right;
  }
}

static int compare_worlds(const void *a, const void *b) {
  const MenuWorldEntry *left = a, *right = b;
  if (left->info.last_saved != right->info.last_saved)
    return left->info.last_saved > right->info.last_saved ? -1 : 1;
  return strcmp(left->info.name, right->info.name);
}

void Menu_RefreshWorlds(Menu *menu) {
  free(menu->worlds);
  menu->worlds = NULL;
  menu->world_count = 0;
  menu->selected = -1;
  if (!DirectoryExists(menu->root))
    return;
  FilePathList files = LoadDirectoryFiles(menu->root);
  menu->worlds = calloc(files.count, sizeof(*menu->worlds));
  if (files.count && !menu->worlds) {
    snprintf(menu->error, sizeof(menu->error), "Not enough memory to list worlds.");
    UnloadDirectoryFiles(files);
    return;
  }
  for (unsigned int i = 0; i < files.count; i++) {
    const char *path = files.paths[i];
    if (!DirectoryExists(path) || GetFileName(path)[0] == '.')
      continue;
    char metadata[WORLD_SAVE_PATH_SIZE + 16];
    snprintf(metadata, sizeof(metadata), "%s/world.meta", path);
    if (!FileExists(metadata))
      continue;
    MenuWorldEntry *entry = &menu->worlds[menu->world_count++];
    entry->valid = WorldSave_ReadInfo(path, &entry->info, entry->error);
    if (!entry->valid) {
      snprintf(entry->info.directory, sizeof(entry->info.directory), "%s", path);
      snprintf(entry->info.name, sizeof(entry->info.name), "%s", GetFileName(path));
      snprintf(entry->detail, sizeof(entry->detail), "Unavailable: %.160s", entry->error);
    } else {
      char date[48] = "Not played yet";
      if (entry->info.last_saved) {
        time_t saved = (time_t)entry->info.last_saved;
        struct tm *local = localtime(&saved);
        if (local)
          strftime(date, sizeof(date), "%Y-%m-%d %H:%M", local);
      }
      snprintf(entry->detail, sizeof(entry->detail), "%s  |  Seed: %" PRId64 "  |  %s",
               entry->info.gameplay_mode == 1 ? "Survival" : "Creative", entry->info.seed, date);
    }
  }
  UnloadDirectoryFiles(files);
  if (menu->world_count > 1)
    qsort(menu->worlds, menu->world_count, sizeof(*menu->worlds), compare_worlds);
  if (menu->world_count)
    menu->selected = 0;
}

void Menu_Init(Menu *menu, const char *root, int64_t seed) {
  *menu = (Menu){.selected = -1};
  snprintf(menu->root, sizeof(menu->root), "%s", root);
  snprintf(menu->seed, sizeof(menu->seed), "%" PRId64, seed);
  snprintf(menu->name, sizeof(menu->name), "New World");
  TextEdit_Reset(&menu->name_edit, menu->name);
  TextEdit_Reset(&menu->seed_edit, menu->seed);
}

void Menu_Shutdown(Menu *menu) {
  free(menu->worlds);
  menu->worlds = NULL;
}

void Menu_CenteredText(Font font, const char *text, Rectangle area, float size, Color color) {
  Vector2 measured = MeasureTextEx(font, text, size, 1);
  DrawTextEx(
      font, text,
      (Vector2){area.x + (area.width - measured.x) / 2, area.y + (area.height - measured.y) / 2},
      size, 1, color);
}

void Menu_DrawLoading(Font font) {
  ClearBackground(background);
  Menu_CenteredText(font, "Loading world...",
                    (Rectangle){0, 0, GetScreenWidth(), GetScreenHeight()}, 24,
                    (Color){239, 231, 206, 255});
}

static MenuCommand build_main(void) {
  MenuCommand command = COMMAND_NONE;
  heading("rlVoxel");
  CLAY({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)},
                   .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}}}) {
    CLAY({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(320), .height = CLAY_SIZING_FIT(0)},
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 16,
                     .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) {
      label("Stop playing with my Cubes!", 20, muted);
      if (Menu_BuildButton(CLAY_ID("menu_play"), "Play", true))
        command = COMMAND_BROWSE;
      if (Menu_BuildButton(CLAY_ID("menu_create"), "Create World", true))
        command = COMMAND_CREATE;
      if (Menu_BuildButton(CLAY_ID("menu_quit"), "Quit", true))
        command = COMMAND_QUIT;
    }
  }
  return command;
}

static MenuCommand build_worlds(Menu *menu) {
  MenuCommand command = COMMAND_NONE;
  heading("Your Worlds");
  CLAY({.id = CLAY_ID("world_list"),
        .layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)},
                   .layoutDirection = CLAY_TOP_TO_BOTTOM,
                   .childGap = 8},
        .clip = {.vertical = true, .childOffset = scroll_offset(CLAY_ID("world_list"))}}) {
    if (!menu->world_count)
      label("No worlds yet. Create your first one.", 20, muted);
    for (int i = 0; i < menu->world_count; i++) {
      MenuWorldEntry *entry = &menu->worlds[i];
      Clay_ElementId id = CLAY_IDI("world_row", i);
      bool selected = menu->selected == i;
      CLAY({.id = id,
            .layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(64)},
                       .layoutDirection = CLAY_TOP_TO_BOTTOM,
                       .padding = CLAY_PADDING_ALL(10),
                       .childGap = 6},
            .backgroundColor = selected ? hovered : surface,
            .border = {.color = selected ? accent : muted, .width = CLAY_BORDER_ALL(1)},
            .clip = {.horizontal = true}}) {
        label(entry->info.name, 20, white);
        CLAY_TEXT(string(entry->detail),
                  CLAY_TEXT_CONFIG(
                      {.fontSize = 16, .textColor = muted, .wrapMode = CLAY_TEXT_WRAP_NONE}));
      }
      if (clicked(id))
        menu->selected = i;
    }
  }
  MenuWorldEntry *selected = menu->selected >= 0 ? &menu->worlds[menu->selected] : NULL;
  CLAY({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)}, .childGap = 12}}) {
    if (Menu_BuildButton(CLAY_ID("world_play"), "Play World", selected && selected->valid))
      command = COMMAND_PLAY;
    if (Menu_BuildButton(CLAY_ID("world_rename"), "Rename", selected && selected->valid))
      command = COMMAND_RENAME;
    if (Menu_BuildButton(CLAY_ID("world_archive"), "Archive", selected != NULL))
      command = COMMAND_ARCHIVE;
  }
  CLAY({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)}, .childGap = 12}}) {
    if (Menu_BuildButton(CLAY_ID("world_create"), "Create World", true))
      command = COMMAND_CREATE;
    if (Menu_BuildButton(CLAY_ID("world_back"), "Back", true))
      command = COMMAND_BACK;
  }
  if (selected && selected->valid && IsKeyPressed(KEY_ENTER))
    command = COMMAND_PLAY;
  return command;
}

static MenuCommand build_form(Menu *menu, Font *font) {
  bool creating = menu->screen == MENU_CREATE;
  if (creating && IsKeyPressed(KEY_TAB)) {
    menu->focused_field = 1 - menu->focused_field;
    menu->caret_blink_start = GetTime();
  }
  if (Clay_PointerOver(CLAY_ID("world_name")) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
    menu->focused_field = 0;
    place_cursor(&menu->name_edit, menu->name, CLAY_ID("world_name"), font);
    menu->caret_blink_start = GetTime();
  }
  if (creating && Clay_PointerOver(CLAY_ID("world_seed")) &&
      IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
    menu->focused_field = 1;
    place_cursor(&menu->seed_edit, menu->seed, CLAY_ID("world_seed"), font);
    menu->caret_blink_start = GetTime();
  }
  bool acted = !creating || menu->focused_field == 0
                   ? edit_text(&menu->name_edit, menu->name, sizeof(menu->name))
                   : edit_text(&menu->seed_edit, menu->seed, sizeof(menu->seed));
  if (acted)
    menu->caret_blink_start = GetTime();
  heading(creating ? "Create World" : "Rename World");
  label("World name", 20, muted);
  build_text_field(CLAY_ID("world_name"), menu->name, &menu->name_edit,
                   !creating || menu->focused_field == 0, font, menu->caret_blink_start);
  if (creating) {
    label("Seed (leave empty for random)", 20, muted);
    build_text_field(CLAY_ID("world_seed"), menu->seed, &menu->seed_edit, menu->focused_field == 1,
                     font, menu->caret_blink_start);
    if (Menu_BuildButton(CLAY_ID("world_mode"),
                         menu->mode == 1 ? "Mode: Survival" : "Mode: Creative", true))
      menu->mode = 1 - menu->mode;
  }
  CLAY({.layout = {.sizing = {.height = CLAY_SIZING_GROW(0)}}}) {}
  MenuCommand command = COMMAND_NONE;
  CLAY({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)}, .childGap = 12}}) {
    if (Menu_BuildButton(CLAY_ID("world_confirm"), creating ? "Create & Play" : "Save Name", true))
      command = COMMAND_CONFIRM;
    if (Menu_BuildButton(CLAY_ID("world_cancel"), "Cancel", true))
      command = COMMAND_BACK;
  }
  if (IsKeyPressed(KEY_ENTER))
    command = COMMAND_CONFIRM;
  return command;
}

static MenuCommand build_archive(Menu *menu) {
  heading("Archive World?");
  label(menu->worlds[menu->selected].info.name, 24, white);
  label("This world will be moved out of your list.", 20, muted);
  label("Its files remain recoverable in the .trash folder.", 20, muted);
  CLAY({.layout = {.sizing = {.height = CLAY_SIZING_GROW(0)}}}) {}
  MenuCommand command = COMMAND_NONE;
  CLAY({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)}, .childGap = 12}}) {
    if (Menu_BuildButton(CLAY_ID("archive_confirm"), "Archive", true))
      command = COMMAND_CONFIRM;
    if (Menu_BuildButton(CLAY_ID("archive_cancel"), "Cancel", true))
      command = COMMAND_BACK;
  }
  return command;
}

static void show_world_browser(Menu *menu) {
  menu->screen = MENU_WORLDS;
  menu->error[0] = 0;
  Menu_RefreshWorlds(menu);
}

static bool parse_world_seed(const char *text, int64_t *seed) {
  if (!text[0]) {
    *seed = (int64_t)((uint64_t)time(NULL) ^ (uint64_t)(GetTime() * 1000000));
    return true;
  }

  char *end;
  errno = 0;
  int64_t parsed = strtoll(text, &end, 10);
  if (errno == ERANGE || end == text || *end)
    return false;
  *seed = parsed;
  return true;
}

static MenuAction confirm_command(Menu *menu, WorldSaveInfo *world) {
  bool succeeded;
  switch (menu->screen) {
  case MENU_CREATE: {
    int64_t seed;
    if (!parse_world_seed(menu->seed, &seed)) {
      snprintf(menu->error, sizeof(menu->error), "Seed must be a signed 64-bit integer.");
      return MENU_NONE;
    }
    if (WorldSave_Create(menu->root, menu->name, seed, menu->mode, world, menu->error))
      return MENU_START_WORLD;
    return MENU_NONE;
  }
  case MENU_RENAME:
    succeeded = WorldSave_Rename(&menu->worlds[menu->selected].info, menu->name, menu->error);
    break;
  case MENU_ARCHIVE:
    succeeded = WorldSave_Archive(menu->root, &menu->worlds[menu->selected].info, menu->error);
    break;
  default:
    return MENU_NONE;
  }
  if (succeeded)
    show_world_browser(menu);
  return MENU_NONE;
}

/* Apply navigation after rendering; Clay strings may refer to world-list storage. */
static MenuAction apply_command(Menu *menu, MenuCommand command, WorldSaveInfo *world) {
  switch (command) {
  case COMMAND_QUIT:
    return MENU_QUIT;
  case COMMAND_PLAY:
    if (menu->selected >= 0 && menu->worlds[menu->selected].valid) {
      *world = menu->worlds[menu->selected].info;
      return MENU_START_WORLD;
    }
    break;
  case COMMAND_CREATE:
    menu->screen = MENU_CREATE;
    menu->focused_field = 0;
    snprintf(menu->name, sizeof(menu->name), "New World");
    TextEdit_Reset(&menu->name_edit, menu->name);
    menu->caret_blink_start = GetTime();
    menu->error[0] = 0;
    break;
  case COMMAND_BROWSE:
    show_world_browser(menu);
    break;
  case COMMAND_RENAME:
    menu->screen = MENU_RENAME;
    snprintf(menu->name, sizeof(menu->name), "%s", menu->worlds[menu->selected].info.name);
    TextEdit_Reset(&menu->name_edit, menu->name);
    menu->caret_blink_start = GetTime();
    break;
  case COMMAND_ARCHIVE:
    menu->screen = MENU_ARCHIVE;
    break;
  case COMMAND_BACK:
    if (menu->screen == MENU_WORLDS) {
      menu->screen = MENU_MAIN;
      menu->error[0] = 0;
    } else if (menu->screen != MENU_MAIN) {
      show_world_browser(menu);
    }
    break;
  case COMMAND_CONFIRM:
    return confirm_command(menu, world);
  default:
    break;
  }
  return MENU_NONE;
}

MenuAction Menu_Draw(Menu *menu, Font *font, WorldSaveInfo *world) {
  ClearBackground(background);
  ClayRender_BeginFrame(font, true);
  Clay_BeginLayout();
  MenuCommand command = COMMAND_NONE;
  CLAY({.id = CLAY_ID("menu_root"),
        .layout = {.sizing = {.width = CLAY_SIZING_FIXED(GetScreenWidth()),
                              .height = CLAY_SIZING_FIXED(GetScreenHeight())},
                   .padding = CLAY_PADDING_ALL(40),
                   .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) {
    CLAY({.id = CLAY_ID("menu_panel"),
          .layout = {.sizing = {.width = CLAY_SIZING_GROW(0, 760), .height = CLAY_SIZING_GROW(0)},
                     .padding = CLAY_PADDING_ALL(24),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 16},
          .backgroundColor = panel}) {
      switch (menu->screen) {
      case MENU_MAIN:
        command = build_main();
        break;
      case MENU_WORLDS:
        command = build_worlds(menu);
        break;
      case MENU_CREATE:
      case MENU_RENAME:
        command = build_form(menu, font);
        break;
      case MENU_ARCHIVE:
        command = build_archive(menu);
        break;
      }
      if (menu->error[0])
        label(menu->error, 16, error_color);
    }
  }
  ClayRender_Draw(Clay_EndLayout(), font);
  if (IsKeyPressed(KEY_ESCAPE))
    command = COMMAND_BACK;
  return apply_command(menu, command, world);
}

MenuPauseAction Menu_BuildPause(MenuPauseView view) {
  MenuPauseAction action = MENU_PAUSE_NONE;
  CLAY({.id = CLAY_ID("pause_overlay"),
        .layout = {.sizing = {.width = CLAY_SIZING_FIXED(GetScreenWidth()),
                              .height = CLAY_SIZING_FIXED(GetScreenHeight())},
                   .padding = CLAY_PADDING_ALL(24),
                   .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
        .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 10},
        .backgroundColor = {16, 23, 17, 160}}) {
    CLAY({.id = CLAY_ID("pause_panel"),
          .layout = {.sizing = {.width = CLAY_SIZING_GROW(0, 420), .height = CLAY_SIZING_FIT(0)},
                     .padding = CLAY_PADDING_ALL(24),
                     .childGap = 12,
                     .layoutDirection = CLAY_TOP_TO_BOTTOM},
          .backgroundColor = panel}) {
      heading(view.show_options ? "Options" : "Game Paused");
      if (view.show_options) {
        if (Menu_BuildButton(CLAY_ID("pause_clouds"),
                             view.clouds_enabled ? "Clouds: On" : "Clouds: Off", true))
          action = MENU_PAUSE_CLOUDS;
        if (Menu_BuildButton(CLAY_ID("pause_pixel"),
                             view.pixel_look_enabled ? "Pixel Look: On" : "Pixel Look: Off", true))
          action = MENU_PAUSE_PIXEL_LOOK;
        if (Menu_BuildButton(CLAY_ID("pause_back"), "Back", true))
          action = MENU_PAUSE_BACK;
      } else {
        if (Menu_BuildButton(CLAY_ID("pause_resume"), "Resume", true))
          action = MENU_PAUSE_RESUME;
        if (Menu_BuildButton(CLAY_ID("pause_options"), "Options", true))
          action = MENU_PAUSE_OPTIONS;
        if (Menu_BuildButton(CLAY_ID("pause_return"),
                             view.remote_mode ? "Disconnect to Menu" : "Save & Return to Menu",
                             true))
          action = MENU_PAUSE_RETURN;
        if (Menu_BuildButton(CLAY_ID("pause_quit"), view.remote_mode ? "Quit" : "Save & Quit",
                             true))
          action = MENU_PAUSE_QUIT;
      }
      if (view.error && view.error[0])
        label(view.error, 16, error_color);
    }
  }
  return action;
}
