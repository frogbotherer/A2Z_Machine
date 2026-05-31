
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
int theme = 2;

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

#define VTFLAG_BOLD 0x1
#define VTFLAG_UL 0x2
#define VTFLAG_INV 0x4
#define VTFLAG_CURS 0x8
#define VTFLAG_SHIFTOUT 0x10

#define VTSCALING 1.0
#define KEYWAIT 180
LGFX_Sprite *canvas, *canvas_cursor, *canvas_debug;
uint32_t lastKeyTime;
static uint8_t vtbuf[16]; // for ansi escapes
static uint8_t *vtbufp;
static uint32_t vtcurs[4];
static uint8_t vtcharsz[2];
static uint8_t vtscroll[2];
uint16_t vtflags;

void Arduino_init()
{

  canvas_debug = new LGFX_Sprite(&M5Cardputer.Display);
  canvas_debug->createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
  canvas_debug->setTextScroll(true);

  canvas = new LGFX_Sprite(&M5Cardputer.Display);
  canvas->createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height()+1); // shutup
  canvas->setBaseColor(themes[theme].bg);
  canvas->clear(themes[theme].bg);
  //canvas->setFont(&fonts::FreeMono24pt7b);
  canvas->setFont(&fonts::Font0);
  canvas->setTextSize(VTSCALING);

  vtcharsz[0] = canvas->textWidth("M");
  vtcharsz[1] = canvas->fontHeight();
  vtscroll[0] = 2; // allow for status line
  vtscroll[1] = DEFAULT_ROWS;
  vtbuf[0] = 0;
  vtbufp = vtbuf;
  current_col = 1; // x
  current_row = 1; // y
  vtcurs[0] = 'B'; // G0 set, SI
  vtcurs[1] = '0'; // G1 set, SO
  vtcurs[2] = themes[theme].fg; // fg
  vtcurs[3] = themes[theme].bg; // bg
  vtflags = 0x0;
  lastKeyTime = 0;

  // initialise scroll region with status line
  canvas->setScrollRect(0, (vtscroll[0]-1)*vtcharsz[1], canvas->width(), (vtscroll[1]-vtscroll[0]+1)*vtcharsz[1], themes[theme].bg);

  // command history buffer
  hist_buf_size = 4096;

  // cursor rendering
  canvas_cursor = new LGFX_Sprite(&M5Cardputer.Display);
  canvas_cursor->createSprite(vtcharsz[0], vtcharsz[1]);
}

void Arduino_deinit()
{
  Arduino_debug("deinit called");
  canvas->deleteSprite();
  canvas_cursor->deleteSprite();
  canvas_debug->deleteSprite();

  delete canvas;
  delete canvas_cursor;
  delete canvas_debug;
}

void Arduino_debug(const char *s, char level)
{
  if(level=='X') return;
  switch(level)
  {
    case 'I':
      canvas_debug->setTextColor(TFT_GREEN);
      break;
    case 'W':
      canvas_debug->setTextColor(TFT_YELLOW);
      break;
    case 'E':
      canvas_debug->setTextColor(TFT_RED);
      break;
    default:
      canvas_debug->setTextColor(TFT_GRAY);
      break;
  }
  canvas_debug->print(level);
  canvas_debug->setTextColor(TFT_WHITE);
  canvas_debug->print(": ");
  canvas_debug->println(s);
  canvas_debug->pushSprite(0,0);
  yield();
  if(level == 'D')
    delay(2000);
}

void push_sprites()
{
  canvas->pushSprite(0, 0);
  if((vtflags & VTFLAG_CURS) == VTFLAG_CURS)
  {
    canvas_cursor->fillRect(0, 0, vtcharsz[0], vtcharsz[1], themes[theme].fg);
    canvas_cursor->pushSprite((current_col-1)*vtcharsz[0], (current_row-1)*vtcharsz[1]);
    Arduino_debug("cursor on", 'D');
  }
}

