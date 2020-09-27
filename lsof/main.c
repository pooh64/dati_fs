#include <sys/stat.h>
#include <sys/types.h>
#include <linux/limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

int handle_fd(int dir_fd, char *fd_str, char *path_buf)
{
	ssize_t sz = readlinkat(dir_fd, fd_str, path_buf, PATH_MAX);
	if (sz < 0) {
		perror("readlinkat");
		return -1;
	}
	path_buf[sz] = 0;

	printf("%5s -> %-64.64s\n", fd_str, path_buf);
	return 0;
}

int handle_pid(char *pid_str)
{
	char *path_buf = malloc(PATH_MAX);
	if (!path_buf) {
		perror("malloc");
		return -1;
	}

	sprintf(path_buf, "/proc/%s/fd", pid_str);
	DIR *dir = opendir(path_buf);
	if (!dir) {
		perror("opendir");
		return -1;
	}

	int dir_fd = dirfd(dir);
	if (dir_fd < 0) {
		perror("dirfd");
		return -1;
	}

	while (1) {
		errno = 0;
		struct dirent *ent = readdir(dir);
		if (!ent) {
			if (errno) {
				perror("readdir");
				exit(1);
			}
			break;
		}
		if (ent->d_name[0] == '.')
			continue;
		if (handle_fd(dir_fd, ent->d_name, path_buf) < 0)
			exit(1);
	}

	closedir(dir);
	free(path_buf);
	return 0;
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

	if (handle_pid(argv[1]) < 0)
		exit(1);

	return 0;
}
