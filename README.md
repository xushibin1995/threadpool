## threadpool1.0

第一版来自游双《Linux高性能服务器编程》，他用信号量和锁构建一个生产者消费者模型，我从中学到了利用静态成员函数来包装成员函数，以此解决pthread_create()回调函数的问题。

## threadpool 2.0

第二版和是我自己设计，改为动态线程池，可以根据任务量的变化，自动动态的创建和销毁线程。主要结构为：工作线程专注于从任务列取数据，管理者线程负责新线程的创建和空闲线程的销毁。在第一版的基础上提高了并发度。

## threadpool 3.0 

第三版本，在线程池初始化的时候，先设置号线程的信号屏蔽集，将SIGUSR1和SIGUSR2加入到信号屏蔽字中，然后根据传入的最小线程数，创建对应数量的工作线程，和一个管理者线程，利用两个条件变量来表示任务队列的状态——非空和非满。

当生产者线程（“主”线程）进入到任务队列中，发现任务队列为满的时候，并且工作线程数量没有达到上限的时候，在被条件变量阻塞之前，会向管理者线程发送SIGUSR1，阻塞在sigwait函数上的管理者线程，会被唤醒，执行创建工作线程任务。

当消费者线程进入任务队列时，先检查一遍忙线程数在所有工作线程中的占比，如果占比不到一半，那么就向管理者线程发送SIGUSR2信号，阻塞在sigwait函数的管理者线程在收到信号后，利用for循环唤醒固定个数的阻塞在条件变量上的线程让他们自杀（因为他们没有忙碌）。

## threadpool 3.0 与2.0对比

第三版改成了以信号驱动的方式来控制线程池的工作线程数量，使得线程池可以根据任务量，实时地添加\删除线程。相比于第二版本用轮询的方式，具有更好地实时性。 

第二版由于采用主动轮询问，每次都要锁住线程池，以获取线程池的状态信息，此时，为了避免竞态条件，工作线程（消费者线程）和工作线程都是阻塞的，大大影响了效率。而第三版用信号驱动的方式不存在这个问题。

第二版本的管理者线程在创建工作线程的时候，为了统一管理所有线程，会在创建线程期间把整个线程池锁着，并且在存放线程号的数组中从头开始遍历，以查找空位，用来存放新的线程号，这种方法在管理者线程创建线程和遍历数组这些操作期间，线程池都是被锁的，严重影响并发。而在第三版本中，用list替换数组，来存放tid，并且让工作线程在临死前自己去删除list中自己的tid。管理者线程在创建新的线程的时候，先把tid加入到一个临时的tmp_list中，这个tmp_list和线程池中的list是独立的，这样就不会产生竞态条件，当管理者线程创建完最后一个线程时，才会去争抢锁，然后利用splice函数把tmp_list挂到线程池list后面，以此更新线程池中的list，在加锁和解锁中间只有一个splice函数，锁的粒度控制到了最小，大大了提高并发度。

# 测试

![](https://github.com/xushibin1995/threadpool/raw/master/picture/Snipaste_2020-09-24_23-03-29.png)

测试程序中设置线程池最小线程数3， 最大线程数100， 任务队列大小10

向线程池中添加500个任务，得到测试结果：

![](https://github.com/xushibin1995/threadpool/raw/master/picture/Snipaste_2020-09-24_23-08-24.png)

![](https://github.com/xushibin1995/threadpool/raw/master/picture/Snipaste_2020-09-24_23-09-04.png)

![](https://github.com/xushibin1995/threadpool/raw/master/picture/Snipaste_2020-09-24_23-09-20.png)

（太多了，此处省略n张图片。。。。）

最后结果：

![](https://github.com/xushibin1995/threadpool/raw/master/picture/Snipaste_2020-09-24_23-10-43.png)

可以看到在最后大量空闲线程退出，最后只剩下 140500795127552，140500954588928，140501197977344三个线程还存活着，符合最初线程池最小线程数3的设定。