#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <sys/resource.h>
#include <string.h>
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <syslog.h>
#include <popt.h>
#include <time.h>
#include "timer.h"

#define LOG(X, ...) if (forkme) { syslog(X, __VA_ARGS__); } else { printf(__VA_ARGS__); }

#define MAX_OPEN_FILES 65535

/* Command-line options and their defaults */
int forkme = 0;                       // -d (daemonize)
char *pwd;                            // Top-most directory to watch (parent working directory)
char *cmdline;                        // The command to run when changes to the system are detected
int idletime = 30;                   // Number of seconds the filesystem must be idle for before call to cmdline
int maxtime = 600;                    // Max number of seconds to wait for the FS to be idle

const struct poptOption optionsTable[] = {
        { "daemon", 'd', POPT_ARG_NONE, NULL, 'd',
          "Fork into the background",
          NULL },
        { "command", 'c', POPT_ARG_STRING, NULL, 'c',
          "Command to run when changes occur in watched directory",
          NULL },
        {"directory", 'w', POPT_ARG_STRING, NULL, 'w',
         "Directory and subdirectories to watch for changes",
         NULL },
        {"idle", 'i', POPT_ARG_INT, &idletime, 'i',
         "Filesystem must be idle for <seconds> before cmdline is called (default 30 seconds)",
         NULL},
        {"maxidle", 't', POPT_ARG_INT, &maxtime, 't',
         "On a busy system, you want to cap the idle timer so cmdline runs more often (default 10 minutes)",
         NULL},
        POPT_AUTOHELP
        { NULL }
};

/* Contains information about the directories we're watching */
struct dirinfo {
    int f;       // File descriptor
    char path[PATH_MAX];
    struct dirinfo *prev, *next;
};

struct dirinfo *tailscan(const char *directory, struct dirinfo *dirinfo) {
    // Open and watch all subdirectories
    DIR *dir;
    struct dirent *entry;

    dir = opendir(directory);
    if (dir == NULL)
    {
        perror("opendir");
        exit(EXIT_FAILURE);
    }
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        dirinfo->next = malloc(sizeof(struct dirinfo));
        dirinfo->next->prev = dirinfo;
        dirinfo->next->next = NULL;
        dirinfo = dirinfo->next;

        snprintf(dirinfo->path, PATH_MAX, "%s/%s", directory, entry->d_name);

        dirinfo->f = open(dirinfo->path, O_RDONLY);
        if (dirinfo->f < 0) {
            // TODO: Log an error here.
            continue;
        }
        if (entry->d_type == DT_DIR)
            dirinfo = tailscan(dirinfo->path, dirinfo);
    }
    closedir(dir);
    return (dirinfo);
}

struct dirinfo *dirscan(const char *path)
{
    struct dirinfo *dirinfo;
    struct dirinfo *dirinfoptr;
    int f;

    f = open(path, O_RDONLY);
    if (f < 0)
    {
        LOG(LOG_ERR, "Error: could not open path: %s\n", path);
        exit(0);
    }
    dirinfo = malloc(sizeof(struct dirinfo));
    dirinfo->f = f;
    strncpy(dirinfo->path, path, PATH_MAX);
    dirinfo->prev = NULL;
    dirinfo->next = NULL;

    tailscan(path, dirinfo);

    return(dirinfo);
}

struct dirinfo *searchfd(int fd, struct dirinfo *dirinfo)
{
    struct dirinfo *dirinfoptr;

    for (dirinfoptr = dirinfo; dirinfoptr; dirinfoptr = dirinfoptr->next)
    {
        if (dirinfoptr->f == fd)
            return dirinfoptr;
    }
    return NULL;
}

// This flushes a dirinfo struct of all data so the parent directory can be rescanned
struct dirinfo *dirflush(struct dirinfo *dirinfo)
{
    struct dirinfo *dirinfoptr;

    // Always free in reverse
    for (dirinfoptr = dirinfo; dirinfoptr->next; dirinfoptr = dirinfoptr->next);

    for (; dirinfoptr; dirinfoptr = dirinfoptr->prev)
    {
        // Always make sure to close to avoid leaking FDs 
        close(dirinfoptr->f);
        if (dirinfoptr->next)
            free(dirinfoptr->next);
    }
    if (dirinfo)
        free(dirinfo);

    return(NULL);
}

void daemonize()
{
    pid_t pid;
    int i;

    /* Fork off the parent process */
    pid = fork();

    /* An error occurred */
    if (pid < 0)
        exit(EXIT_FAILURE);

    /* Success: Let the parent terminate */
    if (pid > 0)
        exit(EXIT_SUCCESS);

    /* On success: The child process becomes session leader */
    if (setsid() < 0)
        exit(EXIT_FAILURE);

    /* Catch, ignore and handle signals */
    // TODO: Implement a working signal handler */
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    /* Fork off for the second time*/
    pid = fork();

    /* An error occurred */
    if (pid < 0)
        exit(EXIT_FAILURE);

    /* Success: Let the parent terminate */
    if (pid > 0)
        exit(EXIT_SUCCESS);

    /* Close all open file descriptors */
    for (i = sysconf(_SC_OPEN_MAX); i >= 0; i--)
    {
        close (i);
    }

    /* Open the log file */
    openlog("dirwatch", LOG_PID, LOG_DAEMON);
}

