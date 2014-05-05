#if (MACHINE == X68000)
/*
 * The keyboard driver for the Sharp X68000.
 *
 * Modified the original 1.1 code to support more
 * function keys and provide a robust way of determing
 * which keys are function keys. 
 * Note: defining KEYBOARD as PC (in <minix/config.h>
 *	 will result in key definitions
 *       that are the same as for Minix 1.1 with the
 *       exception that <Shift><ClrHome> works and
 *       <Insert> generates ESC [I
 *                                    S.Poole 21.1.89 
 */
#include "kernel.h"
#include <minix/com.h>
#include <sgtty.h>

#include "x6addr.h"
#include "x6mfp.h"
#include "x6io.h"

#include "tty.h"

#define THRESHOLD                 20	/* # chars to accumulate before msg */

#define		SPCE	0x35	/* patched by Seisei Yamaguchi */
#define		SHFT	0x70
#define		CTRL	0x71
#define		XF1	0x55
#define		XF2	0x56
#define		XF3	0x57
#define		XF4	0x58
#define		XF5	0x59
#define		CAPS	0x5d
#define		INS	0x5e
#define		ROLLUP	0x38
#define		ROLLDOWN	0x39
#define		KIGOUNYU	0x52
#define		TOUROKU		0x53
#define		OPT1	0x72
#define		OPT2	0x73
#define		COPY	0x62
#define		HIRAGANA	0x5f
#define		ZENKAKU		0x60
#define		KANA		0x5a
#define		ROUMAJI		0x5b
#define		KOUDONYU	0x5c

#define		PF1	0x63
#define		PF2	0x64
#define		PF3	0x65
#define		PF4	0x66
#define		PF5	0x67
#define		PF6	0x68
#define		PF7	0x69
#define		PF8	0x6a
#define		PF9	0x6b
#define		PF10	0x6c

static void kbdkey();
static int national();
static void kbdkeypad();
static void kbdarrow();
static void kbdpf();
PRIVATE void kbd_send_comm();

/*
 * Translation from keyboard codes into internal (ASCII like) codes
 * These tables represents a US keyboard, so MINIX.IMG as found on
 * the MINIX-ST BOOT floppy works well in the US.
 * The TOS program FIXKEYS.PRG is supplied to replace the keyboard
 * tables compiled into MINIX.IMG on the BOOT floppy, by the proper
 * national version of the keyboard tables used by TOS at the time
 * of running FIXKEYS.PRG.
 *
 * Since these three tables are not fully sufficient to deal with
 * all the differences, some special code is added in this driver
 * to cope with the national combinations with the ALT key. See
 * the routine national() below.
 *
 * Currently there are no provisions for non-ASCII characters.
 * Only the problem of entering the ASCII character set from a
 * variety of different keyboard layout is solved.
 * Non-ASCII characters cause three kinds of problems:
 *  - character codes above 127 loose their 8th bit in the TTY driver,
 *    if not in RAW mode
 *  - application programs do not know what to do with non-ASCII,
 *    and certainly not with the character codes Atari did assign
 *  - only ASCII characters can be displayed on the screen, since the
 *    font tables used by MINIX-ST have only 128 entries.
 */
 
#include "keymap.h"

/*
 * Flag for keypad mode
 * this can be set by code in stvdu.c
 */
#if (KEYBOARD == IBM_PC)
PUBLIC int keypad = FALSE;
#else
PUBLIC int keypad = TRUE;
#endif
/*
 * Flag for arrow key application mode
 * this can be set by code in stvdu.c
 */
PUBLIC int app_mode = FALSE;

/* 
 * Map function keys to an index into the 
 * table of function key values 
 */
