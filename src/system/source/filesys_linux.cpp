// Altirra - Atari 800/XL/XE/5200 emulator
// VD2 System Library - Linux filesystem implementation
// Copyright (C) 2024 Avery Lee, All Rights Reserved.
// Linux port additions are licensed under the same terms as the original.

#include <stdafx.h>
#include <cstring>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <cstdlib>
#include <cwchar>
#include <fcntl.h>

#include <vd2/system/VDString.h>
#include <vd2/system/filesys.h>
#include <vd2/system/Error.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/strutil.h>
#include <vd2/system/text.h>

///////////////////////////////////////////////////////////////////////////////
// String split helpers
///////////////////////////////////////////////////////////////////////////////

namespace {
	template<class T, class U>
	inline T splitimpL(const T& string, const U *s) {
		const U *p = string.c_str();
		return T(p, s - p);
	}

	template<class T, class U>
	inline T splitimpR(const T& string, const U *s) {
		return T(s);
	}
}

///////////////////////////////////////////////////////////////////////////////
// Path splitting
///////////////////////////////////////////////////////////////////////////////

const char *VDFileSplitFirstDir(const char *s) {
	const char *start = s;

	while(*s++)
		if (s[-1] == '\\' || s[-1] == '/')
			return s;

	return start;
}

const wchar_t *VDFileSplitFirstDir(const wchar_t *s) {
	const wchar_t *start = s;

	while(*s++)
		if (s[-1] == L'\\' || s[-1] == L'/')
			return s;

	return start;
}

const char *VDFileSplitPath(const char *s) {
	const char *lastsep = s;

	while(*s++)
		if (s[-1] == ':' || s[-1] == '\\' || s[-1] == '/')
			lastsep = s;

	return lastsep;
}

const wchar_t *VDFileSplitPath(const wchar_t *s) {
	const wchar_t *lastsep = s;

	while(*s++)
		if (s[-1] == L':' || s[-1] == L'\\' || s[-1] == L'/')
			lastsep = s;

	return lastsep;
}

VDString  VDFileSplitPathLeft (const VDString&  s) { return splitimpL(s, VDFileSplitPath(s.c_str())); }
VDStringW VDFileSplitPathLeft (const VDStringW& s) { return splitimpL(s, VDFileSplitPath(s.c_str())); }
VDString  VDFileSplitPathRight(const VDString&  s) { return splitimpR(s, VDFileSplitPath(s.c_str())); }
VDStringW VDFileSplitPathRight(const VDStringW& s) { return splitimpR(s, VDFileSplitPath(s.c_str())); }

const char *VDFileSplitPath(const char *s, const char *t) {
	const char *lastsep = s;

	while(s != t) {
		++s;
		if (s[-1] == ':' || s[-1] == '\\' || s[-1] == '/')
			lastsep = s;
	}

	return lastsep;
}

const wchar_t *VDFileSplitPath(const wchar_t *s, const wchar_t *t) {
	const wchar_t *lastsep = s;

	while(s != t) {
		++s;

		if (s[-1] == L':' || s[-1] == L'\\' || s[-1] == L'/')
			lastsep = s;
	}

	return lastsep;
}

VDStringSpanA VDFileSplitPathLeftSpan(const VDStringSpanA& s) {
	return VDStringSpanA(s.begin(), VDFileSplitPath(s.begin(), s.end()));
}

VDStringSpanA VDFileSplitPathRightSpan(const VDStringSpanA& s) {
	return VDStringSpanA(VDFileSplitPath(s.begin(), s.end()), s.end());
}

VDStringSpanW VDFileSplitPathLeftSpan(const VDStringSpanW& s) {
	return VDStringSpanW(s.begin(), VDFileSplitPath(s.begin(), s.end()));
}

VDStringSpanW VDFileSplitPathRightSpan(const VDStringSpanW& s) {
	return VDStringSpanW(VDFileSplitPath(s.begin(), s.end()), s.end());
}

///////////////////////////////////////////////////////////////////////////////
// Root splitting (simplified for Linux - no drive letters or UNC paths)
///////////////////////////////////////////////////////////////////////////////

