#include <stdio.h>
#include <regex.h>

int main(int argc, char *argv[])
{
	regex_t regex;

	if (argc != 3)
	{
		fprintf(stderr, "regex <pattern> <string>\n");
		return 1;
	}

	if (regcomp(&regex, argv[1], REG_NOSUB | REG_EXTENDED))
	{
		fprintf(stderr, "memory error\n");
		return 1;
	}

	if (regexec(&regex, argv[2], 0, 0, 0))
		printf("no match\n");
	else
		printf("match\n");

	return 0;
}
