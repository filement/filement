#include <stdlib.h>
#include <unistd.h>

#include "types.h"
#include "protocol.h"
#include "uuid.h"

int main(void)
{
	struct string *uuid = uuid_alloc(0, 0);
	write(1, uuid->data, uuid->length);
	write(1, "\n", 1);
	free(uuid);

	return 0;
}