const char *VDFileSplitRoot(const char *s) {
	if (s[0] == '/')
		return s + 1;
	return s;
}

const wchar_t *VDFileSplitRoot(const wchar_t *s) {
	if (s[0] == L'/')
		return s + 1;
	return s;
}

VDString  VDFileSplitRoot(const VDString&  s) { return splitimpL(s, VDFileSplitRoot(s.c_str())); }
VDStringW VDFileSplitRoot(const VDStringW& s) { return splitimpL(s, VDFileSplitRoot(s.c_str())); }

///////////////////////////////////////////////////////////////////////////////
// Extension splitting
///////////////////////////////////////////////////////////////////////////////

const char *VDFileSplitExt(const char *s, const char *t) {
	const char *const end = t;

	while(t>s) {
		--t;

		if (*t == '.')
			return t;

		if (*t == ':' || *t == '\\' || *t == '/')
			break;
	}

	return end;
}

const wchar_t *VDFileSplitExt(const wchar_t *s, const wchar_t *t) {
	const wchar_t *const end = t;

	while(t>s) {
		--t;

		if (*t == L'.')
			return t;

		if (*t == L':' || *t == L'\\' || *t == L'/')
			break;
	}

	return end;
}

const char *VDFileSplitExt(const char *s) {
	const char *t = s;

	while(*t)
		++t;

	return VDFileSplitExt(s, t);
}

const wchar_t *VDFileSplitExt(const wchar_t *s) {
	const wchar_t *t = s;

	while(*t)
		++t;

	return VDFileSplitExt(s, t);
}

VDString  VDFileSplitExtLeft (const VDString&  s) { return splitimpL(s, VDFileSplitExt(s.c_str())); }
VDStringW VDFileSplitExtLeft (const VDStringW& s) { return splitimpL(s, VDFileSplitExt(s.c_str())); }
VDString  VDFileSplitExtRight(const VDString&  s) { return splitimpR(s, VDFileSplitExt(s.c_str())); }
VDStringW VDFileSplitExtRight(const VDStringW& s) { return splitimpR(s, VDFileSplitExt(s.c_str())); }

VDStringSpanA VDFileSplitExtLeftSpan (const VDStringSpanA& s) { return VDStringSpanA(s.begin(), VDFileSplitExt(s.begin(), s.end())); }
VDStringSpanW VDFileSplitExtLeftSpan (const VDStringSpanW& s) { return VDStringSpanW(s.begin(), VDFileSplitExt(s.begin(), s.end())); }
VDStringSpanA VDFileSplitExtRightSpan(const VDStringSpanA& s) { return VDStringSpanA(VDFileSplitExt(s.begin(), s.end()), s.end()); }
VDStringSpanW VDFileSplitExtRightSpan(const VDStringSpanW& s) { return VDStringSpanW(VDFileSplitExt(s.begin(), s.end()), s.end()); }

/////////////////////////////////////////////////////////////////////////////
// Wildcard matching
/////////////////////////////////////////////////////////////////////////////

bool VDFileWildMatch(const char *pattern, const char *path) {
	bool star = false;
	int i = 0;
	for(;;) {
		char c = (char)tolower((unsigned char)pattern[i]);
		if (c == '*') {
			star = true;
			pattern += i+1;
			if (!*pattern)
				return true;
			path += i;
			i = 0;
			continue;
		}

		char d = (char)tolower((unsigned char)path[i]);
		++i;

		if (c == '?') {
			if (!d)
				return false;
		} else if (c != d) {
			if (!star || !d || !i)
				return false;

			++path;
			i = 0;
			continue;
		}

		if (!c)
			return true;
	}
}

