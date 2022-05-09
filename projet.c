#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <poll.h>
#include <time.h>


#define CTRL_KEY(k) ((k) & 0x1f) //macro permettant d'interpreter la touche ctrl
#define ARROW_KEY(k) ((k) | 0x80) //macro permettant d'interpreter les fleches

#define BUFSIZE 1024
#define DEF_TERMCOLUMN 80
#define DEF_TERMLINE 24
#define TAB_SIM 5
#define FNAME_LGMAX 255
#define ED_HELPNAME "EditorUpec-v0.1.hlp"
#define ED_DEFFNAME "e"
#define ED_NBMAX 10
#define MOUSE_FDNAME "/dev/input/mice"
#define MOUSE_XRATIO 3
#define MOUSE_YRATIO 6
#define TTY_MINLINES 20
#define TTY_MINCOLS  65

const int true = 1;
const int false = 0;

int ret;

//modes de l'editeur
enum editorMode {
  NORMAL,
  INSERT,
  COMMAND,
  REPLACE
};

//codes graphiques des messages de service
enum msgVideoMode {
  VM_NORMAL,
  VM_INVERSE,
  VM_ERREUR,
};

//Structure d'une ligne de l'editeur
struct edrow {
  int size;
  char *str;
};

//structure gardant les informations de l'editeur
struct editorState {
  int x , y; //position du curseur dans le TTY
  int bx , by; // colonne et ligne du caractere de l'editeur sous le curseur
  int cx, cy; // coordonnees curseur TTY calculees via edComputeCursor
  int mx, my; //coordonees curseur editeur calculees depuis le curseur souris via la fonction edComputeCursorFromMouse
  int rows; //nb de lignes TTY
  int cols; //nb de colonnes TTY
  int nbrows; //nb de lignes dans l'editeur
  int roffset; //Decalage vertical de la fenetre d'affichage des lignes d'editeur
  int disprows;//Nb de lignes buffer affichees dans l'offset courant
  struct edrow *trow; //tableau de lignes constituant l'editeur
  enum editorMode mode; //mode courant
  int modified; //Booleen. Vrai si texte modifie dans l'editeur non sauvegarde.
  char fname[FNAME_LGMAX+1]; //Nom du fichier lie au contenu de l'editeur.
  char *cmd; //Buffer de commande a traiter.
  char *msg; //Message de service de l'editeur.
  int readonly; //Booleen de protection en ecriture de l'editeur
};

struct editorState tEd[ED_NBMAX]; //Tableau d'editeurs
int iEd=0;                        //Index de l'editeur courant

// Structure emulant un " append buffer " comme vu en page 18 du cours
struct appbuf{
  char *buf;
  int length;
};
#define APPBUF_BLANK {NULL , 0}

//Structure gardant les parametres de la souris
struct mouse {
  long x;             //Position souris courante
  long y;
  long ox;            //Ancienne position souris
  long oy;
  int left, middle, right; //Etat des boutons
  int pleft, clic;         //Gestion clic gauche
  signed char dx, dy;     //Deplacement souris
  struct pollfd fds[1];   //Descripteur fichier souris pour fonction poll
  int timeout_msecs;      //Temporisation pour fonction poll
};
struct mouse M;

//Structure stockant les parametres du terminal
struct Tty {
  int lines;
  int cols;
  struct termios cooked_termios; //termios d'origine
};
struct Tty Term = {DEF_TERMLINE,DEF_TERMCOLUMN,{0}};

//Prototypes des fonctions utilisees avant d'etre definies
void refresh(struct editorState *S);
void _beep(struct appbuf *a);
void ClrScr(struct appbuf *a);
void Cursor_Off();
void Cursor_On();
void PutCursor (int x, int y, struct appbuf *a);
void PutMouseCursor (int x, int y);
int  getWinSize(int *r, int *c);
void editorState_Blank(struct editorState *S);
void ServiceLine(struct editorState *S, struct appbuf *a);
void edAppendRow(struct editorState *S, char *s, size_t l);
void edRowInsertChar(struct edrow *row, int i, int c);
void edInsertChar(struct editorState *S, int c);
void edDeleteChar(struct editorState *S);
void edInsertRow(struct editorState *S, int i, char *s, size_t lg);
void edDeleteRow(struct editorState *S, int i);
void edInsertNewline(struct editorState *S);
int  Open_file(const char *file, struct editorState *S);
int  Save_file(struct editorState *S);
void edComputeCursor(struct editorState *S);
void SetComputedCursor(struct editorState *S);
void edCmdAppendChar(struct editorState *S, char c);
int  edCmdDeleteLastChar(struct editorState *S);
void edCmdWipe(struct editorState *S);
int  edDisplayMessage(struct editorState *S, char *msg, char *prompt, enum msgVideoMode vm, char *accept, int t);
void edDisplayHelpEditor(int t);
int  edExecuteCommand(struct editorState *S);
int  mouseUpdate(struct mouse *M);
void edComputeCursorFromMouse(struct editorState *S, struct mouse *M);
void SetComputedCursorFromMouse(struct editorState *S);
void edDisplayEditorsInfos();
int  edChkSafeExit(struct editorState *S);
int  edSaveAllFiles();
void edSearchAndReplace(struct editorState *S);


// Traitement d'erreur
void exception(const char *error){
  ClrScr(NULL);
  perror(error);
  exit(1);
}

//Initialisation de l'editeur
void editorState_Blank(struct editorState *S){
  S->x = 1;
  S->y = 1;
  S->bx = 1;
  S->by = 1;
  S->rows = DEF_TERMLINE;
  S->cols = DEF_TERMCOLUMN;
  S->nbrows = 0;
  S->roffset = 0;
  S->disprows = DEF_TERMLINE;
  S->trow = NULL;
  S->mode = NORMAL;
  S->modified = false;
  S->fname[0] = '\0';
  S->cmd = NULL;
  S->msg = NULL;
  S->readonly = false;
  if (getWinSize(&S->rows, &S->cols) == -1) exception("Error get window size. ");
}

