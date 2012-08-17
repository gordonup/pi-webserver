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
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/types.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include "uart.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <linux/serial.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/types.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/types.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include "mongoose.h"
#include <errno.h>

int open_serial_port_3() {
	int fd;
	fd = open("/dev/ttyAMA0", O_RDWR);
	return fd;
}
void set_serial_port_3(int fd) {
}

int read_serial_port_3(int fd) {
	int read_output = -1;
	void *mesg;
	mesg = (char *) malloc(1024);
	void *buf = 0;
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
	//fcntl(fd, F_SETFL, 0);

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

	/*
	 //set characters
	 char* strOut;
	 int len = asprintf(&strOut, "%s\n", "sss");

	 // Write to the port
	 int n = write(fd, strOut, len);
	 if (n < 0) {
	 perror("error writing");
	 }
	 fprintf(stdout, "sent: %s\n", NULL);
	 */

	int read_output;
	//fd = open_serial_port_3();
	//while (1){
	//sleep(10);
	read_output = read_serial_port_3(fd);
	//}
	fprintf(stdout, "received: %d\n", read_output);
	// Don't forget to clean up
	close(fd);
	//pthread_join((((struct thread_param*) args)->thread_id), NULL);
}

int uart_thread() {
	/*
	 pthread_t thread_id;
	 pthread_attr_t attr;

	 (void) pthread_attr_init(&attr);
	 (void) pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	 // TODO(lsm): figure out why mongoose dies on Linux if next line is enabled
	 // (void) pthread_attr_setstacksize(&attr, sizeof(struct mg_connection) * 5);

	 return pthread_create(&thread_id, &attr, func, param);
	 */

	void *uart_thread_func(void *args);
	pthread_t thread_id;

	struct thread_param *tp;
	//tp->thread_id=thread_id;
	pthread_create(&thread_id, NULL,uart_thread_func, (void*)tp);
	pthread_join(thread_id,NULL);
	return 0;

}
