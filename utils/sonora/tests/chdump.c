#include "sonora_chapters.h"
int main(int c, char **v){ static struct sonora_chapter ch[SONORA_MAX_CHAPTERS]; for(int a=1;a<c;a++){int src; int n=sonora_chapters_read(v[a],ch,SONORA_MAX_CHAPTERS,&src); printf("%s: %d chapters (source %d)\n", v[a], n, src); for(int i=0;i<n;i++) printf("  %6u ms  %s\n", ch[i].start_ms, ch[i].title);} return 0;}
