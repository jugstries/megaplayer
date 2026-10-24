/*
 * Incluso a forza nei sorgenti C di UADE quando si compila per Windows
 * (MinGW-w64): le poche funzioni POSIX che UADE usa e Windows non ha. Nella
 * cartella include/ accanto ci sono gli header di rete e di processo che UADE
 * include, ridotti a Winsock o vuoti.
 */
#ifndef MP_UADE_WINDOWS_COMPAT_H
#define MP_UADE_WINDOWS_COMPAT_H

#include <direct.h>
#include <io.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static inline char *realpath(const char *path, char *resolved)
{
	/* Con resolved == NULL alloca con malloc(), come realpath(). */
	return _fullpath(resolved, path, resolved ? PATH_MAX : 0);
}

/* mkdir(path, mode) di POSIX: su Windows non c'è il modo. Gli header di
   sistema che dichiarano mkdir() sono già inclusi qui sopra. */
#define mkdir(path, mode) _mkdir(path)

/* Niente link simbolici da seguire. */
#define lstat stat
#ifndef S_ISLNK
#define S_ISLNK(mode) 0
#endif

/* UADE blocca il database delle durate contro altri processi uadecore: qui
   c'è un solo processo. */
#define F_ULOCK 0
#define F_LOCK 1
static inline int lockf(int fd, int command, off_t length)
{
	(void) fd;
	(void) command;
	(void) length;
	return 0;
}

static inline char *strsep(char **string, const char *delimiters)
{
	char *start = *string;
	char *end;
	if (start == NULL)
		return NULL;
	end = start + strcspn(start, delimiters);
	if (*end != '\0') {
		*end = '\0';
		*string = end + 1;
	} else {
		*string = NULL;
	}
	return start;
}

#endif
