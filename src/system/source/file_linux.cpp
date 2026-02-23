// Altirra - Atari 800/XL/XE/5200 emulator
// VD2 System Library - Linux file I/O implementation
// Copyright (C) 2024 Avery Lee, All Rights Reserved.
// Linux port additions are licensed under the same terms as the original.

#include <stdafx.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <cstdlib>
#include <cstring>

#include <vd2/system/Error.h>
#include <vd2/system/date.h>
#include <vd2/system/filesys.h>
#include <vd2/system/VDString.h>
#include <vd2/system/file.h>
#include <vd2/system/text.h>

using namespace nsVDFile;

///////////////////////////////////////////////////////////////////////////////
//
//	VDDate helpers — convert between VDDate (100ns ticks since 1601-01-01)
//	and Unix timespec (seconds since 1970-01-01).
//
//	Offset: 11644473600 seconds = 116444736000000000 ticks (100ns)
//
///////////////////////////////////////////////////////////////////////////////

static constexpr uint64 kUnixEpochOffset = UINT64_C(116444736000000000);

static VDDate VDDateFromTimespec(const struct timespec& ts) {
	uint64 ticks = (uint64)ts.tv_sec * 10000000ULL + (uint64)ts.tv_nsec / 100ULL;
	return VDDate { ticks + kUnixEpochOffset };
}

static struct timespec VDDateToTimespec(const VDDate& date) {
	struct timespec ts;
	if (date.mTicks <= kUnixEpochOffset) {
		ts.tv_sec = 0;
		ts.tv_nsec = 0;
	} else {
		uint64 unixTicks = date.mTicks - kUnixEpochOffset;
		ts.tv_sec = (time_t)(unixTicks / 10000000ULL);
		ts.tv_nsec = (long)((unixTicks % 10000000ULL) * 100ULL);
	}
	return ts;
}

///////////////////////////////////////////////////////////////////////////////
// Convert wide string path to UTF-8 for POSIX calls
///////////////////////////////////////////////////////////////////////////////

static VDStringA WideToUTF8Path(const wchar_t *path) {
	return VDTextWToU8(VDStringSpanW(path));
}

///////////////////////////////////////////////////////////////////////////////
//
//	VDFile
//
///////////////////////////////////////////////////////////////////////////////

VDFile::VDFile(const char *pszFileName, uint32 flags)
	: mhFile(VD_INVALID_FILE_HANDLE)
{
	open(pszFileName, flags);
}

VDFile::VDFile(const wchar_t *pwszFileName, uint32 flags)
	: mhFile(VD_INVALID_FILE_HANDLE)
{
	open(pwszFileName, flags);
}

VDFile::VDFile(VDFileHandle h)
	: mhFile(h)
{
	if (h != VD_INVALID_FILE_HANDLE)
		mFilePosition = lseek64(h, 0, SEEK_CUR);
}

vdnothrow VDFile::VDFile(VDFile&& other) vdnoexcept
	: mhFile(other.mhFile)
	, mpFilename(std::move(other.mpFilename))
	, mFilePosition(other.mFilePosition)
{
	other.mhFile = VD_INVALID_FILE_HANDLE;
}

VDFile::~VDFile() {
	closeNT();
}

vdnothrow VDFile& VDFile::operator=(VDFile&& other) vdnoexcept {
	std::swap(mhFile, other.mhFile);
	std::swap(mpFilename, other.mpFilename);
	std::swap(mFilePosition, other.mFilePosition);

	return *this;
}

void VDFile::open(const char *pszFilename, uint32 flags) {
	uint32 err = open_internal(pszFilename, NULL, flags);

	if (err)
		throw VDWin32Exception(L"Cannot open file \"%hs\":\n%%s", err, pszFilename);
}

void VDFile::open(const wchar_t *pwszFilename, uint32 flags) {
	uint32 err = open_internal(NULL, pwszFilename, flags);

	if (err)
		throw VDWin32Exception(L"Cannot open file \"%ls\":\n%%s", err, pwszFilename);
}

bool VDFile::openNT(const wchar_t *pwszFilename, uint32 flags) {
	return open_internal(NULL, pwszFilename, flags) == 0;
}

bool VDFile::tryOpen(const wchar_t *pwszFilename, uint32 flags) {
	uint32 err = open_internal(NULL, pwszFilename, flags);

	if (err == ENOENT)
		return false;

	if (err)
		throw VDWin32Exception(L"Cannot open file \"%ls\":\n%%s", err, pwszFilename);

	return true;
}

