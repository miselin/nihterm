#ifndef _NIHTERM_CORD_H
#define _NIHTERM_CORD_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// struct cord defines a cord, a chain of strings that can be efficiently edited
// and manipulated.
struct cord;

// cord_new creates a new, empty cord.
struct cord *cord_new();

// cord_new_str creates a new cord containing the given string.
struct cord *cord_new_str(const char *str);

// cord_new_str_len creates a new cord containing the given string of the given
// length.
struct cord *cord_new_str_len(const char *str, size_t len);

// cord_destroy destroys the cord and frees associated memory.
void cord_destroy(struct cord *cord);

// cord_insert inserts the given string to the cord at the given index.
// If index is negative, the string will be appended to the end of the cord.
// Mutates the cord's string directly, use cord_split/cord_attach to avoid.
size_t cord_insert(struct cord *cord, ssize_t index, const char *str);

size_t cord_insert_len(struct cord *cord, ssize_t index, const char *str,
                       size_t length);

// cord_delete deletes characters from the cord at the given index.
// If index is negative, the characters are removed from the end of the cord.
size_t cord_delete(struct cord *cord, ssize_t index, size_t count);

// cord_split splits the cord into two cords at the given index
// The right half of the split is returned. The passed cord becomes the left
// half.
struct cord *cord_split(struct cord *cord, size_t index);

// cord_append appends the other cord onto this one
size_t cord_append(struct cord *cord, struct cord *other);

// cord_string places the full contents of the cord into buf.
ssize_t cord_string(struct cord *cord, char *buf, size_t maxlen);

// cord_substring places partial contents of the cord into buf.
size_t cord_substring(struct cord *cord, size_t at, char *buf, size_t length);

// attach the given cord after this one
void cord_attach(struct cord *cord, struct cord *next);

// detach the given cord from this one
void cord_detach(struct cord *cord, struct cord *other);

// attach the given cord to another one without modifying the other
void cord_attach_direct(struct cord *cord, struct cord *other);

// get the next cord from this one
struct cord *cord_next(struct cord *cord);

// get the current length of this cord
size_t cord_length(struct cord *cord);

// cord_unlink removes any linked cords from this one
void cord_unlink(struct cord *cord);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _NIHTERM_CORD_H
