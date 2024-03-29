// Copyright (c) 2004-2011 Sergey Lyubka
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS  // Disable deprecation warning in VS2005
#else
#define _XOPEN_SOURCE 600  // For PATH_MAX on linux
#endif

#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdarg.h>
#include <pthread.h>
#include <assert.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/types.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/time.h>

#include "mongoose.h"
#include "uart.h"

#ifdef _WIN32
#include <windows.h>
#include <winsvc.h>
#define PATH_MAX MAX_PATH
#define S_ISDIR(x) ((x) & _S_IFDIR)
#define DIRSEP '\\'
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#define sleep(x) Sleep((x) * 1000)
#define WINCDECL __cdecl
#else
#include <sys/wait.h>
#include <unistd.h>
#define DIRSEP '/'
#define WINCDECL
#endif // _WIN32
#define MAX_OPTIONS 40
#define MAX_CONF_FILE_LINE_SIZE (8 * 1024)

static int exit_flag;
static char server_name[40]; // Set by init_server_name()
static char config_file[PATH_MAX]; // Set by process_command_line_arguments()
static struct mg_context *ctx; // Set by start_mongoose()

#if !defined(CONFIG_FILE)
#define CONFIG_FILE "mongoose.conf"
#endif /* !CONFIG_FILE */

#define MAX_USER_LEN  20
#define MAX_MESSAGE_LEN  100
#define MAX_MESSAGES 5
#define MAX_SESSIONS 2

#define MAX_MESSAGES 5

static const char *ajax_reply_start = "HTTP/1.1 200 OK\r\n"
		"Cache: no-cache\r\n"
		"Content-Type: application/x-javascript\r\n"
		"\r\n";

// Describes single message sent to a chat. If user is empty (0 length),
// the message is then originated from the server itself
struct message {
	long id; // Message ID
	char user[MAX_USER_LEN]; // User that have sent the message
	char text[MAX_MESSAGE_LEN]; // Message text
	time_t timestamp; // Message timestamp, UTC
};

// Describes web session.
struct session {
	char session_id[33]; // Session ID, must be unique
	char random[20]; // Random data used for extra user validation
	char user[MAX_USER_LEN]; // Authenticated user
	time_t expire; // Expiration timestamp, UTC
};
static struct message messages[MAX_MESSAGES];
static struct session sessions[MAX_SESSIONS]; // Current sessions
static long last_message_id;
// Protects messages, sessions, last_message_id
static pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;
static void WINCDECL signal_handler(int sig_num) {
	exit_flag = sig_num;
}

static void die(const char *fmt, ...) {
	va_list ap;
	char msg[200];

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

#if defined(_WIN32)
	MessageBox(NULL, msg, "Error", MB_OK);
#else
	fprintf(stderr, "%s\n", msg);
#endif

	exit(EXIT_FAILURE);
}

static void show_usage_and_exit(void) {
	const char **names;
	int i;

	fprintf(stderr, "Mongoose version %s (c) Sergey Lyubka\n", mg_version());
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "  mongoose -A <htpasswd_file> <realm> <user> <passwd>\n");
	fprintf(stderr, "  mongoose <config_file>\n");
	fprintf(stderr, "  mongoose [-option value ...]\n");
	fprintf(stderr, "OPTIONS:\n");

	names = mg_get_valid_option_names();
	for (i = 0; names[i] != NULL; i += 3) {
		fprintf(stderr, "  -%s %s (default: \"%s\")\n", names[i], names[i + 1],
				names[i + 2] == NULL ? "" : names[i + 2]);
	}
	fprintf(stderr, "See  http://code.google.com/p/mongoose/wiki/MongooseManual"
			" for more details.\n");
	fprintf(stderr, "Example:\n  mongoose -s cert.pem -p 80,443s -d no\n");
	exit(EXIT_FAILURE);
}

static void verify_document_root(const char *root) {
	const char *p, *path;
	char buf[PATH_MAX];
	struct stat st;

	path = root;
	if ((p = strchr(root, ',')) != NULL && (size_t) (p - root) < sizeof(buf)) {
		memcpy(buf, root, p - root);
		buf[p - root] = '\0';
		path = buf;
	}

	if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
		die("Invalid root directory: [%s]: %s", root, strerror(errno));
	}
}

static char *sdup(const char *str) {
	char *p;
	if ((p = (char *) malloc(strlen(str) + 1)) != NULL) {
		strcpy(p, str);
	}
	return p;
}

