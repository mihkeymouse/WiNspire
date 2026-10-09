#define main unused_frontend_main
#include "../winspire/main.c"
#undef main
#include <assert.h>
static void defaults(void) {
    const char *names[]={E_RT,E_SCALE,E_IDLE,E_MOVS,E_STOS,E_VGA,E_VGA_FULL,E_INPUT,E_BURST,E_TEXT,E_2K_FAST,E_TIMER_READS,"WINSPIRE_PROFILE"};
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++) unsetenv(names[i]);
}
int main(void) {
    const char *paths[]={"/configs/win9x.ini","/configs/win95.ini","C:\\configs\\win98.ini","/configs/winme.ini"};
    for(unsigned i=0;i<sizeof(paths)/sizeof(paths[0]);i++) {
        defaults();profile_apply(paths[i]);
        assert(!strcmp(getenv(E_RT),"0") && !strcmp(getenv(E_SCALE),"12"));
        assert(!strcmp(getenv(E_IDLE),"0") && !strcmp(getenv(E_MOVS),"0") && !strcmp(getenv(E_STOS),"0"));
        assert(!strcmp(getenv(E_VGA),"1") && !strcmp(getenv(E_INPUT),"1"));
        assert(!strcmp(getenv(E_TEXT),"4") && !strcmp(getenv(E_TIMER_READS),"1"));
    }
    defaults();profile_apply("/configs/xpe.ini");
    assert(!strcmp(getenv(E_RT),"0") && !strcmp(getenv(E_SCALE),"1") && !strcmp(getenv(E_VGA),"2048"));
    assert(!strcmp(getenv(E_IDLE),"1") && !strcmp(getenv(E_TIMER_READS),"0"));
    defaults();profile_apply("/configs/win2000.ini");
    assert(!strcmp(getenv(E_RT),"1") && !strcmp(getenv(E_VGA),"512") && !strcmp(getenv(E_TIMER_READS),"0"));
    defaults();setenv(E_VGA,"16",1);setenv(E_TIMER_READS,"0",1);profile_apply("/configs/win95.ini");
    assert(!strcmp(getenv(E_VGA),"16") && !strcmp(getenv(E_TIMER_READS),"0"));
    puts("PASS: Win9x aliases/cadence, existing NT profiles and explicit overrides");
}
