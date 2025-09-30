#include "config.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <threads.h>
#include <unistd.h>

typedef struct inotify_event inotify_event;
typedef struct option option;
typedef struct stat sstat;
typedef struct msg_fmt {
  size_t n_arg;
  char *msg;
} msg_fmt;

typedef enum mask_type { create = 0, delete, move, n_masks } mask_type;

#define PATH_MAX 4096
#define MAX_EVENT_MONITOR 2048
#define NAME_LEN 32
#define MONITOR_EVENT_SIZE (sizeof(inotify_event))
#define BUFFER_LEN MAX_EVENT_MONITOR *(MONITOR_EVENT_SIZE + NAME_LEN)

static int log_level = 0;
static const option opts[] = {{"listen", required_argument, 0, 'l'},
                              {"target", required_argument, 0, 't'},
                              {"help", no_argument, 0, 'h'},
                              {"version", no_argument, 0, 'v'},
                              {"verbose", no_argument, 0, 'V'},
                              // {"logfile", required_argument, 0, 'L'},
                              {0, 0, 0, 0}};
static const msg_fmt error_msg_fmt_table[] = {
    {2, "could not symlink from %s to %s"},
    {1, "could not remove symlink %s"},
    {2, "could not move symlink %s to %s"},
};
static const msg_fmt success_msg_fmt_table[] = {
    {2, "symlinked from %s to %s"},
    {1, "removed symlink %s"},
    {2, "moved symlink %s to %s"},
};
static const msg_fmt generic_msg_fmt = {1, "%s"};

// static const char *log_filepath = "./tmp/duplicator.log";
FILE *log_fd;

void usage(void);
void version(void);
int log_withlevel(const msg_fmt *fmt, ...);

