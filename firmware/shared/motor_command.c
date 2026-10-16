#include "motor_command.h"
#include "board_link_config.h"
#include <string.h>

int motor_command_parse(const char *s,uint32_t *session,int16_t targets[4])
{
    unsigned i; int16_t values[4]; uint32_t token=0;
    if(!s || !targets || !session || strncmp(s,"PWM ",4)) return 0;
    s+=4;
    for(i=0;i<8;i++) {
        unsigned digit;
        if(*s>='0' && *s<='9') digit=(unsigned)(*s-'0');
        else if(*s>='a' && *s<='f') digit=(unsigned)(*s-'a'+10);
        else if(*s>='A' && *s<='F') digit=(unsigned)(*s-'A'+10);
        else return 0;
        token=(token<<4)|digit; s++;
    }
    if(!token) return 0;
    for(i=0;i<4;i++) {
        int negative=0, value=0;
        if(*s!=' ') return 0;
        while(*s==' ') s++;
        if(*s=='-') { negative=1; s++; }
        if(*s<'0' || *s>'9') return 0;
        while(*s>='0' && *s<='9') {
            value=value*10+(*s++-'0');
            if(value>LINK_PWM_LIMIT_PERMILLE) return 0;
        }
        values[i]=(int16_t)(negative?-value:value);
    }
    while(*s==' ') s++;
    if(*s) return 0;
    memcpy(targets,values,sizeof(values)); *session=token; return 1;
}