static void set_option(char **options, const char *name, const char *value) {
	int i;

	if (!strcmp(name, "document_root") || !(strcmp(name, "r"))) {
		verify_document_root(value);
	}

	for (i = 0; i < MAX_OPTIONS - 3; i++) {
		if (options[i] == NULL) {
			options[i] = sdup(name);
			options[i + 1] = sdup(value);
			options[i + 2] = NULL;
			break;
		}
	}

	if (i == MAX_OPTIONS - 3) {
		die("%s", "Too many options specified");
	}
}

static void process_command_line_arguments(char *argv[], char **options) {
	char line[MAX_CONF_FILE_LINE_SIZE], opt[sizeof(line)], val[sizeof(line)],
			*p;
	FILE *fp = NULL;
	size_t i, cmd_line_opts_start = 1, line_no = 0;

	options[0] = NULL;

	// Should we use a config file ?
	if (argv[1] != NULL && argv[1][0] != '-') {
		snprintf(config_file, sizeof(config_file), "%s", argv[1]);
		cmd_line_opts_start = 2;
	} else if ((p = strrchr(argv[0], DIRSEP)) == NULL) {
		// No command line flags specified. Look where binary lives
		snprintf(config_file, sizeof(config_file), "%s", CONFIG_FILE);
	} else {
		snprintf(config_file, sizeof(config_file), "%.*s%c%s",
				(int) (p - argv[0]), argv[0], DIRSEP, CONFIG_FILE);
	}

	fp = fopen(config_file, "r");

	// If config file was set in command line and open failed, die
	if (cmd_line_opts_start == 2 && fp == NULL) {
		die("Cannot open config file %s: %s", config_file, strerror(errno));
	}

	// Load config file settings first
	if (fp != NULL) {
		fprintf(stderr, "Loading config file %s\n", config_file);

		// Loop over the lines in config file
		while (fgets(line, sizeof(line), fp) != NULL) {

			line_no++;

			// Ignore empty lines and comments
			if (line[0] == '#' || line[0] == '\n')
				continue;

			if (sscanf(line, "%s %[^\r\n#]", opt, val) != 2) {
				die("%s: line %d is invalid", config_file, (int) line_no);
			}
			set_option(options, opt, val);
		}

		(void) fclose(fp);
	}

	// Now handle command line flags. They override config file settings.
	for (i = cmd_line_opts_start; argv[i] != NULL; i += 2) {
		if (argv[i][0] != '-' || argv[i + 1] == NULL) {
			show_usage_and_exit();
		}
		set_option(options, &argv[i][1], argv[i + 1]);
	}
}

static void init_server_name(void) {
	snprintf(server_name, sizeof(server_name), "Mongoose web server v. %s",
			mg_version());
}
static void get_qsvar(const struct mg_request_info *request_info,
		const char *name, char *dst, size_t dst_len) {
	const char *qs = request_info->query_string;
	mg_get_var(qs, strlen(qs == NULL ? "" : qs), name, dst, dst_len);
}
// If "callback" param is present in query string, this is JSONP call.
// Return 1 in this case, or 0 if "callback" is not specified.
// Wrap an output in Javascript function call.
static int handle_jsonp(struct mg_connection *conn,
		const struct mg_request_info *request_info) {
	char cb[64];

	// !!!FIND IF QUERY_STRING OF REQUEST_INFO HAS "CALLBACK" INSIDE!!!
	get_qsvar(request_info, "callback", cb, sizeof(cb));
	if (cb[0] != '\0') {
		mg_printf(conn, "%s(", cb);
	}

	return cb[0] == '\0' ? 0 : 1;
}
// Allocate new message. Caller must hold the lock.

static struct message *new_message(void) {
	static int size = sizeof(messages) / sizeof(messages[0]);
	struct message *message = &messages[last_message_id % size];
	message->id = last_message_id++;
	message->timestamp = time(0);
	return message;
}

