
/* 
 * Arduino version of curses library
 */

#include "ztypes.h"

#include <mcurses.h>
#include <M5Cardputer.h>
#include <sys/types.h>
#include <sys/time.h>

// rrrr rggg gggb bbbb
// rrrr r000 gggg gg00 bbbb b000
#define RGB565(r,g,b) (((r>>3) << 11) | ((g>>2) << 5) | b >> 3)
#define FIXRGB(c) ((((c & 0xf800) << 8) | ((c & 0x7e0) << 5) | ((c & 0x1f) << 3)))

ztheme_t themes[] = {
  {"TRS-80 Black", RGB565(0,0,0), RGB565(170,170,170)},
  {"Lisa White", RGB565(170,170,170), RGB565(0,0,0)},
  {"Compaq Green", RGB565(0,0,0), RGB565(0,170,0)},
  {"IBM XT Amber", RGB565(0,0,0), RGB565(170,85,0)},
  {"Amiga Blue", RGB565(0,0,170), RGB565(170,170,170)},
  {"Amstrad Blue and Gold", RGB565(0,0,85), RGB565(170,85,0)}
};
int themecount = sizeof(themes)/sizeof(themes[0]);

extern int theme;
#define EXTENDED 1
#define PLAIN    2


#ifdef HARD_COLORS
static ZINT16 current_fg;
static ZINT16 current_bg;
#endif
extern ZINT16 default_fg;
extern ZINT16 default_bg;

extern int hist_buf_size;
extern int use_bg_color;

/* new stuff for command editing */
int BUFFER_SIZE;
char *commands;
int space_avail;
static int ptr1, ptr2 = 0;
static int end_ptr = 0;
static int row, head_col;
static int keypad_avail = 1;

/* done with editing global info */

static int current_row = 0;
static int current_col = 0;

static int saved_row;
static int saved_col;

static int status_row = 0;
static int status_col = 0;
static int text_col = 0;

static int cursor_saved = OFF;

static char tcbuf[1024];
static char cmbuf[1024];
static char *cmbufp;

static ZINT16 current_fg;
static ZINT16 current_bg;
extern ZINT16 default_fg;
extern ZINT16 default_bg;

static void display_string( char * );
static int read_char( int timeout );

#define VTSCALING 1.0
#define KEYWAIT 180
LGFX_Sprite *canvas;
uint32_t lastKeyTime;
static uint8_t vtbuf[16]; // for ansi escapes
static uint8_t *vtbufp;
static uint32_t vtcurs[4];
static uint8_t vtcharsz[2];
static uint8_t vtscroll[2];
uint16_t vtflags;

void Arduino_init()
{
  canvas = new LGFX_Sprite(&M5Cardputer.Display);
  canvas->createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height()+1); // shutup
  canvas->setBaseColor(themes[theme].bg);
  canvas->clear(themes[theme].bg);
  //canvas->setFont(&fonts::FreeMono9pt7b);
  //canvas->setTextSize(VTSCALING);
  vtcharsz[0] = canvas->textWidth("M");
  vtcharsz[1] = canvas->fontHeight();
  vtscroll[0] = 2; // TODO 1
  vtscroll[1] = DEFAULT_ROWS;
  vtbuf[0] = 0;
  vtbufp = vtbuf;
  vtcurs[0] = 1; // x
  vtcurs[1] = 1; // y
  vtcurs[2] = themes[theme].fg; // fg
  vtcurs[3] = themes[theme].bg; // bg
  vtflags = 0x0;
  lastKeyTime = 0;

  // TODO WHY?
  canvas->setScrollRect(0, (vtscroll[0]-1)*vtcharsz[1], canvas->width(), (vtscroll[1]-vtscroll[0]+1)*vtcharsz[1], themes[theme].bg);
  //canvas->setScrollRect(0,(vtscroll[0]-1)*vtcharsz[1], canvas->width(), 128, themes[theme].bg);
}

