/*
 * This file is part of Moonlight Embedded.
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdbool.h>

struct _CONFIGURATION;
struct _SERVER_DATA;

// Gamepad-driven launcher (Settings / Play / Connect). Settings picked in the
// launcher are kept in <keydir>/launcher.conf and override base.
// Returns true once the user picked an app: session holds base plus the launcher
// settings and server is connected and paired, ready to stream. Returns false
// when the user quits. message (may be NULL) is shown when the launcher opens.
bool ui_run(const struct _CONFIGURATION* base, struct _CONFIGURATION* session, struct _SERVER_DATA* server, const char* message, bool splash);
