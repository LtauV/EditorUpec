#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include <stdlib.h>
#include <termios.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <string.h>
#include <ctype.h>

#define MOUSE_XRATIO 3
#define MOUSE_YRATIO 6

signed char mice_data[3];
long mouse_x = 0;
long mouse_y = 36;
int left, middle, right;
signed char dx, dy;
int fd;
struct pollfd fds[1];
int timeout_msecs = 50;
struct termios cooked_termios; //termios d'origine


int init_mouse(){

  fds[0].fd = open("/dev/input/mice", O_RDONLY |O_NONBLOCK);
  fd = fds[0].fd;
  if(fd == -1){
    printf("ERROR Opening %s\n", "/dev/input/mice");
    return -1;
  }
  fds[0].events = POLLIN;
  return 0;
}

int init_kbd(){
  int ret;
  ret = fcntl(STDIN_FILENO, F_GETFL);
  fcntl(STDIN_FILENO, F_SETFL, ret | O_NONBLOCK);
  //fds[1].fd = STDIN_FILENO;
  //fds[1].events = POLLIN;
  return 0;
}

//Gestion du curseur souris a la coordonnee (x,y)
void PutMouseCursor (int x, int y){ //, struct appbuf *a){
  char s[50], e='\x1b', n=' '; //'\0';
    sprintf(s,"%c7%c[%03d;%03dH%c[7m%c%c8",e,e,y,x,e,n,e);
    write(STDOUT_FILENO, s, 19); //sleep(1);
}

int mice_move(){
  int ret;
  //fflush(stdout);
  ret=read(fd, mice_data, sizeof(mice_data));
  if (ret <= 0) for(int i=1; i<=2; i++) mice_data[i]=0;
  return 0;
}

int close_mouse() {
  close(fd);
  return 0;
}

int update_mouse(){
    mice_move();
    mouse_x += (int) mice_data[1]; mouse_x = (mouse_x<0?0:mouse_x); mouse_x = (mouse_x>73*MOUSE_XRATIO?73*MOUSE_XRATIO:mouse_x);
    mouse_y -= (int) mice_data[2]; mouse_y = (mouse_y<0?0:mouse_y); mouse_y = (mouse_y>36*MOUSE_YRATIO?36*MOUSE_YRATIO:mouse_y);
    left = mice_data[0] & 0x1;
    //right = data[0] & 0x2; //marche pas
    //middle = data[0] & 0x4; //marche pas
    PutMouseCursor(mouse_x/MOUSE_XRATIO, mouse_y/MOUSE_YRATIO);
    return 0;
}

// Traitement d'erreur
void exception(const char *error){
//  ClrScr(NULL);
  perror(error);
  exit(1);
}

//Activation du TTY en mode non-canonique
void RawMode_on() {
    int ret;
    ret=tcgetattr(STDIN_FILENO,&cooked_termios);
    if(ret == -1) exception("error in RawMode_on get");

    struct termios raw_termios = cooked_termios;

    raw_termios.c_iflag &= ~(IXON | ICRNL);
    raw_termios.c_lflag &= ~(ICANON | ECHO | IEXTEN | ISIG);
    raw_termios.c_oflag &= ~(OPOST);
    raw_termios.c_cc[VMIN]=0;
    raw_termios.c_cc[VTIME]=0;
    ret = tcsetattr(STDIN_FILENO,TCSAFLUSH,&raw_termios);
    if(ret == -1) exception("error in RawMode_on get");
}

//Retour du TTY au mode canonique
void RawMode_off() {
  int ret;
  ret = tcsetattr(STDIN_FILENO, TCSAFLUSH, &cooked_termios);
  if(ret == -1) exception("error in RawMode_off");
  printf("\r");
}


int main(){
    int ret=0,c=0,l;
    char s[1024];
    init_mouse();
    //init_kbd();
    atexit(RawMode_off);
    RawMode_on();
    write(STDOUT_FILENO,"\x1b[2J",4);
    while(1){
     	ret = poll(fds,(nfds_t) 1,timeout_msecs);
     	if (ret > 0)	{    // An event on one of the fds has occurred.
       	if (fds[0].revents & POLLIN)	{
          update_mouse();
        }
      }
      ret = read(STDIN_FILENO,&c,1);
      if (ret == -1 ) { c='\0';}
//      printf("X %ld\tY %ld\t\tdx %d\tdy %d\t\t%c\t%d,%c\n\r", mouse_x/MOUSE_XRATIO, mouse_y/MOUSE_YRATIO, mice_data[1], mice_data[2],left?'X':'\0',ret,c);
      sprintf(s,"\x1b[36;1H\x1b[2KX %ld\tY %ld\t\tdx %d\tdy %d\t\t%c\t%d,%c", mouse_x/MOUSE_XRATIO, mouse_y/MOUSE_YRATIO, mice_data[1], mice_data[2],left?'X':' ',ret,c);
      l=strlen(s);
      write(STDOUT_FILENO,s,l+1);
      if (c=='\x03') exit(0);
      if (c=='\x1b') {mouse_x=0; mouse_y=0;}
      c=0;
    }
    close_mouse();
    return 0;
}
