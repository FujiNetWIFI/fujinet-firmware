/* fujiedit.h -- the on-screen keyboard, the grid the Intellivision and Astrocade clients
 * use: all 64 characters from space to underscore in 16 columns by 4 rows. CASE redraws
 * the letter rows in lowercase; only the ESC cell cancels, never backspace. It runs no
 * mailbox transaction, so a caller's data in the reply window survives the call.
 */

#ifndef FUJIEDIT_H
#define FUJIEDIT_H

#include <stdbool.h>

/* The edit buffer. The caller preloads it NUL-terminated and reads it back
 * afterwards; it is mutated either way, so copy out only on accept. Sized for
 * the longest thing anyone types here, a 63-character WiFi password. */
#define FN_ENTRY_MAX 64
extern char fn_entry[FN_ENTRY_MAX];

/* Run the editor over fn_entry. `maxlen` is the longest string to allow, not
 * counting the NUL. Returns true on accept, false on cancel. */
bool fn_edit(const char *title, unsigned char maxlen);

#endif /* FUJIEDIT_H */
