/*** includes ***/
#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define EDITOR_VERSION "0.0.1"

/*** data ***/
struct editorConfig{
	int screenrows;
	int screencols;
	struct termios orig_termios;
};

struct editorConfig E;


/*** terminal ***/
void editorClearScreen();
void die(const char* s){
	editorClearScreen();
	perror(s);
	exit(1);
}

void disableRawMode(){
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr");
}

void enableRawMode(){
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
	atexit(disableRawMode);

	struct termios raw = E.orig_termios;
	raw.c_iflag &= ~(BRKINT | ISTRIP | ICRNL | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int getWindowSize(int* rows, int* cols){
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)==-1 || ws.ws_col==0){
		return -1;
	} else{
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}
/*** Append buffer ***/
struct abuff{
	char* b;
	int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuff* ab, const char* s, int len){
	char* new = realloc(ab->b, ab->len+len);

	if (new==NULL) return;
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void abFree(struct abuff* ab){
	free(ab->b);
}

/*** output ***/
void editorClearScreen(struct abuff* ab){
	abAppend(ab, "\x1b[2J", 4);
	abAppend(ab, "\x1b[H", 3);
}

void editorDrawRows(struct abuff* ab){
	for (int i=0; i < E.screenrows; i++){
		if(i==E.screenrows/3){
			char welcome[80];
			int welcomelen = snprintf(welcome, sizeof(welcome), "Welcome to TextEditor v%s", EDITOR_VERSION);
			if (welcomelen>E.screencols) welcomelen=E.screencols;
			int padding=(E.screencols-welcomelen)/2;
			if (padding) abAppend(ab, "~", 1);
			for (int y=0; y<padding; y++){
				abAppend(ab, " ", 1);
			}
			abAppend(ab, welcome, welcomelen);
		} else{
			abAppend(ab, "~", 1);
		}
		abAppend(ab, "\x1b[K", 3);
		if (i < E.screenrows-1) abAppend(ab, "\r\n", 2);
	}
}

void editorRefreshScreen(){
	struct abuff ab = ABUF_INIT;
	abAppend(&ab, "\x1b[?25l", 6);
	abAppend(&ab, "\x1b[H", 3);
	editorDrawRows(&ab);	
	abAppend(&ab, "\x1b[H", 3);
	abAppend(&ab, "\x1b[?25h", 6);
	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

/*** input ***/
char editorReadKey(){
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1){
		if (nread == -1 && errno!=EAGAIN) die("read");
	}
	return c;
}

void editorProcessKeyPress(){
	struct abuff ab = ABUF_INIT;
	char c = editorReadKey();
	switch (c){
		case CTRL_KEY('q'):
			abAppend(&ab, "\x1b[2J", 4);
			abAppend(&ab, "\x1b[H", 3);
			write(STDOUT_FILENO, ab.b, ab.len);
			abFree(&ab);
			exit(0);
			break;
	}
}

/*** init ***/
void initEditor(){
	if (getWindowSize(&E.screenrows, &E.screencols)==-1) die("getWindowSize");
}

int main(){
	enableRawMode();
	initEditor();
	while(1){
		editorRefreshScreen();
		editorProcessKeyPress();	
	}

	return 0;	
}
