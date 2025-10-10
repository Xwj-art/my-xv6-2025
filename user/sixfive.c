#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define BUFSIZE 512
#define ISNUM(x) ('0' <= x && x <= '9')
#define ISSF(x) (x > 0 && ((x%5 == 0) || (x%6 == 0)))
#define ISSEPARATORS(x) ( ((x) == ' ') || \
                          ((x) == '-') || \
                          ((x) == '\r') || \
                          ((x) == '\t') || \
                          ((x) == '\n') || \
                          ((x) == '.') || \
                          ((x) == '/') || \
                          ((x) == ',') )

// STEP1 能够读取单个文件的输入
// 输入到缓冲区后，需要及时的将数字读入
// STEP2 能够处理单个文件的输入
// STEP3 能够处理多个文件的输入

int flag = 1; // 初始时flag为处于分隔符的状态
int num = 0;

int printFS(char *buf, int n) {
  for (int i=0; i<n; i++) {
    // 如果处于分隔符状态并且这个数字确实是数字，那么读入
    if (flag && ISNUM(buf[i])) {
      num = num * 10 + buf[i] - '0';
      continue;
    }
    else if (flag && !ISNUM(buf[i])) {
      // 如果确实处于分隔符状态但是并非数字，说明已经结束了一次读取
      // 若这个数字是5和6的倍数，则打印
      flag = 0;
      if (ISSF(num)) fprintf(1, "%d\n", num);
    }
    if (ISSEPARATORS(buf[i])){
      // 判断是否是分隔符
      flag = 1;
    }
    num = 0;
  }
  if (ISSF(num)) fprintf(1, "%d\n", num);
  return 0;
}

int handle(int fd) {
  char buf[BUFSIZE];
  memset(buf, 0, sizeof(buf));
  int n = 0;
  // while循环读取数据
  while ((n = read(fd, buf, sizeof(buf)))) {
    printFS(buf, n);
    memset(buf, 0, sizeof(buf));
  }
  return 0;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(1, "argc = %d", argc);
    exit(1);
  }
  int filecount = argc - 1;
  int fd = 0;
  for (int i=1; i<=filecount; i++) {
    if ((fd = open(argv[i], O_RDONLY)) < 0) exit(1);
    // handle来处理文件
    if (handle(fd) != 0) exit(1);
    close(fd);
  }
  exit(0);
}