//Deplacement du curseur avec les touches gamer et fleches
void editorMoveCursor(struct editorState *S, int key) {
  int o=S->roffset, bx=S->bx, by=S->by;

  switch (key) {
    case 'q':
    case ARROW_KEY('q'): //Gauche
       bx--; if (bx<1) {if (by>1) {by--; bx=S->trow[o+by-1].size; bx=bx==0?1:bx;} else {by=1; if (o==0) {bx=1; _beep(NULL); break;} else {S->roffset--;bx=S->trow[o+by-2].size; bx=bx==0?1:bx;}}}
       S->bx = bx; S->by = by;
       break;
    case 'd':
    case ARROW_KEY('d'): //Droite
      if (S->nbrows == 0) {_beep(NULL); break;}               //Editeur vierge
      int m = (S->mode == INSERT && S->trow[o+by-1].size > 0); //en mode Insertion, +1 au decalage max du curseur pour une ligne d'editeur non vide
      if (o+by == S->nbrows && bx >= S->trow[o+by-1].size+m) {_beep(NULL); break;} //Dernier caractere de l'editeur
      bx++; if (bx > S->trow[o+by-1].size + m) {if (by<S->disprows) {by++; bx=1;} else {by=1; bx=1; S->roffset+=S->disprows;}}
      S->bx = bx; S->by = by;
      break;
    case 'z':
    case ARROW_KEY('z'): //Haut
      if (S->nbrows == 0) {_beep(NULL); break;}               //Editeur vierge
      by--; if (by < 1) {by=1; if (o==0) {_beep(NULL); break;} else {S->roffset--;}}
      bx=1;
      S->bx = bx; S->by = by;
      break;
    case 's':
    case ARROW_KEY('s'): //Bas
      if (o+by+1 > S->nbrows) {_beep(NULL); break;}
      if (by>=S->disprows) {by=1; S->roffset+=S->disprows;} else by++;
      bx=1;
      S->bx = bx; S->by = by;
      break;
    case '0' :
    case ARROW_KEY('0'): //Home
      if (S->nbrows == 0) {_beep(NULL); break;}               //Editeur vierge
      S->bx=1;
      break;
    case '$' :
    case ARROW_KEY('$'): //Fin
      if (S->nbrows == 0) {_beep(NULL); break;}               //Editeur vierge
      if (S->trow[o+by-1].size) {
        bx = S->trow[o+by-1].size;
      }
      S->bx=bx;
      break;
    case CTRL_KEY('F'): //PgDown
    case CTRL_KEY('B'): //PgUp
      S->by=1; S->bx=1;
      break;
  }
  SetComputedCursor(S);
  PutCursor(S->x, S->y, NULL);
}

