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
int read_serial_port(int fd);
int uart_thread();

#endif /* UART_H_ */
