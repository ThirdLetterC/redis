#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hiredis/alloc.h"
#include "hiredis/hiredis.h"

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "Assertion failed: %s (%s:%d)\n", #cond, __FILE__,      \
              __LINE__);                                                       \
      ok = false;                                                              \
      goto cleanup;                                                            \
    }                                                                          \
  } while (false)

static bool test_format_command_simple() {
  bool ok = true;
  char *cmd = nullptr;

  auto len = redisFormatCommand(&cmd, "SET %s %s", "foo", "bar");
  static constexpr const char expected[] =
      "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n";

  CHECK(len > 0);
  CHECK((size_t)len == sizeof(expected) - 1);
  CHECK(memcmp(cmd, expected, sizeof(expected)) == 0);

cleanup:
  redisFreeCommand(cmd);
  return ok;
}

static bool test_format_command_binary_payload() {
  bool ok = true;
  char *cmd = nullptr;

  static constexpr char payload[] = {'h', 'i', '\0', '!'};
  auto len = redisFormatCommand(&cmd, "SET %s %b", "blob", payload,
                                sizeof(payload));
  static constexpr const char expected[] =
      "*3\r\n$3\r\nSET\r\n$4\r\nblob\r\n$4\r\nhi\0!\r\n";

  CHECK(len > 0);
  CHECK((size_t)len == sizeof(expected) - 1);
  CHECK(memcmp(cmd, expected, sizeof(expected)) == 0);

cleanup:
  redisFreeCommand(cmd);
  return ok;
}

static bool test_format_command_argv_with_explicit_lengths() {
  bool ok = true;
  char *cmd = nullptr;

  static constexpr char key[] = {'k', '\0', 'y'};
  static constexpr char value[] = {'v', '\0', 'l'};
  static constexpr char op[] = "SET";
  static const char *argv[] = {op, key, value};
  static constexpr size_t argvlen[] = {sizeof(op) - 1, sizeof(key),
                                       sizeof(value)};

  auto len = redisFormatCommandArgv(&cmd, 3, argv, argvlen);
  static constexpr const char expected[] =
      "*3\r\n$3\r\nSET\r\n$3\r\nk\0y\r\n$3\r\nv\0l\r\n";

  CHECK(len > 0);
  CHECK((size_t)len == sizeof(expected) - 1);
  CHECK(memcmp(cmd, expected, sizeof(expected)) == 0);

cleanup:
  redisFreeCommand(cmd);
  return ok;
}

static bool test_format_command_argv_with_strlen_lengths() {
  bool ok = true;
  char *cmd = nullptr;

  static const char *argv[] = {"INCR", "counter"};
  auto len = redisFormatCommandArgv(&cmd, 2, argv, nullptr);
  static constexpr const char expected[] = "*2\r\n$4\r\nINCR\r\n$7\r\ncounter\r\n";

  CHECK(len > 0);
  CHECK((size_t)len == sizeof(expected) - 1);
  CHECK(memcmp(cmd, expected, sizeof(expected)) == 0);

cleanup:
  redisFreeCommand(cmd);
  return ok;
}

static bool test_format_command_invalid_inputs() {
  bool ok = true;
  char *cmd = nullptr;
  static const char *argv[] = {"PING"};
  static const char *bad_argv[] = {"SET", nullptr, "x"};

  CHECK(redisFormatCommand(nullptr, "PING") == -1);
  CHECK(redisFormatCommand(&cmd, "PING %q", "x") == -1);
  CHECK(cmd == nullptr);

  CHECK(redisFormatCommandArgv(nullptr, 1, argv, nullptr) == -1);
  CHECK(redisFormatCommandArgv(&cmd, -1, argv, nullptr) == -1);
  CHECK(redisFormatCommandArgv(&cmd, 1, nullptr, nullptr) == -1);
  CHECK(redisFormatCommandArgv(&cmd, 3, bad_argv, nullptr) == -1);
  CHECK(cmd == nullptr);

cleanup:
  redisFreeCommand(cmd);
  return ok;
}

static bool test_reader_parses_fragmented_status_reply() {
  bool ok = true;
  redisReader *reader = redisReaderCreate();
  void *reply = nullptr;

  CHECK(reader != nullptr);
  CHECK(redisReaderFeed(reader, "+PO", 3) == REDIS_OK);
  CHECK(redisReaderGetReply(reader, &reply) == REDIS_OK);
  CHECK(reply == nullptr);

  CHECK(redisReaderFeed(reader, "NG\r\n", 4) == REDIS_OK);
  CHECK(redisReaderGetReply(reader, &reply) == REDIS_OK);
  CHECK(reply != nullptr);

  auto typed = (redisReply *)reply;
  CHECK(typed->type == REDIS_REPLY_STATUS);
  CHECK(typed->len == 4);
  CHECK(memcmp(typed->str, "PONG", 4) == 0);

cleanup:
  freeReplyObject(reply);
  redisReaderFree(reader);
  return ok;
}

