#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct obi_os_fswatch_event_node_v0 {
    obi_fs_watch_event_v0 event;
    char* path;
    char* path2;
    struct obi_os_fswatch_event_node_v0* next;
} obi_os_fswatch_event_node_v0;

typedef struct obi_os_fswatch_batch_ctx_v0 {
    obi_fs_watch_event_v0* events;
    size_t count;
} obi_os_fswatch_batch_ctx_v0;

static char* _os_fswatch_dup_str(const char* s) {
    if (!s) {
        return NULL;
    }

    size_t n = strlen(s);
    char* out = (char*)malloc(n + 1u);
    if (!out) {
        return NULL;
    }
    if (n > 0u) {
        memcpy(out, s, n);
    }
    out[n] = '\0';
    return out;
}

static void _os_fswatch_queue_event_owned(obi_os_fswatch_event_node_v0** head,
                                          obi_os_fswatch_event_node_v0** tail,
                                          bool* overflowed,
                                          uint64_t watch_id,
                                          obi_fs_watch_event_kind_v0 kind,
                                          uint32_t flags,
                                          char* path,
                                          char* path2) {
    obi_os_fswatch_event_node_v0* node;

    if (!head || !tail) {
        free(path);
        free(path2);
        if (overflowed) {
            *overflowed = true;
        }
        return;
    }

    node = (obi_os_fswatch_event_node_v0*)calloc(1u, sizeof(*node));
    if (!node) {
        free(path);
        free(path2);
        if (overflowed) {
            *overflowed = true;
        }
        return;
    }

    node->path = path;
    node->path2 = path2;
    node->event.kind = kind;
    node->event.flags = flags;
    node->event.watch_id = watch_id;
    node->event.cookie = 0u;
    node->event.path.data = node->path;
    node->event.path.size = node->path ? strlen(node->path) : 0u;
    node->event.path2.data = node->path2;
    node->event.path2.size = node->path2 ? strlen(node->path2) : 0u;

    if (!*head) {
        *head = node;
        *tail = node;
    } else {
        (*tail)->next = node;
        *tail = node;
    }
}

static void _os_fswatch_clear_events(obi_os_fswatch_event_node_v0** head,
                                     obi_os_fswatch_event_node_v0** tail,
                                     bool* overflowed) {
    obi_os_fswatch_event_node_v0* it;

    if (!head || !tail) {
        return;
    }

    it = *head;
    while (it) {
        obi_os_fswatch_event_node_v0* next = it->next;
        free(it->path);
        free(it->path2);
        free(it);
        it = next;
    }

    *head = NULL;
    *tail = NULL;
    if (overflowed) {
        *overflowed = false;
    }
}

static void _os_fswatch_batch_release(void* release_ctx, obi_fs_watch_event_batch_v0* batch) {
    obi_os_fswatch_batch_ctx_v0* b = (obi_os_fswatch_batch_ctx_v0*)release_ctx;
    if (b) {
        if (b->events) {
            for (size_t i = 0u; i < b->count; i++) {
                const char* path = b->events[i].path.data;
                const char* path2 = b->events[i].path2.data;
                free((void*)path);
                if (path2 != path) {
                    free((void*)path2);
                }
            }
        }
        free(b->events);
        free(b);
    }

    if (batch) {
        batch->events = NULL;
        batch->count = 0u;
        batch->release_ctx = NULL;
        batch->release = NULL;
    }
}

static obi_status _os_fswatch_take_batch(obi_os_fswatch_event_node_v0** head,
                                         obi_os_fswatch_event_node_v0** tail,
                                         bool* overflowed,
                                         obi_fs_watch_event_batch_v0* out_batch,
                                         bool* out_has_batch) {
    size_t event_count = 0u;
    obi_os_fswatch_event_node_v0* it;
    obi_os_fswatch_batch_ctx_v0* b;
    size_t out_i = 0u;

    if (!head || !tail || !overflowed || !out_batch || !out_has_batch) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_batch, 0, sizeof(*out_batch));
    *out_has_batch = false;

    for (it = *head; it; it = it->next) {
        event_count++;
    }
    if (*overflowed) {
        event_count++;
    }
    if (event_count == 0u) {
        return OBI_STATUS_OK;
    }

    b = (obi_os_fswatch_batch_ctx_v0*)calloc(1u, sizeof(*b));
    if (!b) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    b->events = (obi_fs_watch_event_v0*)calloc(event_count, sizeof(*b->events));
    if (!b->events) {
        free(b);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    for (it = *head; it;) {
        obi_os_fswatch_event_node_v0* next = it->next;
        b->events[out_i] = it->event;
        b->events[out_i].path.data = it->path;
        b->events[out_i].path.size = it->path ? strlen(it->path) : 0u;
        b->events[out_i].path2.data = it->path2;
        b->events[out_i].path2.size = it->path2 ? strlen(it->path2) : 0u;
        out_i++;
        free(it);
        it = next;
    }
    *head = NULL;
    *tail = NULL;

    if (*overflowed) {
        obi_fs_watch_event_v0* ev = &b->events[out_i++];
        memset(ev, 0, sizeof(*ev));
        ev->kind = OBI_FS_WATCH_EVENT_OTHER;
        ev->flags = OBI_FS_WATCH_EVENT_FLAG_OVERFLOW;
        *overflowed = false;
    }

    b->count = out_i;
    out_batch->events = b->events;
    out_batch->count = b->count;
    out_batch->release_ctx = b;
    out_batch->release = _os_fswatch_batch_release;
    *out_has_batch = (b->count > 0u);
    return OBI_STATUS_OK;
}