//Recuperation de la touche tapee
int editorKeypress(struct editorState *S){
  char c, esc[3];

  while((ret = read(STDIN_FILENO,&c,1)) != 1){
    if (ret == -1 ) exception ("error in read");
    if (!S) continue;               //Appel avec parametre NULL, pas de traitement souris
    if (!mouseUpdate(&M)) continue; //Pas d'evenement souris
    if (M.clic) {                   //clic bouton gauche
      SetComputedCursorFromMouse(S);
      SetComputedCursor(S);
      refresh(S);
    } else {                        //deplacement souris
      ServiceLine(S, NULL);
      PutMouseCursor(M.x/MOUSE_XRATIO+1, M.y/MOUSE_YRATIO+1);
    }
  }

  if (c=='\x1b') { //Gestion des fleches curseur

    if (read(STDIN_FILENO, &esc[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &esc[1], 1) != 1) return '\x1b';
    if (esc[0] == '[') {
      if (isdigit(esc[1])){
        if (read(STDIN_FILENO, &esc[2], 1) != 1) return '\x1b'; //Captation du caractere '~'
      }
      switch (esc[1]) {
        case 'A': return ARROW_KEY('z'); // Haut
        case 'B': return ARROW_KEY('s'); // Bas
        case 'C': return ARROW_KEY('d'); // Droite
        case 'D': return ARROW_KEY('q'); // Gauche
        case 'H': return ARROW_KEY('0'); // Home
        case 'F': return ARROW_KEY('$'); // Fin
        case '5': return CTRL_KEY('B');  // PgUp
        case '6': return CTRL_KEY('F');  // PgDown
        case '3': return ARROW_KEY('x'); // Del
        case 'Z': return ARROW_KEY('\t');// Shift-Tab
      }
    }
  }
  return c;
}

//Action selon la touche tapee
int editorKeyProcessor(struct editorState *S) {
  int c = editorKeypress(S);
  switch (S->mode) {
    case NORMAL :
      switch (c) {
        case CTRL_KEY('q'): { //Raccourci pour :q
          int b=true;
          S->cmd = strdup("q");
          Cursor_Off();
          b = edExecuteCommand(S);
          edCmdWipe(S);
          Cursor_On();
          refresh(S);
          return b;
          break; }
        case CTRL_KEY('s'): //Raccourci pour :w
          S->cmd = strdup("w");
          Cursor_Off();
          edExecuteCommand(S);
          edCmdWipe(S);
          Cursor_On();
          refresh(S);
          return true;
          break;
        case CTRL_KEY('h'): //Raccourci pour :help
          edDisplayHelpEditor(3);
          break;
        case ARROW_KEY('\t') : //Shift-Tab
        case CTRL_KEY('\t'):   //Tab ou Ctrl-Tab  pour switcher d'editeur
          iEd++; iEd %= ED_NBMAX;
          refresh(tEd+iEd);
          return true;
          break;
        case 'i':
          if (!S->readonly) S->mode = INSERT; else _beep(NULL);
          break;
        case ':':
          S->mode = COMMAND;
          Cursor_Off();
          break;
        case '/':
          if (S->readonly) {_beep(NULL); break;}
          S->mode = REPLACE;
          Cursor_Off();
          break;
        case ARROW_KEY('x'): //Del
          _beep(NULL);
          break;
        case CTRL_KEY('F'): //PgDown
          if (S->roffset + S->disprows < S->nbrows) S->roffset += S->disprows;
          editorMoveCursor(S,c);
          break;
        case CTRL_KEY('B'): //PgUp
          if (S->roffset - S->disprows > 0) S->roffset -= S->disprows; else S->roffset = 0;
          editorMoveCursor(S,c);
          break;
        case '\x7F': //BackSpace (Ascii 127)
          editorMoveCursor(S,ARROW_KEY('q'));
          break;
        case '\r' : //Enter
          editorMoveCursor(S,'s');
          break;
        default :
          editorMoveCursor(S,c);
      }
      break;
    case INSERT :
      switch (c) {
        case '\x1b': //ESC
          S->mode = NORMAL;
          editorMoveCursor(S,ARROW_KEY('q')); //Gauche
          break;
        case '\r': //Enter
          edInsertNewline(S);
          if (S->y == S->rows-1) {
            S->roffset += S->disprows;
            editorMoveCursor(S,CTRL_KEY('F')); //PgDown en fin de page
          } else {
            editorMoveCursor(S,ARROW_KEY('s')); //Bas
          }
          break;
        case ARROW_KEY('x'): //Del
          if (S->nbrows==1 && S->trow[0].size == 0) { //Editeur reduit a une ligne vide
            edDeleteRow(S,0);
            break;
          }
          if(S->by == S->disprows) {S->roffset += S->disprows-1; S->by=1;}
          editorMoveCursor(S,ARROW_KEY('d')); //Droite
          edDeleteChar(S);
          editorMoveCursor(S,ARROW_KEY('q')); //Gauche
          break;
        case '\x7F': //BackSpace (Ascii 127)
          edDeleteChar(S);
          if (S->roffset != 0 && S->by < 1) {S->roffset--; S->by=1;}
          editorMoveCursor(S,ARROW_KEY('q')); //Gauche
          break;
        case CTRL_KEY('F'): //PgDown
          if (S->roffset + S->disprows < S->nbrows) S->roffset += S->disprows;
          editorMoveCursor(S,c);
          break;
        case CTRL_KEY('B'): //PgUp
          if (S->roffset - S->disprows > 0) S->roffset -= S->disprows; else S->roffset = 0;
          editorMoveCursor(S,c);
          break;
        case ARROW_KEY('\t'): //Shift-Tab pour switcher d'editeur
          iEd++; iEd %= ED_NBMAX;
          refresh(tEd+iEd);
          return true;
          break;
        default :
          if (isprint(c) || c=='\t') {
            if(S->by == S->disprows) {S->roffset += S->disprows-1; S->by=1;}
            int nc = 1;
            if (c=='\t') {nc=TAB_SIM; c=' ';}   //Traitement touche Tab
            for (int i=0;i<nc;i++) {
              edInsertChar(S,c);
              editorMoveCursor(S,ARROW_KEY('d')); //Droite
            }
          } else {
            editorMoveCursor(S,c);
          }
      }
      break;
    case COMMAND :
      switch (c) {
        case '\x1b': //ESC
          S->mode = NORMAL;
          edCmdWipe(S);
          Cursor_On();
          break;
        case '\x7F': //BackSpace (Ascii 127)
          if (edCmdDeleteLastChar(S)) {
            S->mode = NORMAL;
            Cursor_On();
          }
          break;
        case '\r': { //Enter
          int b=true;
          if(S->cmd) b = edExecuteCommand(S);
          S->mode = NORMAL;
          edCmdWipe(S);
          Cursor_On();
          if (!b) return false;
          if (S != tEd+iEd) { //Changement d'editeur
            refresh(tEd+iEd);
            return true;
          }
          break; }
        default :
          if (isprint(c)) edCmdAppendChar(S,c);
          break;
      }
    break;
    case REPLACE :
      switch (c) {
        case '\x1b': //ESC
          S->mode = NORMAL;
          edCmdWipe(S);
          Cursor_On();
          break;
        case '\x7F': //BackSpace (Ascii 127)
          if (edCmdDeleteLastChar(S)) {
            S->mode = NORMAL;
            Cursor_On();
          }
          break;
        case '\r': //Enter
          if(S->cmd) edSearchAndReplace(S);
          S->mode = NORMAL;
          edCmdWipe(S);
          Cursor_On();
          break;
        default :
          if (isprint(c)) edCmdAppendChar(S,c);
          break;
      }
    break;
  }
  refresh(S);
  return true;
}

//RaZ d'un Editeur
void edRaZ(struct editorState *S) {
  for (int i=0; i<S->nbrows; i++) {
    if (S->trow[i].str) free(S->trow[i].str);
    S->trow[i].size=0;
  }
  if (S->trow) free (S->trow);
  if (S->cmd) free(S->cmd);
  if (S->msg) free(S->msg);
  editorState_Blank(S);
}

//Insertion d'un caractere dans une ligne d'editeur
void edRowInsertChar(struct edrow *row, int i, int c) {
  if (i < 0 || i > row->size) i = row->size;
  row->str = realloc(row->str, row->size + 2);
  memmove(&row->str[i+1], &row->str[i], row->size - i + 1);
  row->size++;
  row->str[i] = c;
}

//Insertion d'un caractere dans l'editeur.
void edInsertChar(struct editorState *S, int c){
  int o=S->roffset, by=S->by, nb=S->nbrows;
  if (o+by > nb) edInsertRow(S, nb, "", 0);
  edRowInsertChar(&S->trow[o+by-1], S->bx-1, c);
  S->modified = true;
}

//Insertion d'une ligne d'editeur
void edInsertRow(struct editorState *S, int i, char *s, size_t lg) {
  if (i < 0 || i > S->nbrows) return;

  S->trow = realloc(S->trow, sizeof(struct edrow) * (S->nbrows + 1));
  if (i<S->nbrows) memmove(&S->trow[i+1], &S->trow[i], sizeof(struct edrow) * (S->nbrows - i));
  S->trow[i].size = lg;
  S->trow[i].str = malloc(lg + 1);
  memcpy(S->trow[i].str, s, lg);
  S->trow[i].str[lg] = '\0';
  S->nbrows++;
  S->modified = true;
}

//Insertion d'une nouvelle ligne dans l'editeur
void edInsertNewline(struct editorState *S) {
  int o=S->roffset, by=S->by;
  if (S->bx == 1) {
    edInsertRow(S, o+by-1, "", 0);
  } else {
    struct edrow *row = &S->trow[o+by-1];
    edInsertRow(S, o+by, &row->str[S->bx-1], row->size - S->bx + 1);
    row = &S->trow[o+by-1];
    row->size = S->bx -1;
    row->str[row->size] = '\0';
  }
  S->bx = 1;
}

//Ajout d'une ligne d'editeur
void edAppendRow(struct editorState *S, char *s, size_t l) {
  int i = S->nbrows;
  S->trow = realloc(S->trow, sizeof(struct edrow)*(i+1));
  S->trow[i].size = l;
  S->trow[i].str = malloc(sizeof(char)*(l+1));
  memcpy(S->trow[i].str, s, l);
  S->trow[i].str[l] = '\0';
  S->nbrows++;
}

//Effacement d'un caractere dans une ligne d'editeur
void edRowDeleteChar(struct edrow *row, int i) {
  if (i < 0 || i >= row->size) return;
  memmove(&row->str[i], &row->str[i+1], row->size - i);
  row->size--;
//  S.modified = true;
}

//Effacement d'une ligne d'editeur
void edDeleteRow(struct editorState *S, int i) {
  if (i < 0 || i >= S->nbrows) return;
  free(S->trow[i].str);
  if (S->nbrows > 1) memmove(&S->trow[i], &S->trow[i+1], sizeof(struct edrow) * (S->nbrows - i - 1));
  S->nbrows--;
  S->modified = true;
}

//Concatenation d'une ligne d'editeur et d'une chaine de caracteres
void edRowAppendString(struct edrow *row, char *s, size_t lg) {
  row->str = realloc(row->str, row->size + lg + 1);
  memcpy(&row->str[row->size], s, lg);
  row->size += lg;
  row->str[row->size] = '\0';
  //S.modified = true;
}

//Effacement d'un caractere dans l'editeur.
void edDeleteChar(struct editorState *S){
  int o=S->roffset, bx=S->bx, by=S->by, nb=S->nbrows;
  if (o+by > nb) return;
  if (bx <= 1 && o+by <= 1) return;

  struct edrow *row = &S->trow[o+by-1];
  if (bx > 1) {
    edRowDeleteChar(row, bx-2);
    S->modified = true;
    //bx--;
  } else {
    bx = S->trow[o+by-2].size + 1;
    edRowAppendString(&S->trow[o+by-2], row->str, row->size);
    edDeleteRow(S,o+by-1);
    S->modified = true;
    S->by--; S->bx=bx+1;
  }
}

//Construction de la commande editeur
void edCmdAppendChar(struct editorState *S, char c) {
  if (S->cmd == NULL) {
    S->cmd = malloc(sizeof (char) * 2);
    S->cmd[0]=c;
    S->cmd[1]='\0';
  } else {
    int l=strlen(S->cmd);
    if (l<S->cols-1) {
      S->cmd = realloc(S->cmd,sizeof(char)*(l+2));
      S->cmd[l]=c;
      S->cmd[l+1]='\0';
    } else {
      _beep(NULL);
    }
  }
}

//Commande editeur : retrait du dernier caractere tape
int edCmdDeleteLastChar(struct editorState *S) {
  if (S->cmd == NULL) return true;
  int l=strlen(S->cmd);
  if(l>0) {
    S->cmd[l-1]='\0';
  } else {
    free( S->cmd);
    S->cmd = NULL;
    return true;
  }
  return false;
}

//Effacement complet de la commande editeur
void edCmdWipe(struct editorState *S) {
  if (S->cmd != NULL) {
    free(S->cmd);
    S->cmd = NULL;
  }
}

//Execution d'une commande editeur
int edExecuteCommand(struct editorState *S) {

  //Commandes pour quitter
  if (strcmp(S->cmd, "qa!") == 0) return false;
  if (strcmp(S->cmd, "q!") == 0) return edChkSafeExit(S);
  if (strcmp(S->cmd, "q") == 0) {
    if (S->modified) {
      edDisplayMessage(S, " Modifications non enregistrées. ", NULL, VM_ERREUR, "\x1b\r", 0);
      return true;
    }
    return edChkSafeExit(S);
  }

  //Commandes pour sauver
  if(strcmp(S->cmd, "w") == 0) { // w
    if (S->readonly) {edDisplayMessage(S, " Fichier en lecture seule. ", NULL, VM_ERREUR, "\x1b\r", 0); return true;}
    if (S->fname[0]) {
      if (!Save_file(S)) edDisplayMessage(S, " Erreur de sauvegarde fichier. ", NULL, VM_ERREUR, "\x1b\r", 0);
      else edDisplayMessage(S, " OK, fichier sauvé.", NULL, VM_INVERSE, NULL, 2);
    } else {
      edDisplayMessage(S, " Pas de nom spécifié. (utilisez w <fic>) ", NULL, VM_ERREUR, "\x1b\r", 0);
    }
    return true;
  }
  if(strcmp(S->cmd, "wq") == 0) { // wq
    if (S->readonly) {edDisplayMessage(S, " Fichier en lecture seule. ", NULL, VM_ERREUR, "\x1b\r", 0); return false;}
    int b = true;
    if (S->fname[0]) {
      if (!Save_file(S)) {
        edDisplayMessage(S, " Erreur de sauvegarde fichier... ", NULL, VM_ERREUR, "\x1b\r", 3);
        b = edDisplayMessage(S, " Quitter quand même ? ", "(o/n)", VM_ERREUR, "\x1boOnN", 0);
        b = ((b=='o'||b=='O')?false:true);
      }
      else {
        edDisplayMessage(S, " OK, fichier sauvé.", NULL, VM_INVERSE, NULL, 2);
        b = false;
      }
    } else {
      edDisplayMessage(S, " Pas de nom spécifié. (utilisez w <fic>) ", NULL, VM_ERREUR, "\x1b\r", 0);
    }
    return b || edChkSafeExit(S);
  }
  if(strncmp(S->cmd, "w ",2) == 0 && strlen(S->cmd) > 2) { // w <fic>
    if (S->readonly) {edDisplayMessage(S, " Fichier en lecture seule. ", NULL, VM_ERREUR, "\x1b\r", 0); return true;}
    char *fn = NULL;
    if (S->fname[0]) fn = strdup(S->fname);
    strcpy(S->fname,&S->cmd[2]);
    if (!Save_file(S)) {
      edDisplayMessage(S, " Erreur d'écriture du fichier. ", NULL, VM_ERREUR, "\x1b\r", 0);
      if(fn) strcpy(S->fname,fn); else S->fname[0]='\0';
    } else {
      edDisplayMessage(S, " OK, fichier sauvé.", NULL, VM_INVERSE, NULL, 2);
    }
    if (fn) free(fn);
    return true;
  }
  if(strcmp(S->cmd, "waq") == 0) { // waq
    int b = true;
    if (!edSaveAllFiles()) {
      edDisplayMessage(S, " Erreur de sauvegarde fichier(s)... ", NULL, VM_ERREUR, "\x1b\r", 3);
      b = edDisplayMessage(S, " Quitter quand même ? ", "(o/n)", VM_ERREUR, "\x1boOnN", 0);
      b = ((b=='o'||b=='O')?false:true);
    } else {
      edDisplayMessage(S, " OK, fichier(s) sauvé(s).", NULL, VM_INVERSE, NULL, 2);
      b = false;
    }
    return b;
  }
  if(strcmp(S->cmd, "wa") == 0) { // wa
    if (!edSaveAllFiles()) {
      edDisplayMessage(S, " Erreur de sauvegarde fichier(s)... ", NULL, VM_ERREUR, "\x1b\r", 0);
    } else {
      edDisplayMessage(S, " OK, fichier(s) sauvé(s).", NULL, VM_INVERSE, NULL, 2);
    }
    return true;
  }

  //Commandes pour charger l'editeur
  if(strcmp(S->cmd, "r") == 0) { // r
    if (S->fname[0] && S->modified) {
      edDisplayMessage(S, " Modifications non enregistrées. ", NULL, VM_ERREUR, "\x1b\r", 0);
    } else {
      edDisplayMessage(S, " Pas de nom spécifié (utilisez r <fic>). ", NULL, VM_ERREUR, "\x1b\r", 0);
    }
    return true;
  }
  if(strcmp(S->cmd, "r!") == 0) { // r!
    edDisplayMessage(S, " Pas de nom spécifié. ", NULL, VM_ERREUR, "\x1b\r", 0);
    return true;
  }
  if(strncmp(S->cmd, "r ",2) == 0 && strlen(S->cmd) > 2) { // r <fic>
    char *fn = NULL, *cd = NULL;
    if (S->modified) {
      edDisplayMessage(S, " Modifications non enregistrées (utilisez r! <fic>). ", NULL, VM_ERREUR, "\x1b\r", 0);
    } else {
      if (S->fname[0]) fn = strdup(S->fname);
      cd = strdup(S->cmd);
      if (S->nbrows > 0)  {
        edRaZ(S);
      }
      if (!Open_file(&cd[2], S)) {
        edDisplayMessage(S, " Erreur de lecture du fichier. ", NULL, VM_ERREUR, "\x1b\r", 0);
        if (fn) Open_file(fn,S);
      } else {
        edDisplayMessage(S, " Chargement du fichier...", NULL, VM_INVERSE, NULL, 1);
      }
      if (fn) free(fn);
      if (cd) free(cd);
    }
    return true;
  }
  if(strncmp(S->cmd, "r! ",3) == 0 && strlen(S->cmd) > 3) { // r! <fic>
    char *fn = NULL, *cd = NULL;
    if (S->fname[0]) fn = strdup(S->fname);
    cd = strdup(S->cmd);
    if (S->nbrows > 0)  {
      edRaZ(S);
    }
    if (!Open_file(&cd[3], S)) {
      edDisplayMessage(S, " Erreur de lecture du fichier. ", NULL, VM_ERREUR, "\x1b\r", 0);
      if (fn) Open_file(fn,S);
    } else {
      edDisplayMessage(S, " Chargement du fichier...", NULL, VM_INVERSE, NULL, 1);
    }
    if (fn) free(fn);
    if (cd) free(cd);
    return true;
  }

  //Commandes pour RaZ l'editeur courant
  if(strcmp(S->cmd, "n") == 0) { // n
    if (S->readonly) {edDisplayMessage(S, " Fichier en lecture seule. ", NULL, VM_ERREUR, "\x1b\r", 0); return true;}
    if (S->modified) {
      edDisplayMessage(S, " Modifications non enregistrées (utilisez n!).", NULL, VM_ERREUR, "\x1b\r", 0);
    } else {    if (S->readonly) {edDisplayMessage(S, " Fichier en lecture seule. ", NULL, VM_ERREUR, "\x1b\r", 0); return true;}

      edRaZ(S);
      edDisplayMessage(S, " Nouveau fichier...", NULL, VM_INVERSE, NULL, 1);
    }
    return true;
  }
  if(strcmp(S->cmd, "n!") == 0) { // n!
    if (S->readonly) {edDisplayMessage(S, " Fichier en lecture seule. ", NULL, VM_ERREUR, "\x1b\r", 0); return true;}
    edRaZ(S);
    edDisplayMessage(S, " Nouveau fichier...", NULL, VM_INVERSE, NULL, 1);
    return true;
  }

  //Commandes pour le multi-editeurs
  if(strcmp(S->cmd, "bi") == 0) { // infos
    edDisplayEditorsInfos();
    return true;
  }
  if (strncmp(S->cmd,"b",1)==0 && strlen(S->cmd)>1) {
    char *c;
    int i = (int) strtol(&S->cmd[1],&c,10);
    if (*c != '\0' || i<0 || i>=ED_NBMAX) {
      edDisplayMessage(S, " N° d'editeur erroné. ", NULL, VM_ERREUR, "\x1b\r", 0);
      return true;
    }
    iEd = i; refresh(tEd+iEd);
    return true;
  }

  //Commandes pour obtenir de l'aide
  if(strcmp(S->cmd, "help") == 0 || strcmp(S->cmd, "h") == 0) {
    edDisplayHelpEditor(3);
    return true;
  }

  edDisplayMessage(S, " Commande inconnue ou erreur de syntaxe. ",NULL, VM_ERREUR, "\x1b\r", 0);
  return true;
}

//Affichage d'un message de service
int edDisplayMessage(struct editorState *S, char *msg, char *prompt, enum msgVideoMode vm, char *accept, int t) {
  int c=0;
  char *m = malloc(sizeof(char)*(S->cols+1)); m[0]='\0';
  char *p = strdup(" [Press Enter]"); //prompt par defaut

  if (t>=1) p[0]='\0'; else if (prompt) {free(p); p=strdup(prompt);}
  int l = strlen(msg)+strlen(p);
  if (l <= S->cols) {
    strcat(m, msg);
  } else {
    l = S->cols - strlen(p);
    strncat(m,msg,l);
    m[l]='\0';
  }
  strcat(m,p);
  l = strlen(m);

  PutCursor(1,S->rows,NULL);
  switch (vm) {
    case VM_NORMAL :
      write(STDOUT_FILENO, "\x1b[K\x1b[0m", 7); //RaZ ligne + Passe en video normale
      break;
    case VM_INVERSE :
      write(STDOUT_FILENO, "\x1b[K\x1b[7m", 7); //RaZ ligne + Passe en video inverse
      break;
    case VM_ERREUR :
      write(STDOUT_FILENO, "\x1b[K\x1b[0;1;37;41m", 15); //RaZ ligne + Passe en blanc/rouge lumineux
      break;
  }
  write(STDOUT_FILENO, m, l);
  if (t>=1) sleep(t); else while (!strchr((accept?accept:"\r"),c=editorKeypress(NULL)));
  PutCursor(1,S->rows,NULL);
  write(STDOUT_FILENO, "\x1b[0m\x1b[K", 7); //RaZ video inverse + RaZ ligne

  return c;
}

//Ajout de caracteres dans le append buffer
void appbuf_add(struct appbuf *a, const char *s, int length){
  char *toadd = realloc(a->buf,a->length + length);
  if (toadd == NULL) return; //echec du realloc
  memcpy(&toadd[a->length],s,length);
  a->buf = toadd;
  a->length = a->length + length;
}

//Destruction du append buffer
void appbuf_Free(struct appbuf *a) {
  free(a->buf);
}

//Emet un avertissement sonore
void _beep(struct appbuf *a) {
  if(a) {
    appbuf_add(a,"\x07", 1); //Ascii Beep
  } else {
    write(STDOUT_FILENO, "\x07", 1);
  }
}

//Chargement d'un fichier dans l'editeur
int Open_file(const char *file, struct editorState *S) {
  int fp = open(file, O_RDONLY);
  //if (fp==-1) exception("file open failed");
  if (fp==-1) return false;

  char buf[BUFSIZE];
  char str[BUFSIZE]="";
  ssize_t byte_read, i, j, c=0;

  while ((byte_read=read(fp, buf, sizeof(buf)))) {
    for (i=0, j=c; i<byte_read; i++,j++) {
      if (buf[i]=='\n') {
        edAppendRow(S,str,j);
        str[0]='\0';
        j = -1;
      } else {
        if (buf[i]=='\t') {  //Simulation du Tab par des espaces
          for (int k=0; k<TAB_SIM; k++) str[j++]=' ';
           j--;
        } else {
          str[j] = buf[i];
        }
      }
    }
    c=j;
  }
  strncpy(S->fname, file ,FNAME_LGMAX); S->fname[FNAME_LGMAX]='\0';
  close(fp);
  return true;
}

//Sauvegarde de l'editeur dans un fichier
int Save_file(struct editorState *S) {
  int len=0,i,b=true;

  if (S->readonly) return !S->modified;

  //creation d'un buffer recap du contenu de l'editeur
  for (i=0; i<S->nbrows; i++) len += S->trow[i].size + 1; //taille du fichier
  char *buf = malloc(sizeof(char)*len);
  char *p = buf;
  for ( i=0; i<S->nbrows; i++) {
    memcpy(p, S->trow[i].str, S->trow[i].size);
    p += S->trow[i].size;
    *p = '\n'; p++;
  }

  //ecriture dans le fichier
  int fp = open(S->fname, O_WRONLY|O_TRUNC|O_CREAT, 0644);
  if (fp==-1) b=false;

  if (write(fp, buf, len) == -1) b=false;
  close(fp);
  free(buf);

  if (b) S->modified = false;
  return b;
}


//Retour du TTY au mode canonique
void RawMode_off() {
  ret = tcsetattr(STDIN_FILENO, TCSAFLUSH, &Term.cooked_termios);
  if(ret == -1) exception("error in RawMode_off");
  printf("\r");
}

//Activation du TTY en mode non-canonique
void RawMode_on() {
    ret=tcgetattr(STDIN_FILENO,&Term.cooked_termios);
    if(ret == -1) exception("error in RawMode_on get");

    struct termios raw_termios = Term.cooked_termios;

    raw_termios.c_iflag &= ~(IXON | ICRNL);
    raw_termios.c_lflag &= ~(ICANON | ECHO | IEXTEN | ISIG);
    raw_termios.c_oflag &= ~(OPOST);
    raw_termios.c_cc[VMIN]=0;
    raw_termios.c_cc[VTIME]=0;
    ret = tcsetattr(STDIN_FILENO,TCSAFLUSH,&raw_termios);
    if(ret == -1) exception("error in RawMode_on get");

    if (getWinSize(&Term.lines, &Term.cols) == -1) exception("Error get window size. ");
    if (Term.lines < TTY_MINLINES || Term.cols < TTY_MINCOLS) {
      char msg[100];
      sprintf(msg, "Error : min size of Tty requested is %dLx%dC. ", TTY_MINLINES, TTY_MINCOLS);
      exception(msg);
    }
}

//Affichage des lignes d'editeur avec "~" en debut de ligne vide, avec gestion du scrolling vertical
void Display_RowsTildes(struct editorState *S, struct appbuf *a) {
  int c, i, j, lg;
  S->disprows = S->rows - 1;
  for (i = 0, c=S->roffset; i < S->rows-1; i++, c++) {
    if (c<S->nbrows) {
      lg = S->trow[c].size;
      if (lg<=S->cols) {
        appbuf_add(a,S->trow[c].str,lg);
        appbuf_add(a,"\r\n", 2);
      } else {
        j=0;
        while (lg > S->cols && i < S->rows-1){
          appbuf_add(a,&S->trow[c].str[j*S->cols],S->cols);
          appbuf_add(a,"\r\n", 2);
          lg -= S->cols;
          i++; j++;
          S->disprows--;
        }
        if (i < S->rows -1) {
          appbuf_add(a,&S->trow[c].str[j*S->cols],lg);
          appbuf_add(a,"\r\n", 2);
        }
      }
    } else {
      appbuf_add(a,"\x1b[1;32;40m~\x1b[0m\r\n", 17);
    }
  }
}

//Effacement de l'ecran
void ClrScr(struct appbuf *a) {
  if (a) {
    appbuf_add(a,"\x1b[2J", 4);
    appbuf_add(a,"\x1b[H", 3);
  } else {
    write(STDOUT_FILENO, "\x1b[2J", 4); //efface l'ecran
    write(STDOUT_FILENO, "\x1b[H", 3);  //reset le curseur
  }
}

//Traitement Ligne de service
void ServiceLine(struct editorState *S, struct appbuf *a) {
  int l;
  char *msg = malloc(sizeof(char)*(S->cols+1));
  msg[0]='\0';

  switch (S->mode) {
    case NORMAL:
      PutCursor(1,S->rows,a);
      l=snprintf(msg,S->cols,"                <%ld,%ld> [%d,%d] Ed.%d \"%.20s\"%c(C%d,L%d)  tty%dx%d", M.x/MOUSE_XRATIO+1, M.y/MOUSE_YRATIO+1, S->x, S->y, iEd, S->fname[0]?S->fname:"[Sans nom]", S->modified?'*':' ',S->bx, S->by+S->roffset, S->rows, S->cols);
      break;
    case INSERT:
      PutCursor(1,S->rows,a);
      l=snprintf(msg,S->cols,"---INSERTION--- <%ld,%ld> [%d,%d] Ed.%d \"%.20s\"%c(C%d,L%d)  tty%dx%d", M.x/MOUSE_XRATIO+1, M.y/MOUSE_YRATIO+1, S->x, S->y, iEd,S->fname[0]?S->fname:"[Sans nom]", S->modified?'*':' ', S->bx, S->by+S->roffset, S->rows, S->cols);
      break;
    case COMMAND:
      l = ((S->cmd)?(int) strlen(S->cmd):0);
      PutCursor(1,S->rows,a);
      sprintf(msg,"%c%s%c", ':', (S->cmd)?S->cmd:"", (l<S->cols-1)?'_':'\0');
      break;
    case REPLACE:
      l = ((S->cmd)?(int) strlen(S->cmd):0);
      PutCursor(1,S->rows,a);
      sprintf(msg,"%c%s%c", '/', (S->cmd)?S->cmd:"", (l<S->cols-1)?'_':'\0');
      break;
  }
  if (l > S->cols) msg[S->cols]='\0';
  if(a) {
    appbuf_add(a,msg,strlen(msg));
  } else {
    write(STDOUT_FILENO, "\x1b[K", 3);
    write(STDOUT_FILENO, msg, strlen(msg));
  }
  if (msg) free(msg);
}

//Extinction du curseur
void Cursor_Off() {
  write(STDOUT_FILENO, "\x1b[?25l", 6);
}

//Reactivation du curseur
void Cursor_On() {
  write(STDOUT_FILENO, "\x1b[?25h", 6);
}

//Renvoi du curseur a la coordonnee (x,y)
void PutCursor (int x, int y, struct appbuf *a){
  char s[20];
  sprintf(s,"\x1b[%03d;%03dH",y,x);
  if (a) {
    appbuf_add(a,s,10);
  } else {
    write(STDOUT_FILENO, s, 10);
  }
}

//Reinitialisation de l'affichage
void refresh(struct editorState *S) {
  struct appbuf a = APPBUF_BLANK;
  ClrScr(&a);
  Display_RowsTildes(S,&a);
  ServiceLine(S,&a);
  PutCursor(S->x,S->y,&a);
  write(STDOUT_FILENO, a.buf, a.length);
  appbuf_Free(&a);
}

//Determination de la geometrie du TTY
int getWinSize(int *r, int *c) {
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
    return -1;
  } else {
    *c = w.ws_col;
    *r = w.ws_row;
    Term.cols = *c;
    Term.lines = *r;
    return 0;
  }
}