PRIVATE unsigned char f_keys[] = {
/*00*/	   0,   0,   0,   0,   0,   0,   0,   0,
/*08*/	   0,   0,   0,   0,   0,   0,   0,   0,
/*10*/	   0,   0,   0,   0,   0,   0,   0,   0,
/*18*/	   0,   0,   0,   0,   0,   0,   0,   0,
/*20*/	   0,   0,   0,   0,   0,   0,   0,   0,
/*28*/	   0,   0,   0,   0,   0,   0,   0,   0,
/*30*/	   0,   0,   0,   0,   0,   0,  17,   0,
/*38*/	   0,   0,  12,  13,  16,  15,  14,  17,
#ifdef KEYPAD
/*40*/	  21,  22,  26,  23,  24,  25,  30,  27,
/*48*/	  28,  29,   0,  31,  32,  33,  36,  34,
/*50*/	   0,  35,   0,   0,  11,   0,   0,   0,
#else
/*40*/	   0,   0,   0,   0,   0,   0,   0,   0,
/*48*/	   0,   0,   0,   0,   0,   0,   0,   0,
/*50*/	   0,   0,   0,   0,  11,   0,   0,   0,
#endif
/*58*/	   0,   0,   0,   0,   0,   0,  18,   0,
/*60*/	   0,   0,   0,   1,   2,   3,   4,   5,
/*68*/	   6,   7,   8,   9,  10,   0,   0,   0,
/*70*/	   0,   0,   0,   0,   0,   0,   0,   0,
/*78*/	   0,   0,   0,   0,   0,   0,   0,   0
};

/*
 * Numbering of the function keys, this scheme was chosen
 * so that it easy to determine which function to call to actually
 * generate the string.
 *
 * Note: the <Help> and <Undo> keys are considered to be function
 *       keys 11 and 12.
 *
 * F-keys:    -----------------------------------------
 *            | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10|
 *            -----------------------------------------
 *
 * Arrow-Keys:    -------------
 *                |  12 |  11 |
 *                -------------
 *                | 18| 16| 17|
 *                -------------
 *                | 13| 14| 15|
 *                -------------
 *
 * Keypad:    -----------------
 *            | 19| 20| 21| 22|
 *            -----------------
 *            | 23| 24| 25| 26|
 *            -----------------
 *            | 27| 28| 29| 30|
 *            -----------------
 *            | 31| 32| 33|   |
 *            ------------- 36|
 *            |   34  | 35|   |
 *            -----------------
 */     

/* 
 * There is no problem with  expanding this struct to
 * have a field for <Control> and <Alternate> (and combinations
 * of them),  but who needs > 152 function keys? 
 */
struct fkey {
	char norm, shift;
};

PRIVATE struct fkey ftbl[] = {
#if (KEYBOARD == IBM_PC)
	/* 1  = F1      */ {'P',   0},
	/* 2  = F2      */ {'Q',   0},
	/* 3  = F3      */ {'R',   0},
	/* 4  = F4      */ {'S',   0},
	/* 5  = F5      */ {'T',   0},
	/* 6  = F6      */ {'U',   0},
	/* 7  = F7      */ {'V',   0},
	/* 8  = F8      */ {'W',   0},
	/* 9  = F9      */ {'X',   0},
	/* 10 = F10     */ {'Y',   0},
	/* 11 = Undo    */ {  0,   0},
	/* 12 = Help    */ {  0,   0},
#else
/* 
 * So that we can produce VT200 style function-key codes, 
 * the values here are integer values that are converted
 * to a string in kbdpf(). 
 *
 * The assignment of numbers to keys is rather chaotic,
 * but at least all the VT200 keys are there.
 */
	/* ST key       */		/* VT200 key	*/
	/* 1  = F1      */ {  1,  21},	/* Find	  F10	*/
	/* 2  = F2      */ {  2,  23},	/* Insert F11	*/
	/* 3  = F3      */ {  3,  24},	/* Remove F12	*/
	/* 4  = F4      */ {  4,  25},	/* Select F13	*/
	/* 5  = F5      */ {  5,  26},	/* Prev.  F14	*/
	/* 6  = F6      */ {  6,  31},	/* Next	  F17	*/
	/* 7  = F7      */ { 17,  32},	/* F6	  F18	*/
	/* 8  = F8      */ { 18,  33},	/* F7	  F19	*/
	/* 9  = F9      */ { 19,  34},	/* F8	  F20	*/
	/* 10 = F10     */ { 20,  35},	/* F9		*/
	/* 11 = Undo    */ { 36,  37},	/*		*/
	/* 12 = Help    */ { 28,  29},	/* Help	  Do	*/
#endif
/* 
 * The following codes are more conventional 
 */
	/* 13 = Left    */ {'D',   0},
	/* 14 = Down    */ {'B',   0},
	/* 15 = Right   */ {'C',   0},
	/* 16 = Up      */ {'A',   0},
	/* 17 = ClrHome */ {'H', 'J'},
	/* 18 = Insert  */ {'I',   0},
/* 
 * Keypad starts here 
 */
	/* 19 = (       */ {'P',   0},
	/* 20 = )       */ {'Q',   0},
	/* 21 = /       */ {'R',   0},
	/* 22 = *       */ {'S',   0},
	/* 23 = 7       */ {'w',   0},
	/* 24 = 8       */ {'x',   0},
	/* 25 = 9       */ {'y',   0},
	/* 26 = -       */ {'m',   0},
	/* 27 = 4       */ {'t',   0},
	/* 28 = 5       */ {'u',   0},
	/* 29 = 6       */ {'v',   0},
	/* 30 = +       */ {'l',   0},
	/* 31 = 1       */ {'q',   0},
	/* 32 = 2       */ {'r',   0},
	/* 33 = 3       */ {'s',   0},
	/* 34 = 0       */ {'p',   0},
	/* 35 = .       */ {'n',   0},
	/* 36 = Enter   */ {'M',   0}
};

