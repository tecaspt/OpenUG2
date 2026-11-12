#ifndef OPENUG2_SHOT_H
#define OPENUG2_SHOT_H

/* Writes <dir>/<name>.ppm from the current draw buffer.
 * Must be called after sceGuSync and BEFORE the buffer swap. */
void shot_write(const char *name);

#endif
