#pragma once
#include <stdbool.h>

#define RELAY_GPIO      4

void relay_init(void);
void relay_set(bool on);
bool relay_get(void);
