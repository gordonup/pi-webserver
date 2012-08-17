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
int open_serial_port_1();
void set_serial_port_1(int fd);
void read_serial_port_1(int fd, char *buf);
int close_serial_port_1(int fd);
serial_port open_serial_port_2(const char *pcPortName);
void set_serial_port_2(serial_port sp, const uint32_t uiPortSpeed);
int read_serial_port_2(serial_port sp, uint8_t *pbtRx, const size_t szRx,
		void *abort_p, int timeout);
void flush_serial_port_2(serial_port sp);
int open_serial_port_3();
int read_serial_port_3(int fd);
int uart_thread();


#endif /* UART_H_ */