bool handle_vt(uint8_t (&buf)[16], uint8_t cmd)
{
  char debug[20];
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
          current_col = 1;
          // no break/return 
        case 'A':
          current_col -= params[0];
          if(params[0] == 0) params[0] = 1;
          return false;
        case 'E':
          current_row = 1;
          // no break/return
        case 'B':
          if(params[0] == 0) params[0] = 1;
          current_col += params[0];
          return false;
        case 'C':
          if(params[0] == 0) params[0] = 1;
          current_row += params[0];
          return false;
        case 'D':
          if(params[0] == 0) params[0] = 1;
          current_row -= params[0];
          return false;
        case 'H':
        case 'f':
	        // move
          current_row = params[0];
          current_col = params[1];
          return true;
        case 'J':
          if(buf[2] == '2')
          {
            // clear screen
            canvas->clear(themes[theme].bg);
            current_row = 1;
            current_col = 1;
            return true;
          }
          else
          {
            // clear to bottom
            for(i=0; i < DEFAULT_COLS; i++)
              for(j=current_row+1; j < DEFAULT_ROWS; j++)
                canvas->drawChar(i*vtcharsz[0], j*vtcharsz[1], ' ', FIXRGB(vtcurs[3]), FIXRGB(vtcurs[2]), VTSCALING);
  
            return true;
          }
          break;
        case 'K':
          switch(buf[2])
          {
            case '2':
              current_col = 1;
            case 'K':
            case '0':
              for(i=current_col-1; i < DEFAULT_COLS; i++)
                canvas->drawChar(i*vtcharsz[0], (current_row-1)*vtcharsz[1], ' ', FIXRGB(vtcurs[3]), FIXRGB(vtcurs[2]), VTSCALING);
              break;
            case '1':
              for(i=0; i < current_col; i++)
                canvas->drawChar(i*vtcharsz[0], (current_row-1)*vtcharsz[1], ' ', FIXRGB(vtcurs[3]), FIXRGB(vtcurs[2]), VTSCALING);
              break;
          }
          return true;
        case 'h':
          if(param_len < 2)
            return false;
          switch(params[1])
          {
            case 25: // cursor visible
              vtflags |= VTFLAG_CURS;
          }
          return true;
        case 'l':
          if(param_len < 2)
            return false;
          switch(params[1])
          {
            case 25: // cursor invisible
              vtflags &= 0xffff ^ VTFLAG_CURS;
          }
          return true;
        case 'L':
        case 'M':
          // TODO SCROLLING NEXT!!!
          Arduino_debug("L/M command is TODO", 'D');
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
              case 1:
                vtflags |= VTFLAG_BOLD;
                break;
              case 21:
                vtflags &= (0xffff ^ VTFLAG_BOLD);
                break;
              case 4:
                vtflags |= VTFLAG_UL;
                break;
              case 24:
                vtflags &= (0xffff ^ VTFLAG_UL);
                break;
              case 7:
                vtflags |= VTFLAG_INV;
                break;
              case 27:
                vtflags &= (0xffff ^ VTFLAG_INV);
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

          canvas->setScrollRect(0, (vtscroll[0]-1)*vtcharsz[1], canvas->width(), (vtscroll[1]-vtscroll[0]+1)*vtcharsz[1], themes[theme].bg);
          return false;
        case 'S':
          canvas->scroll(0, -vtcharsz[1]*params[0]);
          return true;
        case 'T':
          canvas->scroll(0, vtcharsz[1]*params[0]);
          return true;
        default:
          sprintf(debug, "Unknown CSI %c", cmd);
          Arduino_debug(debug, 'D'); //cmd
      }
      break;
    case '(': // SCS: G0 sets
      vtcurs[0] = cmd;
    case ')': // SCS: G1 sets
      vtcurs[1] = cmd;
      switch(cmd)
      {
        case 'A': // uk
        case 'B': // ascii
        case '0': // special
        case '1': // alt std
        case '2': // alt special
          break;
        default:
          sprintf(debug, "Unknown SCS set %c", cmd);
          Arduino_debug(debug, 'D');
 
          break;
      }
      break;
    default:
      sprintf(debug, "Unknown escape %c", buf[1]);
      Arduino_debug(debug, 'D'); //buf[1]

  }
  return false;
}

