/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Provided under GPL version 2 or later.
*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif


#ifdef WIN32
#include <windows.h>
#endif

#include <stdarg.h>
#include <stdio.h>
#include "libburn.h"
#include "debug.h"

static int burn_verbosity = 0;

void burn_set_verbosity(int v)
{
	burn_verbosity = v;
}