PRIVATE message	kbdmes;		/* message used for console input chars */
PRIVATE int	repeatkey;	/* character to repeat */
PRIVATE int	repeattic;	/* time to next repeat */
PRIVATE char	key_led;	/* keyboard LED */

/*===========================================================================*
 *				kbdint					     *
 *===========================================================================*/
PUBLIC void kbdint()
{
  register code, make, k;
  int s = lock();

  k = tty_buf_count(tty_driver_buf);
  /*
   * There may be multiple keys available. Read them all.
   */
  do {
	MFP->mf_isra &= ~IA_RRDY; /* clear irq bit. */
	code = MFP->mf_udr;
/*	printf("kbd: got %x\n", code & 0xFF); */ 
	/*
	 * The ST's keyboard interrupts twice per key,
	 * once when depressed, once when released.
	 * Filter out the latter, ignoring all but
	 * the shift-type keys.
         */
    if ((0xff & code) == 0xFF){
      kbd_send_comm(key_led);
    } else {
	make = code & 0x80 ? 0 : 1;	/* 1=depressed, 0=released */
	code &= 0x7F;
	switch (code) {
	case SPCE:	/* space key */
		code = SHFT;
		shift1 = make; continue;	/*patched by Seisei Yamaguchi 019970113*/
	case SHFT:	/* shift key on left */
		code = CTRL;
		control = make; continue;
/*		shift1 = make; continue;	patched by Seisei Yamaguchi 019970113*/
#if 0
	case 0x36:	/* shift key on right */
#endif
/*		shift2 = make; continue;*/
	case CTRL:	/* control */
		code = SPCE; break;
/*		control = make; continue;	patched by Seisei Yamaguchi 019970113*/
	case CAPS:	/* caps lock */
		if (make){
			capslock ^= 1;
			kbd_send_comm(key_led ^= 0x08);
		}
		continue;
	case XF2:	/* meta key */
	    meta = make; continue;
	case XF1:	/* alt key */
	case OPT1:
/*
	case XF3:
	case XF4:
	case XF5:
	case ROLLUP:
	case ROLLDOWN:
	case OPT2:
	case KIGOUNYU:
	case TOUROKU:
	case COPY:
	case HIRAGANA:
	case ZENKAKU:
	case KANA:
	case ROUMAJI:
	case KOUDONYU:
*/
		alt = make; continue;
	}
	if (make == 0) {
		repeattic = 0;
		continue;
	}
	repeatkey = code;
	repeattic = 24;	/* delay: 24 * 16 msec == 0.4 sec */
	kbdkey(code);
    }
  } while (MFP->mf_isra & IA_RRDY);
#ifdef KB_INT_BUF
  if (tty_buf_count(tty_driver_buf) < THRESHOLD) {
	/* Don't send message.  Just accumulate.  Let clock do it. */
	INT_CTL_ENABLE;
	flush_flag++;
  } else rs_flush();			/* send TTY task a message */
#else
  rs_flush();
#endif

  restore(s);
}

