#include "common.h"

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

enum {
    LogFlagNone = 0,
    LogFlagWarn = 1 << 0,
    LogFlagError = 1 << 1,
};

void *const Null = 0;

Program program;

static FILE *log_file = Null;

static usize error_cnt = 0;
static usize warn_cnt = 0;

static char *log_path(void) {
    return "springengine.log";
}

static void timestamp(char *buffer, size_t buf_size) {
    time_t now = time(Null);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, buf_size, "%Y-%m-%d %H:%M:%S", tm_info);
}

result open_logger(void) {
    log_file = fopen(log_path(), "a");
    if (!log_file) {
        fprintf(stderr, "Failed to open log file: %s\n", log_path());
        return Err;
    }

    //Insert a header to the log file, including the time it was opened
    char time_buffer[20];
    timestamp(time_buffer, sizeof(time_buffer));
    fprintf(log_file, "=== Log opened at %s ===\n", time_buffer);

    return Ok;
}

result close_logger(void) {
    if (!log_file) 
        return Err;

    //Insert a footer to the log file, including the time it was closed
    char time_buffer[20];
    timestamp(time_buffer, sizeof(time_buffer));
    fprintf(log_file, "=== Log closed at %s ===\n", time_buffer);

    fclose(log_file);
    log_file = Null;

    return Ok;
}

static result log_message(const char *fmt, va_list args, int log_flags) {
    if (!log_file) 
        return Err;

    // convert message to string and prepend timestamp
    char time_buffer[20];
    timestamp(time_buffer, sizeof(time_buffer));

    char msg_buffer[1024];
    vsnprintf(msg_buffer, sizeof(msg_buffer), fmt, args);

    // log the message to the file
    fprintf(log_file, (log_flags & LogFlagError) ? "(EE) [%s] %s\n" : "[%s] %s\n", time_buffer, msg_buffer);
    fflush(log_file);

    // log the message to the console as well
    fprintf(stderr, (log_flags & LogFlagError) ? "(EE) [%s] %s\n" : "[%s] %s\n", time_buffer, msg_buffer);

    error_cnt += (log_flags & LogFlagError);
    warn_cnt += (log_flags & LogFlagWarn);

    return Ok;
}

void log_msg(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    log_message(fmt, args, LogFlagNone);
    
    va_end(args);
}

void log_warn(const char *fmt, ...){
    va_list args;
    va_start(args, fmt);

    log_message(fmt, args, LogFlagWarn);
    
    va_end(args);
}

void log_err(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    log_message(fmt, args, LogFlagError);
    
    va_end(args);
}

usize get_error_count(void){
    return error_cnt;
}

usize get_warn_count(void){
    return warn_cnt;
}
