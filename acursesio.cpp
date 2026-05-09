
/* 
 * Arduino version of curses library
 */

#include "ztypes.h"

#include <mcurses.h>
#include <M5Cardputer.h>
#include <sys/types.h>
#include <sys/time.h>

ztheme_t themes[] = {
  {"TRS-80 Black", (F_BLACK|B_WHITE), (F_WHITE|B_BLACK)},
  {"Lisa White", (F_WHITE|B_BLACK), (F_BLACK|B_WHITE)},
  {"Compaq Green", (F_BLACK|B_GREEN), (F_GREEN|B_BLACK)},
  {"IBM XT Amber", (F_BLACK|B_YELLOW|A_DIM), (F_YELLOW|B_BLACK|A_DIM)},
  {"Amiga Blue", (F_BLUE|B_WHITE), (F_WHITE|B_BLUE)},
  {"Amstrad Blue and Gold", (F_YELLOW|B_BLACK|A_BOLD), (F_YELLOW|B_BLUE|A_BOLD)}
};
//int themecount = 5;
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
LGFX_Sprite *canvas;
static uint8_t vtbuf[16]; // for ansi escapes
static uint8_t *vtbufp;
static uint32_t vtcurs[4];
static uint8_t vtcharsz[2];
uint16_t vtflags;

void Arduino_init()
{
  canvas = new LGFX_Sprite(&M5Cardputer.Display);
  canvas->createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
  //canvas->setFont(&fonts::FreeMono9pt7b);
  //canvas->setTextSize(VTSCALING);
  vtcharsz[0] = canvas->textWidth("M");
  vtcharsz[1] = canvas->fontHeight();
  vtbuf[0] = 0;
  vtbufp = vtbuf;
  vtcurs[0] = 1; // x
  vtcurs[1] = 1; // y
  vtcurs[2] = TFT_WHITE; // fg
  vtcurs[3] = TFT_BLACK; // bg
  vtflags = 0x0;
}

bool handle_vt(uint8_t (&buf)[16], uint8_t cmd)
{
  int i = 0, j = 0;
  switch(buf[1])
  {
    case '[': // CSI
      switch(cmd)
      {
        case 'H':
        case 'f':
	        // move
          vtcurs[1] = buf[2] - 48;
          if(buf[3] != ';')
          {
            vtcurs[1] = vtcurs[1]*10 + buf[3] - 48;
            i = 1;
          }
          vtcurs[0] = buf[4+i] - 48;
          if(buf[5+i] != 'H' && buf[5+i] != 'f')
          {
            vtcurs[0] = vtcurs[0]*10 + buf[5+i] - 48;
          }
	        //vtcurs[0]=1;
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
            return false;
          }
          break;
        case 'm':
          for(i=2; i<16; i++)
          {
            if(buf[i] == ';' || buf[i] == 'm')
            {
              switch(j)
              {
                case 7:
                  vtflags |= 0x2;
                  break;
                case 27:
                  vtflags &= (0xffff ^ 0x2);
                  break;
                case 30:
                  vtcurs[2] = TFT_BLACK;
                  break;
                case 31:
                  vtcurs[2] = TFT_RED;
                  break;
                case 32:
                  vtcurs[2] = TFT_GREEN;
                  break;
                case 33:
                  vtcurs[2] = TFT_YELLOW;
                  break;
                case 34:
                  vtcurs[2] = TFT_BLUE;
                  break;
                case 35:
                  vtcurs[2] = TFT_MAGENTA;
                  break;
                case 36:
                  vtcurs[2] = TFT_CYAN;
                  break;
                case 37:
                  vtcurs[2] = TFT_WHITE;
                  break;
                case 39:
                  // default
                  vtcurs[2] = TFT_WHITE;
                  break;
                case 40:
                  vtcurs[3] = TFT_BLACK;
                  break;
                case 41:
                  vtcurs[3] = TFT_RED;
                  break;
                case 42:
                  vtcurs[3] = TFT_GREEN;
                  break;
                case 43:
                  vtcurs[3] = TFT_YELLOW;
                  break;
                case 44:
                  vtcurs[3] = TFT_BLUE;
                  break;
                case 45:
                  vtcurs[3] = TFT_MAGENTA;
                  break;
                case 46:
                  vtcurs[3] = TFT_CYAN;
                  break;
                case 47:
                  vtcurs[3] = TFT_WHITE;
                  break;
                case 49:
                  // default
                  vtcurs[3] = TFT_BLACK;
                  break;
                default:
                  Arduino_putchar((uint8_t) j+48);
              }
              j = 0;
              if(buf[i] == 'm') break;
            }
            else if(j == 0)
            {
              j = buf[i] - 48;
            }
            else
            {
              j = j * 10 + buf[i] - 48;
            }
          }
          return false;
      }
      break;
  }
  return false;
}

