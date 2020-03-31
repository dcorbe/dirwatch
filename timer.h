#ifndef TIMER_H
#define TIMER_H

struct timer
{
    struct timespec interval;
    struct timespec expires;
    struct timer *prev;
    struct timer *next;
};

struct timer *timer_init(void);
struct timer *timer_set(struct timer *timers, struct timespec *ctime,
                        struct timespec *interval);
struct timer *get_next_offset(struct timer *timers, struct timespec *ctime,
                              struct timespec *offset);
struct timer *timer_reset(struct timer *timers, struct timespec *ctime);
struct timer *timer_destroy(struct timer *timer);
struct timer *get_expired_timer(struct timer *timers, struct timespec *ctime);

#endif /* TIMER_H */ 