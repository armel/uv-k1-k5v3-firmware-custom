#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../monitor.h"
int main(void) {
    monitor_t m = {0};
    assert(!monitor_record(&m, 0));
    for (unsigned i=0;i<16;i++) assert(monitor_character(i)=="0123456789ABCD*#"[i]);
    assert(monitor_character(16)==0);
    monitor_event(&m, '1', 10);
    monitor_event(&m, '1', 110);
    monitor_event(&m, '#', 210);
    assert(strcmp(monitor_record(&m,0)->text,"11#")==0);
    monitor_tick(&m,1209); assert(m.active);
    monitor_tick(&m,1210); assert(!m.active);
    monitor_event(&m,'A',1210); assert(m.count==2);
    assert(strcmp(monitor_record(&m,1)->text,"11#")==0);
    assert(!monitor_record(&m,2));
    monitor_clear(&m);
    monitor_event(&m,'*',UINT32_MAX-200);
    monitor_tick(&m,798); assert(m.active);
    monitor_tick(&m,799); assert(!m.active);
    monitor_clear(&m);
    monitor_event(&m,'0',0);
    monitor_event(&m,'1',1000); assert(m.count==2); /* no tick necessary */
    monitor_clear(&m);
    for (unsigned i=0;i<100;i++) monitor_event(&m,'9',i*10);
    assert(monitor_record(&m,0)->length==32);
    assert(strlen(monitor_record(&m,0)->text)==32);
    assert(monitor_record(&m,0)->truncated);
    for (unsigned i=0;i<10;i++) monitor_event(&m,'0'+i,i*1000+2000);
    assert(m.count==8);
    for (unsigned i=0;i<8;i++) assert(monitor_record(&m,i)->text[0]==(char)('9'-i));
    monitor_clear(&m); assert(m.count==0 && !m.active);
    puts("model: mapping, repeated digits, boundary, rollover, overflow, history, clear PASS");
}
