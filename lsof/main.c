#include <sys/stat.h>
#include <sys/types.h>
#include <linux/limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

/* buf size >= PATH_MAX required */
static ssize_t read_link_str_at(int dir_fd, char *link_path, char *buf)
{
	ssize_t sz = readlinkat(dir_fd, link_path, buf, PATH_MAX);
	if (sz < 0)
		return -1;

	buf[sz] = 0;
	return sz;
}

static int handle_fd(int dir_fd, char *fd_str, char *path_buf)
{
	if (read_link_str_at(dir_fd, fd_str, path_buf) < 0) {
		perror("read_link_str_at");
		return -1;
	}

	printf("%5s -> %-64.64s\n", fd_str, path_buf);
	return 0;
}

static int lsof_pid(char *pid_str)
{
	char *path_buf = NULL;
	DIR *dir = NULL;
	int dir_fd;

	if (!(path_buf = malloc(PATH_MAX))) {
		perror("malloc");
		goto handle_err;
	}

	sprintf(path_buf, "/proc/%s/fd", pid_str);
	if (!(dir = opendir(path_buf))) {
		perror("opendir");
		goto handle_err;
	}

	if ((dir_fd = dirfd(dir)) < 0) {
		perror("dirfd");
		goto handle_err;
	}

	while (1) {
		errno = 0;
		struct dirent *ent = readdir(dir);
		if (!ent) {
			if (errno) {
				perror("readdir");
				goto handle_err;
			}
			break;
		}
		if (ent->d_name[0] == '.')
			continue;
		if (handle_fd(dir_fd, ent->d_name, path_buf) < 0)
			goto handle_err;
	}

	closedir(dir);
	free(path_buf);
	return 0;

handle_err:
	if (path_buf)	free(path_buf);
	if (dir)	closedir(dir);
	return -1;
}

int main(int argc, char **argv)
{
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

	if (lsof_pid(argv[1]) < 0)
		exit(1);

	return 0;
}
