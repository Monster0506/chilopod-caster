#ifndef __REFCNT_H__
#define __REFCNT_H__

/* Macros for reference counting */

#include <assert.h>

#define	REFCNT	_Atomic int refcnt
#define	REFCNT_INIT(p)			do {atomic_init(&(p)->refcnt, 1);} while(0)

/* *_incref function declaration */
#define	REFCNT_INCREF_DECL(_f_incref, _tp)	\
	void _f_incref(_tp *p)

/* *_incref function body */
#define	REFCNT_INCREF_BODY(_f_incref, _tp)	\
	void _f_incref(_tp *p) {	\
	assert((p)->refcnt > 0);	\
	atomic_fetch_add(&(p)->refcnt, 1);}

/* *_incref function declaration with 2 arguments */
#define	REFCNT_INCREF2_DECL(_f_incref, _tp)	\
	void _f_incref(_tp *p, char *ch)

/* *_incref function body with 2 arguments */
#define	REFCNT_INCREF2_BODY(_f_incref, _tp)	\
	void _f_incref(_tp *p, char *ch) {	\
	assert((p)->refcnt > 0);		\
	atomic_fetch_add(&(p)->refcnt, 1);}

/* *_decref function declaration */
#define	REFCNT_DECREF_DECL(_f_decref, _tp)	\
	void _f_decref(_tp *p)

/* *_decref function body */
#define	REFCNT_DECREF_BODY(_f_decref, _tp, _f_free)	\
	void _f_decref(_tp *p) {	\
		assert(p->refcnt > 0);	\
		if (atomic_fetch_sub(&p->refcnt, 1) == 1) _f_free(p);	\
	}

/* *_decref function declaration */
#define	REFCNT_DECREF2_DECL(_f_decref, _tp)	\
	void _f_decref(_tp *p, char *ch)

/* *_decref function body with 2 arguments */
#define	REFCNT_DECREF2_BODY(_f_decref, _tp, _f_free)	\
	void _f_decref(_tp *p, char *ch) {	\
		assert(p->refcnt > 0);		\
		if (atomic_fetch_sub(&p->refcnt, 1) == 1) _f_free(p, ch);	\
	}

#endif
