// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

static void consputc(int);

static int panicked = 0;

static struct {
	struct spinlock lock;
	int locking;
} cons;

static int foreground[10] =
{
	0x0000, 0x0400, 0x0200, 0x0600, 0x0100, 0x0500, 0x0300, 0x0700, 0x0700, 0x0700
};

static int background[10] =
{
	0x0000, 0x4000, 0x2000, 0x6000, 0x1000, 0x5000, 0x3000, 0x7000, 0x0000, 0x0000
};

static void
printint(int xx, int base, int sign)
{
	static char digits[] = "0123456789abcdef";
	char buf[16];
	int i;
	uint x;

	if(sign && (sign = xx < 0))
		x = -xx;
	else
		x = xx;

	i = 0;
	do{
		buf[i++] = digits[x % base];
	}while((x /= base) != 0);

	if(sign)
		buf[i++] = '-';

	while(--i >= 0)
		consputc(buf[i]);
}

// Print to the console. only understands %d, %x, %p, %s.
void
cprintf(char *fmt, ...)
{
	int i, c, locking;
	uint *argp;
	char *s;

	locking = cons.locking;
	if(locking)
		acquire(&cons.lock);

	if (fmt == 0)
		panic("null fmt");

	argp = (uint*)(void*)(&fmt + 1);
	for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
		if(c != '%'){
			consputc(c);
			continue;
		}
		c = fmt[++i] & 0xff;
		if(c == 0)
			break;
		switch(c){
		case 'd':
			printint(*argp++, 10, 1);
			break;
		case 'x':
		case 'p':
			printint(*argp++, 16, 0);
			break;
		case 's':
			if((s = (char*)*argp++) == 0)
				s = "(null)";
			for(; *s; s++)
				consputc(*s);
			break;
		case '%':
			consputc('%');
			break;
		default:
			// Print unknown % sequence to draw attention.
			consputc('%');
			consputc(c);
			break;
		}
	}

	if(locking)
		release(&cons.lock);
}

void
panic(char *s)
{
	int i;
	uint pcs[10];

	cli();
	cons.locking = 0;
	// use lapiccpunum so that we can call panic from mycpu()
	cprintf("lapicid %d: panic: ", lapicid());
	cprintf(s);
	cprintf("\n");
	getcallerpcs(&s, pcs);
	for(i=0; i<10; i++)
		cprintf(" %p", pcs[i]);
	panicked = 1; // freeze other CPU
	for(;;)
		;
}

#define BACKSPACE 0x100
#define CRTPORT 0x3d4
static ushort *crt = (ushort*)P2V(0xb8000);  // CGA memory

int atoi(char* str) 
{ 
    int res = 0; // Initialize result 
  
    // Iterate through all characters of input string and 
    // update result 
    for (int i = 0; str[i] != '\0'; ++i) 
        res = res * 10 + str[i] - '0'; 
  
    // return result. 
    return res; 
} 

static void
cgaputc(int c)
{
	int pos;

	static int escActive = 0;
	static int readingFormat = 0;
	static int formatActive = 0;
	static int formatIdx = 0;
	static int valueIdx = 0;

	static char format[3] = "";
	static int values[2] = { -1, -1 };

	if (c == '\033') {
		escActive = 1;
		readingFormat = 0;
		formatActive = 0;
	}

	outb(CRTPORT, 14);
	pos = inb(CRTPORT+1) << 8;
	outb(CRTPORT, 15);
	pos |= inb(CRTPORT+1);

	if(c == '\n')
		pos += 80 - pos%80;
	else if(c == BACKSPACE) {
		if(pos > 0) --pos;
	} else if (c == '\033' || (escActive && c == '[')) {
		readingFormat = 1;
		formatIdx = 0;
		valueIdx = 0;
		format[0] = '0';
		format[1] = '0';
		format[2] = '\0';
		values[0] = -1;
		values[1] = -1;
	}
	else if (escActive && c == 'm') {
		formatActive = 1;
		escActive = 0;
		readingFormat = 0;

		int val = atoi(format);
		values[valueIdx] = val;

		valueIdx = 0;
	}
	else if (readingFormat) {
		if (c == ';') {
			int val = atoi(format);
			values[valueIdx++] = val;

			format[0] = '0';
			format[1] = '0';
			format[2] = '\0';
			formatIdx = 0;
		} else {
			format[formatIdx++] = c;
		}
	}
	else if (formatActive) {
		int i;

		// Check if there are 2 ANSI commands:
		int n = values[1] != -1 ? 2 : 1;

		crt[pos] = (c&0xff);
		for (i = 0; i < n; i ++) {
			int val = values[i];

			if (val >= 40 && val <= 49) {
				crt[pos] = crt[pos] | background[val - 40];
			}
			else if (val >= 30 && val <= 39) {
				crt[pos] = crt[pos] | foreground[val - 30];
			} 
			else if (val == 0) {
				crt[pos] = crt[pos] | 0x0700;

				// Resetting format:
				values[0] = -1;
				values[1] = -1;

				readingFormat = 0;
				formatActive = 0;

				break;
			}
			else if (val == -1)
				crt[pos] = crt[pos] | 0x0500;
		}

		pos ++;
	} else {
		crt[pos++] = (c&0xff) | 0x0700;
	}

	if(pos < 0 || pos > 25*80)
		panic("pos under/overflow");

	if((pos/80) >= 24){  // Scroll up.
		memmove(crt, crt+80, sizeof(crt[0])*23*80);
		pos -= 80;
		memset(crt+pos, 0, sizeof(crt[0])*(24*80 - pos));
	}

	outb(CRTPORT, 14);
	outb(CRTPORT+1, pos>>8);
	outb(CRTPORT, 15);
	outb(CRTPORT+1, pos);
	crt[pos] = ' ' | 0x0700;
}