bool handle_vt(uint8_t (&buf)[16], uint8_t cmd)
{
  // parse params out of buf[]
  uint8_t params[4] = {0,0,0,0};
  int i = 0, j = -1, param_len = 0;
  for(i=2; i<16; i++)
  {
    if(buf[i] == cmd)
    {
      if(j >= 0)
      {
        params[param_len] = j;
        param_len++;
      }
      break;
    }
    else if(buf[i] == ';')
    {
      if(j >= 0)
      {
        params[param_len] = j;
        param_len++;
      }
      j = -1;
    }
    else if(j >= 0)
    {
      j = j*10 + (buf[i] - 48);
    }
    else
    {
      j = buf[i] - 48;
    }
  }

  switch(buf[1])
  {
    case '[': // CSI
      switch(cmd)
      {
        case 'F':
          vtcurs[0] = 1;
          // no break/return 
        case 'A':
          vtcurs[0] -= params[0];
          if(params[0] == 0) params[0] = 1;
          return false;
        case 'E':
          vtcurs[1] = 1;
          // no break/return
        case 'B':
          if(params[0] == 0) params[0] = 1;
          vtcurs[0] += params[0];
          return false;
        case 'C':
          if(params[0] == 0) params[0] = 1;
          vtcurs[1] += params[0];
          return false;
        case 'D':
          if(params[0] == 0) params[0] = 1;
          vtcurs[1] -= params[0];
          return false;
        case 'H':
        case 'f':
	        // move
          vtcurs[1] = params[0];
          vtcurs[0] = params[1];
          return true;
        case 'J':
          if(buf[2] == '2')
          {
            // clear screen
            canvas->deleteSprite();
            Arduino_init();
            return true;
          }
          else
          {
            // clear to bottom
            for(i=0; i < DEFAULT_COLS; i++)
              for(j=vtcurs[1]+1; j < DEFAULT_ROWS; j++)
                canvas->drawChar(i*vtcharsz[0], j*vtcharsz[1], ' ', FIXRGB(vtcurs[3]), FIXRGB(vtcurs[2]), VTSCALING);
  
            return true;
          }
          break;
        case 'K':
          switch(buf[2])
          {
            case '2':
              vtcurs[0] = 1;
            case 'K':
            case '0':
              for(i=vtcurs[0]; i < DEFAULT_COLS; i++)
                canvas->drawChar(i*vtcharsz[0], (vtcurs[1])*vtcharsz[1], ' ', FIXRGB(vtcurs[3]), FIXRGB(vtcurs[2]), VTSCALING);
              break;
            case '1':
              for(i=0; i < vtcurs[0]; i++)
                canvas->drawChar(i*vtcharsz[0], (vtcurs[1])*vtcharsz[1], ' ', FIXRGB(vtcurs[3]), FIXRGB(vtcurs[2]), VTSCALING);
              break;
          }
          return true;
        case 'L':
        case 'M':
          // TODO SCROLLING NEXT!!!
          return true;
        case 'm':
          for(i=0; i<param_len; i++)
          {
            switch(params[i])
            {
              case 0:
                vtcurs[2] = themes[theme].fg; // fg
                vtcurs[3] = themes[theme].bg; // bg
                vtflags = 0x0;
                break;
              case 4:
                vtflags |= 0x8;
                break;
              case 24:
                vtflags &= (0xffff ^ 0x8);
                break;
              case 7:
                vtflags |= 0x20;
                break;
              case 27:
                vtflags &= (0xffff ^ 0x20);
                break;
              case 30:
                vtcurs[2] = RGB565(0,0,0); //TFT_BLACK;
                break;
              case 31:
                vtcurs[2] = RGB565(170,0,0); //TFT_RED;
                break;
              case 32:
                vtcurs[2] = RGB565(0,170,0); //TFT_GREEN;
                break;
              case 33:
                vtcurs[2] = RGB565(170,85,0); //TFT_YELLOW;
                break;
              case 34:
                vtcurs[2] = RGB565(0,0,170); //TFT_BLUE;
                break;
              case 35:
                vtcurs[2] = RGB565(170,0,170); //TFT_MAGENTA;
                break;
              case 36:
                vtcurs[2] = RGB565(0,170,170); //TFT_CYAN;
                break;
              case 37:
                vtcurs[2] = RGB565(170,170,170); //TFT_WHITE;
                break;
              case 39:
                // default
                vtcurs[2] = themes[theme].fg;
                break;
              case 90:
                vtcurs[2] = RGB565(85,85,85); // black
                break;
              case 91:
                vtcurs[2] = RGB565(255,85,85); // red
                break;
              case 92:
                vtcurs[2] = RGB565(85,255,85); // green
                break;
              case 93:
                vtcurs[2] = RGB565(255,255,85); // yellow
                break;
              case 94:
                vtcurs[2] = RGB565(85,85,255); // blue
                break;
              case 95:
                vtcurs[2] = RGB565(255,85,255); //magenta
                break;
              case 96:
                vtcurs[2] = RGB565(85,255,255); // cyan
                break;
              case 97:
                vtcurs[2] = RGB565(255,255,255); // white
                break;
              case 40:
                vtcurs[3] = RGB565(0,0,0); //TFT_BLACK;
                break;
              case 41:
                vtcurs[3] = RGB565(170,0,0); //TFT_RED;
                break;
              case 42:
                vtcurs[3] = RGB565(0,170,0); //TFT_GREEN;
                break;
              case 43:
                vtcurs[3] = RGB565(170,85,0); //TFT_YELLOW;
                break;
              case 44:
                vtcurs[3] = RGB565(0,0,170); //TFT_BLUE;
                break;
              case 45:
                vtcurs[3] = RGB565(170,0,170); //TFT_MAGENTA;
                break;
              case 46:
                vtcurs[3] = RGB565(0,170,170); //TFT_CYAN;
                break;
              case 47:
                vtcurs[3] = RGB565(170,170,170); //TFT_WHITE;
                break;
              case 49:
                // default
                vtcurs[3] = themes[theme].bg;
                break;
              case 100:
                vtcurs[3] = RGB565(85,85,85); // black
                break;
              case 101:
                vtcurs[3] = RGB565(255,85,85); // red
                break;
              case 102:
                vtcurs[3] = RGB565(85,255,85); // green
                break;
              case 103:
                vtcurs[3] = RGB565(255,255,85); // yellow
                break;
              case 104:
                vtcurs[3] = RGB565(85,85,255); // blue
                break;
              case 105:
                vtcurs[3] = RGB565(255,85,255); //magenta
                break;
              case 106:
                vtcurs[3] = RGB565(85,255,255); // cyan
                break;
              case 107:
                vtcurs[3] = RGB565(255,255,255); // white
                break;
              default:
                Arduino_putchar((uint8_t) j+48);
            }
          }
          return false;
        case 'r':
          if(param_len == 0)
          {
            // reset
            vtscroll[0] = 1;
            vtscroll[1] = DEFAULT_ROWS;
          }
          else
          {
            vtscroll[0] = params[0];
            vtscroll[1] = params[1];
          }

          canvas->setScrollRect(0, (vtscroll[0]-1)*vtcharsz[1], canvas->width(), (vtscroll[1]-vtscroll[0])*vtcharsz[1], 0x00ff);// themes[theme].bg);
          //return false;
          canvas->drawLine(0,(vtscroll[0]-1)*vtcharsz[1],canvas->width(),(vtscroll[0]-1)*vtcharsz[1],0xFF7F00);
          canvas->drawLine(0,(vtscroll[1]-vtscroll[0])*vtcharsz[1],canvas->width(),(vtscroll[1]-vtscroll[0])*vtcharsz[1],0xFF7F00);
          canvas->drawLine(0,(vtscroll[0]-1)*vtcharsz[1],canvas->width(),(vtscroll[1]-vtscroll[0])*vtcharsz[1],0xFF7F00);
          return true;
        case 'S':
          canvas->scroll(0, -vtcharsz[1]*params[0]);
          return true;
        case 'T':
          canvas->scroll(0, vtcharsz[1]*params[0]);
          return true;
      }
      break;
  }
  return false;
}