// Get session object for the connection. Caller must hold the lock.
static struct session *get_session(const struct mg_connection *conn) {
	int i;
	char session_id[33];
	time_t now = time(NULL);
	mg_get_cookie(conn, "session", session_id, sizeof(session_id));
	for (i = 0; i < MAX_SESSIONS; i++) {
		if (sessions[i].expire != 0 && sessions[i].expire > now
				&& strcmp(sessions[i].session_id, session_id) == 0) {
			break;
		}
	}
	return i == MAX_SESSIONS ? NULL : &sessions[i];
}
static void my_strlcpy(char *dst, const char *src, size_t len) {
	strncpy(dst, src, len);
	dst[len - 1] = '\0';
}
static void my_uart_tx(char *uart_text) {
	int fd;
	// Open the Port. We want read/write, no "controlling tty" status, and open it no matter what state DCD is in
	fd = open("/dev/ttyAMA0", O_WRONLY | O_NOCTTY | O_NDELAY | O_NONBLOCK);
	if (fd == -1) {
		perror("open_port: Unable to open /dev/ttyAMA0 - ");
		// return (-1);
	}

	// Turn off blocking for reads, use (fd, F_SETFL, FNDELAY) if you want that
	//fcntl(fd, F_SETFL, 0);

	//Set the baud rate
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

	//set characters
	char* strOut;
	int len = asprintf(&strOut, "%s\n", uart_text);

	// Write to the port
	int n = write(fd, strOut, len);
	if (n < 0) {
		perror("error writing");
		// return -1;
	}

	// Don't forget to clean up
	close(fd);
	// return 0;
}
// A handler for the /ajax/send_message endpoint.
static void ajax_send_message(struct mg_connection *conn,
		const struct mg_request_info *request_info) {
	struct message *message;
	struct session *session;
	char text[sizeof(message->text) - 1];
	int is_jsonp;
	mg_printf(conn, "%s", ajax_reply_start);
	is_jsonp = handle_jsonp(conn, request_info);
	get_qsvar(request_info, "text", text, sizeof(text));
	if (text[0] != '\0') {
		// We have a message to store. Write-lock the ringbuffer,
		// grab the next message and copy data into it.
		pthread_rwlock_wrlock(&rwlock);
		message = new_message();
		// TODO(lsm): JSON-encode all text strings
		// session = get_session(conn);
		// assert(session != NULL);
		my_strlcpy(message->text, text, sizeof(text));
		// !!! TRY UART !!!
		my_uart_tx(message->text);
		// my_strlcpy(message->user, session->user, sizeof(message->user));
		my_strlcpy(message->user, "from_webpage", sizeof(message->user));
		pthread_rwlock_unlock(&rwlock);
	}
	mg_printf(conn, "%s", text[0] == '\0' ? "false" : "true");

	if (is_jsonp) {
		mg_printf(conn, "%s", ")");
	}

}
static char *messages_to_json(long last_id) {
	const struct message *message;
	int max_msgs, len;
	char buf[sizeof(messages)]; // Large enough to hold all messages

	// Read-lock the ringbuffer. Loop over all messages, making a JSON string.
	pthread_rwlock_rdlock(&rwlock);
	len = 0;
	// MAX_MSGS: THINK ABOUT MESSAGES CONTAINING JUST 1 MESSAGE --> MAX_MSGS=1
	max_msgs = sizeof(messages) / sizeof(messages[0]);
	// If client is too far behind, return all messages.
	if (last_message_id - last_id > max_msgs) {
		last_id = last_message_id - max_msgs;
	}
	for (; last_id < last_message_id; last_id++) {
		message = &messages[last_id % max_msgs];
		if (message->timestamp == 0) {
			break;
		}
		// buf is allocated on stack and hopefully is large enough to hold all
		// messages (it may be too small if the ringbuffer is full and all
		// messages are large. in this case asserts will trigger).
		len += snprintf(buf + len, sizeof(buf) - len,
				"{user: '%s', text: '%s', timestamp: %lu, id: %lu},",
				message->user, message->text, message->timestamp, message->id);
		assert(len > 0);
		assert((size_t) len < sizeof(buf));
	}
	// TRIM ENDING ',' IN BUFFER
	buf[len - 1] = 0;
	pthread_rwlock_unlock(&rwlock);
	// GOOD FOR DEBUGGING
	// fprintf(stdout, "string %s\n", buf);
	return len == 0 ? NULL : strdup(buf);
}
// A handler for the /ajax/get_messages endpoint.
// Return a list of messages with ID greater than requested.
static void ajax_get_messages(struct mg_connection *conn,
		const struct mg_request_info *request_info) {

	char last_id[32], *json;
	int is_jsonp;

	mg_printf(conn, "%s", ajax_reply_start);
	is_jsonp = handle_jsonp(conn, request_info);

	get_qsvar(request_info, "last_id", last_id, sizeof(last_id));
	// HOVER THE MOUSE OVER STRTOUL
	if ((json = messages_to_json(strtoul(last_id, NULL, 10))) != NULL) {
		// CAST JSON BUFFER INTO SQUARE BRACKETS
		mg_printf(conn, "[%s]", json);
		free(json);
	}

	if (is_jsonp) {
		mg_printf(conn, "%s", ")");
	}

}
static void *mongoose_callback(enum mg_event ev, struct mg_connection *conn) {
	if (ev == MG_EVENT_LOG) {
		printf("%s\n", mg_get_request_info(conn)->log_message);
	}

	const struct mg_request_info *request_info = mg_get_request_info(conn);
	void *processed = (void *) "yes";

	if (ev == MG_NEW_REQUEST) {
		//if (!request_info->is_ssl) {
		//  redirect_to_ssl(conn, request_info);
		//} else if (!is_authorized(conn, request_info)) {
		// redirect_to_login(conn, request_info);
		//} else if (strcmp(request_info->uri, authorize_url) == 0) {
		//  authorize(conn, request_info);
		//} else if (strcmp(request_info->uri, "/ajax/get_messages") == 0) {
		if (strcmp(request_info->uri, "/ajax/get_messages") == 0) {
			ajax_get_messages(conn, request_info);
		} else if (strcmp(request_info->uri, "/ajax/send_message") == 0) {
			ajax_send_message(conn, request_info);
		} else {
			// No suitable handler found, mark as not processed. Mongoose will
			// try to serve the request.
			processed = NULL;
		}
	} else {
		processed = NULL;
	}

	return processed;
}