void Arduino_putchar(uint8_t c)
{
  if(c == 255)
  {
    // -1 being returned from other fns, this is a bug
    canvas->drawChar((current_col-1)*vtcharsz[0], (current_row-1)*vtcharsz[1], '!', FIXRGB(vtcurs[3]), (uint32_t)0xff0000, VTSCALING);
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

    if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') 
        || (c >= '0' && c <= '9' && (vtbuf[1] == ')' || vtbuf[1] == '(')))
    {
      // end of escape, handle it
      handle_vt(vtbuf, c);
      vtbuf[0] = 0;
    }
  }
  else if(c < ' ')
  {
    // non-printable
    switch(c)
    {
      case '\r':
        current_col = 1;
        break;
      case '\n':
        if(current_row < vtscroll[1])
        {
          current_row++;
        }
        else
        {
          // scroll scrolling region up one
          canvas->scroll(0, -vtcharsz[1]);
        }
        break;
      case '\b':
        // backspace
        if(current_col > 1)
        {
          current_col --;
          canvas->drawChar((current_col-1)*vtcharsz[0], (current_row-1)*vtcharsz[1], ' ', FIXRGB(vtcurs[3]), FIXRGB(vtcurs[2]), VTSCALING);
        }
        break;
      case 14: // shift out
        vtflags |= VTFLAG_SHIFTOUT;
        break;
      case 15: // shift in
        vtflags &= 0xffff ^ VTFLAG_SHIFTOUT;
        break;
      default:
        canvas->drawChar((current_col-1)*vtcharsz[0], (current_row-1)*vtcharsz[1], '?', FIXRGB(vtcurs[3]), (uint32_t)0xff0000, VTSCALING);
        break;
    }
  }
  else 
  {
    if(false && c >= 127)
    {
      /* bad idea now line editing is in */
      char debug[100];
      sprintf(debug, "Got high char %d, G0=%c G1=%c", c, vtcurs[0], vtcurs[1]);
      Arduino_debug(debug, 'D');

      // TODO: use VTFLAG_SHIFTOUT and G0/G1 to map 'c' into printable utf8
    }
    uint8_t bg = 3;
    uint8_t fg = 2;
    if((vtflags & VTFLAG_INV) == VTFLAG_INV)
    {
      // inverse video
      bg = 2;
      fg = 3;
    }
    // ul
    if((vtflags & VTFLAG_UL) == VTFLAG_UL)
      canvas->drawFastHLine((current_col-1)*vtcharsz[0], current_row*vtcharsz[1]-1, vtcharsz[0], FIXRGB(vtcurs[fg]));

    // char
    canvas->drawChar((current_col-1)*vtcharsz[0], (current_row-1)*vtcharsz[1], c, FIXRGB(vtcurs[bg]), FIXRGB(vtcurs[fg]), VTSCALING);
    current_col ++;

  }
}

char kstochar(Keyboard_Class::KeysState &s)
{
  if(s.enter)
    return '\r';
  else if(s.tab)
    return '\t';
  else if(s.del)
  {
    if(s.fn)
      return 127; // del
    else
      return '\b'; // backspace
  }
  else if(s.word.size() > 0)
  {
    if(s.fn)
    {
      switch(s.word[0])
      {
        case ';':
          return 0x81;
        case '.':
          return 0x82;
        case ',':
          return 0x83;
        case '/':
          return 0x84;
        default:
          return s.word[0];
      }
    }
    else if(s.ctrl)
    {
      switch(s.word[0])
      {
        case 'a':
        case 'A':
        case ',':
          return 0x98;
        case 'e':
        case 'E':
        case '/':
          return 0x92;
        case ';':
          return 0x9a;
        case '.':
          return 0x94;
        default:
          return s.word[0];
      }
    }
    else
    {
      return s.word[0];
    }
  }
  return 0; 
}

char Arduino_getchar()
{
  push_sprites();
  for(;;)
  {
    M5Cardputer.update(); // must be in AND out of loop
    while((!M5Cardputer.Keyboard.isChange() && !M5Cardputer.Keyboard.isPressed())
      || (lastKeyTime+KEYWAIT) > millis()) {
      yield();
      M5Cardputer.update();
    };
    char c = kstochar(M5Cardputer.Keyboard.keysState());
    lastKeyTime = millis();
    if(c > 0)
      return c;
  }
}

