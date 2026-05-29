/*
 * BS5FileManager - PS5 notification helpers.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "notify.h"


#define SCE_NOTIFICATION_LOCAL_USER_ID_SYSTEM 0xFE


typedef struct notify_request {
  char unused[45];
  char message[3075];
} notify_request_t;


int sceNotificationSend(int userId, bool isLogged, const char *payload);
int sceKernelSendNotificationRequest(int, notify_request_t *req, size_t size,
                                     int flags);


static void
notify_debug(const char *message, const char *submessage) {
  notify_request_t req;
  memset(&req, 0, sizeof(req));

  if(submessage && submessage[0]) {
    snprintf(req.message, sizeof(req.message), "%s\n%s", message, submessage);
  } else {
    snprintf(req.message, sizeof(req.message), "%s", message);
  }

  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}


void
bs5fm_notify(const char *message, const char *submessage) {
  char payload[4096];
  const char *msg = message ? message : "BS5FileManager";
  const char *sub = submessage ? submessage : "";

  snprintf(payload, sizeof(payload),
           "{"
           "\"rawData\":{"
           "\"viewTemplateType\":\"InteractiveToastTemplateB\","
           "\"channelType\":\"Downloads\","
           "\"useCaseId\":\"IDC\","
           "\"toastOverwriteType\":\"No\","
           "\"isImmediate\":true,"
           "\"priority\":100,"
           "\"viewData\":{"
           "\"icon\":{\"type\":\"Predefined\",\"parameters\":{\"icon\":\"download\"}},"
           "\"message\":{\"body\":\"%s\"},"
           "\"subMessage\":{\"body\":\"%s\"}"
           "},"
           "\"platformViews\":{"
           "\"previewDisabled\":{"
           "\"viewData\":{"
           "\"icon\":{\"type\":\"Predefined\",\"parameters\":{\"icon\":\"download\"}},"
           "\"message\":{\"body\":\"%s\"}"
           "}"
           "}"
           "}"
           "},"
           "\"localNotificationId\":\"5905\""
           "}",
           msg, sub, msg);

  sceNotificationSend(SCE_NOTIFICATION_LOCAL_USER_ID_SYSTEM, true, payload);
  notify_debug(msg, sub);
}