void Arduino_putchar(uint8_t c)
{
  if(c == 255)
  {
    // -1 being returned from other fns
    canvas->drawChar((vtcurs[0]-1)*vtcharsz[0], (vtcurs[1]-1)*vtcharsz[1], '!', FIXRGB(vtcurs[3]), (uint32_t)0xff0000, VTSCALING);
    canvas->pushSprite(0,0);
  }
  else if(c == '\033')
  {
    // start of esc seq
    vtbuf[0] = c;
    vtbufp = vtbuf;
  }
  else if(vtbuf[0] == '\033')
  {
    // continued escape
    vtbufp ++;
    *vtbufp = c;

    if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || *vtbufp == ')' || *vtbufp == '(')
    {
      // end of escape, handle it
      if(handle_vt(vtbuf, c))
      {
        canvas->pushSprite(0,0);
      }
      vtbuf[0] = 0;
    }
  }
  else if(c < ' ')
  {
    // non-printable
    switch(c)
    {
      case '\r':
        vtcurs[0] = 1;
        break;
      case '\n':
        if(vtcurs[1] < vtscroll[1])
        {
          vtcurs[1]++;
        }
        else
        {
          // scroll scrolling region up one
          canvas->scroll(0, -vtcharsz[1]);
          canvas->pushSprite(0,0);
        }
        break;
      default:
        canvas->drawChar((vtcurs[0]-1)*vtcharsz[0], (vtcurs[1]-1)*vtcharsz[1], '?', FIXRGB(vtcurs[3]), (uint32_t)0xff0000, VTSCALING);
        canvas->pushSprite(0,0);
        break;
    }
  }
  else if(c == 127)
  {
    // backspace
    // TODO any logic about prompt or left gutter
    if(vtcurs[0] > 1)
    {
      vtcurs[0] --;
      canvas->drawChar((vtcurs[0]-1)*vtcharsz[0], (vtcurs[1]-1)*vtcharsz[1], ' ', FIXRGB(vtcurs[3]), FIXRGB(vtcurs[2]), VTSCALING);
      canvas->pushSprite(0,0);
    }
  }
  else if(c > 127)
  {
    canvas->drawChar((vtcurs[0]-1)*vtcharsz[0], (vtcurs[1]-1)*vtcharsz[1], c-192, (uint32_t)0xff0000, FIXRGB(vtcurs[2]), VTSCALING);
    canvas->pushSprite(0,0);
    vtcurs[0] ++;
  }
  else
  {
    uint8_t bg = 3;
    uint8_t fg = 2;
    if((vtflags & 0x20) == 0x20)
    {
      // inverse video
      bg = 2;
      fg = 3;
    }
    canvas->drawChar((vtcurs[0]-1)*vtcharsz[0], (vtcurs[1]-1)*vtcharsz[1], c, FIXRGB(vtcurs[bg]), FIXRGB(vtcurs[fg]), VTSCALING);
    if((vtflags & 0x8) == 0x8)
      canvas->drawFastHLine((vtcurs[0]-1)*vtcharsz[0], vtcurs[1]*vtcharsz[1]-1, vtcharsz[0], FIXRGB(vtcurs[fg]));
    canvas->pushSprite(0,0);
    vtcurs[0] ++;
  }
}

