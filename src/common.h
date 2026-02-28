#ifndef COMMON_H
#define COMMON_H

#define Version "0.1.0"

enum {
    True = 1,
    False = 0,

    Ok = 0,
    Err = 1,
};

typedef unsigned char boolean; 
typedef unsigned char result;
typedef unsigned long usize;

typedef struct {
    char *title;

    usize argc;
    char **argv;
} Program;

extern void *const Null;
extern Program program;

result open_logger(void);
result close_logger(void);

void log_msg(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_err(const char *fmt, ...);

usize get_error_count(void);
usize get_warn_count(void);

#endif // COMMON_H
