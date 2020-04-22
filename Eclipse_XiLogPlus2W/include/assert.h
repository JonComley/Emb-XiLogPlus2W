/*
 *	Assertion - use liberally for debugging. Defining NDEBUG
 *	turns assertions off.
 *	assert(exp) where exp is non-zero does nothing, while
 *	assert(exp) where exp evaluates to zero aborts the program
 *	with a message like
 *
 *	Assertion failed: prog.c line 123: "exp"
 *
 */

#ifndef	NDEBUG
#ifndef	__mkstr__
#define	__mkstr__(exp)	#exp
#endif

#ifdef __C30__
#include <stdio.h>
#include <stdlib.h>
#define	assert(exp)	if(!(exp)) {fprintf(stderr, \
																"%s:%i %s -- assertion failed\n", \
																__FILE__, __LINE__, __mkstr__(exp)); \
																abort();}
#else
extern void	_fassert(int, const char *, const char *);
#define	assert(exp)	if(!(exp)) {_fassert(__LINE__, __FILE__, __mkstr__(exp));}
#endif /* C30 */

#else
#define	assert(exp)
#endif
