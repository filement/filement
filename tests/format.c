#include <string.h>

#include <check.h>

#include "types.h"
#include "format.h"

// http://check.sourceforge.net/doc/check_html/check_3.html

/*
int a = creat("check", 0644);
write(a, output, sizeof(expected));
write(a, "\n", 1);
write(a, expected, sizeof(expected));
write(a, "\n", 1);
close(a);
*/

START_TEST(test_byte)
{
	char input0 = '@', input1 = '?';
	char expected[5] = "@@@\x7f\x81";
	char output[5];
	output[3] = '\x81';
	output[4] = '\x81';

	*format_byte(output, input0, 3) = '\x7f';

	ck_assert(!memcmp(output, expected, sizeof(expected)));

	////

	expected[0] = input1;
	format_byte(output, input1, 1);

	ck_assert(!memcmp(output, expected, sizeof(expected)));
}
END_TEST

START_TEST(test_bytes)
{
	#define STRING "some string \n\xff\x00 !@#$%^&*()"

	char input[sizeof(STRING) - 1] = STRING;
	char expected[sizeof(input) + 2] = STRING "\x7f\x81";
	char output[sizeof(input) + 2];
	output[sizeof(input)] = '\x81';
	output[sizeof(input) + 1] = '\x81';

	#undef STRING

	*format_bytes(output, input, sizeof(input)) = '\x7f';

	ck_assert(!memcmp(output, expected, sizeof(expected)));
}
END_TEST

int main()
{
	unsigned failed;

	Suite *s = suite_create("format");
	TCase *tc;

	tc = tcase_create("byte");
	tcase_add_test(tc, test_byte);
	suite_add_tcase(s, tc);

	tc = tcase_create("bytes");
	tcase_add_test(tc, test_bytes);
	suite_add_tcase(s, tc);

	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_VERBOSE);
	failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return failed;
}