//Calcul de position du curseur sur le TTY
// en fonction du caractere courant dans l'editeur
void edComputeCursor(struct editorState *S) {
  int i, s, x, y, bx=S->bx, by=S->by, c=S->cols, o=S->roffset;

  if (S->nbrows <1) {S->cx=1; S->cy=1;return;}
  y = 1;
  for (i=1; i<by; i++) {
    s = S->trow[o+i-1].size;
    y += (s?((s/c)+((s%c)?1:0)):1);
  }
  y += (bx/c - ((bx%c)?0:1));
  s = S->trow[o+by-1].size;
  x = (s?bx%c:1); x = (x?x:c);

  S->cx = x; S->cy = y;
}

//Positionne le curseur texte TTY sur la position calculee
// a partir du caractere courant dans l'editeur
void SetComputedCursor(struct editorState *S) {
  edComputeCursor(S);
  S->x=S->cx; S->y=S->cy;
}


//Calcule la position dans l'editeur du caractere
//  pointe par le curseur souris
void edComputeCursorFromMouse(struct editorState *S, struct mouse *M) {
  int i, j, m, n, s, x, y, mx=M->x/MOUSE_XRATIO+1, my=M->y/MOUSE_YRATIO+1, c=S->cols, o=S->roffset;

  if (S->nbrows <1) {S->mx=1; S->my=1;return;}
  y = 1; j=1;
  for (i=1; j<=my && i<=S->disprows && o+i<=S->nbrows; i++) {
    s = S->trow[o+i-1].size;
    n = (s?((s/c)+((s%c)?1:0)):1); //nb de lignes TTY consommees par i-eme ligne editeur
    j+=n;                          //cumul des decalages entre les lignes TTY et editeur
    if (my >= j) {
       y++;
       continue;
    }
    if (j-my != 1) {m=c;} else {m=((s%c)?(s%c):c);} //Determine la colonne max en fonction de la ligne TTY cliquee
    x = (n-(j-my))*c + (s?(mx<m?mx:m):1);
    S->mx = x; S->my = y;
    break;
  }
}

