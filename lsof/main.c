#include <sys/stat.h>
#include <sys/types.h>
#include <linux/limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <error.h>

#define err_make(val) (-(val))
#define err_display(errc, fmt, ...) error(0, (int) -(errc), fmt, ##__VA_ARGS__)

/* buf size >= PATH_MAX required */
static ssize_t read_link_str_at(int dir_fd, char *link_path, char *buf)
{
	ssize_t sz = readlinkat(dir_fd, link_path, buf, PATH_MAX);
	if (sz < 0)
		return (ssize_t) err_make(errno);

	buf[sz] = 0;
	return sz;
}

static int handle_fd(int dir_fd, char *fd_str, char *path_buf)
{
	int errc;
	if ((errc = read_link_str_at(dir_fd, fd_str, path_buf)) < 0) {
		err_display(errc, "read_link_str_at");
		return errc;
	}

	printf("%5s -> %-64.64s\n", fd_str, path_buf);
	return 0;
}

static int lsof_pid(char *pid_str)
{
	int errc;
	char *path_buf = NULL;
	DIR *dir = NULL;
	int dir_fd;

	if (!(path_buf = malloc(PATH_MAX))) {
		errc = err_make(errno);
		err_display(errc, "malloc");
		goto out;
	}

	sprintf(path_buf, "/proc/%s/fd", pid_str);
	if (!(dir = opendir(path_buf))) {
		errc = err_make(errno);
		err_display(errc, "opendir");
		goto out;
	}
	if ((dir_fd = dirfd(dir)) < 0) {
		errc = err_make(errno);
		err_display(errc, "dirfd");
		goto out;
	}

	while (1) {
		errno = 0;
		struct dirent *ent = readdir(dir);
		if (!ent) {
			if (errno) {
				errc = err_make(errno);
				err_display(errc, "readdir");
				goto out;
			}
			break;
		}
		if (ent->d_name[0] == '.')
			continue;
		if ((errc = handle_fd(dir_fd, ent->d_name, path_buf)) < 0)
			goto out;
	}

	closedir(dir);
	free(path_buf);
	return 0;

out:
	free(path_buf);
	if (dir)	closedir(dir);
	return errc;
}

int main(int argc, char **argv)
{
	int errc;

	if (argc != 2) {
		fprintf(stderr, "Usage: lsof pid\n");
		exit(1);
	}

	char *eptr; errno = 0;
	strtol(argv[1], &eptr, 0);
	if (*eptr || errno) {
		fprintf(stderr, "Wrong pid format\n");
		exit(1);
	}

	if ((errc = lsof_pid(argv[1])) < 0) {
		err_display(errc, "fatal error");
		exit(1);
	}

	return 0;
}
