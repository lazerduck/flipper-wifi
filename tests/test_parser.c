#include "unity.h"
#include "parser.h"

void setUp(void)    {}
void tearDown(void) {}

/* ─── Tests that pass today ───────────────────────────────────────────────── *
 * These check boundary conditions that the stub satisfies correctly and that  *
 * the real implementation must also satisfy.                                  */

void test_parse_empty_line_returns_false(void)
{
    parsed_cmd_t cmd = {0};
    TEST_ASSERT_FALSE(parser_parse("", &cmd));
}

void test_parse_whitespace_only_returns_false(void)
{
    parsed_cmd_t cmd = {0};
    TEST_ASSERT_FALSE(parser_parse("   \n", &cmd));
}

void test_get_arg_returns_null_on_zeroed_cmd(void)
{
    parsed_cmd_t cmd = {0};
    TEST_ASSERT_NULL(parser_get_arg(&cmd, "duration"));
}

void test_parser_free_on_zeroed_cmd_does_not_crash(void)
{
    parsed_cmd_t cmd = {0};
    parser_free(&cmd);
    TEST_PASS_MESSAGE("completed without crash");
}

/* ─── Tests requiring a real parser implementation ───────────────────────── *
 * Remove TEST_IGNORE_MESSAGE and verify the assertions below it once          *
 * parser_parse() is implemented in parser.c.                                  *
 *                                                                             *
 * Manual cmd setup pattern (since parser_parse itself is under test):         *
 *   parsed_cmd_t cmd = {0};                                                   *
 *   TEST_ASSERT_TRUE(parser_parse("BLE_SCAN duration=\"10\"\n", &cmd));       *
 *   TEST_ASSERT_EQUAL_INT(2, cmd.token_count);                                *
 *   TEST_ASSERT_EQUAL_STRING("BLE",  cmd.tokens[0]);                          *
 *   TEST_ASSERT_EQUAL_STRING("SCAN", cmd.tokens[1]);                          *
 *   TEST_ASSERT_EQUAL_STRING("10", parser_get_arg(&cmd, "duration"));         */

void test_parse_single_token_command(void)
{
    TEST_IGNORE_MESSAGE("Implement parser_parse() — PING\\n → 1 token [PING], 0 args");
}

void test_parse_two_token_command(void)
{
    TEST_IGNORE_MESSAGE("Implement parser_parse() — BLE_SCAN\\n → tokens [BLE, SCAN]");
}

void test_parse_three_token_command(void)
{
    TEST_IGNORE_MESSAGE("Implement parser_parse() — WIFI_PROMISCUOUS_SCAN ... → tokens [WIFI, PROMISCUOUS, SCAN]");
}

void test_parse_command_with_one_arg(void)
{
    TEST_IGNORE_MESSAGE("Implement parser_parse() — duration=\"10\" arg parsed and accessible");
}

void test_parse_command_with_multiple_args(void)
{
    TEST_IGNORE_MESSAGE("Implement parser_parse() — channels=\"1|6|11\" duration=\"30\" both accessible");
}

void test_get_arg_returns_value_for_known_key(void)
{
    TEST_IGNORE_MESSAGE("Implement parser_parse() — parser_get_arg(\"duration\") == \"10\"");
}

void test_get_arg_returns_null_for_unknown_key(void)
{
    TEST_IGNORE_MESSAGE("Implement parser_parse() — parser_get_arg(\"nonexistent\") == NULL");
}

void test_parser_free_resets_token_and_arg_counts(void)
{
    TEST_IGNORE_MESSAGE("Implement parser_parse() — token_count and arg_count both 0 after parser_free()");
}

void test_crlf_line_ending_accepted(void)
{
    TEST_IGNORE_MESSAGE("Implement parser_parse() — PING\\r\\n should parse identically to PING\\n");
}

int main(void)
{
    UNITY_BEGIN();

    /* Passing today */
    RUN_TEST(test_parse_empty_line_returns_false);
    RUN_TEST(test_parse_whitespace_only_returns_false);
    RUN_TEST(test_get_arg_returns_null_on_zeroed_cmd);
    RUN_TEST(test_parser_free_on_zeroed_cmd_does_not_crash);

    /* Ignored until parser_parse() is implemented */
    RUN_TEST(test_parse_single_token_command);
    RUN_TEST(test_parse_two_token_command);
    RUN_TEST(test_parse_three_token_command);
    RUN_TEST(test_parse_command_with_one_arg);
    RUN_TEST(test_parse_command_with_multiple_args);
    RUN_TEST(test_get_arg_returns_value_for_known_key);
    RUN_TEST(test_get_arg_returns_null_for_unknown_key);
    RUN_TEST(test_parser_free_resets_token_and_arg_counts);
    RUN_TEST(test_crlf_line_ending_accepted);

    return UNITY_END();
}
