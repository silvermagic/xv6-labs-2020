MIT 6.S801 2020

> 所有代码已通过make grade，但存在两处疑惑(答题思路后期再补上)


- lock实验不是很理解怎么用time-stamps做到brelse能不用bcahe锁，但是bget又能找到最近最少未使用的buffer，但是也能通过测试
- net实验感觉中断读取前是不是应该先禁用读中断，读取完成后再启用？驱动开发还是不太熟悉，后面有空再研究下(傻了，RISCV中断不支持嵌套，硬件会在发出中断后关闭中断，只有等内核处理完成后才会继续发出新中断，所以并不需要我们自己关闭，详见plic.c和trap.c，而且虽然过了测试，但是实现方法存在问题，应该参考uart.c使用独立kernel线程来处理net_rx，而不是像我这样直接调用net_rx，中断函数应该将mbuf缓存到某个队列，然后调用wakeup通知内核线程调用net_rx处理后立马返回，内核线程处理net_rx过程中是可以被时钟中断打断的，可以触发sched，这样不会影响其他中断的处理，必须保证中断快速处理，这边我就偷懒不改了，留下个思路)
