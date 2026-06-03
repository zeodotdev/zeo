/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef ZEO_CONSTANTS_H
#define ZEO_CONSTANTS_H

#include <string>

#ifdef ZEO_RELEASE_BUILD
static const std::string ZEO_BASE_URL = "https://www.zeo.dev";
#else
static const std::string ZEO_BASE_URL = "https://zeo-staging.vercel.app";
#endif

#endif // ZEO_CONSTANTS_H
