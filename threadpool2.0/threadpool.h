#include<stdio.h>
#include<pthread.h>
#include<unistd.h>
#include<assert.h>
#include<stdlib.h>
#include<string.h>
#include<signal.h>
#include<errno.h>
#include<vector>
#include<iostream>
#include"Locker.h"

#define DEFAULT_TIME 10
#define MIN_WAIT_TASK_NUM 10
#define DEFAULT_THREAD_VARY 10
using namespace std;


class threadpool_task_t{
public:
	void *(*function)(void *);
	void *arg;
};



class threadpool{
public:
	Locker locker;// 用于锁住本结构体
	Locker thread_counter;
	Cond queue_not_full;
	Cond queue_not_empty;

	vector<pthread_t> threads;  //存放线程池中每个线程的tid的vector
	pthread_t adjust_tid;	//管理者
	vector<threadpool_task_t> task_queue;	//任务队列

	int min_thr_num;
	int max_thr_num;
	int live_thr_num;
	int busy_thr_num;
	int wait_exit_thr_num;

	int queue_front;
	int queue_rear;
	int queue_size;
	int queue_max_size;

	bool shutdown;

public:
	threadpool(int min_thr_num, int max_thr_num, int queue_max_size)
	: task_queue(queue_max_size), threads(max_thr_num)
	{
		int i;
		this->min_thr_num = min_thr_num;
		this->max_thr_num = max_thr_num;
		this->busy_thr_num = 0;
		this->live_thr_num = min_thr_num;
		this->queue_size =  0;
		this->queue_max_size = queue_max_size;
		this->queue_front = 0;
		this->queue_rear = 0;
		this->shutdown = false;

		for(int i = 0; i < min_thr_num; i++){
			pthread_create(&threads[i], NULL, worker, this);  //threadpool_thread函数就是worker函数
			cout<<"start thread : "<<threads[i]<<endl;
		}
		pthread_create(&adjust_tid, NULL, manager, this);  //管理者线程


	}

	//向线程池中添加任务
	int threadpool_add(void*(*function)(void * arg), void *arg ){
		locker.lock();
		while(queue_size == queue_max_size && !shutdown){
			queue_not_full.wait(locker.get() );
		}
		if(shutdown){
			locker.unlock();
		}

		//如果工作线程原来的而参清空工作线程原来的回调函数的参数
		if(task_queue[queue_rear].arg != NULL){
			//delete task_queue[queue_rear].arg;
			free(task_queue[queue_rear].arg);
			task_queue[queue_rear].arg = NULL;

		}
		task_queue[queue_rear].function = function;
		task_queue[queue_rear].arg = arg;
		queue_rear = (queue_rear + 1) % queue_max_size;  //队尾指针移动，模拟环形队列
		queue_size++;

		queue_not_empty.signal();
		locker.unlock();
		return 0;
	}

	static void *worker(void *arg){
		threadpool* pool = static_cast<threadpool *>(arg);
		pool->threadpool_thread();
		return pool;
	}

	//线程池中的工作线程
	 void  threadpool_thread(){
		threadpool_task_t task;
		//每一个线程不停地循环工作，往任务队列里面拿产品，直到被锁
		while(true){
			locker.lock();
			while(queue_size == 0 && !shutdown){
				cout<<"thread : "<<pthread_self()<<"is waiting"<<endl;
				queue_not_empty.wait(locker.get() );

				//清楚指定数目的空闲线程，如果要结束的线程个数大于0，就结束线程
				if(wait_exit_thr_num > 0){
					wait_exit_thr_num--;

					//wait_exit_thr_num不能太大，万一超过了min_thr_num就不好了，如果线程池的线程个数大于最小值时可以结束当前线程
					if(live_thr_num > min_thr_num){
						cout<<"thread : "<<pthread_self()<<"is exiting"<<endl;
						live_thr_num--;
						locker.unlock();
						pthread_exit(NULL); //线程自杀
					}
				}
			}

			if(shutdown){
				locker.unlock();
				cout<<"thread : "<<pthread_self()<<"is exiting"<<endl;
				pthread_exit(NULL);
			}

			task.function = task_queue[queue_front].function;
			task.arg = task_queue[queue_front].arg;

			queue_front = (queue_front + 1) % queue_max_size;
			queue_size--;

			queue_not_full.broadcast();  //通知产品生产者，可以往里面加东西了
			locker.unlock();

			cout<<"thread : "<<pthread_self()<<"is working"<<endl;
			thread_counter.lock();
			busy_thr_num++;
			thread_counter.unlock();
			(*task.function)(task.arg);		//执行回调函数

			cout<<"thread : "<<pthread_self()<<"end working"<<endl;
			thread_counter.lock();
			busy_thr_num--;
			thread_counter.unlock();

		}
	}


