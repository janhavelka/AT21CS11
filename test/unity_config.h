#pragma once

#include <stdio.h>

#define UNITY_OUTPUT_START() ((void)0)
#define UNITY_OUTPUT_CHAR(c) \
  ((void)putchar((int)(unsigned char)(c)))
#define UNITY_OUTPUT_FLUSH() ((void)fflush(stdout))
#define UNITY_OUTPUT_COMPLETE() ((void)0)
