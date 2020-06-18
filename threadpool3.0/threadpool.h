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
#include<list>
#include<signal.h>
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

	list<pthread_t> threads;  //存放线程池中每个线程的tid的vector

	pthread_t adjust_tid;	//管理者
	list<threadpool_task_t> task_queue;	//任务队列

	int min_thr_num;
	int max_thr_num;
	int live_thr_num;
	int busy_thr_num;
	int wait_exit_thr_num;

	//int queue_front;
	//int queue_rear;
	//int queue_size;
	int queue_max_size;

	sigset_t mask;

	bool shutdown;

public:
	threadpool(int min_thr_num, int max_thr_num, int queue_max_size)
	: threads(min_thr_num)
	{
		int i;
		this->min_thr_num = min_thr_num;
		this->max_thr_num = max_thr_num;
		this->busy_thr_num = 0;
		this->live_thr_num = min_thr_num;
		this->queue_max_size = queue_max_size;
		this->shutdown = false;

		sigemptyset(&mask);
		sigaddset(&mask, SIGUSR1);
		sigaddset(&mask, SIGUSR2);
		pthread_sigmask(SIG_BLOCK, &mask, NULL);
		auto p = threads.begin();
		for(int i = 0; i < min_thr_num; i++, p++){
			pthread_create( &(*p), NULL, worker, this);  //threadpool_thread函数就是worker函数
			cout<<"start thread : "<<*p<<endl;
		}
		pthread_create(&adjust_tid, NULL, manager, this);  //管理者线程


	}

	//向线程池中添加任务
	int threadpool_add(void*(*function)(void * arg), void *arg ){
		locker.lock();
		while(task_queue.size() > queue_max_size && !shutdown){
			if(live_thr_num < max_thr_num){
				pthread_kill(adjust_tid, SIGUSR1);
			}
			queue_not_full.wait(locker.get() );
		}
		if(shutdown){
			locker.unlock();
		}

		task_queue.emplace_back();
		task_queue.back().function = function;
		task_queue.back().arg = arg;

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
	 	pthread_detach(pthread_self() );
		threadpool_task_t task;
		//每一个线程不停地循环工作，往任务队列里面拿产品，直到被锁
		while(true){
			locker.lock();
			thread_counter.lock();
			busy_thr_num = this->busy_thr_num;
			thread_counter.unlock();
			while(task_queue.empty()  && !shutdown){			
				if( (busy_thr_num * 2) < live_thr_num && live_thr_num > min_thr_num){
					pthread_kill(adjust_tid, SIGUSR2);
				}
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
						threads.remove(pthread_self() ); //找到特定的线程id号的位置，然后remove，remove函数传入的是元素值
						pthread_exit(NULL); //线程自杀
					}
				}
			}

			if(shutdown){
				locker.unlock();
				cout<<"thread : "<<pthread_self()<<"is exiting"<<endl;
				pthread_exit(NULL);
			}

		//	task.function = task_queue[queue_front].function;
		//	task.arg = task_queue[queue_front].arg;

			task = task_queue.front();
			task_queue.pop_front();
			queue_not_full.broadcast();  //通知产品生产者，可以往里面加东西了
			locker.unlock();

			cout<<"thread : "<<pthread_self()<<"is working"<<endl;
			thread_counter.lock();
			busy_thr_num++;					//忙线程数的锁需要用单独一把锁，这样才不影响其他线程从任务队列中拿东西
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
	 	pthread_detach(pthread_self() );
	 	int err;
	 	int signo = 0;
		while(!shutdown){
			err =  sigwait(&mask, &signo);
			if(err != 0){
				cout<<"sigwiat faild"<<endl;
				exit(0);
			}
			switch(signo){
				case SIGUSR1:
				{
					list<pthread_t> tmp_list(DEFAULT_THREAD_VARY);
					auto iter = tmp_list.begin();
					for(int i = 0; i < DEFAULT_THREAD_VARY; i++, iter++){						
						pthread_create(&(*iter), NULL, worker, this);
						cout<<"new thread created"<<*iter<<endl;
					}
					locker.lock();
					threads.splice(threads.end(), tmp_list);
					live_thr_num += DEFAULT_THREAD_VARY;
					locker.unlock();
					break;
				}

				case SIGUSR2:
				{
					locker.lock();
					wait_exit_thr_num = DEFAULT_THREAD_VARY;
					locker.unlock();
					for(int i = 0; i < DEFAULT_THREAD_VARY; i++){
						queue_not_empty.signal();
					}
					break;
				}
				default:
					exit(1);

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

	int is_thread_alive(pthread_t tid){
    	int stat = pthread_kill(tid, 0);     //发0号信号，测试线程是否存活
    	if (stat == ESRCH){
        	return false;
    	}

    	return true;
	}


	void threadpool_destory(){
		shutdown = true;
		//pthread_join(adjust_tid, NULL);
		for(int i = 0; live_thr_num; i++){
			//通知阻塞在queue_not_empty条件变量上的空闲线程
			//之所以要多次用broadcast是因为，空闲线程是要等原理的忙线程工作结束,会有新的线程变成空闲线程
			queue_not_empty.broadcast();
		}
	//	for(int i = 0; i < live_thr_num; i++){
	//		pthread_join(threads[i], NULL);
	//	}

	}

	~threadpool(){
		threadpool_destory();
	}


};
