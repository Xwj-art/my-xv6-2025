# 最小化调试配置
set architecture riscv:rv64
file kernel/kernel
target remote localhost:25501
break main
continue
