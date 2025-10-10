#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"

#define MAXFNNUM 100

int flag = 1;
char *fns[MAXFNNUM];
int fncount = 0;

char* getName(char *path) {
  char *name = path;
  name = name + strlen(path);
  while (*name != '/') name--;
  return ++name;
}

void printi(int a) {
  fprintf(1, "=======================\n");
  fprintf(1, "==========%d==========\n", a);
  fprintf(1, "=======================\n");
}

void prints(char *a) {
  fprintf(1, "=======================\n");
  fprintf(1, "==========%s==========\n", a);
  fprintf(1, "=======================\n");
}

int execfile(char *path, char **argv) {
  char *buf[MAXARG];
  int i;
  for (i=0; argv[i] != (void*)0; i++) {
    buf[i] = malloc(strlen(argv[i]) + 1);
    buf[i] = argv[i];
    // prints(buf[i]);
  }
  buf[i] = path;
  // prints(buf[i]);
  int ret = 0;
  int pid = fork();
  if(pid > 0){
    pid = wait((int *) 0);
  } else if (pid == 0) {
    ret = exec(buf[0], buf);
    // ret = exec(path, argv);
  } else {
    printf("fork error\n");
    ret = -1;
  }
  return ret;
}

// 当前目录和文件名
int find(char *path, char *fn) {
  /*
  print("test");
  int a = open("./rm", O_RDONLY);
  fprintf(1, "%d", a);
  print("test");
  */
  // 目录项和文件原信息
  struct dirent de;
  struct stat st;
  int ret = 0;
  // 目录，和在目录后面拼接下一级文件的指针
  char buf[512], *p;
  int fd;
  if ((fd = open(path, O_RDONLY)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    ret = 1;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    ret = 1;
  }
  // 判断当前文件是目录还是文件
  switch (st.type) {
    case T_FILE: {
      // 判断名字和目标是否相同，若相同则打印路径
      // 获取当前文件的文件名
      if (strcmp(getName(path), fn) == 0) {
        fns[fncount] = malloc(strlen(path) + 1);
        strcpy(fns[fncount++], path);
      }
      break;
    }
    case T_DIR: {
      if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
        printf("find: path too long\n");
        ret = 1;
        break;
      }
      strcpy(buf, path);
      p = buf+strlen(buf);
      *p++ = '/';
      // 在目录文件中读取目录项，inum为inode号，name为文件名或者目录名
      while(read(fd, &de, sizeof(de)) == sizeof(de)){
        // 空目录，当前目录和上一级目录
        if(de.inum == 0 || (strcmp(de.name, ".") == 0) || (strcmp(de.name, "..") == 0))
          continue;
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;
        // 递归找子目录下是否有符合的文件
        find(buf, fn);
        // if(stat(buf, &st) < 0){
        //   printf("find: cannot stat %s\n", buf);
        //   continue;
        // }
        // 这里打印目录去了
        // printf("%s %d %d %d\n", fmtname(buf), st.type, st.ino, (int) st.size);
      }
    }
  }
  close(fd);
  return ret;
}

int
main(int argc, char *argv[MAXARG]) {
  if (strcmp(argv[3], "-exec") == 0) flag = 0;
  if (find(argv[1], argv[2]) < 0) exit(1);
  if (flag) {
    for (int i=0; i<fncount; i++)
      fprintf(1, "%s\n", fns[i]);
  } else {
    for (int i=0; i<fncount; i++)
      execfile(fns[i], argv + 4);
  }
  exit(0);
}
