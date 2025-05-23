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
enum editorKey{
	ARROW_LEFT=1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL,
	HOME,
	END,
	PAGE_UP,
	PAGE_DOWN
};
/*** data ***/
struct editorConfig{
	int cx, cy;
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

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy+1, E.cx+1);
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}
/*** input ***/
int editorReadKey(){
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1){
		if (nread == -1 && errno!=EAGAIN) die("read");
	}
	if (c=='\x1b'){
		char seq[3];

		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

		if (seq[0] == '['){
			if (seq[1]>='0' && seq[1]<='9'){
				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if (seq[2]=='~'){
					switch (seq[1]){
						case '1': return HOME;
						case '3': return DEL;
						case '4': return END;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME;
						case '8': return END;
					}
				} 
			}


			else{
				switch (seq[1]){
					case 'A': return ARROW_UP; 
					case 'B': return ARROW_DOWN; 
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME;
					case 'F': return END;
				}
			}

		}
		else if (seq[0]=='O'){
			switch (seq[1]){
				case 'H': return HOME;
				case 'F': return END;
			}
		}
		return '\x1b';
	} else{
		return c;
	}
}


void editorMoveCursor(int key);
void editorProcessKeyPress(){
	struct abuff ab = ABUF_INIT;
	int c = editorReadKey();
	switch (c){
		case CTRL_KEY('q'):
			abAppend(&ab, "\x1b[2J", 4);
			abAppend(&ab, "\x1b[H", 3);
			write(STDOUT_FILENO, ab.b, ab.len);
			abFree(&ab);
			exit(0);
			break;
		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editorMoveCursor(c);
			break;
		case PAGE_UP:
		case PAGE_DOWN:
			{
			int times = E.screenrows;
			while(times--){
				editorMoveCursor(c==PAGE_UP?ARROW_UP:ARROW_DOWN);
			}
			}
			break;
		case HOME:
			E.cx=0;
			break;
		case END:
			E.cx=E.screencols-1;
			break;
		}
}

void editorMoveCursor(int key){ 
	switch(key)
	{
		case ARROW_LEFT:
			if (E.cx != 0){
				E.cx--;
			}
			break;
		case ARROW_RIGHT:
			if (E.cx != E.screencols-1){
				E.cx++;
			}
			break;
		case ARROW_UP:
			if (E.cy != 0){
				E.cy--;
			}
			break;
		case ARROW_DOWN:
			if (E.cy != E.screenrows-1){
				E.cy++;
			}
			break;
	}
}

/*** init ***/
void initEditor(){
	E.cx = 0;
	E.cy = 0;
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
