#ifndef CommonH
#define CommonH

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

typedef struct {
    void *pointer;
    usize size;
    void *priv;
} Heap;

extern void *const Null;
extern Program program;
extern const Heap NullHeap;

void quit(result res);

result open_logger(void);
result close_logger(void);

void log_msg(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_err(const char *fmt, ...);

usize get_error_count(void);
usize get_warn_count(void);

#ifdef Testing
void restore_diagnostic_counts(usize warn_count, usize error_count);
#endif // Testing

Heap allocate(usize count, usize bytes);
Heap reallocate(Heap heap, usize new_size);
void deallocate(Heap heap);

result scan_and_deallocate(void); // returns Err if any leaked bytes are reclaimed, otherwise Ok

void set_default_raylib_log_level(void);

#if !defined(CommonAllowStdlibAllocators)
#define malloc(...) SpringengineUseAllocateInsteadOfMalloc
#define realloc(...) SpringengineUseReallocateInsteadOfRealloc
#define free(...) SpringengineUseDeallocateInsteadOfFree
#endif

#endif // COMMON_H
