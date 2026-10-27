| scrcon_zz9k.s -- the console hook of the RTG text console (amix-z3660rtg
| integration/c1-console.txt), for a 68040/68060 kernel built by
| relink-040-c1hook.sh.
|
| The hook belongs at the top of scrcon_put() in amiga/console/c1.c:
|     if (sp == displayedscreen && zz9000consolewrite(p, n))
|         return;
| A linked image cannot take a recompiled c1.o, so scrcon_put is weakened,
| the stock body stays reachable as scrcon_put_stock, and this wrapper runs
| first.  cp->screen is at offset 8 of struct console (flags, pad0, tty,
| screen); relink-040-c1hook.sh asserts that the stock body loads it from
| there.  zz9000consolewrite() returns 0 while the redirect is off, so the
| screen driver keeps drawing as before until zz9k_test ... redirect.
|
|   scrcon_put(cp, p, n): %sp@(4) cp, %sp@(8) p, %sp@(12) n

	.text
	.globl	scrcon_put
scrcon_put:
	movel	%sp@(4),%a0			| cp
	movel	%a0@(8),%d0			| cp->screen
	cmpl	displayedscreen,%d0
	bnes	1f
	movel	%sp@(12),%sp@-			| n
	movel	%sp@(12),%sp@-			| p
	jsr	zz9000consolewrite
	addql	&8,%sp
	tstl	%d0
	beqs	1f
	rts
1:	jmp	scrcon_put_stock

	.balign 4