static void start_mongoose(int argc, char *argv[]) {
	char *options[MAX_OPTIONS];
	int i;

	// Edit passwords file if -A option is specified
	if (argc > 1 && !strcmp(argv[1], "-A")) {
		if (argc != 6) {
			show_usage_and_exit();
		}
		exit(
				mg_modify_passwords_file(argv[2], argv[3], argv[4], argv[5]) ?
						EXIT_SUCCESS : EXIT_FAILURE);
	}

	// Show usage if -h or --help options are specified
	if (argc == 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		show_usage_and_exit();
	}

	/* Update config based on command line arguments */
	process_command_line_arguments(argv, options);

	/* Setup signal handler: quit on Ctrl-C */
	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);

	/* Start Mongoose */
	ctx = mg_start(&mongoose_callback, NULL, (const char **) options);
	for (i = 0; options[i] != NULL; i++) {
		free(options[i]);
	}

	if (ctx == NULL) {
		die("%s", "Failed to start Mongoose.");
	}
}
#ifdef _WIN32
static SERVICE_STATUS ss;
static SERVICE_STATUS_HANDLE hStatus;
static const char *service_magic_argument = "--";

static void WINAPI ControlHandler(DWORD code) {
	if (code == SERVICE_CONTROL_STOP || code == SERVICE_CONTROL_SHUTDOWN) {
		ss.dwWin32ExitCode = 0;
		ss.dwCurrentState = SERVICE_STOPPED;
	}
	SetServiceStatus(hStatus, &ss);
}

static void WINAPI ServiceMain(void) {
	ss.dwServiceType = SERVICE_WIN32;
	ss.dwCurrentState = SERVICE_RUNNING;
	ss.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;

	hStatus = RegisterServiceCtrlHandler(server_name, ControlHandler);
	SetServiceStatus(hStatus, &ss);

	while (ss.dwCurrentState == SERVICE_RUNNING) {
		Sleep(1000);
	}
	mg_stop(ctx);

	ss.dwCurrentState = SERVICE_STOPPED;
	ss.dwWin32ExitCode = (DWORD) -1;
	SetServiceStatus(hStatus, &ss);
}

#define ID_TRAYICON 100
#define ID_QUIT 101
#define ID_EDIT_CONFIG 102
#define ID_SEPARATOR 103
#define ID_INSTALL_SERVICE 104
#define ID_REMOVE_SERVICE 105
#define ID_ICON 200
static NOTIFYICONDATA TrayIcon;

static void edit_config_file(void) {
	const char **names, *value;
	FILE *fp;
	int i;
	char cmd[200];

	// Create config file if it is not present yet
	if ((fp = fopen(config_file, "r")) != NULL) {
		fclose(fp);
	} else if ((fp = fopen(config_file, "a+")) != NULL) {
		fprintf(fp,
				"# Mongoose web server configuration file.\n"
				"# Lines starting with '#' and empty lines are ignored.\n"
				"# For detailed description of every option, visit\n"
				"# http://code.google.com/p/mongoose/wiki/MongooseManual\n\n");
		names = mg_get_valid_option_names();
		for (i = 0; names[i] != NULL; i += 3) {
			value = mg_get_option(ctx, names[i]);
			fprintf(fp, "# %s %s\n", names[i + 1], *value ? value : "<value>");
		}
		fclose(fp);
	}

	snprintf(cmd, sizeof(cmd), "notepad.exe %s", config_file);
	WinExec(cmd, SW_SHOW);
}

