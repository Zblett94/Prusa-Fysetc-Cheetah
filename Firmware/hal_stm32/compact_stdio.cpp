//! @file
//! @brief Compact printf family and strtod for the 256 KiB flash of the STM32F446RC (COMPACT_STDIO)
//!
//! newlib's printf with floating point support and its strtod work in double precision
//! with arbitrary precision helpers; together with the software double arithmetic they
//! take about 12 KiB. The firmware only needs what avr-libc offered: the conversions
//! d i u x X c s f n %, the flags - 0 + space, width and precision (also *), and the
//! length modifiers hh h l ll z. Floating point values are formatted and parsed in
//! single precision, which is the precision of all the values the firmware prints.
//!
//! The output functions go through stdio (fputs) so that their output stays in order
//! with puts()/putchar() on stdout.
#ifdef COMPACT_STDIO

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace {

struct Sink {
	virtual void put(char c) = 0;
	int count = 0;
};

//! into a buffer of a given size (snprintf semantics)
struct BufSink : Sink {
	char *buf;
	size_t size;
	BufSink(char *b, size_t n) : buf(b), size(n) {}
	void put(char c) override {
		if ((size_t)count + 1 < size) buf[count] = c;
		count++;
	}
	void finish() {
		if (size) buf[(size_t)count < size ? count : size - 1] = 0;
	}
};

//! into a stdio stream, in chunks
struct FileSink : Sink {
	FILE *f;
	char chunk[64];
	uint8_t n = 0;
	explicit FileSink(FILE *file) : f(file) {}
	void put(char c) override {
		chunk[n++] = c;
		count++;
		if (n == sizeof(chunk) - 1) flush();
	}
	void flush() {
		chunk[n] = 0;
		if (n) fputs(chunk, f);
		n = 0;
	}
};

enum : uint8_t { F_LEFT = 1, F_ZERO = 2, F_PLUS = 4, F_SPACE = 8 };

//! digits, sign and padding of a number given as text (without sign)
void emit_number(Sink &s, const char *digits, int len, bool neg, uint8_t flags, int width) {
	char sign = neg ? '-' : (flags & F_PLUS) ? '+' : (flags & F_SPACE) ? ' ' : 0;
	int total = len + (sign ? 1 : 0);
	int pad = width > total ? width - total : 0;
	if (!(flags & F_LEFT) && !(flags & F_ZERO))
		while (pad-- > 0) s.put(' ');
	if (sign) s.put(sign);
	if (!(flags & F_LEFT) && (flags & F_ZERO))
		while (pad-- > 0) s.put('0');
	for (int i = 0; i < len; i++) s.put(digits[i]);
	if (flags & F_LEFT)
		while (pad-- > 0) s.put(' ');
}

int utoa_rev(uint64_t v, unsigned base, bool upper, char *out) {
	const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	int n = 0;
	do {
		out[n++] = dig[v % base];
		v /= base;
	} while (v);
	return n;
}

void emit_integer(Sink &s, uint64_t v, bool neg, unsigned base, bool upper, uint8_t flags, int width, int prec) {
	char rev[24], txt[48];
	int n = (v == 0 && prec == 0) ? 0 : utoa_rev(v, base, upper, rev);
	int len = 0;
	for (int z = prec - n; z > 0 && len < 24; z--) txt[len++] = '0';
	while (n) txt[len++] = rev[--n];
	if (prec >= 0) flags &= ~F_ZERO;
	emit_number(s, txt, len, neg, flags, width);
}

void emit_float(Sink &s, float x, uint8_t flags, int width, int prec) {
	if (prec < 0) prec = 6;
	if (prec > 9) prec = 9;
	bool neg = x < 0 || (x == 0 && (1.f / x) < 0);
	float v = neg ? -x : x;
	if (v != v) { emit_number(s, "nan", 3, false, flags & ~F_ZERO, width); return; }
	if (v > 3.4e38f) { emit_number(s, "inf", 3, neg, flags & ~F_ZERO, width); return; }
	static const uint32_t p10[10] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};
	// integer part: below 2^32 by the FPU; above from the float bits (an exact integer
	// there; (uint64_t)v would pull in libgcc's double based __aeabi_f2ulz)
	uint64_t ip;
	float frac = 0.f;
	if (v < 4294967296.f) {
		ip = (uint32_t)v;
		frac = v - (float)(uint32_t)ip;      // exact
	} else {
		uint32_t b;
		memcpy(&b, &v, sizeof(b));
		const int e = (int)((b >> 23) & 0xFF) - 150; // v = mantissa * 2^e, e >= 9 here
		ip = (e > 40) ? ~(uint64_t)0 : ((uint64_t)((b & 0x7FFFFFUL) | 0x800000UL) << e);
	}
	uint32_t fp = (uint32_t)(frac * (float)p10[prec] + 0.5f);
	if (fp >= p10[prec]) { fp -= p10[prec]; ip++; }
	char rev[24], txt[48];
	int len = 0, n = utoa_rev(ip, 10, false, rev);
	while (n) txt[len++] = rev[--n];
	if (prec) {
		txt[len++] = '.';
		for (int i = prec - 1; i >= 0; i--) txt[len + i] = '0' + fp % 10, fp /= 10;
		len += prec;
	}
	emit_number(s, txt, len, neg, flags, width);
}

