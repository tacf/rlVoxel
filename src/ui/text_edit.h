#ifndef RLVOXEL_UI_TEXT_EDIT_H
#define RLVOXEL_UI_TEXT_EDIT_H

#include <stdbool.h>
#include <stddef.h>

typedef struct TextEdit {
  size_t cursor;
  size_t anchor;
} TextEdit;

typedef enum TextEditMovement {
  TEXT_EDIT_LEFT,
  TEXT_EDIT_RIGHT,
  TEXT_EDIT_HOME,
  TEXT_EDIT_END
} TextEditMovement;

void TextEdit_Reset(TextEdit *edit, const char *text);
void TextEdit_Move(TextEdit *edit, const char *text, TextEditMovement movement,
                   bool extend_selection);
void TextEdit_SelectAll(TextEdit *edit, const char *text);
bool TextEdit_Insert(TextEdit *edit, char *text, size_t capacity, const char *insert);
bool TextEdit_Delete(TextEdit *edit, char *text, bool backwards);

#endif
