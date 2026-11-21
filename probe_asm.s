; Hardware probe — 68K glue: our own VBL handler (refreshes the probe's
; object list from a template each frame, so the probe can switch between
; Object Processor loads), the live list, and accessors for startup.s's
; non-C-visible video globals.

		.include "jaguar.inc"

		.globl	_probe_vbl
		.globl	_ol_live
		.globl	_ol_src
		.globl	_ol_nlongs
		.globl	_probe_vdb
		.globl	_probe_vde
		.globl	_probe_height
		.globl	_probe_width
		.extern	ticks
		.extern	a_vdb
		.extern	a_vde
		.extern	height
		.extern	width

		.text

_probe_vbl:
		movem.l	d0/a0-a1,-(sp)
		move.l	_ol_src,a0
		lea	_ol_live,a1
		move.w	_ol_nlongs,d0
		subq.w	#1,d0
		bmi	.done
.cp:		move.l	(a0)+,(a1)+
		dbra	d0,.cp
.done:		addq.w	#1,_hb			; heartbeat: border colour
		move.w	_hb,d0			; steps every 8 frames, so a
		lsl.w	#5,d0			; live VBL shows even if the
		and.w	#$FF00,d0		; picture is gone
		move.w	d0,BORD1
		addq.l	#1,ticks
		move.w	#$101,INT1		; ack video interrupt
		move.w	#0,INT2
		movem.l	(sp)+,d0/a0-a1
		rte

_probe_vdb:	moveq	#0,d0
		move.w	a_vdb,d0
		rts
_probe_vde:	moveq	#0,d0
		move.w	a_vde,d0
		rts
_probe_height:	moveq	#0,d0
		move.w	height,d0
		rts
_probe_width:	moveq	#0,d0
		move.w	width,d0
		rts

		.bss
		.qphrase
_ol_live:	.ds.l	64			; up to 32 phrases
_ol_src:	.ds.l	1
_ol_nlongs:	.ds.w	1
_hb:		.ds.w	1
		.even
		.end
