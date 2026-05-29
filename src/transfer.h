/*
 * BS5FileManager - file-manager API.
 */

#pragma once

#include <stddef.h>

#include "websrv.h"


int transfer_request(const http_request_t *req, const char *url);

int transfer_upload_request(const http_request_t *req, const char *initial_data,
                            size_t initial_size, size_t content_size);