int main(int argc, char *argv[]) {
  assert((int)n_masks == (sizeof(error_msg_fmt_table) /
                          sizeof(typeof(error_msg_fmt_table[0]))));

  int fd, fstatid, wd, c;
  sstat st;
  char ipath[PATH_MAX], iresolved_path[PATH_MAX] = {'\0'};
  char opath[PATH_MAX], oresolved_path[PATH_MAX] = {'\0'};
  char buffer[BUFFER_LEN];

  while (-1 != (c = getopt_long(argc, argv, "l:t:hvV", opts, NULL))) {
    switch (c) {
    case 'l':
      if (NULL == strncpy(ipath, optarg, sizeof(ipath))) {
        perror("failed to read path");
        exit(errno);
      } else if (NULL == realpath(ipath, iresolved_path)) {
        perror("could not resolve path");
        exit(errno);
      } else {
        memset((void *)ipath, '\0', sizeof(ipath));
        continue;
      }
    case 't':
      if (NULL == strncpy(opath, optarg, sizeof(opath))) {
        perror("failed to read path");
        exit(errno);
      } else if (NULL == realpath(opath, oresolved_path)) {
        perror("could not resolve path");
        exit(errno);
      } else {
        memset((void *)opath, '\0', sizeof(opath));
        continue;
      }
    case 'v': {
      version();
      return 0;
    }
    case 'L': {
      // log_filepath = optarg; // currently unused
      continue;
    }
    case 'V':
      log_level++;
      continue;
    case '?':
      return -EINVAL;
    case 'h':
      usage();
    default:
      return 0;
    }
  }

  if (!strcmp(iresolved_path, "\0") || !strcmp(oresolved_path, "\0")) {
    usage();
    exit(0);
  }

  const size_t ipathlen = strlen(iresolved_path),
               opathlen = strlen(oresolved_path);
  if ((PATH_MAX - 1 <= ipathlen) || (PATH_MAX - 1 <= opathlen)) {
    perror("paths are too long");
    return ENAMETOOLONG;
  }

  if (0 > (fd = inotify_init())) {
    perror("could not initialize notifications");
    exit(errno);
  }

  if (-1 == (wd = inotify_add_watch(fd, iresolved_path,
                                    IN_CREATE | IN_DELETE | IN_MOVE))) {
    printf("could not listen to notifications in %s", iresolved_path);
    exit(errno);
  }

  int totalread, symlink_status, remove_status;
  size_t i, j, k;
  char target[PATH_MAX], link_name[PATH_MAX], old_link_name[PATH_MAX];

  for (j = 0; j < ipathlen; j++) {
    target[j] = iresolved_path[j];
  }
  for (k = 0; k < opathlen; k++) {
    link_name[k] = oresolved_path[k];
  }
  target[j] = '/', link_name[k] = '/';

  while (1) {
    if (0 > (totalread = read(fd, buffer, BUFFER_LEN))) {
      perror("failed to read notifications");
      exit(errno);
    }

    i = 0;
    while (i < (unsigned long)totalread) {
      inotify_event *event = (inotify_event *)&buffer[i];

      if (!event->len && !event->name[0]) {
        continue;
      }

      if (!(event->mask & (IN_CREATE | IN_DELETE | IN_MOVE))) {
        continue;
      }

      if (PATH_MAX <= opathlen + event->len) {
        fprintf(stderr, "link name is too long");
        continue;
      }

      if (NULL == (strcat(target, event->name)) ||
          NULL == (strcat(link_name, event->name))) {
        perror("could not format target name");
        exit(errno);
      }

      if (event->mask & IN_CREATE) {
		if (0 != (symlink_status = symlink(target, link_name))) {
		  log_withlevel(&error_msg_fmt_table[create], target, link_name);
		} else {
		  log_withlevel(&success_msg_fmt_table[create], target, link_name);
		}
	  }
	  if (event->mask & IN_DELETE) {
		if (0 != (remove_status = unlink(link_name))) {
		  log_withlevel(&error_msg_fmt_table[delete], target, link_name);
		} else {
		  log_withlevel(&success_msg_fmt_table[create], target, link_name);
		}
	  }
	  if (event->mask & IN_MOVE) {
        if (event->mask & IN_MOVED_FROM) {
		  if ((NULL == (strncpy(old_link_name, link_name, PATH_MAX))) &&
			  (NULL == memset((char *)&link_name + opathlen + 1, 0, event->len))) {
			log_withlevel(&generic_msg_fmt, "could not format names");
		  } else
			log_withlevel(&generic_msg_fmt, "saved old link name");
		  if (fstatid = open(target, O_RDONLY), -1 == fstat(fstatid, &st)) {
			/* stupid hack... */
			log_withlevel(&generic_msg_fmt, target);
			log_withlevel(&generic_msg_fmt, "target does not exists");
			event->mask = IN_MOVED_TO;
			memset((char*)&link_name + opathlen + 1, 0, PATH_MAX - opathlen - 1);
			close(fstatid);
		  }
		}
		if (event->mask & IN_MOVED_TO) {
		  if (0 != unlink(old_link_name)) {
			log_withlevel(&error_msg_fmt_table[delete], target, link_name);
		  } else {
			log_withlevel(&success_msg_fmt_table[delete], target, link_name);
		  }
		  if (0 != symlink(target, link_name)) {
			log_withlevel(&error_msg_fmt_table[create], target, link_name);
		  } else {
			log_withlevel(&success_msg_fmt_table[create], target, link_name);
		  }
		  memset((void *)old_link_name, 0, sizeof(old_link_name));
		}
	  }
	  // clear leftovers
	  for (j = ipathlen + 1, k = opathlen + 1; target[j] || target[k]; ++j, ++k) {
		target[j] = '\0', link_name[k] = '\0';
	  }
	  i += MONITOR_EVENT_SIZE + event->len;
	}
  }
  inotify_rm_watch(fd, wd);
  close(fd);

  return 0;
}


void usage(void) {
  printf("duplicator %s\n", "Usage: duplicator -l source -t target\n\n");
  printf(" Options:\n"
         "  -l, --listen <path>       path to watch over for events\n"
         "  -t, --target <path>       path to symlink to\n"
         "  -v, --version             prints version and exit\n"
         "  -V,  --verbose           enable verbose output\n"
         "  -h, --help                prints help and exit\n");
}

void version(void) { printf("%s %s\n", PROJECT_NAME, PROJECT_VERSION); }

int log_withlevel(const msg_fmt *fmt, ...) {
  FILE *fd = stderr;
  switch (log_level) {
  case 2:
    fd = log_fd;
  case 1: {
    if (NULL == fmt->msg)
      return fprintf(stderr,
                     "[Error] %s:%d format string empty, make sure to call %s "
                     "with an appropriate msg_fmt entry.",
                     __FILE_NAME__, __LINE__ - 3, __FUNCTION__);

    va_list ap;
    char msg[strlen(fmt->msg) + PATH_MAX];

    va_start(ap, fmt);

    if (0 == (vsprintf(msg, fmt->msg, ap)))
      // if (0 == (vsprintf((char*)&msg, fmt->msg, ap)))
      return fprintf(stderr,
                     "[Error] %s:%d parsing arguments, make sure to call %s "
                     "with enough arguments.",
                     __FILE_NAME__, __LINE__ - 3, __FUNCTION__);

    va_end(ap);

    int n_bytes = fprintf(fd, "%s\n", msg);
    return n_bytes;
  }
  case 0:
  default:
    return log_level == 0;
  }
}

