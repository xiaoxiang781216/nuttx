/****************************************************************************
 * arch/xtensa/src/iss-hifi4/hifi4_head.S
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <arch/xtensa/xtensa_corebits.h>

	.begin	literal_prefix	.ResetVector
	.section		.ResetVector.text, "ax"

	.align	4
	.global	__start
	.type	__start, @function
	.global	_ResetVector
__start:
_ResetVector:

	j	_ResetHandler

	.size	_ResetVector, . - _ResetVector

	.align	4
	.literal_position	/* tells the assembler/linker to place literals here */

	.align	4
	.global	_ResetHandler
_ResetHandler:

	movi	a0, 0		/* a0 is always 0 in this code, used to initialize lots of things */

	wsr.intenable	a0	/* make sure that interrupts are shut off (*before* we lower PS.INTLEVEL and PS.EXCM!) */

	/*
	 *  For processors with relocatable vectors, apply any alternate
	 *  vector base given to xt-genldscripts, which sets the
	 *  _memmap_vecbase_reset symbol accordingly.
	 */
	movi	a2, _memmap_vecbase_reset	/* note: absolute symbol, not a ptr */
	wsr.vecbase	a2

	rsil	a2, 1		/* lower PS.INTLEVEL here to make reset vector easier to debug */

	/* Windowed register init, so we can call windowed code (eg. C code). */
	movi	a1, 1
	wsr.windowstart	a1
	wsr.windowbase	a0
	rsync
	movi	a0, 0		/* possibly a different a0, clear it */

	movi	a1, g_idlestacktop		/* default SP */

	movi	a3, PS_UM|PS_WOE		/* PS.WOE = 0|1, PS.UM = 1, PS.EXCM = 0, PS.INTLEVEL = 0 */
	wsr.ps	a3
	rsync

	/*
	 *  Complete reset initialization outside the vector,
	 *  to avoid requiring a vector that is larger than necessary.
	 *  This 2nd-stage startup code sets up the C Run-Time (CRT) and calls main().
	 *
	 *  Here we use call0 not because we expect any return, but
	 *  because the assembler/linker dynamically sizes call0 as
	 *  needed (with -mlongcalls) which it doesn't with j or jx.
	 *  Note:  This needs to be call0 regardless of the selected ABI.
	 */
	call0	_start		/* jump to _start */
	/* does not return */

	.size	_ResetHandler, . - _ResetHandler

	.end	literal_prefix

	.data
	.section	.data

	.align	16
	.global	g_idlestack
	.type	g_idlestack, @object
g_idlestack:
	.skip	CONFIG_IDLETHREAD_STACKSIZE
g_idlestacktop:
