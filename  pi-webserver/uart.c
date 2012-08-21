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

// struct message messages2[MAX_MESSAGES]; // Ringbuffer for messages
// struct message *messages2 = (message *)malloc(sizeof(message) * 5);

int open_serial_port() {
	return 0;
}
void set_serial_port(int fd) {
}

void choppy(char *s) {
	s[strcspn(s, "\n")] = '\0';
}

char *read_serial_port(int fd) {
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
	choppy((char *) mesg);
	return (char *) mesg;
}