int format(Sink &s, const char *f, va_list ap) {
	for (; *f; f++) {
		if (*f != '%') { s.put(*f); continue; }
		f++;
		uint8_t flags = 0;
		for (;; f++) {
			if (*f == '-') flags |= F_LEFT;
			else if (*f == '0') flags |= F_ZERO;
			else if (*f == '+') flags |= F_PLUS;
			else if (*f == ' ') flags |= F_SPACE;
			else if (*f == '#') {}
			else break;
		}
		int width = 0;
		if (*f == '*') { width = va_arg(ap, int); if (width < 0) { flags |= F_LEFT; width = -width; } f++; }
		else while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
		int prec = -1;
		if (*f == '.') {
			f++;
			prec = 0;
			if (*f == '*') { prec = va_arg(ap, int); f++; }
			else while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0');
		}
		int len = 0; // -2 hh, -1 h, 0 int, 1 l, 2 ll
		if (*f == 'h') { len = -1; if (*++f == 'h') { len = -2; f++; } }
		else if (*f == 'l') { len = 1; if (*++f == 'l') { len = 2; f++; } }
		else if (*f == 'z' || *f == 't' || *f == 'j') { len = (*f == 'j') ? 2 : 1; f++; }
		const char c = *f;
		switch (c) {
		case 'd': case 'i': {
			int64_t v = (len == 2) ? va_arg(ap, long long) : (len == 1) ? va_arg(ap, long) : va_arg(ap, int);
			if (len == -1) v = (short)v;
			else if (len == -2) v = (signed char)v;
			emit_integer(s, v < 0 ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v, v < 0, 10, false, flags, width, prec);
			break;
		}
		case 'u': case 'x': case 'X': case 'o': {
			uint64_t v = (len == 2) ? va_arg(ap, unsigned long long) : (len == 1) ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
			if (len == -1) v = (unsigned short)v;
			else if (len == -2) v = (unsigned char)v;
			emit_integer(s, v, false, c == 'u' ? 10 : c == 'o' ? 8 : 16, c == 'X', flags & ~(F_PLUS | F_SPACE), width, prec);
			break;
		}
		case 'p': {
			s.put('0'); s.put('x');
			emit_integer(s, (uintptr_t)va_arg(ap, void *), false, 16, false, 0, 0, -1);
			break;
		}
		case 'c': {
			char ch = (char)va_arg(ap, int);
			emit_number(s, &ch, 1, false, flags & ~(F_ZERO | F_PLUS | F_SPACE), width);
			break;
		}
		case 's': {
			const char *str = va_arg(ap, const char *);
			if (!str) str = "(null)";
			int n = 0;
			while (str[n] && (prec < 0 || n < prec)) n++;
			emit_number(s, str, n, false, flags & ~(F_ZERO | F_PLUS | F_SPACE), width);
			break;
		}
		case 'f': case 'F': case 'e': case 'g': case 'E': case 'G':
			emit_float(s, (float)va_arg(ap, double), flags, width, prec);
			break;
		case 'n':
			*va_arg(ap, int *) = s.count;
			break;
		case '%':
			s.put('%');
			break;
		case 0:
			return s.count;
		default:
			s.put('%'); s.put(c);
		}
	}
	return s.count;
}

} // namespace

