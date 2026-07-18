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

#include "key-protect.hpp"

#include <QByteArray>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#endif

namespace keyprotect {

#ifdef _WIN32

static const wchar_t *kBlobDescription = L"obs-multiscene-stream";

std::string protect(const std::string &plain)
{
	if (plain.empty())
		return {};

	DATA_BLOB in;
	in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(plain.data()));
	in.cbData = static_cast<DWORD>(plain.size());

	DATA_BLOB out = {};
	if (!CryptProtectData(&in, kBlobDescription, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
		return {};

	QByteArray b64 =
		QByteArray(reinterpret_cast<const char *>(out.pbData), static_cast<int>(out.cbData)).toBase64();
	SecureZeroMemory(out.pbData, out.cbData);
	LocalFree(out.pbData);
	return b64.toStdString();
}

std::optional<std::string> unprotect(const std::string &blob)
{
	if (blob.empty())
		return std::string();

	QByteArray raw = QByteArray::fromBase64(QByteArray(blob.data(), static_cast<int>(blob.size())));
	if (raw.isEmpty())
		return std::nullopt;

	DATA_BLOB in;
	in.pbData = reinterpret_cast<BYTE *>(raw.data());
	in.cbData = static_cast<DWORD>(raw.size());

	DATA_BLOB out = {};
	if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
		return std::nullopt;

	std::string plain(reinterpret_cast<const char *>(out.pbData), out.cbData);
	SecureZeroMemory(out.pbData, out.cbData);
	LocalFree(out.pbData);
	return plain;
}

#else // Linux/macOS: base64 obfuscation only; OS keychain planned for v2.

static const char kPrefix[] = "b64:";

std::string protect(const std::string &plain)
{
	if (plain.empty())
		return {};

	QByteArray b64 = QByteArray(plain.data(), static_cast<int>(plain.size())).toBase64();
	return kPrefix + b64.toStdString();
}

std::optional<std::string> unprotect(const std::string &blob)
{
	if (blob.empty())
		return std::string();

	if (blob.rfind(kPrefix, 0) != 0)
		return std::nullopt;

	QByteArray raw = QByteArray::fromBase64(
		QByteArray(blob.data() + sizeof(kPrefix) - 1, static_cast<int>(blob.size() - (sizeof(kPrefix) - 1))));
	return std::string(raw.constData(), static_cast<size_t>(raw.size()));
}

#endif

} // namespace keyprotect