bool VDFileWildMatch(const wchar_t *pattern, const wchar_t *path) {
	bool star = false;
	int i = 0;
	for(;;) {
		wchar_t c = towlower(pattern[i]);
		if (c == L'*') {
			star = true;
			pattern += i+1;
			if (!*pattern)
				return true;
			path += i;
			i = 0;
			continue;
		}

		wchar_t d = towlower(path[i]);
		++i;

		if (c == L'?') {
			if (!d)
				return false;
		} else if (c != d) {
			if (!star || !d || !i)
				return false;

			++path;
			i = 0;
			continue;
		}

		if (!c)
			return true;
	}
}

///////////////////////////////////////////////////////////////////////////////
// VDParsedPath
///////////////////////////////////////////////////////////////////////////////

VDParsedPath::VDParsedPath()
	: mbIsRelative(true)
{
}

VDParsedPath::VDParsedPath(const wchar_t *path)
	: mbIsRelative(true)
{
	// On Linux, root is simply '/'. Check for it.
	const wchar_t *rootSplit = VDFileSplitRoot(path);
	if (rootSplit != path) {
		mRoot.assign(path, rootSplit);
		mbIsRelative = false;
		path = rootSplit;
	}

	// Parse out additional components.
	for(;;) {
		// Skip any separators.
		wchar_t c = *path++;

		while(c == L'\\' || c == L'/')
			c = *path++;

		// If we've hit a null, we're done.
		if (!c)
			break;

		// Skip until we hit a separator or a null.
		const wchar_t *compStart = path - 1;

		while(c && c != L'\\' && c != L'/') {
			c = *path++;
		}

		--path;

		const wchar_t *compEnd = path;

		// Check if we've got a component that starts with .
		const size_t compLen = compEnd - compStart;
		if (*compStart == L'.') {
			// Is it . (current)?
			if (compLen == 1) {
				continue;
			}

			// Is it .. (parent)?
			if (compLen == 2 && compStart[1] == L'.') {
				if (!mComponents.empty() && (!mbIsRelative || mComponents.back() != L"..")) {
					mComponents.pop_back();
				} else if (mbIsRelative) {
					mComponents.push_back() = L"..";
				}
				continue;
			}
		}

		// Copy the component.
		mComponents.push_back().assign(compStart, compEnd);
	}
}

VDStringW VDParsedPath::ToString() const {
	VDStringW s(mRoot);

	if (!mbIsRelative && !s.empty() && !VDIsPathSeparator(s.back()) && !mComponents.empty())
		s += L'/';

	bool first = true;
	for(Components::const_iterator it(mComponents.begin()), itEnd(mComponents.end()); it != itEnd; ++it) {
		if (!first)
			s += L'/';
		else
			first = false;

		s.append(*it);
	}

	if (s.empty())
		s = L".";

	if (!mStream.empty()) {
		s += L';';
		s.append(mStream);
	}

	return s;
}

/////////////////////////////////////////////////////////////////////////////

VDStringW VDFileGetCanonicalPath(const wchar_t *path) {
	return VDParsedPath(path).ToString();
}

VDStringW VDFileGetRelativePath(const wchar_t *basePath, const wchar_t *pathToConvert, bool allowAscent) {
	VDParsedPath base(basePath);
	VDParsedPath path(pathToConvert);

	if (base.IsRelative() || path.IsRelative())
		return VDStringW();

	if (vdwcsicmp(base.GetRoot(), path.GetRoot()))
		return VDStringW();

	size_t n1 = base.GetComponentCount();
	size_t n2 = path.GetComponentCount();
	size_t nc = 0;

	while(nc < n1 && nc < n2 && !vdwcsicmp(base.GetComponent(nc), path.GetComponent(nc)))
		++nc;

	VDParsedPath relPath;

	if (n1 > nc) {
		if (!allowAscent)
			return VDStringW();

		while(n1 > nc) {
			relPath.AddComponent(L"..");
			--n1;
		}
	}

	while(nc < n2) {
		relPath.AddComponent(path.GetComponent(nc++));
	}

	relPath.SetStream(path.GetStream());

	return relPath.ToString();
}

bool VDFileIsRelativePath(const wchar_t *path) {
	VDParsedPath ppath(path);

	return ppath.IsRelative();
}