	static void *manager(void *arg){
		threadpool* pool = static_cast<threadpool *>(arg);
		pool->adjust_thread();
		return pool;
	}

	 void adjust_thread(){
		while(!shutdown){
			sleep(DEFAULT_TIME);
			locker.lock();
			int queue_size = this->queue_size;
			int live_thr_num = this->live_thr_num;
			locker.unlock();

			thread_counter.lock();
			int busy_thr_num = this->busy_thr_num;
			thread_counter.unlock();

			if(queue_size >= MIN_WAIT_TASK_NUM && live_thr_num < max_thr_num){
				locker.lock(); 
				//变量i是用来遍历threads名单用的
				for(int add = 0, i = 0; i < max_thr_num && add < DEFAULT_THREAD_VARY
					&& 	live_thr_num < max_thr_num; i++){
					//检查threads线程名单，名单上有空位或者有死者，那么就把新的线程id放在那个位置上
					if(threads[i] == 0 || is_thread_alive(threads[i]) ){ 
						pthread_create(&threads[i], NULL, worker, this);
						add;
						live_thr_num++;   //live_thr_num变量只有管理者线程会做写操作，不会出现竞态条件，所以不用加锁						
					}
				}
				locker.unlock();
			}

			//忙线程不到存活线程的一半，
			if( ((busy_thr_num * 2) < live_thr_num) ){
				locker.lock();
				wait_exit_thr_num = DEFAULT_THREAD_VARY;
				locker.unlock();
				for(int i = 0; i < DEFAULT_THREAD_VARY; i++){
					queue_not_empty.signal();  //空闲线程阻塞在queue_not_empty条件变量上，因为没有产品了（queue为空）所以阻塞
											   //唤醒空闲线程后，由于wait_exit_thr_num > 0，所以进入自杀模式
				}
			}

		}
	}

	int threadpool_all_threadnum(){
		int live_threadnum = -1;
		locker.lock();
		live_threadnum = live_thr_num;
		locker.unlock();
		return live_threadnum;
	}

	int threadpool_busy_threadnum(){
    	int busy_threadnum = -1;
    	thread_counter.lock();
    	busy_threadnum = busy_thr_num;
    	thread_counter.unlock();
    	return busy_threadnum;
	}

	int is_thread_alive(pthread_t tid)
	{
    	int stat = pthread_kill(tid, 0);     //发0号信号，测试线程是否存活
    	if (stat == ESRCH){
        	return false;
    	}

    	return true;
	}


	void threadpool_destory(){
		shutdown = true;
		pthread_join(adjust_tid, NULL);
		for(int i = 0; live_thr_num; i++){
			//通知阻塞在queue_not_empty条件变量上的空闲线程
			//之所以要多次用broadcast是因为，空闲线程是要等原理的忙线程工作结束,会有新的线程变成空闲线程
			queue_not_empty.broadcast();
		}
		for(int i = 0; i < live_thr_num; i++){
			pthread_join(threads[i], NULL);
		}

	}

	~threadpool(){
		threadpool_destory();
	}


};
