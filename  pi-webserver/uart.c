/*
 * uart.c
 *
 *  Created on: 16/ago/2012
 *      Author: Utente
 */

#include <termios.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <linux/serial.h>
#include <stdlib.h>
#include <pthread.h>

#include "mongoose.h"
#include "uart.h"

int open_serial_port() {
	return 0;
}
void set_serial_port(int fd) {
}

int read_serial_port(int fd) {
	int read_output = -1;
	void *mesg;
	mesg = (char *) malloc(1024);
	do {
		//sleep(2);
		read_output = read(fd, mesg, sizeof(mesg));
		if (read_output == -1) {
			//perror("error ");
		}
	} while (read_output < 0);
	fprintf(stdout, "Buffer %s", (char *) mesg);

	return read_output;
}
struct thread_param {
	int device_status;
	char timecode[4];
	pthread_t thread_id;
};
void uart_thread_func(void *args) {
	int fd;
	// Open the Port. We want read/write, no "controlling tty" status, and open it no matter what state DCD is in
	fd = open("/dev/ttyAMA0", O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd == -1) {
		perror("open_port: Unable to open /dev/ttyAMA0 - ");
	}

	// Turn off blocking for reads, use (fd, F_SETFL, FNDELAY) if you want that
	// fcntl(fd, F_SETFL, 0);

	//Set the baud rate
	/*
	 struct termios options;
	 tcgetattr(fd, &options);
	 cfsetispeed(&options, B9600);
	 cfsetospeed(&options, B9600);
	 //Set flow control and all that
	 options.c_cflag = B9600 | CS8 | CLOCAL | CREAD;
	 options.c_iflag = IGNPAR | ICRNL;
	 options.c_oflag = 0;
	 //Flush line and set options
	 tcflush(fd, TCIFLUSH);
	 tcsetattr(fd, TCSANOW, &options);
	 */

	int read_output;
	// while (1){
	// sleep(10);
	read_output = read_serial_port(fd);
	// }
	fprintf(stdout, "received: %d\n", read_output);
	// Don't forget to clean up
	// close(fd);
	pthread_exit((void*) args);
}

int uart_thread() {
	void *uart_thread_func(void *args);
	pthread_t thread_id;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	struct thread_param *tp;
	pthread_create(&thread_id, &attr, uart_thread_func, (void*) tp);
	pthread_attr_destroy(&attr);
	pthread_join(thread_id, NULL);
	return 0;
}

