#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

//above the includes because header files included use the macros
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <termios.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_VERSION "0.0.1"

//New Vocab:

//Raw Mode: characters are directly read from and written to the device
//without translation or interpretation

//Canonical Mode: the mode that your terminal is in by default also 
//called cooked mode. Keyboard input doesn't get sent to the program
//until the user press enter.
  
//termios: reads in terminal attributes and ttcsetattr applies them

//c_cc: control characters

//TIOCGWINSZ Terminal input output control get window size

//define keys for cursor movement
enum editorKey{
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

struct termios orig_termios;

//error handling
void die(const char *s){
    //4 means that we are writing 4 bytes to the terminal
    // \x1b is first byte which is the escape character
    write(STDOUT_FILENO, "\x1b[2J", 4);
    //reset the cursor position
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}
//stores a line of text as pointer to dynamically allocated character and data length
typedef struct erow{
    int size;
    char *chars;
}
erow;

struct editorConfig{
    //cursor x and y position
    int cx, cy;
    //row offset 
    int rowoff;
    //column offset
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    struct termios orig_termios;
};

struct editorConfig E;

//disabling raw mode
void disableRawMode(){
    //tcgetattr() sets a terminal's attributes
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1){
        die("tcsetattr");
    }
}

//enabling raw mode 
void enableRawMode(){
    //tcgetattr 
    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr") ;
    //disable raw mode at exit
    atexit(disableRawMode);

    //store original terminal attributes
    struct termios raw = E.orig_termios;
    
    //turn off ctrl s and ctrl q IXON
    //fix control m ICRNL
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    //turn off output processing
    raw.c_oflag &= ~(OPOST);
    //I don't know what flag this is turning off but it's here
    raw.c_cflag |= (CS8);
    //turns off the echo function, echo prints each key pressed to the terminal
    //ICANON turns off canonical mode
    //c_lflag is for local flags 
    //ISIG turn of ctrl c and ctrl z
    //IEXTEN turn off ctrl v
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    //set time for timeout so read() returns if it doesn't get input
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    //TCSaFLUSH specifies when to apply the change
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int editorReadKey(){
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if(nread == -1 && errno != EAGAIN) die("read");
    }
    if(c == '\x1b'){
        char seq[3];

        if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';    

        //use arrow keys to move cursor and page up and page down
        if(seq[0] == '['){
            //if at the top or bottom of the page
            if(seq[1] >= '0' && seq[1] <= '9'){
                if(read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if(seq[2] == '~'){
                    switch(seq[1]){
                        //make cases for the home and end key
                        case '1': return HOME_KEY;
                        //case for the delete key
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        //case for when page_up is pressed
                        case '5': return PAGE_UP;
                        //case for when page_down is pressed
                        case '6': return PAGE_DOWN;
                        //again?
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            }
            else{
                switch(seq[1]){
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        }
        else if(seq[0] == 'O'){
            //home and end key returns
            switch(seq[1]){
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
    return '\x1b';
    }
    else{
        return c; 
    }
}

int getCursorPosition(int *rows, int *cols){
    char buf[32];
    unsigned int i = 0; 
    //die closed 
    if(write(STDIN_FILENO, "\x1b[6n", 4) != 4) return -1;
    //print new line
    printf("\r\n");
    while(i < sizeof(buf) - 1){
        //break if there's an error
        if(read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if(buf[i] == 'R') break;
        i++;
    }

    //assign 0 to the final byte of buf so it knows when string terminates
    buf[i] = '\0';
    
    //ignore esacpe characters and open brace
    if(buf[0] != '\x1b' || buf[1] != '[') return -1;
    //parse two integers seperated by ; and put values in rows and cols variables
    if(sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
    return 0;
}

//get the window size, it's right in the name
int getWindowSize(int *rows, int *cols){
    struct winsize ws;
    //check if broken, specifically the window size 
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        //don't let cursor go past window boundaries?
        return getCursorPosition(rows, cols);
    }
    //place cols and rows into winsize struct 
    else{
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}
void editorAppendRow(char *s, size_t len){
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    
    int at = E.numrows;
    E.row[at].size = len;
    //allocate enough memory for the length of the message
    E.row[at].chars = malloc(len + 1);
    //memcpy() the message to the chars field which points to the allocated memory
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    //set numrows to one to indict erow has a line that now needs to be displayed
    E.numrows++;    
}
//File i/o
void editorOpen(char *filename){
    //fopen takes a filename and opens the file for reading
    FILE *fp = fopen(filename, "r");
    if(!fp) die ("fopen");
    char *line = NULL;
    size_t linecap = 0;
    //Length of each line
    ssize_t linelen;
    //getline is useful for as it does memory management for you, as long as linecap still has space it'll continue to read
    linelen = getline(&line, &linecap, fp);
    if(linelen != -1){
        while(linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')){
            linelen--;
        }
        editorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp);
}
//append buffer
struct abuf{
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len){
    //allocate enough memory to hold a new string  
    char *new = realloc(ab->b, ab->len + len);
    if(new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}
//get rid of excess memory 
void abFree(struct abuf *ab){
    free(ab->b);
}
//scroll up and down in the editor
void editorScroll(){
    if(E.cy < E.rowoff){
        E.rowoff = E.cy;
    }
    if(E.cy >= E.rowoff + E.screenrows){
        E.rowoff = E.cy - E.screenrows + 1;
    }
    //horizontal scrolling
    if(E.cx < E.coloff){
        E.coloff = E.cx - E.screencols + 1;
    }
    if(E.cx >= E.coloff + E.screencols){
        E.coloff = E.cx - E.screencols + 1;
    }
}
//draw each row of the buffer of text being edited
void editorDrawRows(struct abuf *ab){
    int y;
    for(y = 0; y < E.screenrows; y++){
        //check to see if the row being drawn is after the end of the text buffer
        int filerow = y  + E.rowoff;
        if(filerow >= E.numrows){
            if(E.numrows == 0 && y == E.screenrows / 3){
                //print a welcome message only if the buffer is completely empty
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo Editor -- version %s", KILO_VERSION);
                if(welcomelen > E.screencols) welcomelen = E.screencols;
                //center the string 
                int padding = (E.screencols - welcomelen) / 2;
                if(padding){
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while(padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            }
            else{
                abAppend(ab, "~", 1);
             }
        }
        //draw a row that's part of the text buffer
        else{
            //length of row
            int len = E.row[filerow].size - E.coloff;
            if(len < 0) len = 0;
            if(len > E.screencols) len = E.screencols;
            abAppend(ab, &E.row[filerow].chars[E.coloff], len);  
        }
        abAppend(ab, "\x1b[K", 3);
        //if at end write last line 
        if(y < E.screenrows -1){
            abAppend(ab, "\r\n", 2);
        }
    }
}

//refresh screen
void editorRefreshScreen(){
    editorScroll();
    struct abuf ab = ABUF_INIT;
    //Hide the cursor when the screen is being drawn 
    abAppend(&ab, "\x1b[?25l", 6);
    //reset the cursor position
    abAppend(&ab, "\x1b[H", 3);
    //draw rows of buffer text 
    editorDrawRows(&ab);

    //Moving the cursor!!
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.cx - E.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));

    //unhide cursor
    abAppend(&ab, "\x1b[?25h", 6);
    write(STDOUT_FILENO, ab.b, ab.len);
    //free extra memory
    abFree(&ab);
}

void editorMoveCursor(int key){
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    //input to move the cursor around
    switch(key){
        //Go left 
        case ARROW_LEFT:
            if(E.cx != 0){
                E.cx--;
            }
            break;
        //Go right, can scroll past the edge of the screen
        case ARROW_RIGHT:
            if(row && E.cx < row->size){
                E.cx++;
            }
            break;
        //Go up
        case ARROW_UP:
            if(E.cy != 0){
                E.cy--;
            }
            break;
        //Go down 
        case ARROW_DOWN:
            if(E.cy != E.numrows){
                E.cy++;
            }
            break;
    }
}

void editorProcessKeypress(){
    int c = editorReadKey();
    
    switch(c){
        //quit on ctrl q 
        case CTRL_KEY('q'):
            //4 means that we are writing 4 bytes to the terminal
            // \x1b is first byte which is the escape character
            write(STDOUT_FILENO, "\x1b[2J", 4);
            //reset the cursor position
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            E.cx = E.screencols - 1;
            break;   
        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = E.screenrows;
                //move cursor to either the top or the bottom of the page
                while(times--){
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
            }
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}
//initialize all fields in the E struct
void initEditor(){
    E.cx = 0;
    E.cy = 0;
    //initialized to 0 so it'll be scrolled up automatically
    E.rowoff = 0;
    //initialized to 0 it'll be scrolled left automatically
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    if(getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(int argc, char *argv[]){
    enableRawMode();
    initEditor();
    if(argc >= 2){
        editorOpen(argv[1]);
    }

    while(1){
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
