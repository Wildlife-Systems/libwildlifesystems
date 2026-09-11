/* =========================================================================
    Unity - A Test Framework for C
    ThrowTheSwitch.org
    Copyright (c) 2007-2021 Mike Karlesky, Mark VanderVoord, Greg Williams
    SPDX-License-Identifier: MIT
========================================================================= */

#ifndef UNITY_H
#define UNITY_H

#include <stddef.h>
#include <stdint.h>

/* Unity Internals */
typedef void (*UnityTestFunction)(void);

typedef struct {
    const char* TestFile;
    const char* CurrentTestName;
    int CurrentTestLineNumber;
    int NumberOfTests;
    int TestFailures;
    int TestIgnores;
    int CurrentTestFailed;
    int CurrentTestIgnored;
} UnityState;

extern UnityState Unity;

void UnityBegin(const char* filename);
int UnityEnd(void);
void UnityConcludeTest(void);
void UnityDefaultTestRun(UnityTestFunction Func, const char* FuncName, int FuncLineNum);

/* Defined by each test file and called by UnityDefaultTestRun around every
   test. Declared here so unity.c does not have to assume them implicitly. */
void setUp(void);
void tearDown(void);

/* Test Macros */
#define TEST_PROTECT() 1
#define TEST_ABORT() return

#define TEST_ASSERT_MESSAGE(condition, message) \
    if (!(condition)) { Unity.CurrentTestFailed = 1; printf("  FAIL: %s (line %d): %s\n", Unity.CurrentTestName, __LINE__, message); }

#define TEST_ASSERT(condition) TEST_ASSERT_MESSAGE(condition, #condition)

#define TEST_ASSERT_TRUE(condition) TEST_ASSERT_MESSAGE(condition, "Expected TRUE")
#define TEST_ASSERT_FALSE(condition) TEST_ASSERT_MESSAGE(!(condition), "Expected FALSE")
#define TEST_ASSERT_NULL(pointer) TEST_ASSERT_MESSAGE((pointer) == NULL, "Expected NULL")
#define TEST_ASSERT_NOT_NULL(pointer) TEST_ASSERT_MESSAGE((pointer) != NULL, "Expected NOT NULL")

#define TEST_ASSERT_EQUAL_INT(expected, actual) \
    if ((expected) != (actual)) { Unity.CurrentTestFailed = 1; printf("  FAIL: %s (line %d): Expected %d, got %d\n", Unity.CurrentTestName, __LINE__, (int)(expected), (int)(actual)); }

#define TEST_ASSERT_EQUAL_STRING(expected, actual) \
    if (strcmp((expected), (actual)) != 0) { Unity.CurrentTestFailed = 1; printf("  FAIL: %s (line %d): Expected \"%s\", got \"%s\"\n", Unity.CurrentTestName, __LINE__, (expected), (actual)); }

#define TEST_ASSERT_EQUAL_FLOAT(expected, actual, epsilon) \
    if (fabs((expected) - (actual)) > (epsilon)) { Unity.CurrentTestFailed = 1; printf("  FAIL: %s (line %d): Expected %f, got %f\n", Unity.CurrentTestName, __LINE__, (double)(expected), (double)(actual)); }

#define RUN_TEST(func) UnityDefaultTestRun(func, #func, __LINE__)

#define UNITY_BEGIN() UnityBegin(__FILE__)
#define UNITY_END() UnityEnd()

#endif /* UNITY_H */