static bool test_reader_parses_array_of_integers() {
  bool ok = true;
  redisReader *reader = redisReaderCreate();
  void *reply = nullptr;

  CHECK(reader != nullptr);
  CHECK(redisReaderFeed(reader, "*2\r\n:41\r\n:42\r\n",
                        sizeof("*2\r\n:41\r\n:42\r\n") - 1) == REDIS_OK);
  CHECK(redisReaderGetReply(reader, &reply) == REDIS_OK);
  CHECK(reply != nullptr);

  auto typed = (redisReply *)reply;
  CHECK(typed->type == REDIS_REPLY_ARRAY);
  CHECK(typed->elements == 2);
  CHECK(typed->element[0] != nullptr);
  CHECK(typed->element[1] != nullptr);
  CHECK(typed->element[0]->type == REDIS_REPLY_INTEGER);
  CHECK(typed->element[1]->type == REDIS_REPLY_INTEGER);
  CHECK(typed->element[0]->integer == 41);
  CHECK(typed->element[1]->integer == 42);

cleanup:
  freeReplyObject(reply);
  redisReaderFree(reader);
  return ok;
}

static bool test_reader_protocol_error() {
  bool ok = true;
  redisReader *reader = redisReaderCreate();
  void *reply = nullptr;

  CHECK(reader != nullptr);
  CHECK(redisReaderFeed(reader, "!\r\n", sizeof("!\r\n") - 1) == REDIS_OK);
  CHECK(redisReaderGetReply(reader, &reply) == REDIS_ERR);
  CHECK(reply == nullptr);
  CHECK(reader->err == REDIS_ERR_PROTOCOL);
  CHECK(strstr(reader->errstr, "Protocol error") != nullptr);

cleanup:
  freeReplyObject(reply);
  redisReaderFree(reader);
  return ok;
}

static size_t g_custom_calloc_calls = 0;

static void *test_calloc(size_t nmemb, size_t size) {
  g_custom_calloc_calls++;
  return calloc(nmemb, size);
}

static bool test_allocator_override_and_overflow_guard() {
  bool ok = true;

  hiredisAllocFuncs initial = hiredisAllocFns;
  hiredisAllocFuncs custom = {
      .mallocFn = nullptr,
      .callocFn = test_calloc,
      .reallocFn = nullptr,
      .strdupFn = nullptr,
      .freeFn = nullptr,
  };

  g_custom_calloc_calls = 0;
  auto previous = hiredisSetAllocators(&custom);
  CHECK(previous.callocFn == initial.callocFn);
  CHECK(hiredisAllocFns.callocFn == test_calloc);

  auto ptr = hi_calloc(4, sizeof(int));
  CHECK(ptr != nullptr);
  CHECK(g_custom_calloc_calls == 1);
  hi_free(ptr);

  constexpr size_t too_many = (SIZE_MAX / 2) + 1;
  auto before = g_custom_calloc_calls;
  CHECK(hi_calloc(too_many, 2) == nullptr);
  CHECK(g_custom_calloc_calls == before);

  hiredisSetAllocators(&previous);
  CHECK(hiredisAllocFns.callocFn == initial.callocFn);

cleanup:
  hiredisSetAllocators(&initial);
  return ok;
}

typedef bool (*test_fn)();

typedef struct test_case {
  const char *name;
  test_fn fn;
} test_case;

int main() {
  static const test_case tests[] = {
      {"format_command_simple", test_format_command_simple},
      {"format_command_binary_payload", test_format_command_binary_payload},
      {"format_command_argv_with_explicit_lengths",
       test_format_command_argv_with_explicit_lengths},
      {"format_command_argv_with_strlen_lengths",
       test_format_command_argv_with_strlen_lengths},
      {"format_command_invalid_inputs", test_format_command_invalid_inputs},
      {"reader_parses_fragmented_status_reply",
       test_reader_parses_fragmented_status_reply},
      {"reader_parses_array_of_integers", test_reader_parses_array_of_integers},
      {"reader_protocol_error", test_reader_protocol_error},
      {"allocator_override_and_overflow_guard",
       test_allocator_override_and_overflow_guard},
  };

  size_t failures = 0;
  for (size_t i = 0; i < (sizeof(tests) / sizeof(tests[0])); i++) {
    auto passed = tests[i].fn();
    printf("[%s] %s\n", passed ? "PASS" : "FAIL", tests[i].name);
    if (!passed) {
      failures++;
    }
  }

  if (failures != 0) {
    fprintf(stderr, "%zu test(s) failed.\n", failures);
    return EXIT_FAILURE;
  }

  printf("All %zu tests passed.\n", sizeof(tests) / sizeof(tests[0]));
  return EXIT_SUCCESS;
}