//Positionne le curseur editeur sur la position calculee
// a partir des coordonnes du clic souris
void SetComputedCursorFromMouse(struct editorState *S) {
  edComputeCursorFromMouse(S,&M);
  S->bx=S->mx; S->by=S->my;
}


//Affiche l'ecran d'accueil au lancement de l'editeur
void edDisplaySplashScreen(int t) {
  struct editorState Sh;
  editorState_Blank(&Sh);
  Sh.readonly=true;

  edAppendRow(&Sh, "\x1b[1;32;40m +---------------------------------+",46);
  edAppendRow(&Sh, " |        \x1b[0mEditeur UPEC v0.1        \x1b[1;32;40m|",50);
  edAppendRow(&Sh, " | \x1b[0m(c) 2020 Valentin LAFITAU - L2  \x1b[1;32;40m|",50);
  edAppendRow(&Sh, " | \x1b[0mProjet L2 - Systeme - Mai 2020  \x1b[1;32;40m|",50);
  edAppendRow(&Sh, " +---------------------------------+\x1b[0m",44);
  edAppendRow(&Sh, "",0);
  edAppendRow(&Sh, "",0);
  edAppendRow(&Sh, "Tapez \x1b[1;32;40m:q\x1b[1;31;40m[Enter]\x1b[0m pour sortir de l'éditeur.",65);
  edAppendRow(&Sh, "",0);
  edAppendRow(&Sh, "Tapez \x1b[1;32;40m:help\x1b[1;31;40m[Enter]\x1b[0m pour obtenir de l'aide.",66);
  refresh(&Sh);

  Cursor_Off();
  edDisplayMessage(&Sh, "\x1b[1;31;40m--- Splash Screen - Editeur UPEC (c)2020 VL ---", NULL, VM_NORMAL, NULL, t);
  Cursor_On();
}

