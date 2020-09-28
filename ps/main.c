#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* openat FILE* in RDONLY mode
 * error: returns NULL, sets errno */
static FILE *proc_fstream_openat(int dirfd, char const *path)
{
	int fd = openat(dirfd, path, O_RDONLY);
	if (fd < 0)
		return NULL;

	return fdopen(fd, "r");
}

/* full read for files with undetermined size
 * error: returns -1, sets errno */
static ssize_t fstream_get_data(FILE *f, char **buf)
{
	char chunk[4096];
	ssize_t nr = 0, rv;
	void *tmp;
	*buf = NULL;

	while ((rv = fread(chunk, 1, sizeof(chunk), f)) > 0) {
		if (!(tmp = realloc(*buf, nr + rv))) {
			goto handle_err;
		}
		*buf = tmp;
		memcpy(*buf + nr, chunk, rv);
		nr += rv;
	}

	if (ferror(f))
		goto handle_err;

	return nr;

handle_err:
	if (buf)
		free(buf);
	return -1;
}

/* /proc/[pid]/stat */
struct proc_pid_stat {
	pid_t pid;
	pid_t ppid;
	pid_t pgrp;
	pid_t sid;
	char state;
	char *comm;
};

/* error: returns -1, sets errno */
static int proc_pid_stat_get(int pid_fd, struct proc_pid_stat *stat)
{
	FILE *file = proc_fstream_openat(pid_fd, "stat");
	if (!file)
		goto handle_err_0;

	if (!(stat->comm = malloc(256)))
		goto handle_err_1;

	errno = 0;
	fscanf(file, "%d", &stat->pid);
	fscanf(file, " (%[^)]s", stat->comm);
	fscanf(file, ") %c", &stat->state);
	fscanf(file, "%d %d %d", &stat->ppid, &stat->pgrp, &stat->sid);
	if (errno)
		goto handle_err_2;

	fclose(file);
	return 0;

handle_err_2:
	free(stat->comm);
handle_err_1:
	fclose(file);
handle_err_0:
	return -1;
}

static void proc_pid_stat_destroy(struct proc_pid_stat *stat)
{
	free(stat->comm);
}

static int proc_pid_cmdline_get(int pid_fd, char **str, size_t *sz)
{
	FILE *file = proc_fstream_openat(pid_fd, "cmdline");
	if (!file)
		goto handle_err_0;

	ssize_t rc = fstream_get_data(file, str);
	if (rc < 0)
		goto handle_err_1;
	*sz = rc;

	for (ssize_t i = 0; i < rc - 1; ++i) {
		char c = (*str)[i];
		(*str)[i] = c ? c : ' ';
	}

	fclose(file);
	return 0;

handle_err_1:
	fclose(file);
handle_err_0:
	return -1;
}

static int display_head()
{
	printf("%-6s  %-25s  %-6s  %-6s  %-6s  %-32s\n",
			"PID", "COMM", "PPID", "PGRP", "SID", "CMDLINE");
	return 0;
}

static int display_pid(int pid_fd)
{
	struct proc_pid_stat stat;
	char *str;
	ssize_t sz;

	if (proc_pid_stat_get(pid_fd, &stat) < 0) {
		perror("obtain /proc/pid/stat data");
		return -1;
	} else {
		printf("%6d", stat.pid);
		printf("  %-25.25s", stat.comm);
		printf("  %6d  %6d  %6d", stat.ppid, stat.pgrp, stat.sid);
		proc_pid_stat_destroy(&stat);
	}

	if (proc_pid_cmdline_get(pid_fd, &str, &sz) < 0) {
		perror("obtain /proc/pid/cmdline");
		return -1;
	} else if (sz) {
		printf("  %-32.32s", str);
		free(str);
	}

	printf("\n");
	return 0;
}

static int handle_pid(int proc_fd, char *pid_str)
{
	int pid_fd = openat(proc_fd, pid_str, O_RDONLY);
	if (pid_fd < 0) {
		perror("openat");
		goto handle_err_0;
	}

	if (display_pid(pid_fd) < 0)
		goto handle_err_1;

	close(pid_fd);
	return 0;

handle_err_1:
	close(pid_fd);
handle_err_0:
	return -1;
}

static int proc_ps()
{
	DIR *proc_dir = opendir("/proc/");
	int proc_fd = dirfd(proc_dir);
	if (proc_fd < 0) {
		perror("dirfd");
		goto handle_err_0;
	}
	display_head();

	while (1) {
		errno = 0;
		struct dirent *ent = readdir(proc_dir);
		if (!ent) {
			if (errno) {
				perror("readdir");
				goto handle_err_1;
			}
			break;
		}
		if (ent->d_type != DT_DIR)
			continue;

		char *eptr; errno = 0;
		strtol(ent->d_name, &eptr, 0);
		if (*eptr || errno)
			continue;

		if (handle_pid(proc_fd, ent->d_name) < 0)
			goto handle_err_1;
	}
	closedir(proc_dir);
	return 0;

handle_err_1:
	closedir(proc_dir);
handle_err_0:
	return -1;
}

int main(int argc, char **argv)
{
	if (proc_ps() < 0) {
		fprintf(stderr, "%s failed", argv[0]);
		exit(1);
	}
	return 0;
}
