/*
 * BS5FileManager - small MIME helper for direct downloads.
 *
 * The UI only needs enough types for browser previews and common archives.
 * Unknown extensions fall back to application/octet-stream in the caller.
 */

#include <stddef.h>
#include <strings.h>

#include "mime.h"

typedef struct mime_entry {
  const char *ext;
  const char *mime;
} mime_entry_t;

static const mime_entry_t g_mime[] = {
  {"css",  "text/css"},
  {"gif",  "image/gif"},
  {"htm",  "text/html"},
  {"html", "text/html"},
  {"jpg",  "image/jpeg"},
  {"jpeg", "image/jpeg"},
  {"js",   "application/javascript"},
  {"json", "application/json"},
  {"log",  "text/plain"},
  {"md",   "text/markdown"},
  {"mp4",  "video/mp4"},
  {"png",  "image/png"},
  {"pkg",  "application/octet-stream"},
  {"rap",  "application/octet-stream"},
  {"rif",  "application/octet-stream"},
  {"self", "application/octet-stream"},
  {"txt",  "text/plain"},
  {"webp", "image/webp"},
  {"xml",  "application/xml"},
  {"zip",  "application/zip"},
  {NULL, NULL}
};


const char *
mime_get_type(const char *filename) {
  const char *ext = NULL;

  if(!filename) return NULL;

  for(const char *p = filename; *p; p++) {
    if(*p == '.') ext = p + 1;
  }
  if(!ext || !*ext) return NULL;

  for(const mime_entry_t *m = g_mime; m->ext; m++) {
    if(!strcasecmp(ext, m->ext)) return m->mime;
  }

  return NULL;
}
