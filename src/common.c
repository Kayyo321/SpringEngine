#include "common.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stddef.h>
#include <time.h>

enum {
    LogFlagNone = 0,
    LogFlagWarn = 1 << 0,
    LogFlagError = 1 << 1,
};

typedef struct _HeapNode {
    Heap heap;
    struct _HeapNode *next;
    struct _HeapNode **prev_next;
} HeapList;

// Extern refs
void *const Null = 0;
Program program;
const Heap NullHeap = {.pointer = Null, .size = 0, .priv = Null};

static FILE *log_file = Null;

static usize error_cnt = 0;
static usize warn_cnt = 0;

static HeapList *heap_list_head = Null;
static HeapList *heap_list_last = Null;

static char *log_path(void) {
    return "springengine.log";
}

static void timestamp(char *buffer, size_t buf_size) {
    time_t now = time(Null);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, buf_size, "%Y-%m-%d %H:%M:%S", tm_info);
}

void quit(result res) {
    if (log_file)
        close_logger();

    res = scan_and_deallocate() == Ok ? res : Err;

    fprintf(stderr, "%s exited with code %d\n", program.title, res);
    exit(res);
}

result open_logger(void)
{
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

#ifdef TESTING
void restore_diagnostic_counts(usize warn_count, usize error_count) {
    warn_cnt = warn_count;
    error_cnt = error_count;
}
#endif // TESTING

static void ensure_heap_list_integrity(void) {
    if (!heap_list_head) {
        heap_list_head = (HeapList *)malloc(sizeof(HeapList));
        if (!heap_list_head) {
            log_err("Failed to allocate memory for heap list head.");
            quit(Err);
        }
        heap_list_head->heap = NullHeap;
        heap_list_head->next = Null;
    }

    // configure last pointer if it's not set
    if (!heap_list_last) {
        HeapList *current = heap_list_head;
        while (current->next) {
            current = current->next;
        }
        heap_list_last = current;
    }
}

static HeapList *get_new_node(void) {
    ensure_heap_list_integrity();

    // change the heap_last to point to a new node, then move the heap_last to the new nodes next pointer
    HeapList *new_node = (HeapList *)malloc(sizeof(HeapList));
    if (!new_node) {
        log_err("Failed to allocate memory for new heap node.");
        quit(Err);
    }
    new_node->heap = NullHeap;
    new_node->next = Null;
    new_node->prev_next = &heap_list_last->next;

    heap_list_last->next = new_node;
    heap_list_last = new_node;

    return new_node;
}

Heap allocate(usize count, usize bytes) {
    HeapList *node = get_new_node();

    const usize total_size = count * bytes;

    node->heap.pointer = malloc(total_size);
    node->heap.size = total_size;
    if (!node->heap.pointer) {
        log_err("Failed to allocate memory: requested %lu bytes.", total_size);
        quit(Err);
    }

    node->heap.priv = node; // private data is a pointer to the node itself, so we can find it when we need to reallocate or deallocate

    return node->heap;
}

Heap reallocate(Heap heap, usize new_size) {
    if (!heap.pointer) {
        log_err("Cannot reallocate a null pointer.");
        quit(Err);
    }

    HeapList *node = (HeapList *)heap.priv;
    if (!node) {
        log_err("Invalid heap provided for reallocation.");
        quit(Err);
    }

    void *new_ptr = realloc(node->heap.pointer, new_size);
    if (!new_ptr) {
        log_err("Failed to reallocate memory: requested %lu bytes.", new_size);
        quit(Err);
    }

    node->heap.pointer = new_ptr;
    node->heap.size = new_size;

    return node->heap;
}

void deallocate(Heap heap) {
    // deallocate the heap and remove it from the list
    if (!heap.pointer) {
        log_err("Cannot deallocate a null pointer.");
        quit(Err);
    }

    HeapList *node = (HeapList *)heap.priv;
    if (!node) {
        log_err("Invalid heap provided for deallocation.");
        quit(Err);
    }

    free(node->heap.pointer);
    node->heap.pointer = Null;
    node->heap.size = 0;
    node->heap.priv = Null;

    if (!node->prev_next || *node->prev_next != node) {
        log_err("Heap list corruption detected during deallocation.");
        quit(Err);
    }

    *node->prev_next = node->next;
    if (node->next) {
        node->next->prev_next = node->prev_next;
    } else {
        if (node->prev_next == &heap_list_head->next) {
            heap_list_last = heap_list_head;
        } else {
            HeapList *prev = (HeapList *)((char *)node->prev_next - offsetof(HeapList, next));
            heap_list_last = prev;
        }
    }

    free(node);
}

result scan_and_deallocate(void) {
    ensure_heap_list_integrity();

    HeapList *current = heap_list_head->next;
    usize reclaimed_bytes = 0;

    while (current) {
        HeapList *next = current->next;
        if (current->heap.pointer) {
            log_warn("Memory leak detected: %lu bytes at %p", current->heap.size, current->heap.pointer);
            reclaimed_bytes += current->heap.size;
            deallocate(current->heap);
        }
        current = next;
    }

    return (reclaimed_bytes > 0) ? Err : Ok;
}