void setlimits()
{
    struct rlimit limits;

    limits.rlim_max = MAX_OPEN_FILES;
    limits.rlim_cur = MAX_OPEN_FILES;
    setrlimit(RLIMIT_NOFILE, &limits);
}

int main (int argc, const char *argv[])
{
    // TODO We have to ask the OS what the current limit is on open files.
    int f, kq, nev;
    struct kevent change[MAX_OPEN_FILES];
    struct kevent event[MAX_OPEN_FILES];
    int n;

    struct dirinfo *directories;
    struct dirinfo *dirinfoptr;
    int i;
    int changes = 0;

    struct timespec timer;
    struct timespec time;
    struct timer *timers;
    struct timer *idletimer;
    struct timer *maxtimer;
    struct timer *timerptr;
    struct timespec next;

    poptContext optCon;
    char c;

    // Parse command line options
    optCon = poptGetContext(NULL, argc, argv, optionsTable, 0);
    if (argc < 2)
    {
        goto usage;
    }
    while ((c = poptGetNextOpt(optCon)) >= 0)
    {
        switch (c)
        {
            case 'd':
                forkme = 1;
                break;
            case 'c':
                cmdline = poptGetOptArg(optCon);
                break;
            case 'w':
                pwd = poptGetOptArg(optCon);
                break;
            default:
                goto usage;
        }
    }
    if (!(cmdline))
        goto usage;
    if (!(pwd))
        goto usage;
    if (forkme)
        daemonize();

    // Warm up kqueue
    kq = kqueue();
    if (kq < 0)
        perror("kqueue");

    // Set default scan timers
    clock_gettime(CLOCK_REALTIME, &time);
    timers = timer_init();

    timer.tv_sec = idletime;
    timer.tv_nsec = 0;
    idletimer = timer_set(timers, NULL, &timer);

    timer.tv_sec = maxtime;
    timer.tv_nsec = 0;
    maxtimer = timer_set(timers, NULL, &timer);

    // Set system resources to ensure we have enough available FDs to work with
    setlimits();

rescan:
    // Scan directories and load event loop
    n=0;
    directories = dirscan(pwd);
    for (dirinfoptr = directories; dirinfoptr; dirinfoptr = dirinfoptr->next)
    {
        EV_SET(&change[n++], dirinfoptr->f, EVFILT_VNODE,
               EV_ADD | EV_ENABLE | EV_ONESHOT,
               NOTE_DELETE | NOTE_EXTEND | NOTE_WRITE | NOTE_ATTRIB,
               0, 0);
    }

eventloop:
    // Loop until an unrecoverable error occurs.
    while (1) {
        timerptr = get_next_offset(timers, NULL, &next);
        nev = kevent(kq, change, n, event, n, &next);
        if (nev == -1) {
            // kevent returned an error
            perror("kevent");
        }
        // Check to see if any timers expired
        while ((timerptr = get_expired_timer(timers, NULL)))
        {
            if (timerptr == maxtimer)
            {
                // We've reached our max idle timer.  Reset and process change requests
                timer_reset(maxtimer, NULL);
                changes = 0;
                system(cmdline);
            }
            else if (timerptr == idletimer) {
                // We've hit our idle timer without any further disk writes.  Reset and process change requests
                timer_reset(idletimer, NULL);
                if (changes > 0) {
                    changes = 0;
                    system(cmdline);
                }
            }
        }
        if (nev > 0) {
            // We have changes to process
            for (i = 0; i < nev; i++) {
                dirinfoptr = searchfd(event[i].ident, directories);

                /*
                 * kevent() does something weird here.  If you remove a directory with one or more files
                 * then an event will pop for each file that's been unklink()d.  The trouble is, kevent()
                 * pops the directory removal event FIRST.  The net effect is searchfd() will return
                 * a NULL pointer because the directory is considered gone to the program at this point.
                 *
                 * The workaround is to check dirinfoptr for NULLness and then move on with our lives.
                 * This also keeps us from having to rescan constantly when the system is busy running
                 * recursive calls to unlink();
                 *                                                                            --Corbe
                 */
                if (!(dirinfoptr))
                    continue;

                LOG(LOG_INFO, "%s\n", dirinfoptr->path);
                switch(event[i].fflags) {
                    case NOTE_DELETE:
                        if ((strncasecmp(dirinfoptr->path, pwd, PATH_MAX)) == 0)
                        {
                            // Our working directory just vanished
                            goto error;
                        }
                    default:
                        dirflush(directories);
                        changes++;
                        timer_reset(idletimer, NULL);
                        goto rescan;
                }
            }
        }
    }

success:
    return EXIT_SUCCESS;

error:
    LOG(LOG_ERR, "Working directory vanished");
    return EXIT_FAILURE;

usage:
    poptPrintUsage(optCon, stderr, 0);
    return EXIT_FAILURE;
}
