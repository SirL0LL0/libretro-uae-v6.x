/*
 * C version of upstream's include/cfgfile_mount.h (WinUAE 6.1), used by the
 * libretro core, which compiles the emulator as C. Same parsing and
 * formatting rules, on fixed size TCHAR buffers instead of std::basic_string.
 */
#ifndef UAE_CFGFILE_MOUNT_C_H
#define UAE_CFGFILE_MOUNT_C_H

/* Read one field without modifying the input. Callers retain their on-disk
 * quoting convention: modern core mount paths keep backslashes literally,
 * while editor fields unescape a backslash followed by another character.
 * Returns false when the field is malformed or does not fit "fieldsize". */
static bool cfgfile_mount_field(const TCHAR **input, TCHAR separator,
	TCHAR *field, size_t fieldsize, bool require_separator,
	bool unescape_quoted, bool allow_path_suffix)
{
	const TCHAR *next = *input;
	size_t len = 0;
#define CFGMOUNT_PUT(c) do { if (len + 1 >= fieldsize) return false; field[len++] = (c); } while (0)
	if (*next == '"') {
		next++;
		while (*next && *next != '"') {
			if (unescape_quoted && *next == '\\') {
				next++;
				if (!*next) {
					return false;
				}
			}
			CFGMOUNT_PUT(*next);
			next++;
		}
		if (*next != '"') {
			return false;
		}
		next++;
	} else {
		while (*next && *next != separator) {
			CFGMOUNT_PUT(*next);
			next++;
		}
	}
#undef CFGMOUNT_PUT
	if (*next == separator) {
		next++;
	} else if (!(allow_path_suffix && *next == ':')
		&& (*next || require_separator)) {
		return false;
	}
	field[len] = 0;
	*input = next;
	return true;
}

struct cfgfile_mount_prefix {
	bool readonly;
	TCHAR device[MAX_DPATH], volume[MAX_DPATH], path[MAX_DPATH];
	size_t consumed;
};

/* The common prefix of filesystem2, hardfile2 and uaehf mount values.
 * Geometry, controller-specific options and legacy drive suffixes follow it. */
static bool cfgfile_parse_mount_prefix(const TCHAR *input, bool directory,
	struct cfgfile_mount_prefix *prefix, bool unescape_quoted,
	bool allow_path_suffix)
{
	const TCHAR *next = input;
	TCHAR access[4];
	struct cfgfile_mount_prefix parsed;
	memset(&parsed, 0, sizeof parsed);
	if (!cfgfile_mount_field(&next, ',', access, sizeof access / sizeof(TCHAR), true, unescape_quoted, false)
		|| _tcslen(access) != 2 || (access[0] != 'r' && access[0] != 'R')) {
		return false;
	}
	if (access[1] == 'o' || access[1] == 'O') {
		parsed.readonly = true;
	} else if (access[1] != 'w' && access[1] != 'W') {
		return false;
	}
	if (!cfgfile_mount_field(&next, ':', parsed.device, MAX_DPATH, true, unescape_quoted, false)
		|| (directory && !cfgfile_mount_field(&next, ':', parsed.volume, MAX_DPATH, true, unescape_quoted, false))
		|| !cfgfile_mount_field(&next, ',', parsed.path, MAX_DPATH, true, unescape_quoted, allow_path_suffix)) {
		return false;
	}
	parsed.consumed = next - input;
	*prefix = parsed;
	return true;
}

/* Escape only commas, quotes and leading or trailing spaces.
 * Returns an allocated string. */
static TCHAR *cfgfile_escape_min_string(const TCHAR *value, bool quote_quotes)
{
	size_t n = _tcslen(value), i;
	bool quote = false;
	TCHAR *result = xmalloc(TCHAR, 2 * n + 3), *p = result;
	for (i = 0; i < n; i++) {
		if (value[i] == ',' || (quote_quotes && value[i] == '"')
			|| (value[i] == ' ' && (i == 0 || i + 1 == n))) {
			quote = true;
		}
	}
	if (quote) {
		*p++ = '"';
	}
	for (i = 0; i < n; i++) {
		if (value[i] == '"') {
			*p++ = '\\';
		}
		*p++ = value[i];
	}
	if (quote) {
		*p++ = '"';
	}
	*p = 0;
	return result;
}

/* Formatters write into "out" (at least MAX_DPATH * 2 characters). */
static void cfgfile_format_filesystem(TCHAR *out, bool readonly,
	const TCHAR *device, const TCHAR *volume, const TCHAR *path, int bootpri)
{
	_stprintf(out, _T("r%c,%s:%s:%s,%d"), readonly ? 'o' : 'w', device, volume, path, bootpri);
}

static void cfgfile_format_hardfile(TCHAR *out, bool readonly,
	const TCHAR *device, const TCHAR *path, const TCHAR *geometry, int bootpri,
	const TCHAR *extra)
{
	_stprintf(out, _T("r%c,%s:%s,%s,%d"), readonly ? 'o' : 'w', device, path, geometry, bootpri);
	if (extra[0]) {
		_tcscat(out, _T(","));
		_tcscat(out, extra);
	}
}

static void cfgfile_format_uaehf_unit(TCHAR *out, const TCHAR *type, int unit, const TCHAR *value)
{
	_stprintf(out, _T("%s%d,%s"), type, unit, value);
}

static void cfgfile_format_uaehf(TCHAR *out, const TCHAR *type, const TCHAR *value)
{
	_stprintf(out, _T("%s,%s"), type, value);
}

#endif /* UAE_CFGFILE_MOUNT_C_H */