extern "C" {

int vsnprintf(char *buf, size_t size, const char *f, va_list ap) {
	BufSink s(buf, size);
	format(s, f, ap);
	s.finish();
	return s.count;
}

int snprintf(char *buf, size_t size, const char *f, ...) {
	va_list ap;
	va_start(ap, f);
	int n = vsnprintf(buf, size, f, ap);
	va_end(ap);
	return n;
}

int vsprintf(char *buf, const char *f, va_list ap) {
	return vsnprintf(buf, (size_t)-1 >> 1, f, ap);
}

int sprintf(char *buf, const char *f, ...) {
	va_list ap;
	va_start(ap, f);
	int n = vsprintf(buf, f, ap);
	va_end(ap);
	return n;
}

int vfprintf(FILE *file, const char *f, va_list ap) {
	FileSink s(file);
	format(s, f, ap);
	s.flush();
	return s.count;
}

int fprintf(FILE *file, const char *f, ...) {
	va_list ap;
	va_start(ap, f);
	int n = vfprintf(file, f, ap);
	va_end(ap);
	return n;
}

int vprintf(const char *f, va_list ap) {
	return vfprintf(stdout, f, ap);
}

int printf(const char *f, ...) {
	va_list ap;
	va_start(ap, f);
	int n = vfprintf(stdout, f, ap);
	va_end(ap);
	return n;
}

//! to a file descriptor (Print::printf of the Arduino core)
int _write(int fd, char *ptr, int len);
int vdprintf(int fd, const char *f, va_list ap) {
	struct FdSink : Sink {
		int fd;
		char chunk[64];
		uint8_t n = 0;
		void put(char c) override {
			chunk[n++] = c;
			count++;
			if (n == sizeof(chunk)) flush();
		}
		void flush() {
			if (n) _write(fd, chunk, n);
			n = 0;
		}
	} s;
	s.fd = fd;
	format(s, f, ap);
	s.flush();
	return s.count;
}
int dprintf(int fd, const char *f, ...) {
	va_list ap;
	va_start(ap, f);
	int n = vdprintf(fd, f, ap);
	va_end(ap);
	return n;
}

// newlib's integer-only variants (assert() uses fiprintf)
int vfiprintf(FILE *file, const char *f, va_list ap) { return vfprintf(file, f, ap); }
int fiprintf(FILE *file, const char *f, ...) {
	va_list ap;
	va_start(ap, f);
	int n = vfprintf(file, f, ap);
	va_end(ap);
	return n;
}
int iprintf(const char *f, ...) {
	va_list ap;
	va_start(ap, f);
	int n = vfprintf(stdout, f, ap);
	va_end(ap);
	return n;
}
int vsniprintf(char *buf, size_t size, const char *f, va_list ap) { return vsnprintf(buf, size, f, ap); }
int sniprintf(char *buf, size_t size, const char *f, ...) {
	va_list ap;
	va_start(ap, f);
	int n = vsnprintf(buf, size, f, ap);
	va_end(ap);
	return n;
}
int siprintf(char *buf, const char *f, ...) {
	va_list ap;
	va_start(ap, f);
	int n = vsprintf(buf, f, ap);
	va_end(ap);
	return n;
}

//! decimal floating point number, single precision (returned as double)
double strtod(const char *str, char **end) {
	const char *p = str;
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f' || *p == '\v') p++;
	bool neg = false;
	if (*p == '+' || *p == '-') neg = (*p++ == '-');
	uint64_t mant = 0;
	int exp10 = 0, digits = 0;
	bool any = false;
	for (; *p >= '0' && *p <= '9'; p++, any = true) {
		if (digits < 19) { mant = mant * 10 + (*p - '0'); if (mant) digits++; }
		else exp10++;
	}
	if (*p == '.') {
		p++;
		for (; *p >= '0' && *p <= '9'; p++, any = true) {
			if (digits < 19) { mant = mant * 10 + (*p - '0'); if (mant) digits++; exp10--; }
		}
	}
	if (!any) {
		if (end) *end = (char *)str;
		return 0;
	}
	if (*p == 'e' || *p == 'E') {
		const char *q = p + 1;
		bool eneg = false;
		if (*q == '+' || *q == '-') eneg = (*q++ == '-');
		if (*q >= '0' && *q <= '9') {
			int e = 0;
			for (; *q >= '0' && *q <= '9'; q++) if (e < 1000) e = e * 10 + (*q - '0');
			exp10 += eneg ? -e : e;
			p = q;
		}
	}
	if (end) *end = (char *)p;
	// powers of ten up to 1e10 are exact in float: one correctly rounded multiply/divide
	static const float p10[11] = {1e0f, 1e1f, 1e2f, 1e3f, 1e4f, 1e5f, 1e6f, 1e7f, 1e8f, 1e9f, 1e10f};
	float v = (float)mant;
	while (exp10 > 10) { v *= 1e10f; exp10 -= 10; }
	while (exp10 < -10) { v /= 1e10f; exp10 += 10; }
	v = exp10 >= 0 ? v * p10[exp10] : v / p10[-exp10];
	return neg ? -v : v;
}

float strtof(const char *str, char **end) { return (float)strtod(str, end); }

double atof(const char *str) { return strtod(str, NULL); }

} // extern "C"

#endif // COMPACT_STDIO
