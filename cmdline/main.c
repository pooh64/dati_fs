#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	char *ptr = argv[0];
	ptr += strlen(ptr) + 1;

	if (prctl(PR_SET_MM, PR_SET_MM_ARG_START, ptr, 0, 0) < 0) {
		perror("prctl");
		exit(1);
	}

	printf("pid: %d\nsleep 100s\n", (int) getpid());
	
	sleep(100);
	return 0;
}