void edDisplayHelpEditor(int t) {
  struct editorState H;
  editorState_Blank(&H);
  H.readonly=true;

  if(!Open_file(ED_HELPNAME, &H)) {
    Cursor_Off();
    edDisplayMessage(&H, "Fichier d'aide introuvable.   ", "Press [Esc] ou [Enter]", VM_ERREUR,"\x1b\r", 0);
    Cursor_On();
    return;
  }
  refresh(&H);

  Cursor_Off();
  edDisplayMessage(&H, "\x1b[1;33;40mPage d'aide Editeur UPEC (c)2020 VL ", NULL, VM_INVERSE, NULL, t);
  PutCursor(1,1,NULL);
  Cursor_On();

  while(editorKeyProcessor(&H));
}

//Initialisation de la souris
int mouseInit(struct mouse *M){

  if((M->fds[0].fd = open(MOUSE_FDNAME, O_RDONLY |O_NONBLOCK)) == -1){
    exception("Erreur d'ouverture device souris.");
    return -1;
  }
  M->x = 0;             //Position souris courante
  M->y = Term.lines;
  M->ox = 0;            //Ancienne position souris
  M->oy = Term.lines;
  M->left=0;
  M->middle=0;
  M->right=0;
  M->pleft=0;
  M->clic=0;
  M->timeout_msecs = 50; //Temporisation pour poll
  M->fds[0].events = POLLIN;

  return 0;
}

