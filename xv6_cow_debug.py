#!/usr/bin/env lldb
#
# xv6 COW 调试脚本
# 专门针对指令页错误 (scause=0xc) 在地址 0x2000 的问题
#

import lldb

# 配置参数
KERNEL_PATH = "kernel/kernel"
QEMU_PORT = 25501  # 默认 GDB 端口

def __lldb_init_module(debugger, internal_dict):
    """LLDB 模块初始化"""

    # 设置目标文件
    debugger.HandleCommand(f"file {KERNEL_PATH}")

    # 连接到 QEMU
    debugger.HandleCommand(f"gdb-remote localhost:{QEMU_PORT}")

    # 设置架构
    debugger.HandleCommand("settings set target.process.architecture riscv64")

    # 设置关键断点
    setup_breakpoints(debugger)

    # 注册自定义命令
    setup_commands(debugger)

    print("🚀 xv6 COW 调试环境已初始化")
    print_help()

def setup_breakpoints(debugger):
    """设置关键断点"""

    # 1. 指令页错误断点（主要问题）
    debugger.HandleCommand("breakpoint set -n usertrap -c '(uint64)r_scause() == 0xc'")

    # 2. COW 相关函数断点
    debugger.HandleCommand("breakpoint set -n uvmcopy")
    debugger.HandleCommand("breakpoint set -n cowalloc")
    debugger.HandleCommand("breakpoint set -n copyout")
    debugger.HandleCommand("breakpoint set -n copyin")

    # 3. 内存管理关键函数
    debugger.HandleCommand("breakpoint set -n kalloc")
    debugger.HandleCommand("breakpoint set -n kfree")

    # 4. 进程管理
    debugger.HandleCommand("breakpoint set -n exec")
    debugger.HandleCommand("breakpoint set -n fork")

    # 5. 栈相关断点（针对 0x2000 错误）
    debugger.HandleCommand("breakpoint set --address 0x2000 --watchpoint write")
    debugger.HandleCommand("breakpoint set -n usertrap -c 'r_sepc() == 0x2000'")

    print("✅ 断点设置完成")

def setup_commands(debugger):
    """注册自定义命令"""

    # 进程信息
    debugger.HandleCommand("command alias ps print_proc_info")
    debugger.HandleCommand("command alias pt print_page_table")
    debugger.HandleCommand("command alias stack check_stack")

    # 内存信息
    debugger.HandleCommand("command alias mem print_mem_info")
    debugger.HandleCommand("command alias ref print_refcount")

    # 调试控制
    debugger.HandleCommand("command alias co continue")
    debugger.HandleCommand("command alias bt backtrace")

    # 注册 Python 命令
    debugger.HandleCommand('command script add -f xv6_cow_debug.print_proc_info print_proc_info')
    debugger.HandleCommand('command script add -f xv6_cow_debug.print_page_table print_page_table')
    debugger.HandleCommand('command script add -f xv6_cow_debug.check_stack check_stack')
    debugger.HandleCommand('command script add -f xv6_cow_debug.print_mem_info print_mem_info')
    debugger.HandleCommand('command script add -f xv6_cow_debug.print_refcount print_refcount')

def print_help():
    """打印帮助信息"""
    print("""
📋 可用命令:
  ps          - 打印当前进程信息
  pt <va>     - 打印虚拟地址的页表项
  stack       - 检查栈状态
  mem         - 打印内存信息
  ref <pa>    - 打印物理地址的引用计数

  co          - 继续执行
  bt          - 打印调用栈

🔍 关键断点:
  • usertrap (scause=0xc) - 指令页错误
  • uvmcopy, cowalloc     - COW 相关函数
  • 0x2000 写监视点        - 栈起始地址监控
""")

# ==================== 自定义命令实现 ====================

def print_proc_info(debugger, command, result, internal_dict):
    """打印当前进程信息"""
    debugger.HandleCommand("p *myproc()")
    debugger.HandleCommand("p myproc()->pid")
    debugger.HandleCommand("p myproc()->sz")
    debugger.HandleCommand("p myproc()->pagetable")

    # 打印陷阱帧
    debugger.HandleCommand("p *myproc()->trapframe")

    # 打印当前寄存器
    debugger.HandleCommand("register read")

def print_page_table(debugger, command, result, internal_dict):
    """打印页表项信息"""
    args = command.split()
    if len(args) < 1:
        print("用法: pt <虚拟地址>")
        return

    va = args[0]

    # 打印页表项
    debugger.HandleCommand(f"p walk(myproc()->pagetable, {va}, 0)")
    debugger.HandleCommand(f"p *walk(myproc()->pagetable, {va}, 0)")

    # 打印物理地址和标志
    debugger.HandleCommand(f"p PTE2PA(*walk(myproc()->pagetable, {va}, 0))")
    debugger.HandleCommand(f"p PTE_FLAGS(*walk(myproc()->pagetable, {va}, 0))")

    # 检查 COW 标志
    debugger.HandleCommand(f"p (PTE_FLAGS(*walk(myproc()->pagetable, {va}, 0)) & PTE_COW) != 0")

