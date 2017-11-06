#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include "filement.h"
#include "format.h"
#include "io.h"
#include "device/startup.h"

#define USAGE "usage: filement [--reset] [device_name email]\n"

#define PASSWORD "Device password: "

// http://en.wikibooks.org/wiki/Serial_Programming/termios

static size_t password(char *restrict buffer, size_t length)
{
	struct termios old, new;

	size_t index = 0, total = 0;
	ssize_t size;

	// TODO check whether 0 and 1 point to the same terminal
	if (!isatty(0)) return 0;

	int flags = fcntl(0, F_GETFL);

	// Get current terminal configuration. Turn echo off. Flush any pending data.
	fcntl(0, F_SETFL, flags & ~O_NONBLOCK);
	tcgetattr(0, &old);
	new = old;
	new.c_lflag &= ~(ECHO | IEXTEN);
	if (tcsetattr(0, TCSAFLUSH, &new) < 0) return 0;

	if (write(1, PASSWORD, sizeof(PASSWORD) - 1) != sizeof(PASSWORD) - 1) goto finally; // output error

	// Read password from standard input into buffer.
	while (true)
	{
		size = read(0, buffer + total, length - total);
		if (!size) break;
		else if (size < 0)
		{
			index = 0;
			break; // input error
		}

		total += size;

		// Find line end.
		do if (buffer[index] == '\n') goto finally;
		while (++index < total);

		if (total == length)
		{
			index = 0;
			break; // input too long
		}
	}

finally:

	if (write(1, "\n", 1) != 1) index = 0; // output error

	// Restore old terminal configuration.
	tcsetattr(0, TCSAFLUSH, &old);
	fcntl(0, F_SETFL, flags);

	return index;
}

#define STRING(s) (s), sizeof(s) - 1

int main(int argc, char *argv[])
{
	bool registered = filement_init();

	switch (argc)
	{
	case 1:
		if (!registered)
		{
			write(2, USAGE, sizeof(USAGE) - 1);
			return -2;
		}
		break;

	case 3:
		{
			char buffer[64];
			size_t length = password(buffer, sizeof(buffer));
			if (!length)
			{
				fprintf(stderr, "Invalid password.\n");
				return -1;
			}

			struct string devname = string(argv[1], strlen(argv[1]));
			struct string email = string(argv[2], strlen(argv[2]));
			struct string password = string(buffer, length);
			if (!filement_register(&email, &devname, &password))
			{
				fprintf(stderr, "Registration error.\n" USAGE, argv[0]);
				return -1;
			}

			// Add startup item.
			struct string path = string(PREFIX "bin/filement");
			startup_add(&path);
		}
		break;

	case 2:
		if (!memcmp(argv[1], STRING("--reset")))
		{
			filement_reset();
			return 0;
		}
	default:
		fprintf(stderr, USAGE, argv[0]);
		return -1;
	}

	filement_daemon();

	// Check for new version of the Filement device software.
	/*if (!filement_upgrade())
	{
		#define MESSAGE "Upgrade failed\n"
		write(2, MESSAGE, sizeof(MESSAGE) - 1);
		#undef MESSAGE
	}*/

	filement_serve();

	return 0;
}
