#
#  Zeo Version Configuration
#
#  Copyright (C) 2024-2025 Zeo Developers
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#

# Zeo version follows semantic versioning, parallel to KiCad's system.
# This version is used when git tags are not available.
#
# Version format: MAJOR.MINOR.PATCH[-extra]
#   MAJOR: Incremented for major releases with breaking changes
#   MINOR: Incremented for new features (99 = nightly/development)
#   PATCH: Incremented for bug fixes
#
set( ZEO_SEMANTIC_VERSION "0.1.1" )

# Default the version to the semantic version.
# This could be overridden by git tags in the future.
set( ZEO_VERSION "${ZEO_SEMANTIC_VERSION}" )