int inc( uint32_t timeout = 0 )
{
  uint32_t timer = millis();
  push_sprites();
  for(;;)
  {
    M5Cardputer.update();
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

    uint8_t c = kstochar(M5Cardputer.Keyboard.keysState());

    if(c == '\r' || c == '\n')
    {
      Arduino_putchar('\r'); // maybe??
      Arduino_putchar('\n'); // maybe??
      return '\r';
    }
    else if(c == '\b')
    {
      // don't echo bksp now, wait for line
      // editor to handle it
      return c;
    }
    else if(c > 0)
    {
      // some other cardputer key
      //no local echo with line editing
      //Arduino_putchar(c);
      return c;
    }
  }
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

   commands = new char[hist_buf_size];

   BUFFER_SIZE = hist_buf_size;
   space_avail = hist_buf_size - 1;

   interp_initialized = 1;

}                               /* initialize_screen */

void restart_screen(  )
{
  zbyte_t high = 1, low = 0;

  cursor_saved = OFF;

  set_byte( H_STANDARD_HIGH, high );
  set_byte( H_STANDARD_LOW, low );
  if ( h_type < V4 )
    set_byte( H_CONFIG, ( get_byte( H_CONFIG ) | CONFIG_WINDOWS ) );
  else
  {
    /* turn stuff on */
    set_byte( H_CONFIG,
             ( get_byte( H_CONFIG ) | CONFIG_BOLDFACE | CONFIG_EMPHASIS | CONFIG_FIXED |
               CONFIG_TIMEDINPUT ) );

    if ( !monochrome )
      set_byte( H_CONFIG, ( get_byte( H_CONFIG ) | CONFIG_COLOUR ) );

    set_byte( H_BG_DEFAULT_COLOR, default_bg + 2 );
    set_byte( H_FG_DEFAULT_COLOR, default_fg + 2 );

    /* turn stuff off */
    set_byte( H_CONFIG, ( get_byte( H_CONFIG ) & ~CONFIG_PICTURES & ~CONFIG_SFX ) );
  }

  /* Force graphics and sound off as we can't do them */
  set_word( H_FLAGS, ( get_word( H_FLAGS ) & ~GRAPHICS_FLAG & ~NEW_SOUND_FLAG ) );

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

      clear(); //erase(  );
      //set_cbreak_mode( 0 );

   }
   display_string( "\r\n" );

}                               /* reset_screen */

void clear_screen(  )
{
   clear(); //erase(  );                   /* clear screen */
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
     if( use_bg_color )
     {
       attrset(A_NORMAL);
     }
     else if( emph || rev )
     {
       emph = 0;
       rev = 0;
       attrset(A_NORMAL);
     }
   }

   if ( attribute & REVERSE )
   {
     // this is the status part of the window
     attrset(A_REVERSE);
     rev = 1;
   }

   if ( attribute & BOLD )
   {
     if( use_bg_color )
       attrset(A_BOLD);
   }
   if ( attribute & EMPHASIS )
   {
     attrset(A_UNDERLINE);
     emph = 1;
   }

   if ( attribute & FIXED_FONT )
   {
   }

  attrset(current_bg | current_fg);
}                               /* set_attribute */

static void display_string( char *s )
{
   while ( *s )
      display_char( *s++ );
}                               /* display_string */

void display_char( int c )
{
   outc( c );
   if ( current_col > screen_cols )
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
   if ( current_row > screen_rows )
      current_row = screen_rows;

}                               /* scroll_line */

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

   //if ( c == 127 )
   //   c = '\b';
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

/* Zcolors:
 * CURRENT 0   DEFAULT 1   BLACK 0   RED  1   GREEN 2   BROWN 3  BLUE 4  MAGENTA 5   CYAN 6    WHITE 7
 * ANSI Colors (foreground over background):
 * BLACK 30  BLUE 34  GREEN 32  CYAN 36  RED 31  MAGENTA 35  BROWN 33  WHITE 37  DEFAULT 39
 * BLACK 40  BLUE 44  GREEN 42  CYAN 46  RED 41  MAGENTA 45  BROWN 43  WHITE 47  DEFAULT 49
 */