//Acquisition du mouvement de la souris
int mouseGetMove(struct mouse *M){
  int ret;
  signed char mice_data[3];

  ret=read(M->fds[0].fd, mice_data, sizeof(mice_data));
  if (ret <= 0) for(int i=1; i<=2; i++) mice_data[i]=0;
  M->dx = (int) mice_data[1];
  M->dy = (int) mice_data[2];
  M->left = mice_data[0] & 0x1;
  M->right = mice_data[0] & 0x2;
  return 0;
}

//Fermeture du fichier device souris
int mouseClose(struct mouse *M) {
  close(M->fds[0].fd);
  return 0;
}

//Positionnement du curseur souris a la coordonnee (x,y)
void PutMouseCursor (int x, int y) {
  char s[50];
  sprintf(s,"\x1b[%03d;%03dH",y,x);
  write(STDOUT_FILENO, s, 10);
}

//Interception et traitement des evenements souris
int mouseUpdate(struct mouse *M){
  int ret = poll(M->fds,(nfds_t) 1,M->timeout_msecs);
  if (ret > 0)	{    // Evenement de souris
    if (M->fds[0].revents & POLLIN)	{
      mouseGetMove(M);
      M->ox = M->x;
      M->oy = M->y;
      M->x += M->dx; M->x = (M->x<0?0:M->x); M->x = (M->x>Term.cols*MOUSE_XRATIO?Term.cols*MOUSE_XRATIO:M->x);
      M->y -= M->dy; M->y = (M->y<0?0:M->y); M->y = (M->y>Term.lines*MOUSE_YRATIO?Term.lines*MOUSE_YRATIO:M->y);
      PutMouseCursor(M->x/MOUSE_XRATIO+1, M->y/MOUSE_YRATIO+1);
      if (M->clic) M->clic=false;
      if (M->pleft && !M->left) M->clic=true;
      M->pleft = M->left;
    }
  }
  return (ret>0);
}

//Init du tableau d'editeurs
void edInitEditors(struct editorState *S) {
  for (int i=0; i<ED_NBMAX; i++) editorState_Blank(&S[i]);
}

//Liberation du tableau d'editeurs
void edFreeEditors(struct editorState *S) {
  for (int i=0; i<ED_NBMAX; i++) edRaZ(&S[i]);
}