char Arduino_getchar()
{
  for(;;)
  {
    M5Cardputer.update(); // must be in AND out of loop
    while((!M5Cardputer.Keyboard.isChange() && !M5Cardputer.Keyboard.isPressed())
      || (lastKeyTime+KEYWAIT) > millis()) {
      yield();
      M5Cardputer.update();
    };
    Keyboard_Class::KeysState s = M5Cardputer.Keyboard.keysState();
    lastKeyTime = millis();
    if(s.enter)
      return '\r';
    else if(s.tab)
      return '\t';
    else if(s.del)
      return 127;
    else if(s.word.size() > 0)
      return s.word[0];
    // TODO cursor keys
  }
}

int inc( uint32_t timeout = 0 )
{
  uint32_t timer = millis();
WAITLOOP: M5Cardputer.update();
  while((lastKeyTime+KEYWAIT) > millis()
      || (!M5Cardputer.Keyboard.isChange() && !M5Cardputer.Keyboard.isPressed()
      && ((timeout == 0) || (timeout > 0 && (timer + timeout*100 > millis())))))
  {
    yield();
    M5Cardputer.update();
  };
  lastKeyTime = millis();
  if(timeout > 0 && ((timer + timeout*100) <= millis()))
    return -1;

  Keyboard_Class::KeysState s = M5Cardputer.Keyboard.keysState();
  uint8_t c = 0;
  if(s.word.size() > 0)
    c = s.word[0];

  if(s.enter)
  {
    Arduino_putchar('\r'); // maybe??
    Arduino_putchar('\n'); // maybe??
    return '\r';
  }
  else if(s.tab)
    return '\t';
  else if(s.del)
    return 127;
  else if(c == 0) // some other cardputer key
    goto WAITLOOP;

  Arduino_putchar(c);
  return c;
}