def check_stack(debugger, command, result, internal_dict):
    """检查栈状态（针对 0x2000 错误）"""
    print("=== 栈状态检查 ===")

    # 检查栈指针
    debugger.HandleCommand("p/x $sp")
    debugger.HandleCommand("p/x myproc()->trapframe->sp")

    # 检查返回地址
    debugger.HandleCommand("p/x $ra")
    debugger.HandleCommand("p/x myproc()->trapframe->ra")

    # 检查栈边界
    debugger.HandleCommand("p USERSTACK")
    debugger.HandleCommand("p PGROUNDUP(myproc()->sz)")

    # 检查 0x2000 地址的页表项
    debugger.HandleCommand("p walk(myproc()->pagetable, 0x2000, 0)")
    debugger.HandleCommand("p *walk(myproc()->pagetable, 0x2000, 0)")

    # 检查栈内容
    debugger.HandleCommand("memory read -f x -c 16 0x2000")

def print_mem_info(debugger, command, result, internal_dict):
    """打印内存信息"""
    print("=== 内存信息 ===")

    # 内核内存分配器状态
    debugger.HandleCommand("p kmem")
    debugger.HandleCommand("p kmem.freelist")

    # 物理内存使用情况
    debugger.HandleCommand("p (PHYSTOP - KERNBASE) / PGSIZE")

    # 检查是否有内存泄漏
    debugger.HandleCommand("p refcount")

def print_refcount(debugger, command, result, internal_dict):
    """打印引用计数信息"""
    args = command.split()
    if len(args) < 1:
        # 打印所有引用计数
        debugger.HandleCommand("p refcount")
        return

    pa = args[0]
    debugger.HandleCommand(f"p getref({pa})")

# ==================== 自动调试逻辑 ====================

def handle_instruction_page_fault(debugger):
    """处理指令页错误（scause=0xc）"""
    print("=== 检测到指令页错误 ===")

    # 获取错误信息
    debugger.HandleCommand("p/x r_scause()")
    debugger.HandleCommand("p/x r_sepc()")
    debugger.HandleCommand("p/x r_stval()")
    debugger.HandleCommand("p/x r_sp()")

    sepc = debugger.GetSelectedTarget().EvaluateExpression("r_sepc()").GetValue()
    stval = debugger.GetSelectedTarget().EvaluateExpression("r_stval()").GetValue()

    print(f"错误地址: sepc={sepc}, stval={stval}")

    if stval == "0x2000":
        print("⚠️  检测到栈起始地址错误！")
        check_stack(debugger, "", None, None)

        # 检查栈溢出
        debugger.HandleCommand("p myproc()->trapframe->sp - 0x2000")

        # 检查返回地址是否被破坏
        debugger.HandleCommand("memory read -f x -c 32 $sp-64")

def breakpoint_handler(debugger, command, result, internal_dict):
    """断点处理函数"""
    frame = debugger.GetSelectedTarget().GetProcess().GetSelectedThread().GetSelectedFrame()
    function_name = frame.GetFunctionName()

    if function_name == "usertrap":
        scause = debugger.GetSelectedTarget().EvaluateExpression("r_scause()").GetValue()
        if scause == "0xc":
            handle_instruction_page_fault(debugger)

    elif function_name == "uvmcopy":
        print("=== uvmcopy 调用 ===")
        debugger.HandleCommand("p parent")
        debugger.HandleCommand("p child")
        debugger.HandleCommand("p sz")

    elif function_name == "cowalloc":
        print("=== cowalloc 调用 ===")
        debugger.HandleCommand("p pagetable")
        debugger.HandleCommand("p va")

    elif function_name == "copyout":
        print("=== copyout 调用 ===")
        debugger.HandleCommand("p pagetable")
        debugger.HandleCommand("p dstva")
        debugger.HandleCommand("p len")

# ==================== 主执行流程 ====================

def main():
    """主函数"""
    debugger = lldb.SBDebugger.Create()
    debugger.SetAsync(False)

    # 设置目标
    target = debugger.CreateTarget(KERNEL_PATH)
    if not target:
        print(f"错误: 无法加载内核 {KERNEL_PATH}")
        return

    # 连接到 QEMU
    error = lldb.SBError()
    process = target.ConnectRemote(debugger.GetListener(),
                                  f"localhost:{QEMU_PORT}", error)

    if error.Fail():
        print(f"连接错误: {error}")
        return

    # 运行
    process.Continue()

    # 设置断点处理
    debugger.HandleCommand("command script add -f xv6_cow_debug.breakpoint_handler bp_handler")

if __name__ == "__main__":
    main()
