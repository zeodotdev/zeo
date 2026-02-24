#ifndef ZEO_CONSTANTS_H
#define ZEO_CONSTANTS_H

#include <string>

#ifdef ZEO_RELEASE_BUILD
static const std::string ZEO_BASE_URL = "https://zeo.dev";
#else
static const std::string ZEO_BASE_URL = "https://zeo-staging.vercel.app";
#endif

#endif // ZEO_CONSTANTS_H
