/*
 * BS5FileManager - PS5 home-screen launcher installer.
 *
 * Returns 1 when the launcher was installed/updated, 0 when it was already
 * current, and -1 on failure.
 */

#pragma once

int bs5fm_install_app_if_needed(void);