static int uninc( int c )
{
    // not supported right now
   //return ungetc( c, stdin );
}

static int outc( int c )
{
   Arduino_putchar((uint8_t) c);
   return c;
}

void initialize_screen(  )
{
   int row, col;

   setFunction_putchar(Arduino_putchar); // tell the library which output channel shall be used
   setFunction_getchar(Arduino_getchar); // tell the library which input channel shall be used

   /* initialize the command buffer */
   cmbufp = cmbuf;

   /* start the curses environment */
   if ( initscr(  ) )
   {
      fatal( "initialize_screen(): Couldn't init curses." );
   }

   /* COLS and LINES set by curses */
   screen_cols = DEFAULT_COLS;
   screen_rows = DEFAULT_ROWS;

   clear_screen(  );

   /* Last release (2.0.1g) claimed DEC tops 20.  I'm a sadist. Sue me. */
   h_interpreter = INTERP_MSDOS;
   JTERP = INTERP_UNIX;

// command history not implemented
/*
   commands = ( char * ) malloc( hist_buf_size * sizeof ( char ) );

   if ( commands == NULL )
      fatal( "initialize_screen(): Couldn't allocate history buffer." );
   BUFFER_SIZE = hist_buf_size;
   space_avail = hist_buf_size - 1;
*/
   interp_initialized = 1;

}                               /* initialize_screen */

void restart_screen(  )
{
   cursor_saved = OFF;
}                               /* restart_screen */

void reset_screen(  )
{
   /* only do this stuff on exit when called AFTER initialize_screen */
   if ( interp_initialized )
   {
      display_string( "\r\n[Hit any key to exit.]" );
      Arduino_getchar();

      delete_status_window(  );
      select_text_window(  );

      //printf( "[0m" );

      erase(  );
      //set_cbreak_mode( 0 );

   }
   display_string( "\r\n" );

}                               /* reset_screen */

void clear_screen(  )
{
   erase(  );                   /* clear screen */
   current_row = 1;
   current_col = 1;
}                               /* clear_screen */


void select_status_window(  )
{
   save_cursor_position(  );
}                               /* select_status_window */


void select_text_window(  )
{
   restore_cursor_position(  );
}                               /* select_text_window */

void create_status_window(  )
{
   int row, col;

   get_cursor_position( &row, &col );

   /* set up a software scrolling region */
   setscrreg(status_size, screen_rows-1);

   move_cursor( row, col );
}                               /* create_status_window */

void delete_status_window(  )
{
   int row, col;

   get_cursor_position( &row, &col );

   /* set up a software scrolling region */
   
   setscrreg(0, screen_rows-1);

   move_cursor( row, col );

}                               /* delete_status_window */

void clear_line(  )
{
   clrtoeol(  );
}                               /* clear_line */