VDStringW VDFileResolvePath(const wchar_t *basePath, const wchar_t *pathToResolve) {
	if (VDFileIsRelativePath(pathToResolve))
		return VDFileGetCanonicalPath(VDMakePath(basePath, pathToResolve).c_str());

	return VDStringW(pathToResolve);
}

///////////////////////////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////////////////////////

static constexpr uint64 kUnixEpochOffset = UINT64_C(116444736000000000);

static VDDate VDDateFromTimespec(const struct timespec& ts) {
	uint64 ticks = (uint64)ts.tv_sec * 10000000ULL + (uint64)ts.tv_nsec / 100ULL;
	return VDDate { ticks + kUnixEpochOffset };
}

static VDStringA WideToUTF8(const wchar_t *s) {
	return VDTextWToU8(VDStringSpanW(s));
}

///////////////////////////////////////////////////////////////////////////////
// Filesystem operations
///////////////////////////////////////////////////////////////////////////////

sint64 VDGetDiskFreeSpace(const wchar_t *path) {
	VDStringA utf8 = WideToUTF8(path);

	struct statvfs st;
	if (statvfs(utf8.c_str(), &st) != 0)
		return -1;

	return (sint64)st.f_bavail * (sint64)st.f_frsize;
}

bool VDDoesPathExist(const wchar_t *fileName) {
	VDStringA utf8 = WideToUTF8(fileName);
	struct stat st;
	return stat(utf8.c_str(), &st) == 0;
}

void VDCreateDirectory(const wchar_t *path) {
	VDStringW::size_type l(wcslen(path));

	if (l) {
		const wchar_t c = path[l - 1];
		if (c == L'/') {
			VDCreateDirectory(VDStringW(path, l - 1).c_str());
			return;
		}
	}

	VDStringA utf8 = WideToUTF8(path);
	if (mkdir(utf8.c_str(), 0755) != 0 && errno != EEXIST)
		throw VDWin32Exception(L"Cannot create directory: %%s", errno);
}

void VDRemoveDirectory(const wchar_t *path) {
	VDStringW::size_type l(wcslen(path));

	if (l) {
		const wchar_t c = path[l - 1];
		if (c == L'/') {
			VDRemoveDirectory(VDStringW(path, l - 1).c_str());
			return;
		}
	}

	VDStringA utf8 = WideToUTF8(path);
	if (rmdir(utf8.c_str()) != 0)
		throw VDWin32Exception(L"Cannot remove directory: %%s", errno);
}

void VDSetDirectoryCreationTime(const wchar_t *path, const VDDate& date) {
	// Linux doesn't support setting creation time
	(void)path;
	(void)date;
}

///////////////////////////////////////////////////////////////////////////

bool VDRemoveFile(const wchar_t *path) {
	VDStringA utf8 = WideToUTF8(path);
	return unlink(utf8.c_str()) == 0;
}

void VDRemoveFileEx(const wchar_t *path) {
	if (!VDRemoveFile(path))
		throw VDWin32Exception(L"Cannot delete \"%ls\": %%s", errno, path);
}

///////////////////////////////////////////////////////////////////////////

void VDMoveFile(const wchar_t *srcPath, const wchar_t *dstPath) {
	VDStringA src8 = WideToUTF8(srcPath);
	VDStringA dst8 = WideToUTF8(dstPath);

	if (rename(src8.c_str(), dst8.c_str()) != 0)
		throw VDWin32Exception(L"Cannot rename \"%ls\" to \"%ls\": %%s", errno, srcPath, dstPath);
}

///////////////////////////////////////////////////////////////////////////

uint64 VDFileGetLastWriteTime(const wchar_t *path) {
	VDStringA utf8 = WideToUTF8(path);
	struct stat st;
	if (stat(utf8.c_str(), &st) != 0)
		return 0;

	uint64 ticks = (uint64)st.st_mtim.tv_sec * 10000000ULL + (uint64)st.st_mtim.tv_nsec / 100ULL;
	return ticks + kUnixEpochOffset;
}

