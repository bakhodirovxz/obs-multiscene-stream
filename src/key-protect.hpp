/*
obs-multiscene-stream
Copyright (C) 2026 obs-multiscene-stream contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include <optional>
#include <string>

namespace keyprotect {

// Returns an opaque blob safe to write to disk (SEC-1).
// Windows: per-user DPAPI, base64-encoded. Other OS: base64 obfuscation.
// Empty input yields an empty blob.
std::string protect(const std::string &plain);

// Returns std::nullopt when the blob cannot be decrypted (e.g. the config
// was copied to another Windows account, T-23). An empty blob decodes to
// an empty key.
std::optional<std::string> unprotect(const std::string &blob);

} // namespace keyprotect
