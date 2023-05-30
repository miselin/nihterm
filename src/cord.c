#include <malloc.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <nihterm/cord.h>

struct rope {
  struct cord *head;
  struct cord *tail;
};

struct cord {
  char *str;
  size_t length;

  struct cord *next;
};

struct cord *cord_new() {
  return calloc(1, sizeof(struct cord));
}

struct cord *cord_new_str(const char *str) {
  struct cord *cord = cord_new();
  cord->length = strlen(str);
  cord->str = malloc(cord->length + 1);
  strncpy(cord->str, str, cord->length + 1);
  return cord;
}

struct cord *cord_new_str_len(const char *str, size_t len) {
  struct cord *cord = cord_new();

  cord->length = len;
  cord->str = malloc(cord->length + 1);
  strncpy(cord->str, str, cord->length);
  return cord;
}

void cord_destroy(struct cord *cord) {
  free(cord->str);
  free(cord);

  /*
  struct cord *head = cord;
  while (head) {
    struct cord *next = head->next;

    free(head->str);
    free(head);

    head = next;
  }
  */
}

size_t cord_insert(struct cord *cord, ssize_t index, const char *str) {
  size_t ins_len = strlen(str);
  return cord_insert_len(cord, index, str, ins_len);
}

size_t cord_insert_len(struct cord *cord, ssize_t index, const char *str,
                       size_t length) {
  size_t ins_len = length;
  char *new_str = calloc(1, cord->length + ins_len);
  if (index < 0) {
    // append
    strncpy(new_str, cord->str, cord->length);
    strncpy(new_str + cord->length, str, ins_len);
  } else {
    strncpy(new_str, cord->str, (size_t)index);
    strncpy(new_str + index, str, ins_len);
    strncpy(new_str + index + ins_len, cord->str + index,
            cord->length - (size_t)index);
  }

  free(cord->str);
  cord->str = new_str;
  cord->length += ins_len;
  return ins_len;
}

size_t cord_delete(struct cord *cord, ssize_t index, size_t count) {
  if (count > cord->length) {
    count = cord->length;
  }

  if (index <= 0 && count == cord->length) {
    free(cord->str);
    cord->str = NULL;
    cord->length = 0;
    return count;
  } else if (index < 0) {
    // deletion from end is fake
    cord->length -= count;
    return count;
  }

  if (((size_t)index + count) > cord->length) {
    count = cord->length - (size_t)index;
  }

  char *new_str = calloc(1, cord->length - count);
  strncpy(new_str, cord->str, (size_t)index);
  strncpy(new_str + index, cord->str + index + count,
          cord->length - (size_t)index - count);

  free(cord->str);
  cord->str = new_str;
  cord->length -= count;
  return count;
}

size_t cord_append(struct cord *cord, struct cord *other) {
  return cord_insert_len(cord, -1, other->str, other->length);
}

struct cord *cord_split(struct cord *cord, size_t index) {
  if (index > cord->length) {
    return cord_new();
  }

  // truncate & create new cord
  cord->length = index;
  return cord_new_str(cord->str + index);
}

ssize_t cord_string(struct cord *cord, char *buf, size_t maxlen) {
  char *result =
      stpncpy(buf, cord->str, cord->length > maxlen ? maxlen : cord->length);
  ssize_t len = (result - buf);
  buf[len] = 0;
  return len;
}

size_t cord_substring(struct cord *cord, size_t at, char *buf, size_t length) {
  if (at > cord->length) {
    buf[0] = 0;
    return 0;
  }

  // this is cord_string
  if (at == 0 && length >= cord->length) {
    return (size_t)cord_string(cord, buf, length);
  }

  if ((at + length) > cord->length) {
    length = cord->length - at;
  }

  char *result = stpncpy(buf, cord->str + at, length);
  ssize_t len = (result - buf);
  buf[len] = 0;
  return (size_t)len;
}

void cord_attach(struct cord *cord, struct cord *next) {
  next->next = cord->next;
  cord->next = next;
}

void cord_attach_direct(struct cord *cord, struct cord *other) {
  cord->next = other;
}

void cord_detach(struct cord *cord, struct cord *other) {
  cord->next = other->next;
}

struct cord *cord_next(struct cord *cord) {
  return cord->next;
}

size_t cord_length(struct cord *cord) { return cord->length; }

void cord_unlink(struct cord *cord) { cord->next = NULL; }
