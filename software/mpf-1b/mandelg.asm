;
;  Compute a Mandelbrot set on a simple Z80 computer.
;
; Porting this program to another Z80 platform should be easy and straight-
; forward: The only dependencies on my homebrew machine are the system-calls 
; used to print strings and characters. These calls are performed by loading
; IX with the number of the system-call and performing an RST 08. To port this
; program to another operating system just replace these system-calls with 
; the appropriate versions. Only three system-calls are used in the following:
; _crlf: Prints a CR/LF, _puts: Prints a 0-terminated string (the adress of 
; which is expected in HL), and _putc: Print a single character which is 
; expected in A. RST 0 give control back to the monitor.
;
        org     $1800

vdp      	equ 	$00f0
hex7seg		equ 	$0678
scan		equ 	$05fe
scan1		equ 	$0624
scale           equ     256                     ; Do NOT change this - the 
                                                ; arithmetic routines rely on
                                                ; this scaling factor! :-)

divergent       equ     scale * 4

		main:
	;; scale = 256
	;; x_start = -512; x_end = 1280 / 10 = 128
	;; x_step = 1024 / 100 = 10.24
	
	;; y_start = -256; y_end = 256
	;; y_step = 256 / 10 = 25.6 

	call scan
	
	ld hl, iteration_max
	ld (hl),    a                    ; How many iterations

	ld hl, x
	ld (hl), 0                       ; x-coordinate

	ld hl, x_start
        ld (hl),  low(-3 * scale) 	; Minimum x-coordinate
	inc hl
        ld (hl),  high(-3 * scale) 	; Minimum x-coordinate
	
	ld hl, x_end
        ld (hl),  low(3 *  scale)         ; Maximum x-coordinate
	inc hl 
        ld (hl), high(3 *  scale)         ; Maximum x-coordinate
	
	ld hl, x_step
        ld (hl), 6  * scale / 200        ; x-coordinate step-width

	;; ld hl, y
        ;; ld (hl), low(-2 * scale)              ; Minimum y-coordinate
	;; inc hl
	;; ld (hl), high(-2 * scale)

	;; ld hl, y_end
        ;; ld (hl), low(2  * scale)              ; Maximum y-coordinate
	;; inc hl 
        ;; ld (hl), high(2  * scale)              ; Maximum y-coordinate

	;; ld hl, y_step
	;; ld (hl), 4  * scale / 100 y-coordinate step-width
	;;
	
	ld hl, y
        ld (hl), low(-480)              ; Minimum y-coordinate
	inc hl
	ld (hl), high(-480)
	ld hl, y_end
        ld (hl), low(480)              ; Maximum y-coordinate
	inc hl 
        ld (hl), high(480)              ; Maximum y-coordinate

	ld hl, y_step
        ld (hl), 960 / 120         ; y-coordinate step-width

	ld hl, z_0
        ld (hl), 0
	ld hl, z_1
        ld (hl), 0
	ld hl, scratch_0
	ld (hl), 0
	ld hl, z_0_square_high
	ld (hl), 0
	ld hl, z_0_square_low
	ld (hl), 0
	ld hl, z_1_square_high
	ld (hl), 0
	ld hl, z_1_square_low
	ld (hl), 0
	

		ld hl, xpos
		ld (hl), 0
		ld hl, ypos
		ld (hl), 0
		
		ld a, 0xfe 
		ld bc, vdp
		out (c), a
		call delay 

		ld a, 0x80 
		ld bc, vdp
		out (c), a
		call delay 

		call delay 
		call delay
		call delay
		call delay

		ld a, 0xff 
		ld bc, vdp
		out (c), a

		call delay 
		call delay
		call delay
		call delay

		call delay 
		call delay
		call delay
		call delay

                ;ld      hl, welcome             ; Print a welcome message
                ;ld      ix, _puts              
                ;rst     08

