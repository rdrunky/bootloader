#include <errno.h>
#include <string.h>
#include <sys/macros.h>

struct error_text {
	int		code;
	const char	*text;
};

static const struct error_text error_texts[] = {
	{ EPERM, "operation not permitted" },
	{ ENOENT, "no such file or directory" },
	{ EIO, "input/output error" },
	{ ENXIO, "no such device or address" },
	{ E2BIG, "argument list too long" },
	{ ENOEXEC, "exec format error" },
	{ EBADF, "bad file descriptor" },
	{ EAGAIN, "resource temporarily unavailable" },
	{ ENOMEM, "out of memory" },
	{ EACCES, "permission denied" },
	{ EFAULT, "bad address" },
	{ EBUSY, "device or resource busy" },
	{ EEXIST, "file exists" },
	{ ENODEV, "no such device" },
	{ ENOTDIR, "not a directory" },
	{ EISDIR, "is a directory" },
	{ EINVAL, "invalid argument" },
	{ EMFILE, "too many open files" },
	{ EFBIG, "file too large" },
	{ ENOSPC, "no space left on device" },
	{ EROFS, "read-only file system" },
	{ ERANGE, "result out of range" },
	{ ENAMETOOLONG, "file name too long" },
	{ ENOSYS, "function not implemented" },
	{ ENOTEMPTY, "directory not empty" },
	{ ELOOP, "too many levels of symbolic links" },
	{ ENODATA, "no data available" },
	{ EPROTO, "protocol error" },
	{ EBADMSG, "bad message" },
	{ EOVERFLOW, "value too large" },
	{ EMSGSIZE, "message too long" },
	{ ENOTSUP, "operation not supported" },
	{ EADDRINUSE, "address already in use" },
	{ ENETDOWN, "network is down" },
	{ ENETUNREACH, "network is unreachable" },
	{ ETIMEDOUT, "timed out" },
	{ ECONNREFUSED, "connection refused" },
	{ EHOSTUNREACH, "host is unreachable" },
	{ EUCLEAN, "structure needs cleaning" },
	{ ECANCELED, "operation canceled" },
};

const char *strerror(int err)
{
	size_t i;

	if (err == 0) {
		return "success";
	}

	for (i = 0; i < ARRAY_SIZE(error_texts); i++) {
		if (error_texts[i].code == err) {
			return error_texts[i].text;
		}
	}

	return "unknown error";
}
