#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <error.h>

#define err_make(val) (-(val))
#define err_display(errc, fmt, ...) error(0, (int) -(errc), fmt, ##__VA_ARGS__)

static int proc_fstream_openat(FILE **file, int dirfd, char const *path)
{
	int fd = openat(dirfd, path, O_RDONLY);
	if (fd < 0)
		return err_make(errno);

	if (!(*file = fdopen(fd, "r")))
		return err_make(errno);
	return 0;
}

#define FSTREAM_GET_DATA_MAX (4096 * 4)

static ssize_t fstream_get_data(FILE *f, char **buf)
{
	int errc;
	char chunk[4096];
	ssize_t nr = 0, rv;
	void *tmp;
	*buf = NULL;

	while ((rv = fread(chunk, 1, sizeof(chunk), f)) > 0) {
		if (nr + rv > FSTREAM_GET_DATA_MAX) {
			errc = err_make(EFBIG);
			goto out_free;
		}
		if (!(tmp = realloc(*buf, nr + rv))) {
			errc = err_make(errno);
			goto out_free;
		}
		*buf = tmp;
		memcpy(*buf + nr, chunk, rv);
		nr += rv;
	}

	if (ferror(f)) {
		errc = err_make(errno);
		goto out_free;
	}
	return nr;

out_free:
	free(buf);
	return (ssize_t) errc;
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

static int proc_pid_stat_get(int pid_fd, struct proc_pid_stat *stat)
{
	int errc;
	FILE *file;
	if ((errc = proc_fstream_openat(&file, pid_fd, "stat")) < 0)
		goto out;

	if (!(stat->comm = malloc(256))) {
		errc = err_make(errno);
		goto out_fclose;
	}

	errno = 0;
	fscanf(file, "%d", &stat->pid);
	fscanf(file, " (%[^)]s", stat->comm);
	fscanf(file, ") %c", &stat->state);
	fscanf(file, "%d %d %d", &stat->ppid, &stat->pgrp, &stat->sid);
	if (errno) {
		errc = err_make(errno);
		goto out_free;
	}

	fclose(file);
	return 0;

out_free:
	free(stat->comm);
out_fclose:
	fclose(file);
out:
	return errc;
}

static void proc_pid_stat_destroy(struct proc_pid_stat *stat)
{
	free(stat->comm);
}

static int proc_pid_cmdline_get(int pid_fd, char **str, size_t *sz)
{
	int errc;
	FILE *file;
	if ((errc = proc_fstream_openat(&file, pid_fd, "cmdline")) < 0)
		goto out;

	ssize_t rc = fstream_get_data(file, str);
	if ((errc = rc) < 0)
		goto out_fclose;
	*sz = rc;

	for (ssize_t i = 0; i < rc - 1; ++i) {
		char c = (*str)[i];
		(*str)[i] = c ? c : ' ';
	}

	fclose(file);
	return 0;

out_fclose:
	fclose(file);
out:
	return errc;
}

static int display_head()
{
	printf("%-6s  %-25s  %-6s  %-6s  %-6s  %-32s\n",
			"PID", "COMM", "PPID", "PGRP", "SID", "CMDLINE");
	return 0;
}

static int display_pid(int pid_fd)
{
	int errc;
	struct proc_pid_stat stat;
	char *str;
	size_t sz;

	if ((errc = proc_pid_stat_get(pid_fd, &stat)) < 0) {
		err_display(errc, "obtain /proc/pid/stat data");
		return errc;
	}
	printf("%6d", stat.pid);
	printf("  %-25.25s", stat.comm);
	printf("  %6d  %6d  %6d", stat.ppid, stat.pgrp, stat.sid);
	proc_pid_stat_destroy(&stat);

	if ((errc = proc_pid_cmdline_get(pid_fd, &str, &sz)) < 0) {
		err_display(errc, "obtain /proc/pid/cmdline");
		return errc;
	}
	if (sz) {
		printf("  %-32.32s", str);
		free(str);
	}

	printf("\n");
	return 0;
}

static int handle_pid(int proc_fd, char *pid_str)
{
	int errc;
	int pid_fd = openat(proc_fd, pid_str, O_RDONLY);
	if (pid_fd < 0) {
		errc = err_make(errno);
		err_display(errc, "openat");
		goto out;
	}

	if ((errc = display_pid(pid_fd)) < 0)
		goto out_close;

	close(pid_fd);
	return 0;

out_close:
	close(pid_fd);
out:
	return errc;
}

static int proc_ps()
{
	int errc;
	DIR *proc_dir;
	int proc_fd;
	
	if (!(proc_dir = opendir("/proc/"))) {
		errc = err_make(errno);
		err_display(errc, "opendir");
		goto out;
	}
	if ((proc_fd = dirfd(proc_dir)) < 0) {
		errc = err_make(errno);
		err_display(errc, "dirfd");
		goto out_closedir;
	}
	display_head();

	while (1) {
		errno = 0;
		struct dirent *ent = readdir(proc_dir);
		if (!ent) {
			if (errno) {
				errc = err_make(errno);
				err_display(errc, "readdir");
				goto out_closedir;
			}
			break;
		}
		if (ent->d_type != DT_DIR)
			continue;

		char *eptr; errno = 0;
		strtol(ent->d_name, &eptr, 0);
		if (*eptr || errno)
			continue;

		if ((errc = handle_pid(proc_fd, ent->d_name)) < 0)
			goto out_closedir;
	}
	closedir(proc_dir);
	return 0;

out_closedir:
	closedir(proc_dir);
out:
	return errc;
}

int main(int argc, char **argv)
{
	int errc;
	if ((errc = proc_ps()) < 0) {
		err_display(errc, "fatal error");
		exit(1);
	}
	return 0;
}
