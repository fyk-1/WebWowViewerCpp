#ifndef AWEBWOWVIEWERCPP_CRASHSTACK_H
#define AWEBWOWVIEWERCPP_CRASHSTACK_H

// Writes the faulting address and the crashing thread's stack to stderr. Takes the filter's EXCEPTION_POINTERS.
void printCrashStack(void *exceptionPointers);

#endif
