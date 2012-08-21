/*
 * uart.h
 *
 *  Created on: 16/ago/2012
 *      Author: Utente
 */

/*
 * uart.h
 *
 *  Created on: 15/ago/2012
 *      Author: Utente
 */

#ifndef UART_H_
#define UART_H_
#include <stdbool.h>
#include <stdint.h>

typedef void *serial_port;
int open_serial_port();
void set_serial_port(int fd);
char *read_serial_port(int fd);

#endif /* UART_H_ */