PUBLIC void kbeint()
{
  register code;

  MFP->mf_isra &= ~IA_RERR; /* clear irq bit. */
  code = MFP->mf_udr;
  return;
}

/*===========================================================================*
 *				kbdkey					     *
 *===========================================================================*/
PRIVATE void kbdkey(code)
int code;
{
  register int c,f,fc;

  f = f_keys[code];
  if (shift1 || shift2)
	c = keyshft[code];
  else if (capslock)
	c = keycaps[code];
  else
	c = keynorm[code];
  if ((!f) && (c == 0)) {
	return;
  }
  /* 
   * check if the key is not a function key 
   */
  if (!f) {
	if (alt)
		c = national(code, c | 0x80);
      if(meta)
	  c = c | 0x80;
	if (control) {
		if (c == 0xFF) reboot(); /* CTRL-ALT-DEL */
		c &= 0x1F;
	}
	/* Check to see if character is XOFF, to stop output. */
	if (
		(tty_struct[fg_console].tty_mode & (RAW | CBREAK)) == 0
		&&
		tty_struct[fg_console].tty_xoff == c
	) {
		tty_struct[fg_console].tty_inhibited = STOPPED;
		return;
	}
	kbdput(c, fg_console);
	return;
  }
  if (control && alt && code >= PF1 && code <= PF10) {
	/*
	 * some special sequences
	 */
	kbdput(code - PF1 + 1, OPERATOR);
	return;
  }
  f--; /* correct for index into ftbl */
  if (shift1 || shift2)
	fc = ftbl[f].shift;
  else
	fc = ftbl[f].norm;
  /* 
   * f naturally has to be >= 0 for this piece 
   * of code to work 
   */
  if (fc) {
	if (f < 12)
#if (KEYBOARD == IBM_PC)
		kbdkeypad(fc);
#else
		kbdpf(fc);
#endif
	else if (f < 18) {
#if (KEYBOARD == VT100)
		if (app_mode)
			kbdkeypad(fc);
		else
#endif
			kbdarrow(fc);
	}
	else if (keypad)
		/*
		 * keypad should be set by stvdu
		 */
		kbdkeypad(fc);
	else
		kbdput( c, fg_console );
  }
}

/*
 * Cope with national ALT and SHIFT-ALT combinations.
 * Currently only for Germany, France and Spain.
 * Extension are straightforward.
 */
PRIVATE int national(code, c)
int code, c;
{
  static char germany[] = {
	0x1A, '@', '\\',
	0x27, '[', '{',
	0x28, ']', '}',
	0
  };
  static char france[] = {
	0x1A, '[', '{',
	0x1B, ']', '}',
	0x28, '\\', 0,
	0x2B, '@', '~',
	0
  };
  static char spain[] = {
	0x1A, '[', '{',
	0x1B, ']', '}',
	0x28, 0x81, 0x9A,	/* lower/upper u-umlaut */
	0x2B, '#', '@',
	0
  };
  register char *p;

  /*
   * Distinguish the right keyboard version somehow
   */
  if (keynorm[0x1A] == 0x81)	/* lower u-umlaut */
	p = germany;
  else if (keynorm[0x1A] == '^')
	p = france;
  else if (keynorm[0x1A] == '\'')
	p = spain;
  else
	return(c);
  /*
   * See if it is one of the keys that need special attention
   */
  while (*p != code) {
	if (*p == 0)
		return(c);
	p += 3;
  }
  /*
   * It is indeed special. Distinguish between upper and lower case.
   */
  p++;
  if (shift1 || shift2)
	p++;
  if (*p == 0)
	return(c);
  return(*p);
}

/*
 * Store the character in memory
 */
