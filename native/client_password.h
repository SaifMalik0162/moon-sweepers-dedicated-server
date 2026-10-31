#ifndef MOON_CLIENT_PASSWORD_H
#define MOON_CLIENT_PASSWORD_H
#include <stddef.h>
/* Runs a client-only prompt on its own UI thread. No password is logged or saved.
   Returns 0 while pending, 1 when ready, -1 if canceled/invalid/unavailable. */
int MoonPassword_Read(char *output,size_t capacity);
void MoonPassword_NotifyRejected(void);
#endif
