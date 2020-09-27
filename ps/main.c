#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

int show_stat(int fd)
{
	FILE *file = fdopen(fd, "r");
	if (!file) {
		perror("fdopen");
		return -1;
	}

	int pid;
	char comm[256];
	char state;

	errno = 0;
	fscanf(file, "%d", &pid);
	fscanf(file, " (%[^)]s", comm);
	fscanf(file, ") %c", &state);
	if (errno) {
		perror("fscanf");
		return -1;
	}

	printf("%6d  %-32.32s  %c  ", pid, comm, state);

	fclose(file);
	return 0;
}

int show_cmdline(int fd)
{
	ssize_t len;
	char buf[32];

	do {
		len = read(fd, buf, sizeof(buf) - 1);
		if (len < 0) {
			perror("read");
			return -1;
		}

		for (int i = 0; i < len; ++i)
			buf[i] = buf[i] ? buf[i] : ' ';
		buf[len] = 0;

		printf("%s", buf);

		break;	// comment for full cmdline out
	} while (len);

	close(fd);
	return 0;
}

#define SHOW_PID_DATA(pid_fd, str, func)		\
do {							\
	int fd = openat((pid_fd), (str), O_RDONLY);	\
	if (fd < 0) {					\
		perror("openat");			\
		return -1;				\
	}						\
	(func)(fd);					\
	/* descriptor passed so no need in close(fd) */	\
} while (0)

int handle_pid(int proc_fd, char *pid_str)
{
	int pid_fd = openat(proc_fd, pid_str, O_RDONLY);
	if (pid_fd < 0) {
		perror("openat");
		return -1;
	}

	SHOW_PID_DATA(pid_fd, "stat", show_stat);
	SHOW_PID_DATA(pid_fd, "cmdline", show_cmdline);

	printf("\n");

	close(pid_fd);
	return 0;
}

int main(int argc, char **argv)
{
	DIR *proc_dir = opendir("/proc/");
	int proc_fd = dirfd(proc_dir);
	if (proc_fd < 0) {
		perror("dirfd");
		exit(1);
	}

	while (1) {
		errno = 0;
		struct dirent *ent = readdir(proc_dir);
		if (!ent) {
			if (errno) {
				perror("readdir");
				exit(1);
			}
			break;
		}
		if (ent->d_type != DT_DIR)
			continue;

		char *eptr; errno = 0;
		strtol(ent->d_name, &eptr, 0);
		if (*eptr || errno)
			continue;
		assert(!errno);

		if (handle_pid(proc_fd, ent->d_name) < 0)
			exit(1);
	}

	closedir(proc_dir);
	return 0;
}