VDStringW VDFileGetRootPath(const wchar_t *partialPath) {
	// On Linux, the root is always "/"
	(void)partialPath;
	return VDStringW(L"/");
}

VDStringW VDGetFullPath(const wchar_t *partialPath) {
	VDStringA utf8 = WideToUTF8(partialPath);

	char resolved[PATH_MAX];
	if (realpath(utf8.c_str(), resolved))
		return VDTextU8ToW(VDStringSpanA(resolved));

	// If realpath fails (file doesn't exist), try to resolve what we can
	return VDStringW(partialPath);
}

VDStringW VDGetLongPath(const wchar_t *path) {
	// No short/long path distinction on Linux
	return VDStringW(path);
}

VDStringW VDMakePath(const wchar_t *base, const wchar_t *file) {
	return VDMakePath(VDStringSpanW(base), VDStringSpanW(file));
}

VDStringW VDMakePath(const VDStringSpanW& base, const VDStringSpanW& file) {
	if (base.empty())
		return VDStringW(file);

	VDStringW result(base);

	const wchar_t c = result[result.size() - 1];
	if (c != L'/')
		result += L'/';

	result.append(file);
	return result;
}

bool VDFileIsPathEqual(const wchar_t *path1, const wchar_t *path2) {
	// Linux paths are case-sensitive
	for (;;) {
		wchar_t c = *path1++;
		wchar_t d = *path2++;

		if (c == L'/') {
			while (*path1 == L'/')
				++path1;
			if (!*path1)
				c = 0;
		}

		if (d == L'/') {
			while (*path2 == L'/')
				++path2;
			if (!*path2)
				d = 0;
		}

		if (c != d)
			return false;

		if (!c)
			return true;
	}
}

void VDFileFixDirPath(VDStringW& path) {
	if (!path.empty()) {
		wchar_t c = path[path.size() - 1];
		if (c != L'/')
			path += L'/';
	}
}

VDStringW VDGetLocalModulePath() {
	return VDGetProgramPath();
}

VDStringW VDGetProgramPath() {
	char buf[PATH_MAX];
	ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);

	if (len <= 0)
		return VDStringW(L".");

	buf[len] = 0;

	// Split off the filename to get the directory
	char *lastSlash = strrchr(buf, '/');
	if (lastSlash)
		*(lastSlash + 1) = 0;

	return VDTextU8ToW(VDStringSpanA(buf));
}

VDStringW VDGetProgramFilePath() {
	char buf[PATH_MAX];
	ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);

	if (len <= 0)
		throw VDWin32Exception(L"Unable to get program path: %%s", errno);

	buf[len] = 0;
	return VDTextU8ToW(VDStringSpanA(buf));
}

VDStringW VDGetSystemPath() {
	return VDStringW(L"/usr/lib");
}

void VDGetRootPaths(vdvector<VDStringW>& paths) {
	paths.push_back() = L"/";
}

VDStringW VDGetRootVolumeLabel(const wchar_t *rootPath) {
	(void)rootPath;
	return VDStringW();
}

///////////////////////////////////////////////////////////////////////////
// File attributes
///////////////////////////////////////////////////////////////////////////

uint32 VDFileGetAttributes(const wchar_t *path) {
	VDStringA utf8 = WideToUTF8(path);
	struct stat st;
	if (lstat(utf8.c_str(), &st) != 0)
		return kVDFileAttr_Invalid;

	uint32 attrs = 0;

	if (S_ISDIR(st.st_mode))
		attrs |= kVDFileAttr_Directory;

	if (S_ISLNK(st.st_mode))
		attrs |= kVDFileAttr_Link;

	if (!(st.st_mode & S_IWUSR))
		attrs |= kVDFileAttr_ReadOnly;

	// Hidden files on Linux start with '.'
	const wchar_t *name = VDFileSplitPath(path);
	if (name && *name == L'.')
		attrs |= kVDFileAttr_Hidden;

	return attrs;
}