; for (y = <initial_value> ; y <= y_end; y += y_step)
; {
outer_loop:
                ld      hl, (y_end)             ; Is y <= y_end?
                ld      de, (y)
                and     a                       ; Clear carry
                sbc     hl, de                  ; Perform the comparison
                jp      m, mandel_end           ; End of outer loop reached

;    for (x = x_start; x <= x_end; x += x_step)
;    {
                ld      hl, (x_start)           ; x = x_start
                ld      (x), hl
inner_loop:
                ld      hl, (x_end)             ; Is x <= x_end?
                ld      de, (x)
                and     a
                sbc     hl, de
                jp      m, inner_loop_end       ; End of inner loop reached

;      z_0 = z_1 = 0;
                ld      hl, 0
                ld      (z_0), hl
                ld      (z_1), hl

;      for (iteration = iteration_max; iteration; iteration--)
;      {
                ld      a, (iteration_max)
                ld      b, a
iteration_loop:
                push    bc                      ; iteration -> stack
;        z2 = (z_0 * z_0 - z_1 * z_1) / SCALE;
                ld      de, (z_1)               ; Compute DE HL = z_1 * z_1
		push de
		pop bc
		;                ld      bc, de
                call    mul_16
                ld      (z_0_square_low), hl    ; z_0 ** 2 is needed later again
                ld      (z_0_square_high), de

                ld      de, (z_0)               ; Compute DE HL = z_0 * z_0
                ;ld      bc, de
		push de
		pop bc 
                call    mul_16
                ld      (z_1_square_low), hl    ; z_1 ** 2 will be also needed
                ld      (z_1_square_high), de

                and     a                       ; Compute subtraction
                ld      bc, (z_0_square_low)
                sbc     hl, bc
                ld      (scratch_0), hl         ; Save lower 16 bit of result
		
                push de
		pop hl
		; ld      hl, de
		
                ld      bc, (z_0_square_high)
                sbc     hl, bc
                ld      bc, (scratch_0)         ; HL BC = z_0 ** 2 - z_1 ** 2

                ld      c, b                    ; Divide by scale = 256
                ld      b, l                    ; Discard the rest
                push    bc                      ; We need BC later

;        z3 = 2 * z0 * z1 / SCALE;
                ld      hl, (z_0)               ; Compute DE HL = 2 * z_0 * z_1
                add     hl, hl
                ;ld      de, hl
		push hl
		pop de
                ld      bc, (z_1)
                call    mul_16

                ld      b, e                    ; Divide by scale (= 256)
                ld      c, h                    ; BC contains now z_3

;        z1 = z3 + y;
                ld      hl, (y)
                add     hl, bc
                ld      (z_1), hl

;        z_0 = z_2 + x;
                pop     bc                      ; Here BC is needed again :-)
                ld      hl, (x)
                add     hl, bc
                ld      (z_0), hl

;        if (z0 * z0 / SCALE + z1 * z1 / SCALE > 4 * SCALE)
                ld      hl, (z_0_square_low)    ; Use the squares computed
                ld      de, (z_1_square_low)    ; above
                add     hl, de
                ; ld      bc, hl                  ; BC contains lower word of sum
		push hl
		pop bc

                ld      hl, (z_0_square_high)
                ld      de, (z_1_square_high)
                adc     hl, de

                ld      h, l                    ; HL now contains (z_0 ** 2 + 
                ld      l, b                    ; z_1 ** 2) / scale

                ld      bc, divergent
                and     a
                sbc     hl, bc

;          break;
                jp      c, iteration_dec        ; No break
                pop     bc                      ; Get latest iteration counter
                jr      iteration_end           ; Exit loop

;        iteration++;
iteration_dec:
                pop     bc                      ; Get iteration counter
                djnz    iteration_loop          ; We might fall through!
;      }
iteration_end:
;      printf("%c", display[iteration % 7]);
                ld      a, b
                ld	(col), a
		call	plot

		ld hl, xpos
		inc (hl)
		
                ;ld      ix, _putc               ; Print the character
                ;rst     08

                ld      de, (x_step)            ; x += x_step
                ld      hl, (x)
                add     hl, de
                ld      (x), hl

                jp      inner_loop
;    }
;    printf("\n");
inner_loop_end:

                ;ld      ix, _crlf               ; Print a CR/LF pair
                ;rst     08

		ld hl, ypos
		inc (hl) 
		ld hl, xpos
		ld (hl), 0

                ld      de, (y_step)            ; y += y_step
                ld      hl, (y)
                add     hl, de
                ld      (y), hl                 ; Store new y-value

                jp      outer_loop
; }