void clear_text_window(  )
{
   int i, row, col;

   get_cursor_position( &row, &col );

   for ( i = status_size + 1; i <= screen_rows; i++ )
   {
      move_cursor( i, 1 );
      clear_line(  );
   }

   move_cursor( row, col );

}                               /* clear_text_window */

void clear_status_window(  )
{
   int i, row, col;

   get_cursor_position( &row, &col );

   for ( i = status_size; i; i-- )
   {
      move_cursor( i, 1 );
      clear_line(  );
   }

   move_cursor( row, col );

}                               /* clear_status_window */

void move_cursor( int row, int col )
{
   move( row - 1, col - 1 );
   current_row = row;
   current_col = col;

}                               /* move_cursor */

void get_cursor_position( int *row, int *col )
{
   *row = current_row;
   *col = current_col;
}                               /* get_cursor_position */

void save_cursor_position(  )
{
   if ( cursor_saved == OFF )
   {
      get_cursor_position( &saved_row, &saved_col );
      cursor_saved = ON;
   }
}                               /* save_cursor_position */


void restore_cursor_position(  )
{
   if ( cursor_saved == ON )
   {
      move_cursor( saved_row, saved_col );
      cursor_saved = OFF;
   }
}                               /* restore_cursor_position */

void set_attribute( int attribute )
{
   static int emph = 0, rev = 0;

   if ( attribute == NORMAL )
   {
      // this is the text part of the window
         attrset(A_NORMAL);
   }

   if ( attribute & REVERSE )
   {
      // this is the status part of the window
      attrset(A_REVERSE);
   }

   if ( attribute & BOLD )
   {
   }
   if ( attribute & EMPHASIS )
   {
   }

   if ( attribute & FIXED_FONT )
   {
   }

}                               /* set_attribute */

static void display_string( char *s )
{
   while ( *s )
      display_char( *s++ );
}                               /* display_string */

void display_char( int c )
{
   outc( c );
   if ( ++current_col > screen_cols )
      current_col = screen_cols;
}                               /* display_char */

void scroll_line(  )
{
   int row, col;

   get_cursor_position( &row, &col );

   if ( row < screen_rows )
   {
     display_char( '\r' );
     display_char( '\n' );
   }
   else
   {
      setscrreg( status_size, screen_rows - 1 );
      display_char( '\r' );
      display_char( '\n' );
   }

   current_col = 1;
   if ( ++current_row > screen_rows )
      current_row = screen_rows;

}                               /* scroll_line */

int input_line( int buflen, char *buffer, int timeout, int *read_size )
{
  int c;
  yield();
  *read_size = 0;
  while ( ( c = read_char(timeout) ) != '\r' ) // use for Arduino line feed
  {
    if(c == 127) // backspace pressed?
    {
      if(*read_size > 0)
      {
        buffer[(*read_size)] = '\0';
        (*read_size)--;
        // OK to backspace, characters still on the left side
        Arduino_putchar(c);
      }
    }
    else if ( c == -1 )
      return -1;
    else if ( *read_size < buflen )
      buffer[( *read_size )++] = c;
  }
  text_col = 0;
  yield();
  return c;
}                               /* input_line */

#define COMMAND_LEN 20
static int read_char( int timeout = 0 )
{
   static int input_is_at_eol = TRUE;
   int c, n;
   char command[COMMAND_LEN + 1];

   for ( ;; )
   {
      if ( ( c = inc(timeout) ) == '\\' )
      {
         c = inc(timeout);
         if ( c == '\\' )
            break;
         uninc( c );
         /* Read a command.  */
         for ( n = 0; n < COMMAND_LEN; n++ )
         {
            command[n] = inc(timeout);
            if ( command[n] == '\r' )
               break;
         }
         command[n] = '\0';
         /* If line was too long, flush input to the end of it.  */
         if ( n == COMMAND_LEN )
            while ( inc(timeout) != '\r' )
               ;
         continue;
      }
      break;
   }
   input_is_at_eol = ( c == '\r' );
   return c;
}                               /* read_char */