void VDFileSetAttributes(const wchar_t *path, uint32 attrsToChange, uint32 newAttrs) {
	VDStringA utf8 = WideToUTF8(path);

	struct stat st;
	if (stat(utf8.c_str(), &st) != 0)
		throw VDWin32Exception(L"Cannot change attributes on \"%ls\": %%s.", errno, path);

	mode_t mode = st.st_mode;

	if (attrsToChange & kVDFileAttr_ReadOnly) {
		if (newAttrs & kVDFileAttr_ReadOnly)
			mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
		else
			mode |= S_IWUSR;
	}

	if (chmod(utf8.c_str(), mode) != 0)
		throw VDWin32Exception(L"Cannot change attributes on \"%ls\": %%s.", errno, path);
}

///////////////////////////////////////////////////////////////////////////
// VDDirectoryIterator
///////////////////////////////////////////////////////////////////////////

VDDirectoryIterator::VDDirectoryIterator(const wchar_t *path)
	: mpHandle(NULL)
	, mbSearchComplete(false)
	, mSearchPath(path)
{
	mBasePath = VDFileSplitPathLeft(mSearchPath);
	VDFileFixDirPath(mBasePath);
}

VDDirectoryIterator::~VDDirectoryIterator() {
	if (mpHandle)
		closedir(static_cast<DIR *>(mpHandle));
}

bool VDDirectoryIterator::Next() {
	if (mbSearchComplete)
		return false;

	// On first call, open the directory
	if (!mpHandle) {
		// The search path might be "dir/*" or "dir/*.ext"
		// Extract the directory part and the pattern part
		VDStringA dirPath = WideToUTF8(mBasePath.c_str());

		if (dirPath.empty())
			dirPath = ".";

		// Strip trailing slash for opendir
		if (dirPath.size() > 1 && dirPath.back() == '/')
			dirPath.resize(dirPath.size() - 1);

		mpHandle = opendir(dirPath.c_str());
		if (!mpHandle) {
			mbSearchComplete = true;
			return false;
		}
	}

	// Extract the wildcard pattern from the search path
	const wchar_t *pattern = VDFileSplitPath(mSearchPath.c_str());

	for (;;) {
		struct dirent *entry = readdir(static_cast<DIR *>(mpHandle));
		if (!entry) {
			mbSearchComplete = true;
			return false;
		}

		// Skip . and ..
		if (entry->d_name[0] == '.') {
			if (!entry->d_name[1] || (entry->d_name[1] == '.' && !entry->d_name[2]))
				continue;
		}

		// Convert entry name to wide
		VDStringW wName = VDTextU8ToW(VDStringSpanA(entry->d_name));

		// Apply wildcard filter if pattern is not just "*"
		if (pattern && *pattern && !(pattern[0] == L'*' && !pattern[1])) {
			if (!VDFileWildMatch(pattern, wName.c_str()))
				continue;
		}

		mFilename = wName;

		// Get file info via stat
		VDStringA fullPath = WideToUTF8(GetFullPath().c_str());
		struct stat st;
		if (lstat(fullPath.c_str(), &st) != 0)
			continue;

		mbDirectory = S_ISDIR(st.st_mode);
		mFileSize = (sint64)st.st_size;
		mLastWriteDate = VDDateFromTimespec(st.st_mtim);
		mCreationDate = VDDateFromTimespec(st.st_ctim);

		mAttributes = 0;
		if (S_ISDIR(st.st_mode))
			mAttributes |= kVDFileAttr_Directory;
		if (S_ISLNK(st.st_mode))
			mAttributes |= kVDFileAttr_Link;
		if (!(st.st_mode & S_IWUSR))
			mAttributes |= kVDFileAttr_ReadOnly;
		if (entry->d_name[0] == '.')
			mAttributes |= kVDFileAttr_Hidden;

		return true;
	}
}

bool VDDirectoryIterator::ResolveLinkSize() {
	if (IsDirectory() || !IsLink())
		return true;

	VDStringA fullPath = WideToUTF8(GetFullPath().c_str());
	struct stat st;
	if (stat(fullPath.c_str(), &st) != 0)
		return false;

	mFileSize = (sint64)st.st_size;
	return true;
}
