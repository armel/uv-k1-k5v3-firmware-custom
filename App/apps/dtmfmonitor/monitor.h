#ifndef DTMF_MONITOR_H
#define DTMF_MONITOR_H
#include <stdint.h>
#include <stdbool.h>
#define MONITOR_HISTORY 8u
#define MONITOR_LENGTH 32u
#define MONITOR_GAP_MS 1000u
typedef struct {
    char text[MONITOR_LENGTH+1];
    uint8_t length;
    bool truncated;
} monitor_record_t;
typedef struct {
    monitor_record_t records[MONITOR_HISTORY];
    uint32_t last_ms;
    uint8_t head, count;
    bool active;
} monitor_t;
static inline char monitor_character(unsigned code) {
    return code<16 ? "0123456789ABCD*#"[code] : 0;
}
static inline void monitor_clear(monitor_t *m) {
    m->head=0; m->count=0; m->active=false; m->last_ms=0;
}
static inline const monitor_record_t *monitor_record(const monitor_t *m, unsigned age) {
    return age<m->count ? &m->records[(m->head-age)&(MONITOR_HISTORY-1)] : 0;
}
static inline void monitor_tick(monitor_t *m, uint32_t now) {
    if (m->active && (uint32_t)(now-m->last_ms)>=MONITOR_GAP_MS) m->active=false;
}
/* Returns true if this event started a new sequence. */
static inline bool monitor_event(monitor_t *m, char c, uint32_t now) {
    monitor_tick(m,now);
    bool fresh=!m->active;
    if (fresh) {
        if (m->count) m->head=(m->head+1)&(MONITOR_HISTORY-1);
        if (m->count<MONITOR_HISTORY) ++m->count;
        m->records[m->head].length=0;
        m->records[m->head].truncated=false;
        m->active=true;
    }
    monitor_record_t *r=&m->records[m->head];
    if (r->length<MONITOR_LENGTH) r->text[r->length++]=c;
    else r->truncated=true;
    r->text[r->length]=0;
    m->last_ms=now;
    return fresh;
}
#endif
