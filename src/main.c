#include "config.h"
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct inotify_event inotify_event;
typedef struct option option;

#define PATH_MAX 4096
#define MAX_EVENT_MONITOR 2048
#define NAME_LEN 32
#define MONITOR_EVENT_SIZE (sizeof(inotify_event))
#define BUFFER_LEN MAX_EVENT_MONITOR *(MONITOR_EVENT_SIZE + NAME_LEN)

void usage(void);
void version(void);
// char *log_filepath = "./duplicator.log";

int main(int argc, char *argv[]) {

  int fd, wd, logd, c;
  char ipath[PATH_MAX], iresolved_path[PATH_MAX] = {'\0'};
  char opath[PATH_MAX], oresolved_path[PATH_MAX] = {'\0'};
  char buffer[BUFFER_LEN];

  static option opts[] = {{"listen", required_argument, 0, 'l'},
                          {"target", required_argument, 0, 't'},
                          {"help", no_argument, 0, 'h'},
                          {"version", no_argument, 0, 'v'},
                          // {"logfile", required_argument, 0, 'L'},
                          {0, 0, 0, 0}};

  while (-1 != (c = getopt_long(argc, argv, "l:t:hv", opts, NULL))) {
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
  if ((PATH_MAX - 1 == ipathlen) || (PATH_MAX - 1 == opathlen)) {
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
  } else {
    // fprintf(stderr, "monitoring source directory %s...\n", iresolved_path);
  }

  int totalread, symlink_status, remove_status, move_status;
  size_t i, j, k;
  char target[PATH_MAX], link_name[PATH_MAX], old_link_name[PATH_MAX];
  const char *symlink_error_fmt = "could not symlink from %s to %s\n";
  const char *remove_error_fmt = "could not remove symlink %s\n";
  const char *move_error_fmt = "could not move symlink %s to %s\n";

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
      size_t event_name_len = strlen(event->name);

      if (!(event->mask & (IN_CREATE | IN_DELETE | IN_MOVE))) {
        continue;
      }

      if (PATH_MAX <= opathlen + event_name_len) {
        fprintf(stderr, "link name is too long");
        continue;
      }

      if (NULL == (strcat(target, event->name)) ||
          NULL == (strcat(link_name, event->name))) {
        perror("could not format target name");
        exit(errno);
      }

      if ((event->mask & IN_CREATE) &&
          (0 != (symlink_status = symlink(target, link_name)))) {
        fprintf(stderr, symlink_error_fmt, target, link_name);
      } else if ((event->mask & IN_DELETE) &&
                 (0 != (remove_status = unlink(link_name)))) {
        fprintf(stderr, remove_error_fmt, link_name);
      } else if (event->mask & IN_MOVE) {
        if ((event->mask & IN_MOVED_FROM) &&
            (NULL == (strncpy(old_link_name, link_name, PATH_MAX))) &&
            (NULL == memset((char *)&link_name + opathlen + 1, 0, event_name_len))) {
          unlink(link_name);
          exit(errno);
        } else if ((event->mask & IN_MOVED_TO) &&
                   (0 != (move_status = rename(old_link_name, link_name))) &&
                   (NULL == memset((void *)old_link_name, 0, sizeof(old_link_name)))) {
          fprintf(stderr, move_error_fmt, old_link_name, link_name);
          if (0 != (remove_status = unlink(old_link_name))) {
            fprintf(stderr, remove_error_fmt, old_link_name);
            exit(errno);
          }
        }
      }
      // clear leftovers
      for (j = ipathlen + 1, k = opathlen + 1;
           j < strlen(target) || k < strlen(link_name); ++j, ++k) {
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
         "  -h, --help                prints help and exit\n");
}

void version(void) { printf("%s %s\n", PROJECT_NAME, PROJECT_VERSION); }

