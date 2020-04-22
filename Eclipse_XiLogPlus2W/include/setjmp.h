#ifndef	_SETJMP_H_
#define	_SETJMP_H_

#ifdef __C30__
typedef unsigned int jmp_buf[18];
#else
typedef unsigned int jmp_buf[4];
#endif

extern	int	setjmp(jmp_buf);
extern void	longjmp(jmp_buf, int);

#endif	/* _SETJMP_H_ */
