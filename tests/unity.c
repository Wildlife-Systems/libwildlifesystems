/* =========================================================================
    Unity - A Test Framework for C
    Minimal implementation for WildlifeSystems tests
========================================================================= */

#include <stdio.h>
#include <string.h>
#include "unity.h"

UnityState Unity;

void UnityBegin(const char* filename) {
    Unity.TestFile = filename;
    Unity.CurrentTestName = NULL;
    Unity.CurrentTestLineNumber = 0;
    Unity.NumberOfTests = 0;
    Unity.TestFailures = 0;
    Unity.TestIgnores = 0;
    Unity.CurrentTestFailed = 0;
    Unity.CurrentTestIgnored = 0;
}

int UnityEnd(void) {
    printf("\n-----------------------\n");
    printf("%d Tests %d Failures %d Ignored\n", 
           Unity.NumberOfTests, Unity.TestFailures, Unity.TestIgnores);
    if (Unity.TestFailures == 0) {
        printf("OK\n");
    } else {
        printf("FAIL\n");
    }
    return Unity.TestFailures;
}

void UnityConcludeTest(void) {
    if (Unity.CurrentTestIgnored) {
        Unity.TestIgnores++;
    } else if (Unity.CurrentTestFailed) {
        Unity.TestFailures++;
    }
    Unity.CurrentTestFailed = 0;
    Unity.CurrentTestIgnored = 0;
}

void UnityDefaultTestRun(UnityTestFunction Func, const char* FuncName, int FuncLineNum) {
    Unity.CurrentTestName = FuncName;
    Unity.CurrentTestLineNumber = FuncLineNum;
    Unity.NumberOfTests++;
    
    printf("%s:%d:%s:", Unity.TestFile, FuncLineNum, FuncName);
    
    if (TEST_PROTECT()) {
        setUp();
        Func();
        tearDown();
    }
    
    UnityConcludeTest();
    
    if (!Unity.CurrentTestFailed && !Unity.CurrentTestIgnored) {
        printf("PASS\n");
    }
}
