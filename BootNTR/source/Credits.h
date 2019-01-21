#pragma once

typedef struct credits_entry_s {
	char* name;
	char* whatHeDo;
} credits_entry_t;

typedef struct credits_s {
	credits_entry_t** entries;
	int count;
} credits_t;