bool VDFile::openAlways(const wchar_t *pwszFilename, uint32 flags) {
	uint32 err = open_internal(NULL, pwszFilename, flags);

	if (err == EEXIST)
		return true;

	if (err)
		throw VDWin32Exception(L"Cannot open file \"%ls\":\n%%s", err, pwszFilename);

	return true;
}

uint32 VDFile::open_internal(const char *pszFilename, const wchar_t *pwszFilename, uint32 flags) {
	close();

	// Get filename for error reporting
	VDStringW wideFilename;
	if (pszFilename) {
		wideFilename = VDTextAToW(pszFilename);
		pwszFilename = wideFilename.c_str();
	}

	mpFilename = wcsdup(VDFileSplitPath(pwszFilename));
	if (!mpFilename)
		return ENOMEM;

	VDASSERT(flags & (kRead | kWrite));

	// Build open flags
	int oflags = 0;

	if ((flags & kReadWrite) == kReadWrite)
		oflags = O_RDWR;
	else if (flags & kWrite)
		oflags = O_WRONLY;
	else
		oflags = O_RDONLY;

	// Creation disposition
	uint32 creationType = flags & kCreationMask;
	switch (creationType) {
	case kOpenExisting:
		break;
	case kOpenAlways:
		oflags |= O_CREAT;
		break;
	case kCreateAlways:
		oflags |= O_CREAT | O_TRUNC;
		break;
	case kCreateNew:
		oflags |= O_CREAT | O_EXCL;
		break;
	case kTruncateExisting:
		oflags |= O_TRUNC;
		break;
	default:
		VDNEVERHERE;
		return EINVAL;
	}

	// Hints
	if (flags & kWriteThrough)
		oflags |= O_DSYNC;

	// Convert wide path to UTF-8
	VDStringA utf8Path;
	if (pszFilename) {
		utf8Path = pszFilename;
	} else {
		utf8Path = WideToUTF8Path(pwszFilename);
	}

	mhFile = ::open(utf8Path.c_str(), oflags, 0666);

	if (mhFile < 0) {
		int err = errno;
		mhFile = VD_INVALID_FILE_HANDLE;
		return (uint32)err;
	}

	// Advisory locking for deny modes (best-effort, non-blocking)
	if (flags & (kDenyRead | kDenyWrite)) {
		struct flock fl = {};
		fl.l_type = (flags & kDenyRead) ? F_WRLCK : F_RDLCK;
		fl.l_whence = SEEK_SET;
		fl.l_start = 0;
		fl.l_len = 0;
		fcntl(mhFile, F_SETLK, &fl);
	}

	// posix_fadvise hints
	if (flags & kSequential)
		posix_fadvise(mhFile, 0, 0, POSIX_FADV_SEQUENTIAL);
	else if (flags & kRandomAccess)
		posix_fadvise(mhFile, 0, 0, POSIX_FADV_RANDOM);

	mFilePosition = 0;
	return 0;
}

bool VDFile::closeNT() {
	if (mhFile != VD_INVALID_FILE_HANDLE) {
		int fd = mhFile;
		mhFile = VD_INVALID_FILE_HANDLE;
		if (::close(fd) != 0)
			return false;
	}

	return true;
}

void VDFile::close() {
	if (!closeNT())
		throw VDWin32Exception(L"Cannot complete file \"%ls\": %%s", errno, mpFilename.get());
}

bool VDFile::truncateNT() {
	return ftruncate(mhFile, mFilePosition) == 0;
}

void VDFile::truncate() {
	if (!truncateNT())
		throw VDWin32Exception(L"Cannot truncate file \"%ls\": %%s", errno, mpFilename.get());
}

bool VDFile::extendValidNT(sint64 pos) {
	// No equivalent on Linux — fallocate is similar but not the same.
	// Just extend the file size.
	return ftruncate(mhFile, pos) == 0;
}

void VDFile::extendValid(sint64 pos) {
	if (!extendValidNT(pos))
		throw VDWin32Exception(L"Cannot extend file \"%ls\": %%s", errno, mpFilename.get());
}

bool VDFile::enableExtendValid() {
	// No privilege needed on Linux
	return true;
}

long VDFile::readData(void *buffer, long length) {
	ssize_t actual = ::read(mhFile, buffer, (size_t)length);

	if (actual < 0)
		throw VDWin32Exception(L"Cannot read from file \"%ls\": %%s", errno, mpFilename.get());

	mFilePosition += actual;
	return (long)actual;
}

void VDFile::read(void *buffer, long length) {
	if (length != readData(buffer, length))
		throw VDWin32Exception(L"Cannot read from file \"%ls\": Premature end of file.", errno, mpFilename.get());
}