void set_colours( zword_t foreground, zword_t background )
{
  int fg, bg;
  static int bgset = 0;

  int fg_colour_map[] = { 0, F_DEFAULT, F_BLACK, F_RED, F_GREEN, F_BROWN, F_BLUE, F_MAGENTA, F_CYAN, F_WHITE };
  int bg_colour_map[] = { 0, B_DEFAULT, B_BLACK, B_RED, B_GREEN, B_BROWN, B_BLUE, B_MAGENTA, B_CYAN, B_WHITE };

  /* Translate from Z-code colour values to natural colour values */

  if ( ( ZINT16 ) foreground >= 1 && ( ZINT16 ) foreground <= 9 )
  {
    fg = fg_colour_map[foreground];
  }
  if ( ( ZINT16 ) background >= 1 && ( ZINT16 ) background <= 9 )
  {
    bg = bg_colour_map[background];
  }

  current_fg = ( ZINT16 ) fg;
  current_bg = ( ZINT16 ) bg;

  /* Set foreground and background colour */
  if ( !monochrome )
  {
    if ( use_bg_color )
    {
      attrset( fg|bg );
    }
    else if ( bg != 49 ) // was 40 i.e. black
    {
      attrset( fg|bg );
      bgset = 1;
    }
    else if ( bgset )
    {
      //printf( "\x1B[0m\x1B[1m" );
      attrset( F_DEFAULT | B_DEFAULT );
      bgset = 0;
    }
  }
  else
  {
    attrset ( F_WHITE | B_BLACK );
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
 *
 *  Arduino Font0 is CP437 "extended ascii"
 *  FreeXXX fonts have only 7bit chars
 */
int codes_to_text( int c, char *s )
{
  if(c>0xff) return 1;

  /* some attempt at translation */

  if ( c > 154 && c < 224 )
  {
    s[0] = zscii2cp437[c - 155];

    switch(c)
    {
      case 220: // oe
        s[1] = 'e';
        s[2] = '\0';
        break;
      case 221: // OE
        s[1] = 'E';
        s[2] = '\0';
        break;
      case 215: // th
      case 216:
      case 217: // Th
      case 218:
        s[1] = 'h';
        s[2] = '\0';
        break;
      default:
        s[1] = '\0';
        break;
    }
    return 0;
  }
  return 1;
}                               /* codes_to_text */

/*
 * Previous command system
 *
 * Here's how this works:
 *
 * The previous command buffer is BUFFER_SIZE bytes long. After the player
 * presses Enter, the command is added to this buffer, with a trailing '\n'
 * added. The '\n' is used to show where one command ends and another begins.
 *
 * The up arrow key retrieves a previous command. This is done by working
 * backwards through the buffer until a '\n' is found. The down arrow
 * retieves the next command by counting forward. The ptr1 and ptr2
 * values hold the start and end of the currently displayed command.
 *
 * PgUp displays the first ("oldest") command, while PgDn displays a blank
 * prompt.
 */
int display_command( char *buffer )
{
   int counter, loop;

   move_cursor( row, head_col );
   clrtoeol(); /* fix scoll bug w/ command history */

   /* ptr1 = end_ptr when the player has selected beyond any previously
    * saved command.
    */

   if ( ptr1 == end_ptr )
   {
      return ( 0 );
   }
   else
   {
      /* Put the characters from the save buffer into the variable "buffer".
       * The return value (counter) is the value of *read_size.
       */

      counter = 0;
      for ( loop = ptr1; loop <= ptr2; loop++ )
      {
         buffer[counter] = commands[loop];
         display_char( buffer[counter++] );
      }
      return ( counter );
   }
}                               /* display_command */

void get_prev_command(  )
{
   /* Checking to see if ptr1 > 0 prevents moving ptr1 and ptr2 into
    * never-never land.
    */

   if ( ptr1 > 0 )
   {
      /* Subtract 2 to jump over any intervening '\n' */

      ptr2 = ptr1 -= 2;

      /* If we've jumped too far, fix it */

      if ( ptr1 < 0 )
         ptr1 = 0;
      if ( ptr2 < 0 )
         ptr2 = 0;

      if ( ptr1 > 0 )
      {
         do

            /* Decrement ptr1 until a '\n' is found */

            ptr1--;
         while ( ( ptr1 >= 0 ) && ( commands[ptr1] != '\n' ) );

         /* Then advance back to the position after the '\n' */

         ptr1++;
      }
   }
}                               /* get_prev_command */

void get_next_command(  )
{
   if ( ptr2 < end_ptr )
   {
      /* Add 2 to advance over any intervening '\n' */

      ptr1 = ptr2 += 2;
      if ( ptr2 >= end_ptr )
      {
         ptr1 = ptr2 = end_ptr;
      }
      else
      {
         do
            ptr2++;
         while ( ( commands[ptr2] != '\n' ) && ( ptr2 <= end_ptr ) );
         ptr2--;
      }
   }
}                               /* get_next_command */

void get_first_command(  )
{

   if ( end_ptr > 1 )
   {
      ptr1 = ptr2 = 0;
      do
         ptr2++;
      while ( commands[ptr2] != '\n' );
      ptr2--;
   }
}                               /* get_first_command */

void delete_command(  )
{

   /* Deletes entire commands from the beginning of the command buffer */

   int loop;

   /* Keep moving the characters in the command buffer one space to the left
    * until a '\n' is found...
    */

   do
   {
      for ( loop = 1; loop < end_ptr; loop++ )
      {
         commands[loop - 1] = commands[loop];
      }
      end_ptr--;
      space_avail++;

   }
   while ( commands[0] != '\n' );

   /* ...then delete the '\n' */

   for ( loop = 1; loop < end_ptr; loop++ )
   {
      commands[loop - 1] = commands[loop];
   }
   end_ptr--;
   space_avail++;
   ptr1 = ptr2 = end_ptr;

}                               /* delete_command */

void add_command( char *buffer, int size )
{
   int loop, counter;

   /* Add the player's last command to the command buffer */

   counter = 0;
   for ( loop = end_ptr; loop < ( end_ptr + size ); loop++ )
   {
      commands[loop] = buffer[counter++];
   }

   /* Add one space for '\n' */

   end_ptr += size + 1;
   ptr1 = ptr2 = end_ptr;
   commands[end_ptr - 1] = '\n';
   space_avail -= size + 1;

}                               /* add_command */


int input_line( int buflen, char *buffer, int timeout, int *read_size )
{
   int c, col;
   int init_char_pos, curr_char_pos;
   int loop, tail_col;
   int keyfunc = 0;
   int start_col = 2; // allows for the prompt

   /*
    * init_char_pos : the initial cursor location
    * curr_char_pos : the current character position within the input line
    * head_col: the head of the input line (used for cursor position)
    *  (global variable)
    * tail_col: the end of the input line (used for cursor position)
    */

   get_cursor_position( &row, &col );
   head_col = start_col;
   tail_col = start_col + *read_size;

   init_char_pos = curr_char_pos = col - start_col;

   ptr1 = ptr2 = end_ptr;

   for ( ;; )
   {
      yield();

      keyfunc = 0;

      /* Read a single keystroke */
      c = read_char( timeout );
      if(c == -1) return -1;

      /****** Previous Command Selection Keys ******/

      //if ( line_editing )
      //{
         if ( c == 0x81 )
         {                   /* Up arrow */
            get_prev_command(  );
            curr_char_pos = *read_size = display_command( buffer );
            tail_col = head_col + *read_size;
            keyfunc = 1;
         }
         else if ( c == 0x82 )
         {                   /* Down arrow */
            get_next_command(  );
            curr_char_pos = *read_size = display_command( buffer );
            tail_col = head_col + *read_size;
            keyfunc = 1;
         }
         else if ( c == 0x9a )
         {                   /* PgUp */
            get_first_command( );
            curr_char_pos = *read_size = display_command( buffer );
            tail_col = head_col + *read_size;
            keyfunc = 1;
         }
         else if (c == 0x94 || c == 27)
         {                   /* PgDn or Esc */
             ptr1 = ptr2 = end_ptr;
             curr_char_pos = *read_size = display_command( buffer );
             tail_col = head_col + *read_size;
             keyfunc = 1;
         }

         /****** Cursor Editing Keys ******/

         else if ( c == 0x83 )
         {                   /* Left arrow */
            get_cursor_position( &row, &col );

            /* Prevents moving the cursor into the prompt */

            if ( col > head_col )
            {
               move_cursor( row, --col );
               curr_char_pos--;
            }
            keyfunc = 1;
         }
         else if ( c == 0x84 )
         {                   /* Right arrow */
            get_cursor_position( &row, &col );

            /* Prevents moving the cursor beyond the end of the input line */

            if ( col < tail_col )
            {
               move_cursor( row, ++col );
               curr_char_pos++;
            }
            keyfunc = 1;
         }
         else if ( c == 0x92 )
         {                   /* End */
            move_cursor( row, tail_col );
            curr_char_pos = init_char_pos + *read_size;
            keyfunc = 1;
         }
         else if ( c == 0x98 )
         {                   /* Home */
            move_cursor( row, head_col );
            curr_char_pos = init_char_pos;
            keyfunc = 1;
         }
         else if ( c == 0x7f )
         {                   /* Delete */
            if ( curr_char_pos < *read_size )
            {
               get_cursor_position ( &row, &col );
 
               for ( loop = curr_char_pos; loop < *read_size; loop++ )
               {
                  buffer[loop] = buffer[loop + 1];
               }
 
               tail_col--;
               ( *read_size )--;
 
               for ( loop = curr_char_pos; loop < *read_size; loop++ )
               {
                  display_char( buffer[loop] );
               }
 
               display_char( ' ' );

               move_cursor( row, col );
            }
            keyfunc = 1;
         }
      //}
      if ( !keyfunc )
      {
         if ( c >= 0x81 && c <= 0x9a )
         {
            int addr = get_word( H_FUNCTION_KEYS_OFFSET );
            if ( h_type >= V5 && addr > 0 )
            {
               int t;
               /* Check for game specific terminating character */
               while ( ( t = get_byte( addr++ ) ) != 0 )
               {
                  if ( t == c || t == 255 )
                  {
                     move_cursor( row, tail_col );
                     return c;
                  }
               }
            }
         }
         else if ( c == '\b' || c == 0x7f )     /* Backspace or Delete */
         {
            get_cursor_position( &row, &col );
            if ( col > head_col )
            {
               move_cursor( row, --col );
               for ( loop = curr_char_pos; loop < *read_size; loop++ )
               {
                  buffer[loop - 1] = buffer[loop];
                  display_char( buffer[loop - 1] );
               }
               display_char( ' ' );
               curr_char_pos--;
               tail_col--;
               ( *read_size )--;
               move_cursor( row, col );
            }
         }
         else if ( c != 27 )
         {
            /* Normal key action */
            if ( *read_size == ( buflen - 1 ) )
            {
               /* Ring bell if buffer is full */
               // TODO outc( BELL );
            }
            else
            {
               /* Scroll line if return key pressed */
               if ( c == '\r' || c == '\n' )
               {
                  c = '\n';
                  move_cursor( row, tail_col );
                  scroll_line(  );
               }

               if ( c == '\n' )
               {
                  /* Add the current command to the command buffer */

                  if ( *read_size > space_avail )
                  {
                     do
                        delete_command(  );
                     while ( *read_size > space_avail );
                  }
                  if ( *read_size > 0 )
                     add_command( buffer, *read_size );

                  /* Return key if it is a line terminator */
                  return ( c );
               }
               else
               {
                  get_cursor_position( &row, &col );

                  /* Used if the cursor is not at the end of the line */
                  if ( col < tail_col )
                  {
                     /* Moves the input line one character to the right */
                     for ( loop = *read_size; loop >= curr_char_pos; loop-- )
                     {
                        buffer[loop + 1] = buffer[loop];
                     }

                     /* Puts the character into the space created by the
                      * "for" loop above */
                     buffer[curr_char_pos] = ( char ) c;

                     /* Increment the end of the line values */

                     ( *read_size )++;
                     tail_col++;

                     /* Move the cursor back to its original position */

                     move_cursor( row, col );

                     /* Redisplays the input line from the point of
                      * insertion */

                     for ( loop = curr_char_pos; loop < *read_size; loop++ )
                     {
                        display_char( buffer[loop] );
                     }

                     /* Moves the cursor to the next position */

                     move_cursor( row, ++col );
                     curr_char_pos++;
                  }
                  else
                  {
                     /* Used if the cursor is at the end of the line */
                     buffer[curr_char_pos++] = ( char ) c;
                     display_char( c );
                     ( *read_size )++;
                     tail_col++;
                  }
               }
            }
         }
      }
   }
}                               /* input_line */


