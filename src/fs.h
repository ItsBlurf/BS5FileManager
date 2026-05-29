/*
 * BS5FileManager - direct file serving helpers.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "websrv.h"


int fs_request(const http_request_t *req, const char *url);

uint8_t *fs_readfile(const char *path, size_t *size);