mandel_end:
                ;ld      hl, finished            ; Print finished-message
                ;ld      ix, _puts
                ;rst     08

                ;rst     0                       ; Return to the monitor
        	call scan
		jp main
	
;
;   Compute DEHL = BC * DE (signed): This routine is not too clever but it 
; works. It is based on a standard 16-by-16 multiplication routine for unsigned
; integers. At the beginning the sign of the result is determined based on the
; signs of the operands which are negated if necessary. Then the unsigned
; multiplication takes place, followed by negating the result if necessary.
;
mul_16:         xor     a                       ; Clear carry and A (-> +)
                bit     7, b                    ; Is BC negative?
                jr      z, bc_positive          ; No
                sub     c                       ; A is still zero, complement
                ld      c, a
                ld      a, 0
                sbc     a, b
                ld      b, a
                scf                             ; Set carry (-> -)
bc_positive:    bit     7, D                    ; Is DE negative?
                jr      z, de_positive          ; No
                push    af                      ; Remember carry for later!
                xor     a
                sub     e
                ld      e, a
                ld      a, 0
                sbc     a, d
                ld      d, a
                pop     af                      ; Restore carry for complement
                ccf                             ; Complement Carry (-> +/-?)
de_positive:    push    af                      ; Remember state of carry
                and     a                       ; Start multiplication
                sbc     hl, hl
                ld      a, 16                   ; 16 rounds
mul_16_loop:    add     hl, hl
                rl      e
                rl      d
                jr      nc, mul_16_exit
                add     hl, bc
                jr      nc, mul_16_exit
                inc     de
mul_16_exit:    dec     a
                jr      nz, mul_16_loop
                pop     af                      ; Restore carry from beginning
                ret     nc                      ; No sign inversion necessary
                xor     a                       ; Complement DE HL
                sub     l
                ld      l, a
                ld      a, 0
                sbc     a, h
                ld      h, a
                ld      a, 0
                sbc     a, e
                ld      e, a
                ld      a, 0
                sbc     a, d
                ld      d, a
                ret

;;;
;;;
;;; 
                
plot:

	call delay 

	ld bc, vdp
	ld a, 0x90
	out (c), a

	call delay 

	ld bc, vdp
	ld a, (xpos)
	out (c), a

	call delay 

	ld bc, vdp
	ld a, (ypos)
	out (c), a

	call delay 

	ld bc, vdp
	ld a, (col)
	out (c), a

	call delay

	ld bc, vdp
	ld a, 0xff
	out (c), a

	call showvalues
	
	ret

;;;
;;;
;;;


delay: 
	ret
	;;  due to display output, we don't need a delay here...
	ld de, $0001
	ld b,e          ; Number of loops is in DE
	dec de          ; Calculate DB value (destroys B, D and E)
	inc d
delay1:
    
	djnz delay1
	dec d
	jp nz,delay1
    
	ret
	
;;;
;;;
;;; 


showvalues:
	ld	de,xpos 
	ld	hl,outbf
	ld	b,3
	
preploop:
	ld	a,(de)
	call    hex7seg
	inc  	de
	djnz 	preploop

	ld 	ix,outbf

	ld b,1 
    
dispdelay:
	call	scan1 
	djnz dispdelay

	ret	
;;;
;;;
;;; 

	
	
iteration_max   defb    10                      ; How many iterations
x               defw    0                       ; x-coordinate
x_start         defw    -2 * scale              ; Minimum x-coordinate
x_end           defw    5 *  scale / 10         ; Maximum x-coordinate
x_step          defw    4  * scale / 100        ; x-coordinate step-width
y               defw    -1 * scale              ; Minimum y-coordinate
y_end           defw    1  * scale              ; Maximum y-coordinate
y_step          defw    1  * scale / 10         ; y-coordinate step-width
z_0             defw    0
z_1             defw    0
scratch_0       defw    0
z_0_square_high defw    0
z_0_square_low  defw    0
z_1_square_high defw    0
z_1_square_low  defw    0
xpos		defb	0
ypos		defb 	0
col		defb	0
display         defb    " .-+*=#@"              ; 8 characters for the display

outbf  	defb 	0
        defb    0
        defb    0
        defb    0 
        defb    0 
        defb    0
	
                end main
