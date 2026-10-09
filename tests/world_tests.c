#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "world/chunk.h"
#include "world/lighting.h"

#define CHECK(condition)                                                                           \
  do {                                                                                             \
    if (!(condition)) {                                                                            \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                              \
      exit(EXIT_FAILURE);                                                                          \
    }                                                                                              \
  } while (0)

static LightNode node_at(int index) {
  return (LightNode){.x = index, .y = index % 128, .z = -index};
}

static void check_pop(LightUpdateQueue *queue, int index) {
  LightNode node;
  LightNode expected = node_at(index);
  CHECK(LightQueue_Pop(queue, &node));
  CHECK(node.x == expected.x && node.y == expected.y && node.z == expected.z);
}

static void test_light_queue(int requested_capacity) {
  LightUpdateQueue queue;
  LightNode node = node_at(-1);
  LightQueue_Init(&queue, requested_capacity);
  CHECK(queue.count == 0);
  CHECK(queue.max_pending >= requested_capacity && queue.max_pending >= 1024);
  CHECK(!LightQueue_Pop(&queue, &node));
  CHECK(node.x == -1);

  LightNode *storage = queue.items;
  int capacity = queue.max_pending;
  int pushed = 0;
  int popped = 0;

  /* Keep a partial backlog while repeatedly wrapping both ends of the queue. */
  for (int round = 0; round < 40; round++) {
    while (queue.count < capacity - 7) {
      LightQueue_Push(&queue, node_at(pushed++));
    }
    for (int i = 0; i < capacity / 2; i++) {
      check_pop(&queue, popped++);
    }
    CHECK(queue.count == pushed - popped);
    CHECK(queue.items == storage);
  }
  while (popped < pushed) {
    check_pop(&queue, popped++);
  }
  CHECK(queue.count == 0);
  CHECK(!LightQueue_Pop(&queue, &node));

  /* Saturation retains exactly the newest capacity nodes in FIFO order. */
  int total = capacity * 3 + 37;
  for (int i = 0; i < total; i++) {
    LightQueue_Push(&queue, node_at(i));
    CHECK(queue.count == (i + 1 < capacity ? i + 1 : capacity));
  }
  CHECK(queue.items == storage);
  for (int i = total - capacity; i < total; i++) {
    check_pop(&queue, i);
  }
  CHECK(!LightQueue_Pop(&queue, &node));

  LightQueue_Push(&queue, node_at(42));
  check_pop(&queue, 42);
  LightQueue_Shutdown(&queue);
  CHECK(queue.items == NULL && queue.count == 0 && queue.max_pending == 0);
}

static void test_coordinates(void) {
  const struct {
    int block;
    int chunk;
    int local;
  } cases[] = {
      {INT_MIN, INT_MIN / 16, 0},
      {INT_MIN + 1, INT_MIN / 16, 1},
      {-33, -3, 15},
      {-32, -2, 0},
      {-17, -2, 15},
      {-16, -1, 0},
      {-15, -1, 1},
      {-1, -1, 15},
      {0, 0, 0},
      {1, 0, 1},
      {15, 0, 15},
      {16, 1, 0},
      {17, 1, 1},
      {INT_MAX, INT_MAX / 16, 15},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    CHECK(floor_div16(cases[i].block) == cases[i].chunk);
    CHECK(floor_mod16(cases[i].block) == cases[i].local);
    CHECK((int64_t)floor_div16(cases[i].block) * 16 + floor_mod16(cases[i].block) ==
          cases[i].block);
  }

  const int coords[] = {INT_MIN, -17, -1, 0, 1, 17, INT_MAX};
  for (size_t x = 0; x < sizeof(coords) / sizeof(coords[0]); x++) {
    for (size_t z = 0; z < sizeof(coords) / sizeof(coords[0]); z++) {
      uint64_t key = (uint64_t)make_chunk_key(coords[x], coords[z]);
      CHECK((uint32_t)(key >> 32) == (uint32_t)coords[x]);
      CHECK((uint32_t)key == (uint32_t)coords[z]);
    }
  }
}

int main(void) {
  test_coordinates();
  test_light_queue(1);
  test_light_queue(2049);
  puts("World regression tests passed.");
  return EXIT_SUCCESS;
}