//Affichage des infos editeurs
void edDisplayEditorsInfos() {
  char msg[500], mode[10];
  struct editorState I;
  editorState_Blank(&I);
  I.readonly=true;

  edAppendRow(&I, "\x1b[1;32;40m ===================================",46);
  edAppendRow(&I, " |          \x1b[0mInfos editeurs         \x1b[1;32;40m|",50);
  edAppendRow(&I, "\x1b[1;32;40m ===================================",46);
  edAppendRow(&I, "",0);
  edAppendRow(&I, "",0);
  edAppendRow(&I, "\x1b[4mn° Nom                  Lignes  Mode      Statut\x1b[0m",57);
  for (int i=0; i<ED_NBMAX; i++) {
    switch (tEd[i].mode) {
      case REPLACE :
      case NORMAL  : strcpy(mode,"Normal"); break;
      case INSERT  : strcpy(mode,"Insertion"); break;
      case COMMAND : strcpy(mode,"Commande"); break;
    }

    sprintf(msg, "%s%2d %-20s %6d  %-9s %s%s",(i==iEd)?"\x1b[7;32;40m":"",i,tEd[i].fname[0]?tEd[i].fname:"[Sans nom]",tEd[i].nbrows,mode,tEd[i].modified?"Modifié.":"-",(i==iEd)?"\x1b[0m":"");
    edAppendRow(&I, msg, strlen(msg));
  }
  refresh(&I);

  Cursor_Off();
  edDisplayMessage(&I, "\x1b[1;32;40m--- Infos editeurs - Editeur UPEC (c)2020 VL ---", NULL, VM_INVERSE, "\x1b\r", 0);
  Cursor_On();
}

//Controle de conditions de sortie de l'editeur
// alerte si un editeur (sauf le courant) est a l'etat modifie
// demande confirmation de sortie sans sauver
int  edChkSafeExit(struct editorState *S) {
  int b=false;
  char msg[80];
  for (int i=0; i<ED_NBMAX; i++) b+=(i!=iEd && tEd[i].modified);
  if (b) {
    sprintf(msg," Au moins %d éditeur(s) non sauvé(s). Quitter quand même ?",b);
    b = edDisplayMessage(S, msg, "(o/n)", VM_ERREUR, "\x1boOnN", 0);
    b = (b=='o'|| b=='O')?false:true;
  }
  return b;
}

//Sauve tous les editeurs
int edSaveAllFiles() {
  int b=true;
  char *fn=NULL;
  char tbuf[256], buf[256];
  time_t ts = time(NULL);

  strftime(tbuf, sizeof(tbuf), "%Y%m%d%H%M%S", localtime(&ts));

  for (int i=0; i<ED_NBMAX; i++) {
    if (!tEd[i].modified || tEd[i].nbrows == 0) continue; //Fichier vierge ou deja sauvegarde
    if (tEd[i].fname[0])
      fn=strdup(tEd[i].fname);
    else {
      sprintf(buf,"%s%d.",ED_DEFFNAME,i);
      strcpy(tEd[i].fname,buf);
      strcat(tEd[i].fname,tbuf);
    }
    if (!Save_file(&tEd[i])) {
      b=false;
      if(!fn) tEd[i].fname[0]='\0';
    }
    if (fn) free(fn);
  }
  return b;
}

// Recherche et remplacement de caracteres
void edSearchAndReplace(struct editorState *S) {
  int i,j,k,c,n,l;
  char *buf=NULL, *sr[3]={NULL,NULL,NULL}, *a=NULL;
  char msg[100];

  //Extraction des motifs search / replace / options
  sr[0] = calloc (100, sizeof(char));
  sr[1] = calloc (100, sizeof(char));
  sr[2] = calloc (100, sizeof(char));
  buf = strdup(S->cmd);  l=strlen(buf);
  for (n=0, j=0, i=0; i<l && n<3; i++) {
     if (buf[i]=='/') {
       sr[n][j]='\0';
       n++; j=0;
     } else {
       sr[n][j]=buf[i];
       j++;
     }
  }

  //Verification stucture des motifs
  c=0; k=0;
  if (n != 2) c++;
  if (strlen(sr[0]) == 0) c++;
  switch (strlen(sr[2])) {
    case 0 : k=1; break;
    case 1 :
      if (strpbrk(sr[2],"gG*") == sr[2]) k=-1;
      else if (isdigit(sr[2][0])) k=atoi(sr[2]);
      else c++;
      break;
    default :
      k=atoi(sr[2]);
      if (k==0) c++;
      break;
  }
  if (c) {
    edDisplayMessage(S," Erreur de syntaxe ou paramètre. ", NULL, VM_ERREUR,"\x1b\r",0);
    return;
  }
  if (k==0 || strcmp(sr[0],sr[1]) == 0 || S->nbrows == 0) {
    edDisplayMessage(S," Rien à effectuer. ", NULL, VM_ERREUR,"\x1b\r",0);
    return;
  }

  //Execution recherche et remplacement
  int bx=S->bx, by=S->by, o=S->roffset, s, nr=S->nbrows;
  int l0 = strlen(sr[0]), l1 = strlen(sr[1]);
  if (k==-1) { o=0; by=1; bx=1; } // Etendue de recherche globale
  i = o+by;                       //Position de depart de la recherche
  c=0;                            //Compteur d'occurence remplacement
  while (k != 0 && i<=nr ) {      //pour chaque ligne depuis la position de depart
    s = S->trow[i-1].size;
    if (!s) {i++; bx=1; continue;} //Ligne vide
    while (k != 0 && bx<=s-l0) {  //pour chaque occurence du motif dans une ligne
      a = strstr(S->trow[i-1].str + bx - 1, sr[0]); //recherche chaine à la position bx
      if (!a) break;                                //Si rien, ligne suivante
      bx = a - S->trow[i-1].str + 1;
      for (j=0;j<l0;j++) edRowDeleteChar(&S->trow[i-1],bx-1); //suppression caracteres sr0
      s = S->trow[i-1].size;
      if (l1>0) {                                             //si sr1 non vide :
        for (j=l1;j>0;j--) edRowInsertChar(&S->trow[i-1],bx-1,sr[1][j-1]); //insertion caracteres sr1
        s = S->trow[i-1].size;
        bx += l1;
      }
      k--; c++;
    }
    i++; bx=1;
  }
  S->bx=1;

  if (c>0) S->modified = true;
  refresh(S);
  sprintf(msg, " %d remplacement(s) effectué(s). ",c);
  edDisplayMessage(S,msg,NULL,VM_INVERSE,"\x1b\r",0);

  if (buf) free(buf);
  free(sr[0]); free(sr[1]); free(sr[2]);
  return;
}

//Fonction principale du programme
int main(int argc, char const *argv[]) {
  int bool = true;

  atexit(RawMode_off);
  RawMode_on();       //passage en mode non canonique
  mouseInit(&M);      //Initialisation Souris

  edInitEditors(tEd); //Init du tableau d'editeurs
  if (argc > 1) {     //Chargement des editeurs avec les fichiers passes en arguments
    for (int i=1; i<argc && i<= ED_NBMAX; i++) Open_file(argv[i], &tEd[i-1]);   // Chargement fichier texte
  }
  PutCursor(1, 1, NULL);
  edDisplaySplashScreen((argc>1));  //Ecran de bienvenue
  refresh(tEd);                     //Affichage de lediteur courant (n°0)

  while(bool){                      //Boucle principale de traitement des evenements clavier / souris
    bool = editorKeyProcessor(tEd+iEd);
  }

  ClrScr(NULL);       //Nettoyage ecran
  RawMode_off();      //Retour Tty en mode canonique
  mouseClose(&M);     //Liberation device souris
  edFreeEditors(tEd); //Liberation des structures memoire
  return 0;
};
