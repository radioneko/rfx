#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/prctl.h>
#include <grp.h>
#include "util.h"

void daemonize(int do_daemonize, const char *pidfile, const char *run_as)
{
	/* Daemonize part 1 */
	if (do_daemonize) {
		switch (fork()) {
			case -1:
				perror_fatal("fork");
			case 0:
				break;
			default:
				exit(EXIT_SUCCESS); 
		}

		if (setsid() == -1) perror_fatal("setsid");
	}

	/* Save PID file if requested */
	if (pidfile) {
		FILE *fpid = fopen(pidfile, "w");
		if (!fpid) perror_fatal("Can't create pid file");
		fprintf(fpid, "%ld", (long)getpid());
		fclose(fpid);
	}

	/* Change effective uid/gid if requested */
	if (run_as) {
		gid_t gid, uid;
		const char *p = strchr(run_as, ':');
		if (p && p - run_as < 30) {
			char name[32];
			struct passwd *pw;
			struct group *gw;

			memcpy(name, run_as, p - run_as);
			name[p - run_as] = 0;
			pw = getpwnam(name);
			if (!pw) {
				fprintf(stderr, "No such user: \"%s\"\n", name);
				exit(EXIT_FAILURE);
			}
			uid = pw->pw_uid;
		
			strncpy(name, p + 1, sizeof(name));
			gw = getgrnam(name);
			if (!gw) {
				fprintf(stderr, "No such group: \"%s\"\n", name);
				exit(EXIT_FAILURE);
			}
			gid = gw->gr_gid;
		} else {
			struct passwd *pw = getpwnam(run_as);
			if (!pw) {
				fprintf(stderr, "No such user: \"%s\"\n", run_as);
				exit(EXIT_FAILURE);
			}
			gid = pw->pw_gid;
			uid = pw->pw_uid;
		}
		if (setregid(gid, gid) < 0 || setreuid(uid, uid) < 0) {
			fprintf(stderr, "Can't run as user \"%s\": %s\n", run_as, strerror(errno));
			exit(EXIT_FAILURE);
		}
		/* Now we need to allow process to make core dumps */
		prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);
	}

	/* Daemonize part 2 */
	if (do_daemonize) {
		if(chdir("/") != 0) perror_fatal("chdir");
	}
}

/* close stdin/stdout/stderr file descriptors */
void close_stdx()
{
	int fd;
	if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
		if(dup2(fd, STDIN_FILENO) < 0) perror_fatal("dup2 stdin");
		if(dup2(fd, STDOUT_FILENO) < 0) perror_fatal("dup2 stdout");
		if(dup2(fd, STDERR_FILENO) < 0) perror_fatal("dup2 stderr");

		if (fd > STDERR_FILENO && close(fd) < 0) perror_fatal("close");
	} else {
		perror_fatal("open(\"/dev/null\")");
	}
}
