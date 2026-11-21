; Hardware probe — Jerry DSP side. Mirrors the game's synth setup (SCLK=25,
; SMODE internal/rising/WS, I2S interrupt on DSP int #1) but instead of
; synthesizing it MEASURES, using Jerry timer 2 as a free-running clock
; (68K programs JPIT3/JPIT4; the DSP reads the divider at $F1003C, which
; counts DOWN and wraps at $FFFF).
;
; Per I2S interrupt:
;   period  = timer delta since the previous interrupt (min / max / count
;             of periods above the 68K-set "late" threshold). A late or
;             missed interrupt here is exactly what bends the game's pitch.
;   dram    = time taken by K back-to-back DRAM loads (K, address set by
;             the 68K): how long the bus makes the DSP wait (JDEV0001).
; No external writes at all (JDEV0002 #7 is moot).
;
; Parameter/stat block PB (DSP RAM, longs):
;   +0 ctl (68K writes 1 = reset stats; DSP clears it)   +4 irq count
;   +8 period min  +12 period max  +16 dram max  +20 dram sum  +24 late
;   +28 K  +32 DRAM address  +36 late threshold

PB		.equ	$F1B800

		.globl	_ProbeDsp
		.globl	_ProbeDspEnd

		.text
		.long
_ProbeDsp:
		.dsp
		.org	$F1B000

		movei	#PMain,r28
		jump	(r28)
		nop
		.dcb.b	6,0			; pad to $F1B010
		.org	$F1B010			; I2S interrupt entry
		movei	#PI2S,r28
		jump	(r28)
		nop
		.dcb.b	38,0			; pad to $F1B040
		.org	$F1B040
PMain:
		movei	#$F1BFE0,r31		; interrupt return stack
		movei	#PB,r14
		movei	#$F1003C,r15		; JPIT4 read: timer 2 divider
		movei	#$F1A148,r17		; L_I2S
		movei	#$F1A14C,r18		; R_I2S
		moveq	#0,r19			; have-previous flag
		moveq	#0,r20			; count
		movei	#$FFFF,r21		; period min
		moveq	#0,r22			; period max
		moveq	#0,r23			; dram max
		moveq	#0,r24			; dram sum
		moveq	#0,r25			; late
		movei	#$F1A150,r1		; SCLK (same as the game)
		moveq	#25,r0
		store	r0,(r1)
		movei	#$F1A154,r1		; SMODE: internal, rising, WS
		moveq	#%001101,r0
		store	r0,(r1)
		movei	#$F1A100,r1		; D_FLAGS: enable I2S interrupt
		movei	#$20,r0
		store	r0,(r1)
		movei	#PIdle,r13		; r13: idle loop only
PIdle:
		jump	(r13)
		nop

PI2S:
		loadw	(r15),r0		; now
		load	(r14),r1		; ctl
		movei	#.nr,r27
		cmpq	#0,r1
		jump	eq,(r27)
		nop
		moveq	#0,r19
		moveq	#0,r20
		movei	#$FFFF,r21
		moveq	#0,r22
		moveq	#0,r23
		moveq	#0,r24
		moveq	#0,r25
		moveq	#0,r1
		store	r1,(r14)		; ack the reset
.nr:
		movei	#.first,r27
		cmpq	#0,r19
		jump	eq,(r27)
		nop
		move	r16,r2
		sub	r0,r2			; prev - now (down counter)
		movei	#$FFFF,r3
		and	r3,r2
		movei	#.n1,r27
		cmp	r21,r2			; d - min
		jump	pl,(r27)
		nop
		move	r2,r21
.n1:		movei	#.n2,r27
		cmp	r2,r22			; max - d
		jump	pl,(r27)
		nop
		move	r2,r22
.n2:		load	(r14+9),r3		; late threshold
		movei	#.n3,r27
		cmp	r3,r2			; d - thr
		jump	mi,(r27)
		nop
		addq	#1,r25
.n3:		movei	#.cnt,r27
		jump	(r27)
		nop
.first:		moveq	#1,r19
.cnt:
		move	r0,r16
		addq	#1,r20
		; ---- DRAM section: K loads from addr
		load	(r14+7),r7		; K
		load	(r14+8),r5		; addr
		loadw	(r15),r4		; t1
		movei	#.ldone,r26
		movei	#.ld,r27
		cmpq	#0,r7
		jump	eq,(r26)
		nop
.ld:		load	(r5),r6
		subq	#1,r7
		jump	ne,(r27)
		nop
		or	r6,r6			; wait for the last load
.ldone:
		loadw	(r15),r8		; t2
		sub	r8,r4			; t1 - t2
		movei	#$FFFF,r3
		and	r3,r4
		add	r4,r24
		movei	#.n4,r27
		cmp	r4,r23			; max - dur
		jump	pl,(r27)
		nop
		move	r4,r23
.n4:
		store	r20,(r14+1)
		store	r21,(r14+2)
		store	r22,(r14+3)
		store	r23,(r14+4)
		store	r24,(r14+5)
		store	r25,(r14+6)
		moveq	#0,r9
		store	r9,(r17)		; silence
		store	r9,(r18)

		load	(r31),r28		; return address
		addq	#2,r28
		addq	#4,r31
		movei	#$F1A100,r26
		movei	#$420,r27		; I2S latch clear | I2S enable
		jump	(r28)
		store	r27,(r26)		; delay slot: also drops IMASK

		.68000
		.long
_ProbeDspEnd:
		.end
