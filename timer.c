#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>

#include "timer.h"

struct timer *timer_init(void)
{
    struct timer *t;

    t = malloc(sizeof(struct timer));
    t->next = NULL;
    t->prev = NULL;
    t->interval.tv_sec = 0;
    t->interval.tv_nsec = 0;

    return(t);
}

struct timer *timer_set(struct timer *timers, struct timespec *ctime,
                        struct timespec *interval)
{
    struct timespec time;
    struct timer *tptr;

    if (ctime == NULL)
    {
        clock_gettime(CLOCK_REALTIME, &time);
        ctime = &time;
    }

    /* Look for an empty (undused) timer */
    tptr = timers;
    while (tptr->next)
    {
        if (tptr->interval.tv_sec == 0 &&
            tptr->interval.tv_nsec == 0)
            break;

        tptr = tptr->next;
    }

    /* Fails if we've reached the end of the list without finding a usable
       timer */
    if (tptr->interval.tv_sec != 0 ||
        tptr->interval.tv_nsec != 0)
    {
        tptr->next = malloc(sizeof(struct timer));
        tptr->next->prev = tptr;
        tptr->next->next = NULL;
        tptr = tptr->next;
    }

    /* Set the timer */
    tptr->interval.tv_sec = interval->tv_sec;
    tptr->interval.tv_nsec = interval->tv_nsec;

    /* Calculate the expiration */
    tptr->expires.tv_sec = interval->tv_sec + ctime->tv_sec;
    tptr->expires.tv_nsec = interval->tv_nsec + interval->tv_nsec;
    if (tptr->expires.tv_nsec > 999999999)
    {
        tptr->expires.tv_sec++;
        tptr->expires.tv_nsec = 999999999 - tptr->expires.tv_nsec;
    }

    /* Done */
    return(tptr);
}

struct timer *get_next_offset(struct timer *timers, struct timespec *ctime,
                              struct timespec *offset)
{
    struct timespec time;
    struct timer *tptr;
    struct timer *floor;

    if (ctime == NULL)
    {
        clock_gettime(CLOCK_REALTIME, &time);
        ctime = &time;
    }

    /* This will search the timer list for the first expiring timer */
    floor = timers;
    for (tptr = timers; tptr; tptr = tptr->next)
    {
        if (tptr->interval.tv_sec > 0 &&
            tptr->interval.tv_nsec > 0)
        {
            if (tptr->expires.tv_sec < floor->expires.tv_sec)
                floor = tptr;
            else if (tptr->expires.tv_sec == floor->expires.tv_sec &&
                     tptr->expires.tv_nsec < floor->expires.tv_nsec)
                floor = tptr;
            else
                continue;
        }
    }

    /* We need to do one final check here to see if we're looking at
       an invalid timer */
    if (floor->interval.tv_sec == 0 &&
        floor->interval.tv_nsec == 0)
    {
        offset->tv_sec = 0;
        offset->tv_nsec = 0;
        return(NULL);
    }

    /* Calculate the difference between ctime and the scheduled expiry */
    offset->tv_sec = floor->expires.tv_sec - ctime->tv_sec;
    if (floor->expires.tv_nsec > ctime->tv_nsec)
    {
        offset->tv_sec--;
        offset->tv_nsec = floor->expires.tv_nsec - 1000000000;
    }
    else
    {
        offset->tv_nsec = floor->expires.tv_nsec;
    }

    /* Done */
    return(floor);
}

/* Rearms a timer */
struct timer *timer_reset(struct timer *timer, struct timespec *ctime)
{
    struct timespec time;

    if (ctime == NULL)
    {
        clock_gettime(CLOCK_REALTIME, &time);
        ctime = &time;
    }

    /* Calculate the expirtation */
    timer->expires.tv_sec = timer->interval.tv_sec + ctime->tv_sec;
    timer->expires.tv_nsec = (timer->interval.tv_nsec +
                              timer->interval.tv_nsec);
    if (timer->expires.tv_nsec > 999999)
    {
        timer->expires.tv_sec++;
        timer->expires.tv_nsec = 1000000 - timer->expires.tv_nsec;
    }

    /* Done */
    return(timer);
}

struct timer *timer_destroy(struct timer *timer)
{
    timer->interval.tv_sec = 0;
    timer->interval.tv_nsec = 0;
    timer->expires.tv_sec = 0;
    timer->expires.tv_nsec = 0;

    return(NULL);
}

/* Return the first expired timer in the list */
struct timer *get_expired_timer(struct timer *timers, struct timespec *ctime)
{
    struct timespec time;
    struct timer *tptr;

    if (ctime == NULL)
    {
        clock_gettime(CLOCK_REALTIME, &time);
        ctime = &time;
    }

    for (tptr = timers; tptr; tptr = tptr->next)
    {
        if (tptr->expires.tv_sec < ctime->tv_sec)
            return(tptr);
        if (tptr->expires.tv_sec == ctime->tv_sec &&
            tptr->expires.tv_nsec <= ctime->tv_nsec)
            return(tptr);
    }

    return(NULL);
}