void Arduino_putchar(uint8_t c)
{
  if(c == '\033')
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
  else
  {
    uint8_t bg = 3;
    uint8_t fg = 2;
    if((vtflags & 0x2) == 0x2)
    {
      // inverse video
      bg = 2;
      fg = 3;
    }
    canvas->drawChar((vtcurs[0]-1)*vtcharsz[0], (vtcurs[1])*vtcharsz[1], c, vtcurs[bg], vtcurs[fg], VTSCALING);
    canvas->pushSprite(0,0);
    vtcurs[0] ++;
    if(c == '\r' || c == '\n')
    {
      vtcurs[0] = 1;
      vtcurs[1] ++;
    }
  }
}

char Arduino_getchar()
{
  while(!M5Cardputer.Keyboard.isChange() && !M5Cardputer.Keyboard.isPressed()) {
    yield();
    M5Cardputer.update();
  };
  Keyboard_Class::KeysState s = M5Cardputer.Keyboard.keysState();

  if(s.enter)
  {
    return '\n';
  }
  else if(s.tab)
  {
    return '\t';
  }
  // TODO backspace, cursor keys
  char ret = s.word[0];
  s.word.clear();
  return ret;
}

int inc( uint32_t timeout = 0, bool dummy = false )
{
   uint32_t timer = millis();
   // while(!Serial.available() && ((timeout == 0) || (timeout > 0 && (timer + timeout*100 > millis())))){yield();};
   while(!M5Cardputer.Keyboard.isChange() && !M5Cardputer.Keyboard.isPressed() && ((timeout == 0) || (timeout > 0 && (timer + timeout*100 > millis())))) {yield();};
   if(timeout > 0 && ((timer + timeout*100) <= millis()))
    return -1;

   Keyboard_Class::KeysState s = M5Cardputer.Keyboard.keysState();
   /* int c = Serial.read();
   if ( c == -1 )
   {
      fatal("acursesio inc: error in Serial.read!");
   }*/
   uint8_t c = s.word[0];
   if(c != 127) // is this a backspace key? will print it later
     Arduino_putchar(c);
     // Serial.write(c);
   if(c == '\r')
     Arduino_putchar('\n');
     // Serial.write('\n');
   return c;
}

static int uninc( int c )
{
    // not supported right now
   //return ungetc( c, stdin );
}

static int outc( int c )
{
   // Serial.print(String((char) c));
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
   attrset(themes[theme].text_attr);

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

/*
    setscrreg(status_size, screen_rows-1);
*/

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
         attrset(themes[theme].text_attr);
   }

   if ( attribute & REVERSE )
   {
      // this is the status part of the window
      attrset(themes[theme].status_attr);
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
   display_char( '\r' );
   display_char( '\n');

   get_cursor_position( &row, &col );

   if ( row < screen_rows )
   {
     display_char( '\r' );
     display_char( '\n');
   }
   else
   {
      setscrreg( status_size, screen_rows - 1 );
      display_char( '\r' );
      //display_char( '\n');
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
   // while ( ( c = read_char(  ) ) != '\n' )
   while ( ( c = read_char(timeout) ) != '\r' ) // use for Arduino line feed
   {
      if(c == 127) // backspace pressed?
      {
        if(*read_size > 0)
        {
          buffer[(*read_size)] = '\0';
          (*read_size)--;
          Serial.print((char) c); // OK to backspace, characters still on the left side
        }
      }
      else if ( *read_size < buflen )
         buffer[( *read_size )++] = c;
   }
   text_col = 0;
   //Serial.print("");
   //delay(1000);
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
            //if ( command[n] == '\n' )
            if ( command[n] == '\r' )
               break;
         }
         command[n] = '\0';
         /* If line was too long, flush input to the end of it.  */
         if ( n == COMMAND_LEN )
            //while ( inc(timeout) != '\n' )
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
         if ( c == 4 )
         {
            reset_screen(  );
            exit( 0 );
         }                      /* CTRL-D (EOF) */
      }
      while ( !( c == 10 || c == 13 || c == 8 ) && ( c < 32 || c > 127 ) );
   }
   else if ( mode == EXTENDED )
   {                            /* also pass ESC character back for editor */
      do
      {
         c = Arduino_getchar();
         if ( c == 4 )
         {
            reset_screen(  );
            exit( 0 );
         }                      /* CTRL-D (EOF) */
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