PUBLIC void kbdput(c, line)
int c;
int line;
{
  register int k;

  /* Store the character in memory so the task can get at it later.
   * tty_driver_buf[0] is the current count, and tty_driver_buf[2] is the
   * maximum allowed to be stored.
   */
  if ( (k = tty_buf_count(tty_driver_buf)) >= tty_buf_max(tty_driver_buf)) 
	/*
	 * Too many characters have been buffered.
	 * Discard excess.
	 */
	return;
  /* There is room to store this character; do it. */
  k <<= 1;				/* each entry uses two bytes */
  tty_driver_buf[k+4] = c;		/* store the char code */
  tty_driver_buf[k+5] = line;		/* which line it came from */
  tty_buf_count(tty_driver_buf)++;	/* increment counter */
}

/*
 * Input escape sequence for keypad keys
 */
PRIVATE void kbdkeypad(c)
register int c;
{
	kbdput('\033', fg_console );
	kbdput('O', fg_console );
	kbdput(c, fg_console );
}

/*
 * Input escape sequence for arrow keys
 */
PRIVATE void kbdarrow(c)
register int c;
{
	kbdput('\033', fg_console );
	kbdput('[', fg_console );
	kbdput(c, fg_console );
}

#if (KEYBOARD == VT100)
/*
 * Input escape sequence for function keys
 */
PRIVATE void kbdpf(c)
register int c;
{
	register int t;

	kbdput('\033', fg_console );
	kbdput('[', fg_console );
	/* this stuff is not robust */
	if ((t = c / 10) > 0)
	  kbdput(t + '0', fg_console );
	kbdput((c % 10) + '0', fg_console );
	kbdput('~', fg_console );
}
#endif

/*
 * Input ANSI escape sequence
 */

PRIVATE kbdansi(c)
{
	kbdput('\033', fg_console );
	kbdput('[', fg_console );
	kbdput(c, fg_console );
}

/*===========================================================================*
 *				kb_timer				     *
 *===========================================================================*/
PUBLIC void kb_timer()
{
  register int k, s;

  s = lock();
  if (repeattic == 0) {
	restore(s);
	return;
  }
  if (--repeattic != 0) {
	restore(s);
	return;
  }
  k = tty_buf_count(tty_driver_buf);
  kbdkey(repeatkey);
  if (k != tty_buf_count(tty_driver_buf))
  {
    if (tty_buf_count(tty_driver_buf) < THRESHOLD) {
	/* Don't send message.  Just accumulate.  Let clock do it. */
	INT_CTL_ENABLE;
	flush_flag++;
    }
    else rs_flush();			/* send TTY task a message */
  }
  repeattic = 4;	/* repeat: 4 * 16 msec == 0.066 sec */
  restore(s);
}

/*===========================================================================*
 *				kbdinit				     	     *
 *===========================================================================*/
PUBLIC void kbdinit()
{
  unsigned s;

  s = lock();
#if 0
  MFP->mf_iera &= ~IA_TIMB;
  MFP->mf_iera &= ~IA_RERR;
  MFP->mf_iera &= ~IA_RRDY;
  MFP->mf_iera &= ~IA_TRDY;
  MFP->mf_iera &= ~IA_TERR;
/*
  MFP->mf_tcdcr = T_STOP;
*/
  MFP->mf_rsr = 0;
  MFP->mf_tsr = 0;
  MFP->mf_tbdr= 0x0d;	/* set delay count */
/*
  MFP->mf_tcdcr = T_Q004;
*/
  MFP->mf_ucr = U_Q16|U_ST1|U_D8;
  MFP->mf_rsr = R_ENA;
  MFP->mf_tsr = T_ENA;
  MFP->mf_iera |= IA_RRDY;
  MFP->mf_iera |= IA_TRDY;
#endif

  kbd_send_comm(key_led = 0xFF);

  restore(s);
}

PRIVATE void kbd_send_comm(command)
char	command;
{
  /* already lock()ed */
  while (!((char)(MFP->mf_tsr) & (char)(T_EMPTY)))
    ;
  MFP->mf_udr = command;
}
#endif