void
consputc(int c)
{
	if(panicked){
		cli();
		for(;;)
			;
	}

	if(c == BACKSPACE){
		uartputc('\b'); uartputc(' '); uartputc('\b');
	} else
		uartputc(c);
	cgaputc(c);
}

#define INPUT_BUF 128
struct Input {
	char buf[INPUT_BUF];
	uint r;  // Read index
	uint w;  // Write index
	uint e;  // Edit index
};

struct Terminal {
	char buf[80*25*2];
	int count;

	struct Input input;

} typedef Terminal;

#define C(x)  ((x)-'@')  // Control-x
#define A(x)  ((x) - 27) // ALT-x

static int activeTty = 1;

static Terminal terminals[6];

void fillscreen(int id) {
	int i;

	// Clearing screen:
	for (i = 0; i < 24 * 80; i ++)
		crt[i] = 0;

	// Restoring data:
	for (i = 0; i < terminals[id - 1].count; i ++) {
		consputc(terminals[id - 1].buf[i]);
	}

	// Returning input: 
	for (i = terminals[id - 1].input.w; i < terminals[id - 1].input.e; i ++) {
		consputc(terminals[id - 1].input.buf[i] & 0xff);
	}
}

void
consoleintr(int (*getc)(void))
{
	int c, doprocdump = 0;

	acquire(&cons.lock);
	while((c = getc()) >= 0){
		switch(c){
		case A('1'):
			fillscreen(activeTty = 1);
			break;
		case A('2'):
			fillscreen(activeTty = 2);
			break;
		case A('3'):
			fillscreen(activeTty = 3);
			break;
		case A('4'):
			fillscreen(activeTty = 4);
			break;
		case A('5'):
			fillscreen(activeTty = 5);
			break;
		case A('6'):
			fillscreen(activeTty = 6);
			break;
		case C('P'):  // Process listing.
			// procdump() locks cons.lock indirectly; invoke later
			doprocdump = 1;
			break;
		case C('U'):  // Kill line.
			while(terminals[activeTty - 1].input.e != terminals[activeTty - 1].input.w &&
			      terminals[activeTty - 1].input.buf[(terminals[activeTty - 1].input.e-1) % INPUT_BUF] != '\n'){
				terminals[activeTty - 1].input.e--;
				consputc(BACKSPACE);
			}
			break;
		case C('H'): case '\x7f':  // Backspace
			if(terminals[activeTty - 1].input.e != terminals[activeTty - 1].input.w){
				terminals[activeTty - 1].input.e--;
				consputc(BACKSPACE);
			}
			break;
		default:
			if(c != 0 && terminals[activeTty - 1].input.e-terminals[activeTty - 1].input.r < INPUT_BUF){
				c = (c == '\r') ? '\n' : c;
				terminals[activeTty - 1].input.buf[terminals[activeTty - 1].input.e++ % INPUT_BUF] = c;
				consputc(c);
				if(c == '\n' || c == C('D') || terminals[activeTty - 1].input.e == terminals[activeTty - 1].input.r+INPUT_BUF){
					terminals[activeTty - 1].input.w = terminals[activeTty - 1].input.e;
					wakeup(&terminals[activeTty - 1].input.r);
				}
			}
			break;
		}
	}
	release(&cons.lock);
	if(doprocdump) {
		procdump();  // now call procdump() wo. cons.lock held
	}
}

int
consoleread(struct inode *ip, char *dst, int n)
{
	uint target;
	int c;

	iunlock(ip);
	target = n;
	acquire(&cons.lock);
	while(n > 0){
		while(terminals[ip->minor - 1].input.r == terminals[ip->minor - 1].input.w){
			if(myproc()->killed){
				release(&cons.lock);
				ilock(ip);
				return -1;
			}
			sleep(&terminals[ip->minor - 1].input.r, &cons.lock);
		}
		c = terminals[ip->minor - 1].input.buf[terminals[ip->minor - 1].input.r++ % INPUT_BUF];
		if(c == C('D')){  // EOF
			if(n < target){
				// Save ^D for next time, to make sure
				// caller gets a 0-byte result.
				terminals[ip->minor - 1].input.r--;
			}
			break;
		}
		*dst++ = c;
		--n;
		if(c == '\n')
			break;
	}
	release(&cons.lock);
	ilock(ip);

	return target - n;
}

int
consolewrite(struct inode *ip, char *buf, int n)
{
	int i;

	for(i = 0; i < n; i++)
		terminals[ip->minor - 1].buf[terminals[ip->minor - 1].count++] = buf[i] & 0xff;

	if (ip -> minor != activeTty)
	{
		return n;
	}

	iunlock(ip);
	acquire(&cons.lock);
	for(i = 0; i < n; i++)
		consputc(buf[i] & 0xff);
	release(&cons.lock);
	ilock(ip);

	return n;
}

void
consoleinit(void)
{
	initlock(&cons.lock, "console");

	devsw[CONSOLE].write = consolewrite;
	devsw[CONSOLE].read = consoleread;
	cons.locking = 1;

	ioapicenable(IRQ_KBD, 0);
}