long VDFile::writeData(const void *buffer, long length) {
	ssize_t actual = ::write(mhFile, buffer, (size_t)length);

	if (actual < 0 || actual != (ssize_t)length)
		throw VDWin32Exception(L"Cannot write to file \"%ls\": %%s", errno, mpFilename.get());

	mFilePosition += actual;
	return (long)actual;
}

void VDFile::write(const void *buffer, long length) {
	if (length != writeData(buffer, length))
		throw VDWin32Exception(L"Cannot write to file \"%ls\": Unable to write all data.", errno, mpFilename.get());
}

bool VDFile::seekNT(sint64 newPos, eSeekMode mode) {
	int whence;

	switch (mode) {
	case kSeekStart:	whence = SEEK_SET; break;
	case kSeekCur:		whence = SEEK_CUR; break;
	case kSeekEnd:		whence = SEEK_END; break;
	default:
		VDNEVERHERE;
		return false;
	}

	off_t result = lseek64(mhFile, (off_t)newPos, whence);
	if (result == (off_t)-1)
		return false;

	mFilePosition = result;
	return true;
}

void VDFile::seek(sint64 newPos, eSeekMode mode) {
	if (!seekNT(newPos, mode))
		throw VDWin32Exception(L"Cannot seek within file \"%ls\": %%s", errno, mpFilename.get());
}

bool VDFile::skipNT(sint64 delta) {
	if (!delta)
		return true;

	char buf[1024];

	if (delta <= (sint64)sizeof buf) {
		return (long)delta == readData(buf, (long)delta);
	} else
		return seekNT(delta, kSeekCur);
}

void VDFile::skip(sint64 delta) {
	if (!delta)
		return;

	char buf[1024];

	if (delta > 0 && delta <= (sint64)sizeof buf) {
		if ((long)delta != readData(buf, (long)delta))
			throw VDWin32Exception(L"Cannot seek within file \"%ls\": %%s", errno, mpFilename.get());
	} else
		seek(delta, kSeekCur);
}

sint64 VDFile::size() const {
	struct stat st;
	if (fstat(mhFile, &st) != 0)
		throw VDWin32Exception(L"Cannot retrieve size of file \"%ls\": %%s", errno, mpFilename.get());

	return (sint64)st.st_size;
}

sint64 VDFile::tell() const {
	return mFilePosition;
}

bool VDFile::flushNT() {
	return fdatasync(mhFile) == 0;
}

void VDFile::flush() {
	if (!flushNT())
		throw VDWin32Exception(L"Cannot flush file \"%ls\": %%s", errno, mpFilename.get());
}

bool VDFile::isOpen() const {
	return mhFile != VD_INVALID_FILE_HANDLE;
}

VDFileHandle VDFile::getRawHandle() const {
	return mhFile;
}

uint32 VDFile::getAttributes() const {
	if (mhFile == VD_INVALID_FILE_HANDLE)
		return 0;

	struct stat st;
	if (fstat(mhFile, &st) != 0)
		return 0;

	uint32 attrs = 0;
	if (S_ISDIR(st.st_mode))
		attrs |= kVDFileAttr_Directory;
	if (S_ISLNK(st.st_mode))
		attrs |= kVDFileAttr_Link;
	if (!(st.st_mode & S_IWUSR))
		attrs |= kVDFileAttr_ReadOnly;

	return attrs;
}

VDDate VDFile::getCreationTime() const {
	if (mhFile == VD_INVALID_FILE_HANDLE)
		return VDDate {};

	struct stat st;
	if (fstat(mhFile, &st) != 0)
		return VDDate {};

	// Linux doesn't reliably have birth time; use ctime (status change time)
	return VDDateFromTimespec(st.st_ctim);
}

void VDFile::setCreationTime(const VDDate& date) {
	// Linux doesn't support setting creation time directly
	(void)date;
}

VDDate VDFile::getLastWriteTime() const {
	if (mhFile == VD_INVALID_FILE_HANDLE)
		return VDDate {};

	struct stat st;
	if (fstat(mhFile, &st) != 0)
		return VDDate {};

	return VDDateFromTimespec(st.st_mtim);
}

void VDFile::setLastWriteTime(const VDDate& date) {
	if (mhFile != VD_INVALID_FILE_HANDLE) {
		struct timespec times[2];
		times[0].tv_sec = 0;
		times[0].tv_nsec = UTIME_OMIT;	// don't change atime
		times[1] = VDDateToTimespec(date);
		futimens(mhFile, times);
	}
}

void *VDFile::AllocUnbuffer(size_t nBytes) {
	void *p = nullptr;
	if (posix_memalign(&p, 4096, nBytes) != 0)
		return nullptr;
	return p;
}

void VDFile::FreeUnbuffer(void *p) {
	free(p);
}