int input_character( int timeout )
{
   int c = read_char( timeout );

   /* Bureaucracy expects CR, not NL.  */
   return ( ( c == '\n' ) ? '\r' : c );
}                               /* input_character */

static int read_key( int mode )
{
   int c;

   if ( mode == PLAIN )
   {
      do
      {
         c = Arduino_getchar();
      }
      while ( !( c == 10 || c == 13 || c == 8 ) && ( c < 32 || c > 127 ) );
   }
   else if ( mode == EXTENDED )
   {                            /* also pass ESC character back for editor */
      do
      {
         c = Arduino_getchar();
      }
      while ( !( c == 27 || c == 10 || c == 13 || c == 8 ) && ( c < 32 || c > 127 ) );
   }

   if ( c == 127 )
      c = '\b';
   else if ( c == 10 )
      c = 13;

   return ( c );

}                               /* read_key */

static void rundown(  )
{
   unload_cache(  );
   close_story(  );
   close_script(  );
   reset_screen(  );
}                               /* rundown */


void set_colours( zword_t foreground, zword_t background )
{
  // not implemented
}
/* Zcolors:
 * BLACK 0   BLUE 4   GREEN 2   CYAN 6   RED 1   MAGENTA 5   BROWN 3   WHITE 7
 * ANSI Colors (foreground over background):
 * BLACK 30  BLUE 34  GREEN 32  CYAN 36  RED 31  MAGENTA 35  BROWN 33  WHITE 37
 * BLACK 40  BLUE 44  GREEN 42  CYAN 46  RED 41  MAGENTA 45  BROWN 43  WHITE 47
 */
void set_colours_( zword_t foreground, zword_t background )
{
   return;
   int fg, bg;

   int fg_colour_map[] = { F_BLACK, F_BLUE, F_GREEN, F_CYAN, F_RED, F_MAGENTA, F_BROWN, F_WHITE };
   int bg_colour_map[] = { B_BLACK, B_BLUE, B_GREEN, B_CYAN, B_RED, B_MAGENTA, B_BROWN, B_WHITE };

   /* Translate from Z-code colour values to natural colour values */

   if ( ( ZINT16 ) foreground >= 1 && ( ZINT16 ) foreground <= 9 )
   {
      fg = ( foreground == 1 ) ? ( fg_colour_map[default_fg] ) : fg_colour_map[foreground];
   }
   if ( ( ZINT16 ) background >= 1 && ( ZINT16 ) background <= 9 )
   {
      bg = ( background == 1 ) ? ( bg_colour_map[default_bg] ) : bg_colour_map[background];
   }

   current_fg = ( ZINT16 ) fg;
   current_bg = ( ZINT16 ) bg;
   if( monochrome)
   {
     attrset ( F_WHITE | B_BLACK );
   }
   else
   {
     attrset (TEXT_ATTR);    
   }

}

/*
 * codes_to_text
 *
 * Translate Z-code characters to machine specific characters. These characters
 * include line drawing characters and international characters.
 *
 * The routine takes one of the Z-code characters from the following table and
 * writes the machine specific text replacement. The target replacement buffer
 * is defined by MAX_TEXT_SIZE in ztypes.h. The replacement text should be in a
 * normal C, zero terminated, string.
 *
 * Return 0 if a translation was available, otherwise 1.
 *
 *  Line drawing characters (0xb3 - 0xda):
 *
 *  0xb3 vertical line (|)
 *  0xba double vertical line (#)
 *  0xc4 horizontal line (-)
 *  0xcd double horizontal line (=)
 *  all other are corner pieces (+)
 */
int codes_to_text( int c, char *s )
{
   /* German characters need translation */

   if ( c > 154 && c < 224 )
   {
      s[0] = zscii2latin1[c - 155];

      if ( c == 220 )
      {
         s[1] = 'e';
         s[2] = '\0';
      }
      else if ( c == 221 )
      {
         s[1] = 'E';
         s[2] = '\0';
      }
      else
      {
         s[1] = '\0';
      }
      return 0;
   }
   return 1;
}                               /* codes_to_text */