static void show_error(void) {
	char buf[256];
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, GetLastError(),
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			buf, sizeof(buf), NULL);
	MessageBox(NULL, buf, "Error", MB_OK);
}

static int manage_service(int action) {
	static const char *service_name = "Mongoose";
	SC_HANDLE hSCM = NULL, hService = NULL;
	SERVICE_DESCRIPTION descr = {server_name};
	char path[PATH_MAX + 20]; // Path to executable plus magic argument
	int success = 1;

	if ((hSCM = OpenSCManager(NULL, NULL, action == ID_INSTALL_SERVICE ?
							GENERIC_WRITE : GENERIC_READ)) == NULL) {
		success = 0;
		show_error();
	} else if (action == ID_INSTALL_SERVICE) {
		GetModuleFileName(NULL, path, sizeof(path));
		strncat(path, " ", sizeof(path));
		strncat(path, service_magic_argument, sizeof(path));
		hService = CreateService(hSCM, service_name, service_name,
				SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
				SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
				path, NULL, NULL, NULL, NULL, NULL);
		if (hService) {
			ChangeServiceConfig2(hService, SERVICE_CONFIG_DESCRIPTION, &descr);
		} else {
			show_error();
		}
	} else if (action == ID_REMOVE_SERVICE) {
		if ((hService = OpenService(hSCM, service_name, DELETE)) == NULL ||
				!DeleteService(hService)) {
			show_error();
		}
	} else if ((hService = OpenService(hSCM, service_name,
							SERVICE_QUERY_STATUS)) == NULL) {
		success = 0;
	}

	CloseServiceHandle(hService);
	CloseServiceHandle(hSCM);

	return success;
}

static LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam,
		LPARAM lParam) {
	static SERVICE_TABLE_ENTRY service_table[] = {
		{	server_name, (LPSERVICE_MAIN_FUNCTION) ServiceMain},
		{	NULL, NULL}
	};
	int service_installed;
	char buf[200], *service_argv[] = {__argv[0], NULL};
	POINT pt;
	HMENU hMenu;

	switch (msg) {
		case WM_CREATE:
		if (__argv[1] != NULL &&
				!strcmp(__argv[1], service_magic_argument)) {
			start_mongoose(1, service_argv);
			StartServiceCtrlDispatcher(service_table);
			exit(EXIT_SUCCESS);
		} else {
			start_mongoose(__argc, __argv);
		}
		break;
		case WM_COMMAND:
		switch (LOWORD(wParam)) {
			case ID_QUIT:
			mg_stop(ctx);
			Shell_NotifyIcon(NIM_DELETE, &TrayIcon);
			PostQuitMessage(0);
			break;
			case ID_EDIT_CONFIG:
			edit_config_file();
			break;
			case ID_INSTALL_SERVICE:
			case ID_REMOVE_SERVICE:
			manage_service(LOWORD(wParam));
			break;
		}
		break;
		case WM_USER:
		switch (lParam) {
			case WM_RBUTTONUP:
			case WM_LBUTTONUP:
			case WM_LBUTTONDBLCLK:
			hMenu = CreatePopupMenu();
			AppendMenu(hMenu, MF_STRING | MF_GRAYED, ID_SEPARATOR, server_name);
			AppendMenu(hMenu, MF_SEPARATOR, ID_SEPARATOR, "");
			service_installed = manage_service(0);
			snprintf(buf, sizeof(buf), "NT service: %s installed",
					service_installed ? "" : "not");
			AppendMenu(hMenu, MF_STRING | MF_GRAYED, ID_SEPARATOR, buf);
			AppendMenu(hMenu, MF_STRING | (service_installed ? MF_GRAYED : 0),
					ID_INSTALL_SERVICE, "Install service");
			AppendMenu(hMenu, MF_STRING | (!service_installed ? MF_GRAYED : 0),
					ID_REMOVE_SERVICE, "Deinstall service");
			AppendMenu(hMenu, MF_SEPARATOR, ID_SEPARATOR, "");
			AppendMenu(hMenu, MF_STRING, ID_EDIT_CONFIG, "Edit config file");
			AppendMenu(hMenu, MF_STRING, ID_QUIT, "Exit");
			GetCursorPos(&pt);
			SetForegroundWindow(hWnd);
			TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, hWnd, NULL);
			PostMessage(hWnd, WM_NULL, 0, 0);
			DestroyMenu(hMenu);
			break;
		}
		break;
	}

	return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdline, int show) {
	WNDCLASS cls;
	HWND hWnd;
	MSG msg;

	init_server_name();
	memset(&cls, 0, sizeof(cls));
	cls.lpfnWndProc = (WNDPROC) WindowProc;
	cls.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	cls.lpszClassName = server_name;

	RegisterClass(&cls);
	hWnd = CreateWindow(cls.lpszClassName, server_name, WS_OVERLAPPEDWINDOW,
			0, 0, 0, 0, NULL, NULL, NULL, NULL);
	ShowWindow(hWnd, SW_HIDE);

	TrayIcon.cbSize = sizeof(TrayIcon);
	TrayIcon.uID = ID_TRAYICON;
	TrayIcon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	TrayIcon.hIcon = LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(ID_ICON),
			IMAGE_ICON, 16, 16, 0);
	TrayIcon.hWnd = hWnd;
	snprintf(TrayIcon.szTip, sizeof(TrayIcon.szTip), "%s", server_name);
	TrayIcon.uCallbackMessage = WM_USER;
	Shell_NotifyIcon(NIM_ADD, &TrayIcon);

	while (GetMessage(&msg, hWnd, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMess
		age(&msg);
	}
}
#else
/* The signal handler just clears the flag and re-enables itself.  */
/* This flag controls termination of the main loop.  */
#define INTERVAL 5
struct thread_param {
	int device_status;
	char timecode[4];
	pthread_t thread_id;
};
void uart_thread_func() {
	int fd;
	struct message *message;
	struct session *session;
	char text[sizeof(message->text) - 1];
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

	char *read_output;
	// while (1){
	// sleep(10);
	read_output = read_serial_port(fd);
	// }
	fprintf(stdout, "received: %s\n", read_output);
	// Don't forget to clean up
	// close(fd);
	pthread_rwlock_wrlock(&rwlock);
	message = new_message();
	// TODO(lsm): JSON-encode all text strings
	// session = get_session(conn);
	// assert(session != NULL);
	my_strlcpy(message->text, read_output, sizeof(text));
	// my_strlcpy(message->user, session->user, sizeof(message->user));
	my_strlcpy(message->user, "from_terminal", sizeof(message->user));
	pthread_rwlock_unlock(&rwlock);
	//pthread_exit((void*) args);
}
int uart_thread() {
	/*
	 void *uart_thread_func(void *args);
	 pthread_t thread_id;
	 pthread_attr_t attr;
	 pthread_attr_init(&attr);
	 pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	 struct thread_param *tp;
	 pthread_create(&thread_id, &attr, uart_thread_func, (void*) tp);
	 pthread_attr_destroy(&attr);
	 pthread_join(thread_id, NULL);
	 */
	uart_thread_func();
	return 0;
}
void catch_alarm(int sig) {

	uart_thread();
	struct itimerval tout_val;

	signal(SIGALRM, catch_alarm);

	tout_val.it_interval.tv_sec = 0;
	tout_val.it_interval.tv_usec = 0;
	tout_val.it_value.tv_sec = INTERVAL; /* 10 seconds timer */
	tout_val.it_value.tv_usec = 0;

	setitimer(ITIMER_REAL, &tout_val, 0);
}
static void start_uart_thread() {
	struct itimerval tout_val;

	tout_val.it_interval.tv_sec = 0;
	tout_val.it_interval.tv_usec = 0;
	tout_val.it_value.tv_sec = INTERVAL; /* 10 seconds timer */
	tout_val.it_value.tv_usec = 0;

	setitimer(ITIMER_REAL, &tout_val, 0);
	signal(SIGALRM, catch_alarm); /* set the Alarm signal capture */
}

int main(int argc, char *argv[]) {
	init_server_name();
	start_mongoose(argc, argv);
	printf("%s started on port(s) %s with web root [%s]\n", server_name,
			mg_get_option(ctx, "listening_ports"),
			mg_get_option(ctx, "document_root"));
	sleep(1);
	start_uart_thread();
	while (exit_flag == 0) {
		sleep(1);
	}
	printf("Exiting on signal %d, waiting for all threads to finish...",
			exit_flag);
	fflush(stdout);
	mg_stop(ctx);
	printf("%s", " done.\n");

	return EXIT_SUCCESS;
}
#endif /* _WIN32 */
