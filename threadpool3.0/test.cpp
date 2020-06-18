#include<iostream>
#include<unistd.h>
#include"threadpool.h"

using namespace std;
void *process(void *arg){
	cout<<"thread : "<<pthread_self()<<"working on task"<<*(int *)arg<<endl;
	sleep(1);
	cout<<"task"<<*(int*)arg<<"is end"<<endl;
	return NULL;
}

int main(){
	threadpool thp(3, 100, 10);
	cout<<"pool inited"<<endl;

	int num[90], i;
	for(i = 0; i< 90; i++){
		num[i]=i;
		cout<<"add task"<<i<<endl;
		thp.threadpool_add(process, (void *)&num[i] );
	}
	sleep(1000);
	return 0;
}