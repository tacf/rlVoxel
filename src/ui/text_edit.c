#include "ui/text_edit.h"

#include <string.h>

/* Positions are byte offsets; movement skips UTF-8 continuation bytes. */
static size_t previous_character(const char *text, size_t position) {
  if (position)
    position--;
  while (position && ((unsigned char)text[position] & 0xc0) == 0x80)
    position--;
  return position;
}

static size_t next_character(const char *text, size_t position) {
  if (text[position])
    position++;
  while (((unsigned char)text[position] & 0xc0) == 0x80)
    position++;
  return position;
}

void TextEdit_Reset(TextEdit *edit, const char *text) {
  size_t length = strlen(text);
  *edit = (TextEdit){.cursor = length, .anchor = length};
}

void TextEdit_Move(TextEdit *edit, const char *text, TextEditMovement movement,
                   bool extend_selection) {
  bool selected = edit->cursor != edit->anchor;
  switch (movement) {
  case TEXT_EDIT_LEFT:
    edit->cursor = selected && !extend_selection
                       ? (edit->cursor < edit->anchor ? edit->cursor : edit->anchor)
                       : previous_character(text, edit->cursor);
    break;
  case TEXT_EDIT_RIGHT:
    edit->cursor = selected && !extend_selection
                       ? (edit->cursor > edit->anchor ? edit->cursor : edit->anchor)
                       : next_character(text, edit->cursor);
    break;
  case TEXT_EDIT_HOME:
    edit->cursor = 0;
    break;
  case TEXT_EDIT_END:
    edit->cursor = strlen(text);
    break;
  }
  if (!extend_selection)
    edit->anchor = edit->cursor;
}

void TextEdit_SelectAll(TextEdit *edit, const char *text) {
  *edit = (TextEdit){.cursor = strlen(text), .anchor = 0};
}

bool TextEdit_Insert(TextEdit *edit, char *text, size_t capacity, const char *insert) {
  size_t start = edit->cursor < edit->anchor ? edit->cursor : edit->anchor;
  size_t end = edit->cursor > edit->anchor ? edit->cursor : edit->anchor;
  size_t length = strlen(text);
  size_t inserted = strlen(insert);
  size_t retained = length - (end - start);
  if (!inserted || retained >= capacity || inserted >= capacity - retained)
    return false;
  memmove(text + start + inserted, text + end, length - end + 1);
  memcpy(text + start, insert, inserted);
  edit->cursor = edit->anchor = start + inserted;
  return true;
}

bool TextEdit_Delete(TextEdit *edit, char *text, bool backwards) {
  size_t start = edit->cursor < edit->anchor ? edit->cursor : edit->anchor;
  size_t end = edit->cursor > edit->anchor ? edit->cursor : edit->anchor;
  if (start == end) {
    if (backwards)
      start = previous_character(text, start);
    else
      end = next_character(text, end);
  }
  if (start == end)
    return false;
  memmove(text + start, text + end, strlen(text) - end + 1);
  edit->cursor = edit->anchor = start;
  return true;
}
